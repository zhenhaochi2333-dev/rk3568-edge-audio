#pragma once

#include <string>

namespace edgeaudio {

class CommandParser {
public:
    std::string parse(const std::string& text) const {
        if (contains(text, "\xE5\xBC\x80\xE5\xA7\x8B\xE7\x9B\x91\xE6\x8E\xA7") ||
            contains(text, "\xE5\x90\xAF\xE5\x8A\xA8\xE7\x9B\x91\xE6\x8E\xA7")) return "START_MONITORING";
        if (contains(text, "\xE5\x81\x9C\xE6\xAD\xA2\xE7\x9B\x91\xE6\x8E\xA7") ||
            contains(text, "\xE7\xBB\x93\xE6\x9D\x9F\xE7\x9B\x91\xE6\x8E\xA7")) return "STOP_MONITORING";
        if (contains(text, "\xE7\xB3\xBB\xE7\xBB\x9F\xE7\x8A\xB6\xE6\x80\x81") ||
            contains(text, "\xE6\x9F\xA5\xE7\x9C\x8B\xE7\x8A\xB6\xE6\x80\x81")) return "QUERY_STATUS";
        return {};
    }

private:
    static bool contains(const std::string& text, const std::string& token) {
        return text.find(token) != std::string::npos;
    }
};

}  // namespace edgeaudio

