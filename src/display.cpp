#include "midicontrol/display.hpp"

#include <cctype>

#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/songs.hpp"

namespace mc::display {

using mc::protocol::NPI_BLANK;
using mc::protocol::NPI_DIGIT_BLANK;
using mc::protocol::NPI_NO_CHANGE;

namespace {

uint8_t nibble(uint8_t d) { return d <= 9 ? d : NPI_DIGIT_BLANK; }

uint8_t bcd(uint8_t hi, uint8_t lo) {
    return static_cast<uint8_t>((nibble(hi) << 4) | nibble(lo));
}

uint8_t encodeLetter(char c) {
    if (c >= 'A' && c <= 'D') return static_cast<uint8_t>(0x0A + (c - 'A'));
    return 0x0F;
}

}  // namespace

std::vector<uint8_t> encodeNowPlaying(const Update& u) {
    if (u.clear) return {};

    std::vector<uint8_t> out(5, NPI_NO_CHANGE);

    if (u.song == Field::Blank) {
        out[0] = NPI_BLANK;
        out[1] = NPI_BLANK;
    } else if (u.song == Field::Set) {
        out[0] = bcd(u.songDigits[0], u.songDigits[1]);
        out[1] = bcd(u.songDigits[2], u.songDigits[3]);
        // "  12" encodes as 0xFF 0x12, which is valid: the song is "no change"
        // only when both bytes are 0xFF, i.e. all four digits blank.
        if (out[0] == NPI_NO_CHANGE && out[1] == NPI_NO_CHANGE) {
            out[0] = NPI_BLANK;
            out[1] = NPI_BLANK;
        }
    }

    if (u.verse == Field::Blank) {
        out[2] = NPI_BLANK;
    } else if (u.verse == Field::Set) {
        out[2] = bcd(u.verseDigits[0], u.verseDigits[1]);
        if (out[2] == NPI_NO_CHANGE) out[2] = NPI_BLANK;  // both digits blank
    }

    if (u.letter == Field::Blank) {
        out[3] = 0x0F;
    } else if (u.letter == Field::Set) {
        out[3] = (u.letterCode >= 0x0A && u.letterCode <= 0x0D) ? u.letterCode : 0x0F;
    }

    if (u.led == Field::Blank) {
        out[4] = 0x0F;
    } else if (u.led == Field::Set) {
        out[4] = (u.ledCode >= 1 && u.ledCode <= 4) ? u.ledCode : 0x0F;
    }
    return out;
}

Update clearUpdate() {
    Update u;
    u.clear = true;
    return u;
}

Update fromSysEx(const mc::sysex::DisplayInfo& info) {
    Update u;
    if (info.hasSong) {
        u.song = Field::Set;
        u.songDigits = info.songDigits;
    }
    if (info.hasVerse) {
        u.verse = Field::Set;
        u.verseDigits = info.verseDigits;
    }
    if (info.hasLetter) {
        u.letter = Field::Set;
        u.letterCode = encodeLetter(info.letter);
    }
    if (info.hasLed) {
        u.led = Field::Set;
        u.ledCode = static_cast<uint8_t>(info.led);
    }
    return u;
}

std::optional<CodeDisplay> fromSongCode(const std::string& code) {
    if (!mc::songs::validCode(code)) return std::nullopt;  // PCW-1 / P1-4: same rule as everywhere (<= 1999)
    size_t n = 0;
    while (n < code.size() && std::isdigit(static_cast<unsigned char>(code[n]))) ++n;
    if (n == 0 || n > 4) return std::nullopt;

    CodeDisplay cd{{NPI_DIGIT_BLANK, NPI_DIGIT_BLANK, NPI_DIGIT_BLANK, NPI_DIGIT_BLANK}, 0x0F};
    for (size_t i = 0; i < n; ++i) {
        cd.songDigits[4 - n + i] = static_cast<uint8_t>(code[i] - '0');
    }
    if (n < code.size()) {
        if (code.size() != n + 1) return std::nullopt;
        char c = static_cast<char>(std::toupper(static_cast<unsigned char>(code[n])));
        if (c < 'A' || c > 'D') return std::nullopt;
        cd.letterCode = encodeLetter(c);
    }
    return cd;
}

std::array<uint8_t, 2> verseDigits(int n, const std::array<uint8_t, 2>& authoredLayout) {
    if (n < 0) n = 0;
    n %= 100;
    const bool tensOnly = authoredLayout[0] <= 9 && authoredLayout[1] > 9;
    if (tensOnly && n >= 1 && n <= 9) {
        return {static_cast<uint8_t>(n), NPI_DIGIT_BLANK};
    }
    uint8_t tens = n >= 10 ? static_cast<uint8_t>(n / 10) : NPI_DIGIT_BLANK;
    return {tens, static_cast<uint8_t>(n % 10)};
}

}  // namespace mc::display
