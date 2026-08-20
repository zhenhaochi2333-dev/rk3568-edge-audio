#include "edgeaudio/asr_engine.h"
#include "edgeaudio/audio_ring_buffer.h"
#include "edgeaudio/command_parser.h"
#include "edgeaudio/json_protocol.h"
#include "edgeaudio/temporal_stabilizer.h"
#include "edgeaudio/vad.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <numeric>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if EDGEAUDIO_HAS_RKNN
#include "rknn_api.h"
#endif

namespace {
using namespace edgeaudio;

struct Label { int index = -1; std::string name; };

std::vector<Label> load_labels(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open label file: " + path);
    std::vector<Label> labels;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        const auto first = line.find(',');
        const auto second = line.find(',', first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        std::string name = line.substr(second + 1);
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
        labels.push_back({std::stoi(line.substr(0, first)), name});
    }
    return labels;
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
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(fd, 4) < 0) {
        close(fd);
        throw std::runtime_error("bind/listen: " + std::string(std::strerror(errno)));
    }
    return fd;
}

class ResultPublisher {
public:
    explicit ResultPublisher(int port) : server_(make_server(port)) {}
    ~ResultPublisher() { stop(); }

    void start() {
        thread_ = std::thread([this] {
            while (!stopping_) {
                const int client = accept(server_, nullptr, nullptr);
                if (client < 0) {
                    if (!stopping_) std::cerr << "result accept failed: " << std::strerror(errno) << "\n";
                    continue;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (client_ >= 0) close(client_);
                client_ = client;
                std::cout << "RESULT_CLIENT_CONNECTED\n" << std::flush;
            }
        });
    }

    void publish(const std::string& json) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (client_ < 0) return;
        const std::string line = json + "\n";
        if (send(client_, line.data(), line.size(), MSG_NOSIGNAL) < 0) {
            close(client_);
            client_ = -1;
        }
    }

    void stop() {
        if (stopping_.exchange(true)) return;
        shutdown(server_, SHUT_RDWR);
        close(server_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (client_ >= 0) close(client_);
            client_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    int server_;
    int client_ = -1;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::thread thread_;
};

class YamnetEngine {
public:
    YamnetEngine(std::string backend, std::string model, std::string labels)
        : backend_(std::move(backend)), model_path_(std::move(model)), labels_(load_labels(labels)) {
#if EDGEAUDIO_HAS_RKNN
        if (backend_ == "rknn") load_rknn();
#else
        if (backend_ == "rknn") error_ = "RKNN runtime not compiled in";
#endif
    }

    const std::string& backend() const { return backend_; }
    const std::string& error() const { return error_; }
    bool ready() const { return backend_ == "mock" || context_ != 0; }

    SoundEventResult infer(const std::vector<std::int16_t>& samples, std::uint64_t timestamp_ms, bool speech) {
        const auto started = std::chrono::steady_clock::now();
        SoundEventResult result;
        result.timestamp_ms = timestamp_ms;
        if (backend_ == "mock") {
            result.topk.push_back({speech ? 0 : 497, speech ? "Speech" : "Silence", speech ? 0.90f : 0.95f});
            result.topk.push_back({speech ? 497 : 0, speech ? "Silence" : "Speech", 0.08f});
        }
#if EDGEAUDIO_HAS_RKNN
        else {
            std::vector<float> input(kYamnetWindowSamples);
            for (int i = 0; i < kYamnetWindowSamples; ++i) input[i] = static_cast<float>(samples[i]) / 32768.0f;
            rknn_input input_desc{};
            input_desc.index = 0;
            input_desc.type = RKNN_TENSOR_FLOAT32;
            input_desc.fmt = RKNN_TENSOR_NCHW;
            input_desc.size = input.size() * sizeof(float);
            input_desc.buf = input.data();
            if (rknn_inputs_set(context_, 1, &input_desc) != RKNN_SUCC || rknn_run(context_, nullptr) != RKNN_SUCC) {
                error_ = "rknn input/run failed";
            } else {
                std::array<rknn_output, 3> outputs{};
                outputs[2].want_float = 1;
                if (rknn_outputs_get(context_, 3, outputs.data(), nullptr) == RKNN_SUCC) {
                    const auto* raw = static_cast<const float*>(outputs[2].buf);
                    std::vector<float> scores(521);
                    for (int row = 0; row < 6; ++row)
                        for (int c = 0; c < 521; ++c) scores[c] += raw[row * 521 + c] / 6.0f;
                    std::vector<int> ids(521);
                    std::iota(ids.begin(), ids.end(), 0);
                    std::partial_sort(ids.begin(), ids.begin() + 5, ids.end(), [&](int a, int b) { return scores[a] > scores[b]; });
                    for (int i = 0; i < 5; ++i) result.topk.push_back({ids[i], labels_[ids[i]].name, scores[ids[i]]});
                    rknn_outputs_release(context_, 3, outputs.data());
                }
            }
        }
#else
        (void)samples;
#endif
        if (!result.topk.empty()) {
            const auto [stable, transition] = stabilizer_.update(result.topk.front().label);
            result.stable_event = stable;
            result.transition = transition;
        }
        result.inference_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        return result;
    }

    ~YamnetEngine() {
#if EDGEAUDIO_HAS_RKNN
        if (context_) rknn_destroy(context_);
#endif
    }

private:
    std::string backend_;
    std::string model_path_;
    std::vector<Label> labels_;
    std::string error_;
    AudioEventStabilizer stabilizer_;
#if EDGEAUDIO_HAS_RKNN
    rknn_context context_ = 0;
    void load_rknn() {
        std::ifstream file(model_path_, std::ios::binary | std::ios::ate);
        if (!file) { error_ = "cannot open YAMNet model: " + model_path_; return; }
        const auto size = file.tellg();
        file.seekg(0);
        std::vector<unsigned char> data(static_cast<size_t>(size));
        file.read(reinterpret_cast<char*>(data.data()), size);
        if (rknn_init(&context_, data.data(), data.size(), 0, nullptr) != RKNN_SUCC) error_ = "rknn_init failed";
    }
#else
    std::uintptr_t context_ = 0;
#endif
};

struct Options {
    std::string yamnet_model;
    std::string labels;
    std::string yamnet_backend = "mock";
    AsrEngine::Config asr;
    bool allow_asr_unavailable = false;
    int audio_port = 5700;
    int result_port = 5701;
};

std::string argument(int argc, char** argv, const std::string& name, const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i) if (argv[i] == name) return argv[i + 1];
    return fallback;
}

bool has_flag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) if (argv[i] == name) return true;
    return false;
}

