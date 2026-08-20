#pragma once

#include "audio_types.h"
#include "temporal_stabilizer.h"

#include <cstddef>
#include <string>
#include <vector>

namespace edgeaudio {

std::vector<SoundEvent> top_k(const std::vector<float>& scores, std::size_t k);

class YamnetPostProcessor {
public:
    explicit YamnetPostProcessor(std::vector<std::string> labels);
    explicit YamnetPostProcessor(const std::string& labels_path);

    SoundEventResult process(const std::vector<float>& frame_scores,
                             std::uint64_t timestamp_ms, double inference_ms,
                             std::size_t frame_count = 6,
                             std::size_t top_k_count = 5);

    const std::vector<std::string>& labels() const { return labels_; }

private:
    static std::vector<std::string> load_labels(const std::string& path);

    std::vector<std::string> labels_;
    AudioEventStabilizer stabilizer_;
};

}  // namespace edgeaudio
