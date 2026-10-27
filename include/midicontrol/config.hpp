#ifndef MIDICONTROL_CONFIG_HPP
#define MIDICONTROL_CONFIG_HPP

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/organ.hpp"

// Loads config.conf (INI format):
//   [FILES]    PlayFolder=..., RecordFolder=...   (~/ and paths relative to
//              the config file are resolved by loadConfig)
//   [PLAYBACK] WarmupMs, KeepPowerOn, VerseGapMs (all optional, see Config)
// The song structure (prelude, verses) comes from the anchors in each MIDI file.
//   [BINDINGS] Bind0=song.mid, ...   (parsed, not used yet)
//   [WEB]      Enabled, Port, Bind (status page, all optional, see Config)
//   [ORGAN]    stop layout and Program Change mapping (all optional, see organ.hpp)
//   [DIAG]     LogPort (override of the log CDC auto-detection), Dir (diagnostics store)
namespace mc {

struct Config {
    std::string playFolder;
    std::string recFolder;
    std::map<std::string, std::string> bindings;  // e.g. "Bind0" -> "song name"

    // [PLAYBACK]
    int warmupMs = 3000;               // organ warm-up after power-on, before the first note
    bool keepPowerOn = false;          // leave organ power on between songs
    int verseGapMs = 0;                // silence before a section repeated with add-verse

    // [WEB] status page + Stops editor (writes stops.conf; 0.0.0.0 exposes that to the LAN)
    bool webEnabled = true;
    int webPort = 50118;
    std::string webBind = "0.0.0.0";   // IPv4 address to listen on

    // [DIAG] base log + remote diagnostics
    std::string diagLogPort;           // e.g. "/dev/ttyACM1"; empty = auto-detect "MIDIControl log"
    std::string diagDir;               // store directory; empty = defaultDiagDir() (filled in by loadConfig)

    // [ORGAN] stops per song (Opus 910 defaults)
    mc::organ::OrganConfig organ = mc::organ::defaultOrgan();

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
// Folder values starting with "~/" are expanded with $HOME and relative ones
// are taken relative to the config file's directory.
// Throws std::runtime_error on any problem.
Config loadConfig(const std::string& path);

// Resolve a folder value from the config (see loadConfig).
std::string resolveFolder(const std::string& value, const std::string& configDir,
                          const std::string& home);

// $XDG_STATE_HOME/midicontrol, else $HOME/.local/state/midicontrol, else /tmp/midicontrol-<uid>/diag.
std::string defaultDiagDir(const std::function<const char*(const char*)>& getenv);

// Pick the config file, first match wins:
//   1. `cliPath` (--config) if non-empty
//   2. $MIDICONTROL_CONFIG if set
//   3. $XDG_CONFIG_HOME/midicontrol/config.conf (default ~/.config/...) if it exists
//   4. ./config.conf
// `getenv` and `exists` are injected for testing.
using EnvLookup = std::function<const char*(const char*)>;
using FileExists = std::function<bool(const std::string&)>;
std::string resolveConfigPath(const std::string& cliPath, const EnvLookup& getenv,
                              const FileExists& exists);

}  // namespace mc

#endif  // MIDICONTROL_CONFIG_HPP
