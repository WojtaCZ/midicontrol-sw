#include "midicontrol/console.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace mc::console {

namespace {

const char* const kUpdateUsage =
    "Usage: update <base|remote> <name-update.bin>\n"
    "  (the trailered -update.bin from the firmware build; a plain\n"
    "  .bin is refused by the bootloader)\n";

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

// "word rest of line" -> word, rest (single separating space dropped).
void split(const std::string& line, std::string& word, std::string& rest) {
    std::istringstream iss(line);
    iss >> word;
    std::getline(iss, rest);
    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
}

}  // namespace

const char* const kCommandsHelp =
    "Commands: play <code>, stop, verse (add verse), list, record, on, off, status,\n"
    "          update <base|remote> <name-update.bin>, quit\n";

Command parse(const std::string& raw) {
    using Kind = Command::Kind;
    const std::string line = trim(raw);
    Command c;
    if (line.empty()) return c;

    std::string word, rest;
    split(line, word, rest);
    word = lower(word);

    if (word == "play" && !rest.empty()) {
        c.kind = Kind::Play;
        c.arg = rest;
    } else if (word == "stop") {
        c.kind = Kind::Stop;
    } else if (word == "verse" || word == "repeat") {
        c.kind = Kind::Verse;
    } else if (word == "list") {
        c.kind = Kind::List;
    } else if (word == "record") {
        c.kind = Kind::Record;
    } else if (word == "on") {
        c.kind = Kind::On;
    } else if (word == "off") {
        c.kind = Kind::Off;
    } else if (word == "status") {
        c.kind = Kind::Status;
    } else if (word == "update") {
        std::string target, path;
        split(rest, target, path);
        target = lower(target);
        if ((target != "base" && target != "remote") || path.empty()) {
            c.kind = Kind::Usage;
            c.arg = kUpdateUsage;
        } else {
            c.kind = Kind::Update;
            c.target = target;
            c.arg = path;
        }
    } else if (word == "quit" || word == "exit") {
        c.kind = Kind::Quit;
    } else {
        c.kind = Kind::Unknown;
        c.arg = line;
    }
    return c;
}

std::string absolutize(const std::string& line, const std::string& cwd) {
    Command c = parse(line);
    if (c.kind != Command::Kind::Update) return line;
    std::string path = c.arg;
    if (path[0] != '/') path = cwd + (cwd.empty() || cwd.back() == '/' ? "" : "/") + path;
    return "update " + c.target + " " + path;
}

std::string endMarker(bool ok) {
    return std::string(1, kEndOfReply) + (ok ? "OK\n" : "FAIL\n");
}

}  // namespace mc::console
