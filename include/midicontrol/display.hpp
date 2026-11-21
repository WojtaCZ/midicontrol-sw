#ifndef MIDICONTROL_DISPLAY_HPP
#define MIDICONTROL_DISPLAY_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/sysex.hpp"

// Church-indicator state and its NOW_PLAYING_INFO encoding (PROTOCOL.md v2).
//
// Payload: [song_h, song_l, verse, letter, led]
//   song_h/song_l : BCD digits 3..0, nibble 0xF = blank digit
//   verse         : BCD tens/units, nibble 0xF = blank digit
//   letter        : 0x0A-0x0D = A-D, 0x0F = none
//   led           : 0x01-0x04 = red/green/blue/yellow, 0x0F = off
//   0xFF in a field = no change (song_h = 0xFF leaves both song bytes alone)
//   0xFE in song_h / verse = blank that field
//   empty payload = clear the whole indicator; [0x01] = clear it only if the base's own number is still shown
namespace mc::display {

enum class Field : uint8_t {
    NoChange,  // 0xFF
    Blank,     // 0xFE (song/verse), 0x0F (letter/led)
    Set,
};

// One indicator update. Fields left at NoChange are sent as 0xFF.
struct Update {
    bool clear = false;  // true -> empty payload (clears everything), or [NPI_CLEAR_IF_OWN] if `conditional`
    bool conditional = false;  // with `clear`: protocol v2 form, the base clears only a number it wrote itself

    Field song = Field::NoChange;
    std::array<uint8_t, 4> songDigits{0x0F, 0x0F, 0x0F, 0x0F};  // 0-9 or 0x0F, left to right

    Field verse = Field::NoChange;
    std::array<uint8_t, 2> verseDigits{0x0F, 0x0F};

    Field letter = Field::NoChange;
    uint8_t letterCode = 0x0F;  // 0x0A-0x0D, 0x0F = none

    Field led = Field::NoChange;
    uint8_t ledCode = 0x0F;  // 1-4, 0x0F = off
};

// Build a NOW_PLAYING_INFO payload.
std::vector<uint8_t> encodeNowPlaying(const Update& u);

// Update that clears the indicator: the empty payload, or with `conditional` the one-byte
// [NPI_CLEAR_IF_OWN] (only for a base that reports PROTO_VER >= 2).
Update clearUpdate(bool conditional = false);

// Convert a decoded display SysEx into an update: only the fields the SysEx
// carried are Set (digits forwarded exactly as authored), the rest NoChange.
Update fromSysEx(const mc::sysex::DisplayInfo& info);

// Display digits derived from a song code such as "406A" or "523": the
// leading number right-aligned into four digits and a trailing letter A-D.
// nullopt if the code does not look like <1-4 digits>[A-D].
struct CodeDisplay {
    std::array<uint8_t, 4> songDigits;
    uint8_t letterCode;  // 0x0A-0x0D or 0x0F
};
std::optional<CodeDisplay> fromSongCode(const std::string& code);

// Verse digits for verse `n`, following the authored layout: if the authored
// verse field holds a single digit (e.g. "1 " in 512.mid, tens position) and n
// is 1..9, n is placed in that same position; otherwise n is right-aligned.
std::array<uint8_t, 2> verseDigits(int n, const std::array<uint8_t, 2>& authoredLayout);

}  // namespace mc::display

#endif  // MIDICONTROL_DISPLAY_HPP
