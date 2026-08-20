#include "edgeaudio/asr_engine.h"
#include "edgeaudio/audio_stream_buffer.h"
#include "edgeaudio/command_parser.h"
#include "edgeaudio/json_protocol.h"
#include "edgeaudio/tcp_audio_receiver.h"
#include "edgeaudio/vad.h"
#include "edgeaudio/yamnet_postprocess.h"
#include "edgeaudio/yamnet_rknn.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <mutex>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
using namespace edgeaudio;

volatile std::sig_atomic_t g_stop_requested = 0;

void handle_signal(int) { g_stop_requested = 1; }

int make_result_server(int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) throw std::runtime_error("result socket: " + std::string(std::strerror(errno)));
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        const std::string message = "result setsockopt: " + std::string(std::strerror(errno));
        close(fd);
        throw std::runtime_error(message);
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        listen(fd, 4) < 0) {
        const std::string message = "result bind/listen: " + std::string(std::strerror(errno));
        close(fd);
        throw std::runtime_error(message);
    }
    return fd;
}

class ResultPublisher {
public:
    explicit ResultPublisher(int port) : server_(make_result_server(port)) {}
    ~ResultPublisher() { stop(); }

    ResultPublisher(const ResultPublisher&) = delete;
    ResultPublisher& operator=(const ResultPublisher&) = delete;

    void start() {
        thread_ = std::thread([this] {
            sigset_t blocked{};
            sigemptyset(&blocked);
            sigaddset(&blocked, SIGINT);
            sigaddset(&blocked, SIGTERM);
            pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
            while (!stopping_) {
                const int client = accept(server_, nullptr, nullptr);
                if (client < 0) {
                    if (errno == EINTR) continue;
                    if (!stopping_) {
                        std::cerr << "[WARN] result accept failed: " << std::strerror(errno) << "\n";
                    }
                    continue;
                }
                std::lock_guard<std::mutex> lock(mutex_);
                if (client_ >= 0) {
                    shutdown(client_, SHUT_RDWR);
                    close(client_);
                }
                client_ = client;
                std::cout << "[INFO] result client connected\n" << std::flush;
            }
        });
    }

    void publish(const std::string& json) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (client_ < 0) return;
        const std::string line = json + "\n";
        std::size_t offset = 0;
        while (offset < line.size()) {
            const ssize_t sent = send(client_, line.data() + offset, line.size() - offset,
                                       MSG_NOSIGNAL);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
                continue;
            }
            if (sent < 0 && errno == EINTR) continue;
            shutdown(client_, SHUT_RDWR);
            close(client_);
            client_ = -1;
            return;
        }
    }

    void stop() {
        if (stopping_.exchange(true)) return;
        shutdown(server_, SHUT_RDWR);
        close(server_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (client_ >= 0) {
                shutdown(client_, SHUT_RDWR);
                close(client_);
            }
            client_ = -1;
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    int server_ = -1;
    int client_ = -1;
    std::atomic<bool> stopping_{false};
    std::mutex mutex_;
    std::thread thread_;
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

std::string argument(int argc, char** argv, const std::string& name,
                     const std::string& fallback = {}) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (argv[i] == name) return argv[i + 1];
    }
    return fallback;
}

bool has_flag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) return true;
    }
    return false;
}

class WindowPerformanceStats {
public:
    void record(std::uint64_t timestamp_ms, double inference_ms) {
        ++windows_;
        total_inference_ms_ += inference_ms;
        min_inference_ms_ = std::min(min_inference_ms_, inference_ms);
        max_inference_ms_ = std::max(max_inference_ms_, inference_ms);
        if (has_timestamp_) {
            total_interval_ms_ += static_cast<double>(timestamp_ms - last_timestamp_ms_);
            ++intervals_;
        }
        last_timestamp_ms_ = timestamp_ms;
        has_timestamp_ = true;
        if (windows_ % 20 == 0) {
            std::cout << "[PERF] windows=" << windows_
                      << " avg_inference_ms=" << total_inference_ms_ / windows_
                      << " min_inference_ms=" << min_inference_ms_
                      << " max_inference_ms=" << max_inference_ms_
                      << " avg_window_interval_ms="
                      << (intervals_ == 0 ? 0.0 : total_interval_ms_ / intervals_) << "\n";
        }
    }

private:
    std::size_t windows_ = 0;
    std::size_t intervals_ = 0;
    double total_inference_ms_ = 0.0;
    double total_interval_ms_ = 0.0;
    double min_inference_ms_ = std::numeric_limits<double>::max();
    double max_inference_ms_ = 0.0;
    std::uint64_t last_timestamp_ms_ = 0;
    bool has_timestamp_ = false;
};

