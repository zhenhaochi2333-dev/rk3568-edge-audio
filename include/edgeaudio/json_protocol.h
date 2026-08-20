#pragma once

#include "audio_types.h"

#include <iomanip>
#include <sstream>
#include <string>

namespace edgeaudio {

inline std::string json_escape(const std::string& value) {
    std::string out;
    for (const char ch : value) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

inline std::string json_number(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2) << value;
    return stream.str();
}

inline std::string asr_json(const AsrResult& result, const std::string& backend,
                            const std::string& command = {}) {
    std::ostringstream out;
    out << "{\"type\":\"asr\",\"timestamp_ms\":" << result.timestamp_ms
        << ",\"text\":\"" << json_escape(result.text) << "\",\"final\":"
        << (result.final ? "true" : "false") << ",\"latency_ms\":"
        << json_number(result.latency_ms) << ",\"rtf\":" << json_number(result.rtf)
        << ",\"backend\":\"" << json_escape(backend) << "\"";
    if (!command.empty()) out << ",\"command\":\"" << json_escape(command) << "\"";
    out << "}";
    return out.str();
}

inline std::string event_json(const SoundEventResult& result, const std::string& backend) {
    std::ostringstream out;
    out << "{\"type\":\"sound_event\",\"timestamp_ms\":" << result.timestamp_ms
        << ",\"topk\":[";
    for (std::size_t i = 0; i < result.topk.size(); ++i) {
        if (i) out << ',';
        out << "{\"index\":" << result.topk[i].index << ",\"label\":\""
            << json_escape(result.topk[i].label) << "\",\"score\":"
            << json_number(result.topk[i].score) << "}";
    }
    out << "],\"stable_event\":\"" << json_escape(result.stable_event)
        << "\",\"transition\":\"" << json_escape(result.transition)
        << "\",\"inference_ms\":" << json_number(result.inference_ms)
        << ",\"backend\":\"" << json_escape(backend) << "\"}";
    return out.str();
}

inline std::string status_json(const std::string& yamnet_backend,
                               const std::string& asr_backend,
                               std::uint64_t samples, bool speech) {
    std::ostringstream out;
    out << "{\"type\":\"status\",\"timestamp_ms\":"
        << (samples * 1000 / kSampleRate) << ",\"audio_samples\":" << samples
        << ",\"speech\":" << (speech ? "true" : "false")
        << ",\"yamnet_backend\":\"" << json_escape(yamnet_backend)
        << "\",\"asr_backend\":\"" << json_escape(asr_backend) << "\"}";
    return out.str();
}

}  // namespace edgeaudio

