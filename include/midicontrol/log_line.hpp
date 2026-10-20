#ifndef MIDICONTROL_LOG_LINE_HPP
#define MIDICONTROL_LOG_LINE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mc::diag {

// One text line of the base's log CDC: "[    12.345] W ble: cmd failed: C".
struct LogLine {
    uint64_t uptimeMs = 0;
    char level = 'I';  // E W I D
    std::string tag, text;
    bool truncated = false;  // the base marks a cut-off line with a trailing '~'
};

// Trailing CR/LF are stripped; a trailing '~' sets `truncated` and is removed.
// Anything that does not match the format yields nullopt.
std::optional<LogLine> parseLogLine(std::string_view line);

}  // namespace mc::diag

#endif
