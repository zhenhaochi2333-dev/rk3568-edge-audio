#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace edgeaudio {

constexpr int kSampleRate = 16000;
constexpr int kChannels = 1;
constexpr int kVadFrameSamples = 320;      // 20 ms
constexpr int kYamnetWindowSamples = 48000; // 3 s
constexpr int kYamnetHopSamples = 24000;    // 1.5 s

struct AudioChunk {
    std::uint64_t first_sample = 0;
    std::vector<std::int16_t> samples;
};

struct AudioCursor {
    std::uint64_t next_sample = 0;
};

struct AsrResult {
    std::uint64_t timestamp_ms = 0;
    std::string text;
    bool final = false;
    double latency_ms = 0.0;
    double rtf = 0.0;
};

struct SoundEvent {
    int index = -1;
    std::string label;
    float score = 0.0f;
};

struct SoundEventResult {
    std::uint64_t timestamp_ms = 0;
    std::vector<SoundEvent> topk;
    std::string stable_event;
    std::string transition;
    double inference_ms = 0.0;
};

}  // namespace edgeaudio
