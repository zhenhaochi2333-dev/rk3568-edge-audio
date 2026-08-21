#pragma once

#include "audio_types.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edgeaudio {

struct VadFrame {
    std::uint64_t timestamp_ms = 0;
    bool speech = false;
    float rms = 0.0f;
};

// Deterministic CPU speech gate. It is deliberately separate from YAMNet:
// YAMNet classifies 3-second windows, while this gate reacts every 20 ms.
// The interface can later be backed by Silero/WebRTC without changing the
// receiver or ASR scheduling code.
class EnergyVad {
public:
    EnergyVad(float start_rms = 0.018f, float end_rms = 0.010f,
              int start_frames = 3, int end_frames = 12)
        : start_rms_(start_rms), end_rms_(end_rms),
          start_frames_(start_frames), end_frames_(end_frames) {}

    VadFrame process(const std::int16_t* samples, std::size_t count,
                     std::uint64_t first_sample) {
        double sum = 0.0;
        for (std::size_t i = 0; i < count; ++i) {
            const float normalized = static_cast<float>(samples[i]) / 32768.0f;
            sum += normalized * normalized;
        }
        const float rms = count == 0 ? 0.0f : static_cast<float>(std::sqrt(sum / count));
        if (speech_) {
            end_count_ = rms < end_rms_ ? end_count_ + 1 : 0;
            if (end_count_ >= end_frames_) {
                speech_ = false;
                end_count_ = 0;
            }
        } else {
            start_count_ = rms >= start_rms_ ? start_count_ + 1 : 0;
            if (start_count_ >= start_frames_) {
                speech_ = true;
                start_count_ = 0;
            }
        }
        return {first_sample * 1000 / kSampleRate, speech_, rms};
    }

    bool speech() const { return speech_; }
    void reset() { speech_ = false; start_count_ = 0; end_count_ = 0; }

private:
    float start_rms_;
    float end_rms_;
    int start_frames_;
    int end_frames_;
    int start_count_ = 0;
    int end_count_ = 0;
    bool speech_ = false;
};

}  // namespace edgeaudio

