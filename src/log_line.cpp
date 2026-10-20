#include "midicontrol/log_line.hpp"

#include <cctype>

namespace mc::diag {

std::optional<LogLine> parseLogLine(std::string_view s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
    size_t i = 0;
    if (i >= s.size() || s[i] != '[') return std::nullopt;
    ++i;
    while (i < s.size() && s[i] == ' ') ++i;
    uint64_t sec = 0;
    size_t digits = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
        sec = sec * 10 + static_cast<uint64_t>(s[i] - '0');
        ++i;
        ++digits;
    }
    if (digits == 0 || i >= s.size() || s[i] != '.') return std::nullopt;
    ++i;
    if (i + 3 >= s.size()) return std::nullopt;
    uint64_t ms = 0;
    for (int k = 0; k < 3; ++k, ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return std::nullopt;
        ms = ms * 10 + static_cast<uint64_t>(s[i] - '0');
    }
    if (s[i] != ']' || i + 2 >= s.size() || s[i + 1] != ' ') return std::nullopt;
    char lvl = s[i + 2];
    if (lvl != 'E' && lvl != 'W' && lvl != 'I' && lvl != 'D') return std::nullopt;
    i += 3;
    if (i >= s.size() || s[i] != ' ') return std::nullopt;
    ++i;
    size_t colon = s.find(':', i);
    if (colon == std::string_view::npos || colon == i) return std::nullopt;
    LogLine l;
    l.uptimeMs = sec * 1000 + ms;
    l.level = lvl;
    l.tag = std::string(s.substr(i, colon - i));
    if (l.tag.find(' ') != std::string::npos) return std::nullopt;
    size_t t = colon + 1;
    if (t < s.size() && s[t] == ' ') ++t;
    std::string_view text = s.substr(t);
    if (!text.empty() && text.back() == '~') {
        l.truncated = true;
        text.remove_suffix(1);
    }
    l.text = std::string(text);
    return l;
}

}  // namespace mc::diag
