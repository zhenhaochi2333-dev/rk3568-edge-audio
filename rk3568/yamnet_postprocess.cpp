#include "edgeaudio/yamnet_postprocess.h"

#include <algorithm>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace edgeaudio {

std::vector<SoundEvent> top_k(const std::vector<float>& scores, std::size_t k) {
    k = std::min(k, scores.size());
    std::vector<int> indices(scores.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::partial_sort(indices.begin(), indices.begin() + k, indices.end(),
                      [&](int left, int right) {
                          if (scores[left] != scores[right]) return scores[left] > scores[right];
                          return left < right;
                      });

    std::vector<SoundEvent> result;
    result.reserve(k);
    for (std::size_t i = 0; i < k; ++i) {
        result.push_back({indices[i], {}, scores[indices[i]]});
    }
    return result;
}

YamnetPostProcessor::YamnetPostProcessor(std::vector<std::string> labels)
    : labels_(std::move(labels)) {}

YamnetPostProcessor::YamnetPostProcessor(const std::string& labels_path)
    : labels_(load_labels(labels_path)) {}

SoundEventResult YamnetPostProcessor::process(const std::vector<float>& frame_scores,
                                              std::uint64_t timestamp_ms,
                                              double inference_ms,
                                              std::size_t frame_count,
                                              std::size_t top_k_count) {
    SoundEventResult result;
    result.timestamp_ms = timestamp_ms;
    result.inference_ms = inference_ms;
    if (frame_count == 0 || labels_.empty() || frame_scores.size() % frame_count != 0) {
        return result;
    }

    const std::size_t class_count = frame_scores.size() / frame_count;
    std::vector<float> averaged(class_count, 0.0f);
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (std::size_t index = 0; index < class_count; ++index) {
            averaged[index] += frame_scores[frame * class_count + index] /
                               static_cast<float>(frame_count);
        }
    }
    result.topk = top_k(averaged, top_k_count);
    for (auto& event : result.topk) {
        if (static_cast<std::size_t>(event.index) < labels_.size()) {
            event.label = labels_[event.index];
        }
    }
    if (!result.topk.empty()) {
        const auto [stable, transition] = stabilizer_.update(result.topk.front().label);
        result.stable_event = stable;
        result.transition = transition;
    }
    return result;
}

std::vector<std::string> YamnetPostProcessor::load_labels(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open label file: " + path);

    std::vector<std::string> labels;
    std::string line;
    std::getline(file, line);
    while (std::getline(file, line)) {
        const auto first = line.find(',');
        const auto second = line.find(',', first == std::string::npos ? first : first + 1);
        if (first == std::string::npos || second == std::string::npos) continue;
        const int index = std::stoi(line.substr(0, first));
        if (index < 0) continue;
        std::string name = line.substr(second + 1);
        if (!name.empty() && name.back() == '\r') name.pop_back();
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            name = name.substr(1, name.size() - 2);
        }
        if (static_cast<std::size_t>(index) >= labels.size()) {
            labels.resize(static_cast<std::size_t>(index) + 1U);
        }
        labels[static_cast<std::size_t>(index)] = std::move(name);
    }
    return labels;
}

}  // namespace edgeaudio
