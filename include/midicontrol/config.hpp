#ifndef MIDICONTROL_CONFIG_HPP
#define MIDICONTROL_CONFIG_HPP

#include <map>
#include <optional>
#include <string>
#include <vector>

// Loads config.conf (INI format), mirroring the Python config.py:
//   [FILES]    PlayFolder=..., RecordFolder=...
//   [BINDINGS] Bind0=song.mid, ...
namespace mc {

struct Config {
    std::string playFolder;
    std::string recFolder;
    std::map<std::string, std::string> bindings;  // e.g. "Bind0" -> "song name"

    // Look up a button binding by key (e.g. "Bind0"). nullopt if absent.
    std::optional<std::string> binding(const std::string& key) const;
};

// Parse INI text into sections -> (key -> value). Comments start with '#' or ';'.
// Keys are case-sensitive; section names are upper-cased for convenience.
std::map<std::string, std::map<std::string, std::string>> parseIni(const std::string& text);

// Build a Config from raw INI text. Throws std::runtime_error if required keys
// are missing. Does NOT touch the filesystem (so it is unit-testable).
Config parseConfig(const std::string& iniText);

// Load and validate config from a file path, checking that the folders exist.
// Throws std::runtime_error on any problem.
Config loadConfig(const std::string& path);

}  // namespace mc

#endif  // MIDICONTROL_CONFIG_HPP
