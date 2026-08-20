#pragma once

#include "audio_types.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace edgeaudio {

// One bounded PCM timeline with independent consumers. Consumers copy only
// the window they own; the receiver never keeps separate PCM branches.
class AudioRingBuffer {
public:
    explicit AudioRingBuffer(std::size_t capacity_samples = kSampleRate * 20)
        : capacity_(capacity_samples) {}

    void push(const std::int16_t* data, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            samples_.push_back(data[i]);
            if (samples_.size() > capacity_) {
                samples_.pop_front();
                ++base_sample_;
            }
        }
    }

    void push(const std::vector<std::int16_t>& data) { push(data.data(), data.size()); }

    std::uint64_t end_sample() const { return base_sample_ + samples_.size(); }
    std::uint64_t base_sample() const { return base_sample_; }
    std::size_t size() const { return samples_.size(); }

    bool read(std::uint64_t first_sample, std::size_t count,
              std::vector<std::int16_t>* output) const {
        if (!output || first_sample < base_sample_ || first_sample + count > end_sample()) {
            return false;
        }
        const auto offset = static_cast<std::size_t>(first_sample - base_sample_);
        output->resize(count);
        for (std::size_t i = 0; i < count; ++i) (*output)[i] = samples_[offset + i];
        return true;
    }

    void resync(AudioCursor* cursor) const {
        if (cursor && cursor->next_sample < base_sample_) cursor->next_sample = base_sample_;
    }

private:
    std::size_t capacity_;
    std::uint64_t base_sample_ = 0;
    std::deque<std::int16_t> samples_;
};

}  // namespace edgeaudio

