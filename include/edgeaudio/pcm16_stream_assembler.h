#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace edgeaudio {

// Converts an arbitrary TCP byte stream into little-endian PCM16 samples.
// The assembler owns at most one residual byte between feed() calls.
class Pcm16StreamAssembler {
public:
    void feed(const std::uint8_t* bytes, std::size_t count,
              std::vector<std::int16_t>* samples);

    void reset() {
        has_residual_ = false;
        residual_byte_ = 0;
    }

    bool has_residual_byte() const { return has_residual_; }

private:
    static std::int16_t decode_sample(std::uint8_t low, std::uint8_t high);

    bool has_residual_ = false;
    std::uint8_t residual_byte_ = 0;
};

}  // namespace edgeaudio
