#pragma once

#include "audio_types.h"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace edgeaudio {

// Bounded absolute sample timeline. Storage is circular, while read() exposes
// absolute sample numbers to independent window consumers.
class AudioStreamBuffer {
public:
    explicit AudioStreamBuffer(std::size_t capacity_samples = kSampleRate * 20)
        : capacity_(capacity_samples), storage_(capacity_samples) {
        if (capacity_ == 0) throw std::invalid_argument("audio buffer capacity must be positive");
    }

    void push(const std::int16_t* data, std::size_t count) {
        if (!data && count != 0) throw std::invalid_argument("audio buffer input is null");
        for (std::size_t i = 0; i < count; ++i) {
            const auto write_index = (base_index_ + size_) % capacity_;
            storage_[write_index] = data[i];
            if (size_ == capacity_) {
                base_index_ = (base_index_ + 1U) % capacity_;
                ++base_sample_;
            } else {
                ++size_;
            }
            ++end_sample_;
        }
    }

    void push(const std::vector<std::int16_t>& data) { push(data.data(), data.size()); }

    std::uint64_t end_sample() const { return end_sample_; }
    std::uint64_t base_sample() const { return base_sample_; }
    std::size_t size() const { return size_; }

    bool read(std::uint64_t first_sample, std::size_t count,
              std::vector<std::int16_t>* output) const {
        if (!output || first_sample < base_sample_ || first_sample > end_sample_ ||
            count > end_sample_ - first_sample) {
            return false;
        }
        output->resize(count);
        const auto offset = static_cast<std::size_t>(first_sample - base_sample_);
        for (std::size_t i = 0; i < count; ++i) {
            (*output)[i] = storage_[(base_index_ + offset + i) % capacity_];
        }
        return true;
    }

    void resync(AudioCursor* cursor) const {
        if (cursor && cursor->next_sample < base_sample_) cursor->next_sample = base_sample_;
    }

private:
    std::size_t capacity_;
    std::vector<std::int16_t> storage_;
    std::size_t base_index_ = 0;
    std::size_t size_ = 0;
    std::uint64_t base_sample_ = 0;
    std::uint64_t end_sample_ = 0;
};

}  // namespace edgeaudio
