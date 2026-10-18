#ifndef MIDICONTROL_SYSEX_HPP
#define MIDICONTROL_SYSEX_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// MIDIControl display SysEx messages (current "per-digit" spec).
//
// Wire structure (the bytes between the standard MIDI F0 / F7 markers):
//
//     [0x4D 0x43] [device] [target] [data...]
//
// Header is always 0x4D 0x43 ("MC"); device 0x01 is the display. Numbers are
// encoded one digit per byte (0x00-0x09, or 0x0F for a blank), so every byte is
// <= 0x0F and therefore valid 7-bit MIDI data. Targets:
//
//     0x00 full display : [S3 S2 S1 S0] [V1 V0] [LETTER] [LED]
//     0x01 song only     : [S3 S2 S1 S0]
//     0x02 verse only    : [V1 V0]
//     0x03 letter only   : [LETTER]
//     0x04 LED only       : [LED]
//
// LETTER: 0x0A-0x0D -> 'A'-'D', 0x0F -> off.
// LED:    1=red 2=green 3=blue 4=yellow, 0x0F -> off.
namespace mc::sysex {

inline constexpr uint8_t HEADER0 = 0x4D;  // 'M'
inline constexpr uint8_t HEADER1 = 0x43;  // 'C'
inline constexpr uint8_t DEVICE_DISPLAY = 0x01;

enum class Target : uint8_t {
    FullDisplay = 0x00,
    Song        = 0x01,
    Verse       = 0x02,
    Letter      = 0x03,
    Led         = 0x04,
};

enum class Led : uint8_t {
    Off    = 0x0F,
    Red    = 0x01,
    Green  = 0x02,
    Blue   = 0x03,
    Yellow = 0x04,
};

inline constexpr uint8_t BLANK_DIGIT = 0x0F;
inline constexpr char LETTER_OFF = '\0';

// Decoded display state. Fields not present in a partial-update message keep
// their default (so callers can merge against the previous state if desired).
struct DisplayInfo {
    int song = 0;       // 0..1999
    int verse = 0;      // 0..99
    char letter = LETTER_OFF;  // 'A'-'D' or '\0' (off)
    Led led = Led::Off;

    // Which fields this particular message actually carried.
    bool hasSong = false;
    bool hasVerse = false;
    bool hasLetter = false;
    bool hasLed = false;

    // The digits exactly as authored (one per byte, 0-9 or BLANK_DIGIT), so a
    // number placed off its usual right-aligned position (e.g. "513 " or verse
    // "1 ") can be forwarded to the indicator unchanged. Valid when hasSong /
    // hasVerse. Not part of operator== (which compares decoded values).
    std::array<uint8_t, 4> songDigits{BLANK_DIGIT, BLANK_DIGIT, BLANK_DIGIT, BLANK_DIGIT};
    std::array<uint8_t, 2> verseDigits{BLANK_DIGIT, BLANK_DIGIT};

    bool operator==(const DisplayInfo& o) const {
        return song == o.song && verse == o.verse && letter == o.letter && led == o.led &&
               hasSong == o.hasSong && hasVerse == o.hasVerse && hasLetter == o.hasLetter &&
               hasLed == o.hasLed;
    }
};

// Parse a SysEx body (bytes WITHOUT the surrounding F0/F7). Returns nullopt if
// the message is not a valid MIDIControl display message.
std::optional<DisplayInfo> parse(const std::vector<uint8_t>& body);
std::optional<DisplayInfo> parse(const uint8_t* body, size_t len);

// Build a SysEx body (no F0/F7) for the given target from a DisplayInfo.
std::vector<uint8_t> build(Target target, const DisplayInfo& info);

// Convenience: a full display body and a full SysEx message (with F0/F7).
std::vector<uint8_t> buildFull(const DisplayInfo& info);
std::vector<uint8_t> wrap(const std::vector<uint8_t>& body);  // prepend F0, append F7

// Human-readable rendering, e.g. "song= 401 verse= 1 letter=- led=off".
std::string toString(const DisplayInfo& info);

}  // namespace mc::sysex

#endif  // MIDICONTROL_SYSEX_HPP