int main_impl(const Options& options) {
    YamnetRknnModel yamnet(options.yamnet_backend, options.yamnet_model);
    if (!yamnet.ready()) throw std::runtime_error("YAMNet unavailable: " + yamnet.error());
    YamnetPostProcessor postprocessor(options.labels);

    AsrEngine asr(options.asr);
    if (!asr.available() && !options.allow_asr_unavailable) {
        throw std::runtime_error("ASR unavailable: " + asr.error());
    }

    ResultPublisher publisher(options.result_port);
    publisher.start();
    TcpAudioReceiver receiver(options.audio_port);
    std::cout << "[INFO] TCP listening audio_port=" << options.audio_port
              << " result_port=" << options.result_port
              << " yamnet_backend=" << yamnet.backend()
              << " asr_backend=" << (asr.available() ? asr.backend() : "UNAVAILABLE") << "\n"
              << "[INFO] audio stream: PCM16 / 16000 Hz / mono\n" << std::flush;

    AudioStreamBuffer ring;
    EnergyVad vad;
    CommandParser command_parser;
    WindowPerformanceStats performance;
    std::vector<std::int16_t> yamnet_window;
    std::vector<float> frame_scores;
    std::deque<std::int16_t> vad_pending;
    std::uint64_t next_yamnet = 0;
    std::uint64_t total_samples = 0;
    std::uint64_t processed_samples = 0;
    bool speech_active = false;
    bool monitoring_enabled = true;
    float latest_rms = 0.0f;

    const auto process_samples = [&](const std::vector<std::int16_t>& samples) {
        ring.push(samples);
        vad_pending.insert(vad_pending.end(), samples.begin(), samples.end());
        while (vad_pending.size() >= kVadFrameSamples) {
            std::array<std::int16_t, kVadFrameSamples> frame{};
            for (auto& sample : frame) {
                sample = vad_pending.front();
                vad_pending.pop_front();
            }
            const auto first = processed_samples;
            const auto frame_vad = vad.process(frame.data(), frame.size(), first);
            latest_rms = frame_vad.rms;
            if (frame_vad.speech && !speech_active) {
                speech_active = true;
                if (monitoring_enabled) {
                    publisher.publish("{\"type\":\"vad\",\"timestamp_ms\":" +
                                      std::to_string(frame_vad.timestamp_ms) +
                                      ",\"state\":\"speech_start\",\"rms\":" +
                                      json_number(frame_vad.rms) + "}");
                }
            }
            if (speech_active) {
                asr.feed(frame.data(), frame.size(), first, [&](const AsrResult& result) {
                    const auto command = result.final ? command_parser.parse(result.text) : std::string();
                    if (command == "START_MONITORING") monitoring_enabled = true;
                    if (command == "STOP_MONITORING") monitoring_enabled = false;
                    if (!command.empty()) {
                        std::cout << "[INFO] command=" << command << " monitoring="
                                  << (monitoring_enabled ? "ON" : "OFF") << "\n" << std::flush;
                    }
                    if (monitoring_enabled || !command.empty()) {
                        publisher.publish(asr_json(result, asr.backend(), command, monitoring_enabled));
                    }
                    if (!command.empty()) {
                        publisher.publish(status_json(yamnet.backend(),
                                                      asr.available() ? asr.backend() : "UNAVAILABLE",
                                                      total_samples, vad.speech(), latest_rms,
                                                      monitoring_enabled));
                    }
                });
            }
            ++processed_samples;
            if (!frame_vad.speech && speech_active) {
                speech_active = false;
                asr.finish(first + kVadFrameSamples, [&](const AsrResult& result) {
                    const auto command = result.final ? command_parser.parse(result.text) : std::string();
                    if (command == "START_MONITORING") monitoring_enabled = true;
                    if (command == "STOP_MONITORING") monitoring_enabled = false;
                    if (!command.empty()) {
                        std::cout << "[INFO] command=" << command << " monitoring="
                                  << (monitoring_enabled ? "ON" : "OFF") << "\n" << std::flush;
                    }
                    if (monitoring_enabled || !command.empty()) {
                        publisher.publish(asr_json(result, asr.backend(), command, monitoring_enabled));
                    }
                    if (!command.empty()) {
                        publisher.publish(status_json(yamnet.backend(),
                                                      asr.available() ? asr.backend() : "UNAVAILABLE",
                                                      total_samples, vad.speech(), latest_rms,
                                                      monitoring_enabled));
                    }
                });
                if (monitoring_enabled) {
                    publisher.publish("{\"type\":\"vad\",\"timestamp_ms\":" +
                                      std::to_string(frame_vad.timestamp_ms) +
                                      ",\"state\":\"speech_end\",\"rms\":" +
                                      json_number(frame_vad.rms) + "}");
                }
            }

            while (ring.end_sample() >= next_yamnet + kYamnetWindowSamples) {
                if (monitoring_enabled) {
                    if (!ring.read(next_yamnet, kYamnetWindowSamples, &yamnet_window)) {
                        std::cerr << "[WARN] YAMNet window is no longer available at sample "
                                  << next_yamnet << "\n";
                    } else {
                        double inference_ms = 0.0;
                        if (!yamnet.infer(yamnet_window, vad.speech(), &frame_scores, &inference_ms)) {
                            std::cerr << "[WARN] YAMNet inference failed: " << yamnet.error() << "\n";
                        } else {
                            const auto event = postprocessor.process(
                                frame_scores, (next_yamnet + kYamnetWindowSamples) * 1000 / kSampleRate,
                                inference_ms);
                            publisher.publish(event_json(event, yamnet.backend()));
                            performance.record(event.timestamp_ms, event.inference_ms);
                        }
                    }
                }
                next_yamnet += kYamnetHopSamples;
            }
        }
        total_samples += samples.size();
        if (total_samples % (kSampleRate * 2) < samples.size()) {
            publisher.publish(status_json(yamnet.backend(), asr.available() ? asr.backend() : "UNAVAILABLE",
                                           total_samples, vad.speech(), latest_rms, monitoring_enabled));
        }
    };

    while (!g_stop_requested) {
        const int client = receiver.accept_client();
        if (client < 0) {
            if (g_stop_requested) break;
            continue;
        }
        std::cout << "[INFO] audio client connected\n" << std::flush;
        vad.reset();
        asr.reset();
        speech_active = false;
        const bool clean_eof = receiver.receive_client(
            client, process_samples, [] { return g_stop_requested != 0; });
        std::cout << "[INFO] audio client disconnected clean_eof=" << (clean_eof ? "true" : "false")
                  << "\n" << std::flush;
        ring = AudioStreamBuffer();
        next_yamnet = 0;
        total_samples = 0;
        processed_samples = 0;
        vad_pending.clear();
        speech_active = false;
        latest_rms = 0.0f;
    }

    receiver.stop();
    publisher.stop();
    std::cout << "[INFO] EdgeAudio stopped\n" << std::flush;
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: audio_receiver --labels labels.csv --yamnet-backend mock|rknn "
                     "[--yamnet-model model.rknn] --asr-tokens tokens.txt "
                     "--asr-encoder encoder.onnx --asr-decoder decoder.onnx "
                     "--asr-joiner joiner.onnx [--asr-backend cpu|rknn] "
                     "[--allow-asr-unavailable] [--audio-port 5700] [--result-port 5701]\n";
        return 2;
    }
    try {
        std::signal(SIGINT, handle_signal);
        std::signal(SIGTERM, handle_signal);
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
        std::cerr << "[ERROR] EdgeAudio fatal: " << error.what() << "\n";
        return 1;
    }
}
