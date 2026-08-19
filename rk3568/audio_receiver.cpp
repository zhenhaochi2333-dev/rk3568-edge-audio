#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <numeric>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "rknn_api.h"

namespace {
constexpr int kRate = 16000;
constexpr int kWindowSamples = 48000;  // yamnet_3s.onnx: 3 seconds
constexpr int kHopSamples = 24000;     // 1.5 second update interval
constexpr int kRows = 6;
constexpr int kClasses = 521;

struct Label {
    int index = -1;
    std::string name;
};

struct Model {
    rknn_context context = 0;
    rknn_input_output_num io{};
};

std::vector<Label> load_labels(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open label file: " + path);
    std::vector<Label> labels;
    std::string line;
    std::getline(file, line);  // CSV header
    while (std::getline(file, line)) {
        const auto first = line.find(',');
        const auto second = line.find(',', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        std::string name = line.substr(second + 1);
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.size() - 2);
        }
        if (!name.empty() && name.back() == '\r') name.pop_back();
        labels.push_back({std::stoi(line.substr(0, first)), name});
    }
    if (labels.size() != kClasses) throw std::runtime_error("expected 521 labels");
    return labels;
}

std::vector<unsigned char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open model: " + path);
    const auto size = file.tellg();
    file.seekg(0);
    std::vector<unsigned char> data(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void check(int ret, const char* what) {
    if (ret != RKNN_SUCC) throw std::runtime_error(std::string(what) + " failed: " + std::to_string(ret));
}

Model load_model(const std::string& path) {
    auto data = read_file(path);
    Model model;
    check(rknn_init(&model.context, data.data(), data.size(), 0, nullptr), "rknn_init");
    try {
        check(rknn_query(model.context, RKNN_QUERY_IN_OUT_NUM, &model.io, sizeof(model.io)), "rknn_query");
        if (model.io.n_input != 1 || model.io.n_output != 3) {
            throw std::runtime_error("unexpected YAMNet input/output count");
        }
    } catch (...) {
        rknn_destroy(model.context);
        throw;
    }
    std::cout << "RKNN model loaded: inputs=" << model.io.n_input
              << " outputs=" << model.io.n_output << " backend=RKNN/NPU\n";
    return model;
}

std::array<int, 3> top3(const float* scores) {
    std::array<int, 3> ids{0, 1, 2};
    for (int i = 3; i < kClasses; ++i) {
        auto worst = std::min_element(ids.begin(), ids.end(),
            [&](int a, int b) { return scores[a] < scores[b]; });
        if (scores[i] > scores[*worst]) *worst = i;
    }
    std::sort(ids.begin(), ids.end(), [&](int a, int b) { return scores[a] > scores[b]; });
    return ids;
}

void infer(Model& model, const std::deque<int16_t>& samples, const std::vector<Label>& labels) {
    std::vector<float> input(kWindowSamples);
    for (int i = 0; i < kWindowSamples; ++i) input[i] = static_cast<float>(samples[i]) / 32768.0f;

    rknn_input input_desc{};
    input_desc.index = 0;
    input_desc.type = RKNN_TENSOR_FLOAT32;
    input_desc.fmt = RKNN_TENSOR_NCHW;
    input_desc.size = input.size() * sizeof(float);
    input_desc.buf = input.data();
    check(rknn_inputs_set(model.context, 1, &input_desc), "rknn_inputs_set");

    const auto start = std::chrono::steady_clock::now();
    check(rknn_run(model.context, nullptr), "rknn_run");
    std::array<rknn_output, 3> outputs{};
    outputs[2].want_float = 1;
    check(rknn_outputs_get(model.context, model.io.n_output, outputs.data(), nullptr), "rknn_outputs_get");
    const auto end = std::chrono::steady_clock::now();
    const auto* raw = static_cast<const float*>(outputs[2].buf);
    std::array<float, kClasses> scores{};
    for (int row = 0; row < kRows; ++row) {
        for (int c = 0; c < kClasses; ++c) scores[c] += raw[row * kClasses + c] / kRows;
    }
    check(rknn_outputs_release(model.context, model.io.n_output, outputs.data()), "rknn_outputs_release");

    const auto ids = top3(scores.data());
    const auto elapsed = std::chrono::duration<double, std::milli>(end - start).count();
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    localtime_r(&now, &tm);
    char timestamp[16];
    std::strftime(timestamp, sizeof(timestamp), "%H:%M:%S", &tm);
    std::cout << "[" << timestamp << "] inference_ms=" << elapsed << "\n";
    for (const int id : ids) std::cout << "  " << labels[id].name << " " << scores[id] << "\n";
    std::cout.flush();
}

int make_server(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("socket: " + std::string(std::strerror(errno)));
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(fd, 1) < 0) {
        close(fd);
        throw std::runtime_error("bind/listen: " + std::string(std::strerror(errno)));
    }
    return fd;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: audio_receiver <yamnet.rknn> <labels.csv> <port>\n";
        return 2;
    }
    try {
        const auto labels = load_labels(argv[2]);
        auto model = load_model(argv[1]);
        const int server = make_server(std::stoi(argv[3]));
        std::cout << "listening on TCP port " << argv[3] << " (PCM16 16kHz mono)\n";
        std::deque<int16_t> buffer;
        std::vector<unsigned char> bytes(8192);
        while (true) {
            const int client = accept(server, nullptr, nullptr);
            if (client < 0) continue;
            std::cout << "client connected\n";
            while (true) {
                const ssize_t count = recv(client, bytes.data(), bytes.size(), 0);
                if (count <= 0) break;
                const size_t samples = static_cast<size_t>(count) / sizeof(int16_t);
                const auto* pcm = reinterpret_cast<const int16_t*>(bytes.data());
                for (size_t i = 0; i < samples; ++i) buffer.push_back(pcm[i]);
                while (buffer.size() >= kWindowSamples) {
                    infer(model, buffer, labels);
                    for (int i = 0; i < kHopSamples; ++i) buffer.pop_front();
                }
            }
            close(client);
            buffer.clear();
            std::cout << "client disconnected; waiting for reconnect\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << "\n";
        return 1;
    }
}
