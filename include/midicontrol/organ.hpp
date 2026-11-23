#ifndef MIDICONTROL_ORGAN_HPP
#define MIDICONTROL_ORGAN_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/playback.hpp"

// Organ stops: the stop table (Johannus Opus 910 by default), the [ORGAN] config
// section and the preamble (all-off SysEx + one Program Change per stop) sent
// before a song. A stop is toggled by a Program Change on `channel`; its program
// is the octal value of the schematic number (STnn) plus `programOffset`, unless
// the stop has an explicit override.
namespace mc::organ {

struct Stop {
    int number;                // 1-based, in console order
    std::string name;
    std::string st;            // schematic number, octal digits ("" = none)
    std::optional<int> program;  // explicit program (0..127), overrides st + offset
};

struct StopGroup {
    std::string label;
    int first;
    int last;
};

struct OrganConfig {
    bool stopsEnabled = true;
    int channel = 12;                  // 1..16
    int programOffset = -1;
    std::vector<uint8_t> allOffSysex;  // default F0 00 4A 4F 48 41 53 7F F7
    int allOffDelayMs = 100;
    int stopDelayMs = 20;
    bool dropSongProgramChanges = true;
    // Default registration for songs without a file (B8): nullopt = not set, such songs leave the organ alone;
    // set = ascending, unique, 1..stopCount; set but empty = all stops off (owner 2026-11-23).
    std::optional<std::vector<int>> defaultStops;
    std::vector<StopGroup> groups;     // ranges contiguous from 1, no overlap
    std::vector<Stop> stops;           // stops[i].number == i + 1
    int stopCount() const { return static_cast<int>(stops.size()); }
};

// The Opus 910 table.
OrganConfig defaultOrgan();

// Apply an [ORGAN] section (key -> value) on top of the defaults.
// Throws std::runtime_error("config: ...") on bad values.
OrganConfig parseOrgan(const std::map<std::string, std::string>& section);

// Program for a stop (override, else octal(st) + offset); -1 if the number is unknown.
int programFor(const OrganConfig& organ, int stopNumber);

// All-off SysEx, then a Program Change for each stop in `stops` (the last step
// also waits stopDelayMs). An empty list gives the all-off step only.
std::vector<MidiStep> buildPreamble(const OrganConfig& organ, const std::vector<int>& stops);

}  // namespace mc::organ

#endif  // MIDICONTROL_ORGAN_HPP
