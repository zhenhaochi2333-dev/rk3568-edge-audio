#include "edgeaudio/pcm16_stream_assembler.h"

#include <stdexcept>

namespace edgeaudio {

std::int16_t Pcm16StreamAssembler::decode_sample(std::uint8_t low, std::uint8_t high) {
    const auto value = static_cast<std::uint16_t>(low) |
                       (static_cast<std::uint16_t>(high) << 8U);
    return static_cast<std::int16_t>(value);
}

void Pcm16StreamAssembler::feed(const std::uint8_t* bytes, std::size_t count,
                                std::vector<std::int16_t>* samples) {
    if (!samples) throw std::invalid_argument("PCM16 output vector is null");
    if (count == 0) return;
    if (!bytes) throw std::invalid_argument("PCM16 input bytes are null");

    samples->reserve(samples->size() + (count + (has_residual_ ? 1U : 0U)) / 2U);
    std::size_t offset = 0;
    if (has_residual_) {
        samples->push_back(decode_sample(residual_byte_, bytes[0]));
        has_residual_ = false;
        residual_byte_ = 0;
        offset = 1;
    }

    while (offset + 1U < count) {
        samples->push_back(decode_sample(bytes[offset], bytes[offset + 1U]));
        offset += 2U;
    }
    if (offset < count) {
        residual_byte_ = bytes[offset];
        has_residual_ = true;
    }
}

}  // namespace edgeaudio
