#pragma once

#include "audio_types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace edgeaudio {

class AsrEngine {
public:
    struct Config {
        std::string tokens;
        std::string encoder;
        std::string decoder;
        std::string joiner;
        std::string backend = "cpu";  // cpu or rknn
        int threads = 2;
        bool endpoint_detection = true;
    };

    using ResultCallback = std::function<void(const AsrResult&)>;

    explicit AsrEngine(Config config);
    ~AsrEngine();
    AsrEngine(const AsrEngine&) = delete;
    AsrEngine& operator=(const AsrEngine&) = delete;

    bool available() const;
    const std::string& backend() const { return config_.backend; }
    const std::string& error() const { return error_; }
    bool feed(const std::int16_t* samples, std::size_t count,
              std::uint64_t first_sample, const ResultCallback& callback);
    bool finish(std::uint64_t first_sample, const ResultCallback& callback);
    void reset();

private:
    Config config_;
    std::string error_;
    void* recognizer_ = nullptr;
    void* stream_ = nullptr;
    std::string last_text_;
};

}  // namespace edgeaudio
