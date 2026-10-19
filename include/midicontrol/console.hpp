#ifndef MIDICONTROL_CONSOLE_HPP
#define MIDICONTROL_CONSOLE_HPP

#include <string>

// The terminal command line, shared by standalone mode and the thin client
// attached to the running service (control socket).
//
// Socket protocol: the client sends one command per line; the service streams
// the command's text output and ends it with endMarker() on its own line.
namespace mc::console {

struct Command {
    enum class Kind { Empty, Play, Stop, Verse, List, Record, On, Off, Status, Update, Quit, Usage, Unknown };
    Kind kind = Kind::Empty;
    std::string arg;     // Play: code as typed; Update: path; Usage: usage text; Unknown: the line
    std::string target;  // Update: "base" or "remote"
};

Command parse(const std::string& line);

// For "update <target> <path>" with a relative path, the same line with the
// path made absolute against `cwd` (the service resolves paths itself, from
// its own working directory). Other lines are returned unchanged.
std::string absolutize(const std::string& line, const std::string& cwd);

extern const char* const kCommandsHelp;

inline constexpr char kEndOfReply = '\x1e';
std::string endMarker(bool ok);  // "\x1eOK\n" or "\x1eFAIL\n"

}  // namespace mc::console

#endif  // MIDICONTROL_CONSOLE_HPP
