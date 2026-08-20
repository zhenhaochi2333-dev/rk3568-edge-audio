#include "edgeaudio/audio_stream_buffer.h"
#include "edgeaudio/pcm16_stream_assembler.h"
#include "edgeaudio/yamnet_postprocess.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::vector<std::int16_t> feed_chunks(const std::vector<std::vector<std::uint8_t>>& chunks) {
    edgeaudio::Pcm16StreamAssembler assembler;
    std::vector<std::int16_t> output;
    for (const auto& chunk : chunks) assembler.feed(chunk.data(), chunk.size(), &output);
    return output;
}

void test_pcm16_framing() {
    check(feed_chunks({{0x34, 0x12, 0xfe, 0xff}}) == std::vector<std::int16_t>{0x1234, -2},
          "PCM Test 1 failed");
    check(feed_chunks({{0x34}, {0x12}}) == std::vector<std::int16_t>{0x1234},
          "PCM Test 2 failed");
    check(feed_chunks({{0x34, 0x12, 0xfe}, {0xff}}) == std::vector<std::int16_t>{0x1234, -2},
          "PCM Test 3 failed");

    const std::vector<std::int16_t> expected{0, 1, -1, 0x1234, -32768, 32767, -2};
    std::vector<std::uint8_t> bytes;
    for (const auto sample : expected) {
        const auto raw = static_cast<std::uint16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(raw & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>(raw >> 8U));
    }
    std::mt19937 generator(20260820);
    std::uniform_int_distribution<std::size_t> split_size(1, 5);
    std::vector<std::vector<std::uint8_t>> chunks;
    for (std::size_t offset = 0; offset < bytes.size();) {
        const auto count = std::min(split_size(generator), bytes.size() - offset);
        chunks.emplace_back(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                            bytes.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    check(feed_chunks(chunks) == expected, "PCM Test 4 random split failed");

    edgeaudio::Pcm16StreamAssembler assembler;
    std::vector<std::int16_t> output;
    const std::uint8_t residual = 0x12;
    assembler.feed(&residual, 1, &output);
    assembler.reset();
    const std::uint8_t fresh_sample[] = {0x78, 0x56};
    assembler.feed(fresh_sample, sizeof(fresh_sample), &output);
    check(output == std::vector<std::int16_t>{0x5678}, "PCM client reset failed");
}

void test_audio_windows() {
    edgeaudio::AudioStreamBuffer buffer(100000);
    std::vector<std::int16_t> samples(72000);
    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<std::int16_t>(i % 30000);

    buffer.push(samples.data(), 47999);
    std::vector<std::int16_t> window;
    check(!buffer.read(0, edgeaudio::kYamnetWindowSamples, &window), "window Test A failed");
    buffer.push(samples.data() + 47999, 24001);
    check(buffer.read(0, edgeaudio::kYamnetWindowSamples, &window), "window Test B failed");
    check(window.front() == samples.front() && window.back() == samples[47999],
          "window Test B content failed");
    buffer.push(samples.data() + 72000 - 1, 0);
    buffer.push(samples.data() + 48000, 24000);
    check(buffer.read(edgeaudio::kYamnetHopSamples, edgeaudio::kYamnetWindowSamples, &window),
          "window Test C failed");
    check(window.front() == samples[24000] && window.back() == samples[71999],
          "window Test C overlap failed");

    edgeaudio::AudioStreamBuffer small(4);
    const std::int16_t sequence[] = {0, 1, 2, 3, 4, 5};
    small.push(sequence, 6);
    check(small.base_sample() == 2 && small.end_sample() == 6, "bounded buffer timeline failed");
    check(!small.read(0, 4, &window), "bounded buffer stale read failed");
    check(!small.read(7, 0, &window), "bounded buffer future read failed");
    check(small.read(2, 4, &window) && window == std::vector<std::int16_t>{2, 3, 4, 5},
          "bounded buffer wrapped read failed");
}

void test_top_k_and_labels() {
    const auto empty = edgeaudio::top_k({}, 3);
    check(empty.empty(), "Top-K empty input failed");
    const auto ranked = edgeaudio::top_k({0.2f, 0.9f, 0.9f, 0.1f}, 3);
    check(ranked.size() == 3 && ranked[0].index == 1 && ranked[1].index == 2 &&
              ranked[2].index == 0,
          "Top-K ordering/tie failed");
    check(edgeaudio::top_k({0.5f, 0.4f}, 10).size() == 2, "Top-K oversized K failed");

    edgeaudio::YamnetPostProcessor post({"Silence", "Speech", "Music"});
    std::vector<float> frame_scores(6 * 3, 0.0f);
    for (std::size_t frame = 0; frame < 6; ++frame) {
        frame_scores[frame * 3 + 1] = 0.8f;
        frame_scores[frame * 3 + 2] = 0.4f;
    }
    const auto result = post.process(frame_scores, 3000, 12.5, 6, 2);
    check(result.topk.size() == 2 && result.topk[0].label == "Speech" &&
              result.topk[1].label == "Music",
          "YAMNet postprocess label mapping failed");
    check(post.process({1.0f}, 0, 0.0, 0).topk.empty(), "postprocess invalid frame count failed");
}

}  // namespace

int main() {
    try {
        test_pcm16_framing();
        std::cout << "[PASS] PCM16 stream framing\n";
        test_audio_windows();
        std::cout << "[PASS] audio window overlap\n";
        test_top_k_and_labels();
        std::cout << "[PASS] Top-K and label postprocess\n";
        std::cout << "[PASS] EdgeAudio unit tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[FAIL] " << error.what() << "\n";
        return 1;
    }
}
