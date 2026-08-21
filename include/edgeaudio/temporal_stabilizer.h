#pragma once

#include "audio_types.h"

#include <cstddef>
#include <deque>
#include <string>

namespace edgeaudio {

class AudioEventStabilizer {
public:
    explicit AudioEventStabilizer(std::size_t required_hits = 2)
        : required_hits_(required_hits) {}

    std::pair<std::string, std::string> update(const std::string& candidate) {
        if (candidate.empty()) return {stable_, ""};
        if (candidate == pending_) {
            ++pending_hits_;
        } else {
            pending_ = candidate;
            pending_hits_ = 1;
        }
        std::string transition;
        if (pending_hits_ >= required_hits_ && stable_ != pending_) {
            transition = stable_.empty() ? "EVENT_START" : "EVENT_CHANGE";
            stable_ = pending_;
        }
        return {stable_, transition};
    }

    std::string close() {
        if (stable_.empty()) return {};
        const auto previous = stable_;
        stable_.clear();
        pending_.clear();
        pending_hits_ = 0;
        return previous;
    }

    const std::string& stable() const { return stable_; }

private:
    std::size_t required_hits_;
    std::string pending_;
    std::size_t pending_hits_ = 0;
    std::string stable_;
};

}  // namespace edgeaudio

