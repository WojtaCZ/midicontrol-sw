#include "midicontrol/sysex.hpp"

namespace mc::sysex {

namespace {

// Decode a run of per-digit bytes into an integer, skipping blanks (0x0F) and
// any out-of-range byte. "01 02 03 04" -> 1234; "0F 04 00 01" -> 401.
int decodeDigits(const uint8_t* d, size_t n) {
    int value = 0;
    for (size_t i = 0; i < n; ++i) {
        if (d[i] <= 9) {
            value = value * 10 + d[i];
        }
        // 0x0F (blank) or anything else: skip.
    }
    return value;
}

// Encode an integer right-aligned into `n` per-digit bytes, blanking leading
// zeros. 401 over 4 -> {0x0F, 0x04, 0x00, 0x01}; 0 -> {0x0F, ..., 0x00}.
void encodeDigits(int value, size_t n, std::vector<uint8_t>& out) {
    if (value < 0) value = 0;
    std::vector<uint8_t> digits(n, 0);
    for (size_t i = 0; i < n; ++i) {
        digits[n - 1 - i] = static_cast<uint8_t>(value % 10);
        value /= 10;
    }
    // Blank leading zeros, but always keep the last (units) digit.
    for (size_t i = 0; i + 1 < n; ++i) {
        if (digits[i] == 0) {
            digits[i] = BLANK_DIGIT;
        } else {
            break;
        }
    }
    out.insert(out.end(), digits.begin(), digits.end());
}

// Copy per-digit bytes as authored, mapping anything that is not a digit to a
// blank so the result is always a valid BCD nibble.
template <size_t N>
std::array<uint8_t, N> rawDigits(const uint8_t* d) {
    std::array<uint8_t, N> out{};
    for (size_t i = 0; i < N; ++i) out[i] = d[i] <= 9 ? d[i] : BLANK_DIGIT;
    return out;
}

char decodeLetter(uint8_t b) {
    if (b >= 0x0A && b <= 0x0D) return static_cast<char>('A' + (b - 0x0A));
    return LETTER_OFF;
}

uint8_t encodeLetter(char c) {
    if (c >= 'A' && c <= 'D') return static_cast<uint8_t>(0x0A + (c - 'A'));
    return 0x0F;  // off
}

Led decodeLed(uint8_t b) {
    switch (b) {
        case 1: return Led::Red;
        case 2: return Led::Green;
        case 3: return Led::Blue;
        case 4: return Led::Yellow;
        default: return Led::Off;
    }
}

}  // namespace

std::optional<DisplayInfo> parse(const uint8_t* body, size_t len) {
    // Need at least header(2) + device(1) + target(1).
    if (len < 4) return std::nullopt;
    if (body[0] != HEADER0 || body[1] != HEADER1) return std::nullopt;
    if (body[2] != DEVICE_DISPLAY) return std::nullopt;

    const uint8_t target = body[3];
    const uint8_t* d = body + 4;
    const size_t n = len - 4;

    DisplayInfo info;
    switch (static_cast<Target>(target)) {
        case Target::FullDisplay:
            if (n < 8) return std::nullopt;  // 4 song + 2 verse + letter + led
            info.song = decodeDigits(d, 4);
            info.verse = decodeDigits(d + 4, 2);
            info.songDigits = rawDigits<4>(d);
            info.verseDigits = rawDigits<2>(d + 4);
            info.letter = decodeLetter(d[6]);
            info.led = decodeLed(d[7]);
            info.hasSong = info.hasVerse = info.hasLetter = info.hasLed = true;
            break;
        case Target::Song:
            if (n < 4) return std::nullopt;
            info.song = decodeDigits(d, 4);
            info.songDigits = rawDigits<4>(d);
            info.hasSong = true;
            break;
        case Target::Verse:
            if (n < 2) return std::nullopt;
            info.verse = decodeDigits(d, 2);
            info.verseDigits = rawDigits<2>(d);
            info.hasVerse = true;
            break;
        case Target::Letter:
            if (n < 1) return std::nullopt;
            info.letter = decodeLetter(d[0]);
            info.hasLetter = true;
            break;
        case Target::Led:
            if (n < 1) return std::nullopt;
            info.led = decodeLed(d[0]);
            info.hasLed = true;
            break;
        default:
            return std::nullopt;  // reserved target
    }
    return info;
}

std::optional<DisplayInfo> parse(const std::vector<uint8_t>& body) {
    return parse(body.data(), body.size());
}

std::vector<uint8_t> build(Target target, const DisplayInfo& info) {
    std::vector<uint8_t> out{HEADER0, HEADER1, DEVICE_DISPLAY, static_cast<uint8_t>(target)};
    switch (target) {
        case Target::FullDisplay:
            encodeDigits(info.song, 4, out);
            encodeDigits(info.verse, 2, out);
            out.push_back(encodeLetter(info.letter));
            out.push_back(static_cast<uint8_t>(info.led));
            break;
        case Target::Song:
            encodeDigits(info.song, 4, out);
            break;
        case Target::Verse:
            encodeDigits(info.verse, 2, out);
            break;
        case Target::Letter:
            out.push_back(encodeLetter(info.letter));
            break;
        case Target::Led:
            out.push_back(static_cast<uint8_t>(info.led));
            break;
    }
    return out;
}

std::vector<uint8_t> buildFull(const DisplayInfo& info) {
    return build(Target::FullDisplay, info);
}

std::vector<uint8_t> wrap(const std::vector<uint8_t>& body) {
    std::vector<uint8_t> out;
    out.reserve(body.size() + 2);
    out.push_back(0xF0);
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(0xF7);
    return out;
}

std::string toString(const DisplayInfo& info) {
    auto pad = [](int v, int width) {
        std::string s = std::to_string(v);
        while (static_cast<int>(s.size()) < width) s = " " + s;
        return s;
    };
    std::string s;
    s += "song=" + pad(info.song, 4);
    s += " verse=" + pad(info.verse, 2);
    s += " letter=";
    s += (info.letter == LETTER_OFF ? '-' : info.letter);
    s += " led=";
    switch (info.led) {
        case Led::Red:    s += "red"; break;
        case Led::Green:  s += "green"; break;
        case Led::Blue:   s += "blue"; break;
        case Led::Yellow: s += "yellow"; break;
        default:          s += "off"; break;
    }
    return s;
}

}  // namespace mc::sysex