int main_impl(const Options& options) {
    YamnetEngine yamnet(options.yamnet_backend, options.yamnet_model, options.labels);
    if (!yamnet.ready()) throw std::runtime_error("YAMNet unavailable: " + yamnet.error());
    AsrEngine asr(options.asr);
    if (!asr.available() && !options.allow_asr_unavailable)
        throw std::runtime_error("ASR unavailable: " + asr.error());
    ResultPublisher publisher(options.result_port);
    publisher.start();
    const int server = make_server(options.audio_port);
    std::cout << "EDGEAUDIO_READY audio_port=" << options.audio_port << " result_port=" << options.result_port
              << " yamnet_backend=" << options.yamnet_backend
              << " asr_backend=" << (asr.available() ? asr.backend() : "UNAVAILABLE") << "\n" << std::flush;

    AudioRingBuffer ring;
    EnergyVad vad;
    CommandParser command_parser;
    std::vector<std::int16_t> yamnet_window;
    std::deque<std::int16_t> preroll;
    std::uint64_t next_yamnet = 0;
    std::uint64_t total_samples = 0;
    std::uint64_t processed_samples = 0;
    bool speech_active = false;
    std::deque<std::int16_t> vad_pending;
    float latest_rms = 0.0f;
    std::vector<unsigned char> pending_bytes;
    std::vector<unsigned char> bytes(8192);
    while (true) {
        const int client = accept(server, nullptr, nullptr);
        if (client < 0) continue;
        std::cout << "AUDIO_CLIENT_CONNECTED\n" << std::flush;
        vad.reset();
        asr.reset();
        while (true) {
            const ssize_t count = recv(client, bytes.data(), bytes.size(), 0);
            if (count <= 0) break;
            pending_bytes.insert(pending_bytes.end(), bytes.begin(), bytes.begin() + count);
            const auto usable_bytes = pending_bytes.size() - pending_bytes.size() % sizeof(std::int16_t);
            const auto sample_count = usable_bytes / sizeof(std::int16_t);
            const auto* pcm = reinterpret_cast<const std::int16_t*>(pending_bytes.data());
            ring.push(pcm, sample_count);
            for (std::size_t i = 0; i < sample_count; ++i) vad_pending.push_back(pcm[i]);
            pending_bytes.erase(pending_bytes.begin(), pending_bytes.begin() + usable_bytes);
            while (vad_pending.size() >= kVadFrameSamples) {
                std::array<std::int16_t, kVadFrameSamples> frame{};
                for (auto& sample : frame) { sample = vad_pending.front(); vad_pending.pop_front(); }
                const auto first = processed_samples;
                const auto frame_vad = vad.process(frame.data(), frame.size(), first);
                latest_rms = frame_vad.rms;
                for (int i = 0; i < kVadFrameSamples; ++i) {
                    preroll.push_back(frame[i]);
                    if (preroll.size() > kSampleRate) preroll.pop_front();
                }
                if (frame_vad.speech && !speech_active) {
                    speech_active = true;
                    publisher.publish("{\"type\":\"vad\",\"timestamp_ms\":" + std::to_string(frame_vad.timestamp_ms) + ",\"state\":\"speech_start\",\"rms\":" + json_number(frame_vad.rms) + "}");
                }
                if (speech_active) {
                    asr.feed(frame.data(), frame.size(), first, [&](const AsrResult& result) {
                        const auto command = command_parser.parse(result.text);
                        publisher.publish(asr_json(result, asr.backend(), command));
                    });
                }
                processed_samples += kVadFrameSamples;
                if (!frame_vad.speech && speech_active) {
                    speech_active = false;
                    asr.finish(first + kVadFrameSamples, [&](const AsrResult& result) {
                        const auto command = command_parser.parse(result.text);
                        publisher.publish(asr_json(result, asr.backend(), command));
                    });
                    publisher.publish("{\"type\":\"vad\",\"timestamp_ms\":" + std::to_string(frame_vad.timestamp_ms) + ",\"state\":\"speech_end\",\"rms\":" + json_number(frame_vad.rms) + "}");
                }
                if (ring.end_sample() >= next_yamnet + kYamnetWindowSamples) {
                    if (ring.read(next_yamnet, kYamnetWindowSamples, &yamnet_window)) {
                        const auto event = yamnet.infer(yamnet_window, (next_yamnet + kYamnetWindowSamples) * 1000 / kSampleRate, vad.speech());
                        publisher.publish(event_json(event, yamnet.backend()));
                    }
                    next_yamnet += kYamnetHopSamples;
                }
            }
            total_samples += sample_count;
            if (total_samples % (kSampleRate * 2) < sample_count)
                publisher.publish(status_json(yamnet.backend(), asr.available() ? asr.backend() : "UNAVAILABLE",
                                               total_samples, vad.speech(), latest_rms));
        }
        close(client);
        std::cout << "AUDIO_CLIENT_DISCONNECTED\n" << std::flush;
        ring = AudioRingBuffer();
        total_samples = 0;
        next_yamnet = 0;
        processed_samples = 0;
        vad_pending.clear();
        pending_bytes.clear();
        speech_active = false;
        latest_rms = 0.0f;
    }
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: audio_receiver --labels labels.csv --yamnet-backend mock|rknn [--yamnet-model model.rknn] "
                     "--asr-tokens tokens.txt --asr-encoder encoder.onnx --asr-decoder decoder.onnx "
                     "--asr-joiner joiner.onnx [--asr-backend cpu|rknn] [--allow-asr-unavailable] "
                     "[--audio-port 5700] [--result-port 5701]\n";
        return 2;
    }
    try {
        Options options;
        options.labels = argument(argc, argv, "--labels");
        options.yamnet_model = argument(argc, argv, "--yamnet-model");
        options.yamnet_backend = argument(argc, argv, "--yamnet-backend", "mock");
        options.asr.tokens = argument(argc, argv, "--asr-tokens");
        options.asr.encoder = argument(argc, argv, "--asr-encoder");
        options.asr.decoder = argument(argc, argv, "--asr-decoder");
        options.asr.joiner = argument(argc, argv, "--asr-joiner");
        options.asr.backend = argument(argc, argv, "--asr-backend", "cpu");
        options.allow_asr_unavailable = has_flag(argc, argv, "--allow-asr-unavailable");
        options.audio_port = std::stoi(argument(argc, argv, "--audio-port", "5700"));
        options.result_port = std::stoi(argument(argc, argv, "--result-port", "5701"));
        return main_impl(options);
    } catch (const std::exception& error) {
        std::cerr << "EDGEAUDIO_FATAL " << error.what() << "\n";
        return 1;
    }
}

