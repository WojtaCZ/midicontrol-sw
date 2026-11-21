#include <vector>

#include "midicontrol/display.hpp"
#include "midicontrol/sysex.hpp"
#include "test_framework.hpp"

using namespace mc::display;
using Bytes = std::vector<uint8_t>;

namespace {
void checkBytes(const Bytes& got, const Bytes& expected) {
    if (got != expected) {
        FAIL_AT("got [" + ::mc::test::toHex(got) + "] expected [" + ::mc::test::toHex(expected) +
                "]");
    }
}
}  // namespace

TEST(npi_full_update_right_aligned) {
    Update u;
    u.song = Field::Set;
    u.songDigits = {0x0F, 4, 0, 1};
    u.verse = Field::Set;
    u.verseDigits = {0x0F, 1};
    u.letter = Field::Set;
    u.letterCode = 0x0F;
    u.led = Field::Set;
    u.ledCode = 2;
    checkBytes(encodeNowPlaying(u), {0xF4, 0x01, 0xF1, 0x0F, 0x02});
}

TEST(npi_untouched_fields_are_no_change) {
    Update u;
    u.verse = Field::Set;
    u.verseDigits = {0x0F, 3};
    checkBytes(encodeNowPlaying(u), {0xFF, 0xFF, 0xF3, 0xFF, 0xFF});
    checkBytes(encodeNowPlaying(Update{}), {0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
}

TEST(npi_blank_fields) {
    Update u;
    u.song = Field::Blank;
    u.verse = Field::Blank;
    u.letter = Field::Blank;
    u.led = Field::Blank;
    checkBytes(encodeNowPlaying(u), {0xFE, 0xFE, 0xFE, 0x0F, 0x0F});
}

TEST(npi_clear_is_empty_payload) { CHECK(encodeNowPlaying(clearUpdate()).empty()); }

// A3: the conditional clear (protocol v2) is the single byte NPI_CLEAR_IF_OWN.
TEST(npi_conditional_clear_is_one_byte_0x01) {
    checkBytes(encodeNowPlaying(clearUpdate(true)), {0x01});
    CHECK(encodeNowPlaying(clearUpdate(false)).empty());
}

TEST(npi_short_song_number_keeps_0xff_high_byte) {
    // "  12" is 0xFF 0x12: no-change for the song needs both bytes 0xFF.
    Update u;
    u.song = Field::Set;
    u.songDigits = {0x0F, 0x0F, 1, 2};
    checkBytes(encodeNowPlaying(u), {0xFF, 0x12, 0xFF, 0xFF, 0xFF});
}

TEST(npi_ambiguous_all_blank_bytes_become_blank_field) {
    // Song "    " and verse "  " would encode as 0xFF (= no change).
    Update u;
    u.song = Field::Set;
    u.songDigits = {0x0F, 0x0F, 0x0F, 0x0F};
    u.verse = Field::Set;
    u.verseDigits = {0x0F, 0x0F};
    checkBytes(encodeNowPlaying(u), {0xFE, 0xFE, 0xFE, 0xFF, 0xFF});
}

TEST(npi_forwards_authored_digit_positions) {
    // 513.mid: song "513 " (shifted left), verse " 1".
    auto info513 = mc::sysex::parse(
        Bytes{0x4D, 0x43, 0x01, 0x00, 0x05, 0x01, 0x03, 0x0F, 0x0F, 0x01, 0x0F, 0x0F});
    CHECK(info513.has_value());
    checkBytes(encodeNowPlaying(fromSysEx(*info513)), {0x51, 0x3F, 0xF1, 0x0F, 0x0F});

    // 512.mid: song " 512", verse "1 " (tens position).
    auto info512 = mc::sysex::parse(
        Bytes{0x4D, 0x43, 0x01, 0x00, 0x0F, 0x05, 0x01, 0x02, 0x01, 0x0F, 0x0F, 0x0F});
    CHECK(info512.has_value());
    checkBytes(encodeNowPlaying(fromSysEx(*info512)), {0xF5, 0x12, 0x1F, 0x0F, 0x0F});
}

TEST(npi_partial_sysex_only_sets_carried_fields) {
    auto letter = mc::sysex::parse(Bytes{0x4D, 0x43, 0x01, 0x03, 0x0B});
    CHECK(letter.has_value());
    checkBytes(encodeNowPlaying(fromSysEx(*letter)), {0xFF, 0xFF, 0xFF, 0x0B, 0xFF});
}

TEST(display_from_song_code) {
    auto a = fromSongCode("406A");
    CHECK(a.has_value());
    CHECK(a->songDigits == (std::array<uint8_t, 4>{0x0F, 4, 0, 6}));
    CHECK_EQ(static_cast<int>(a->letterCode), 0x0A);

    auto b = fromSongCode("523");
    CHECK(b.has_value());
    CHECK(b->songDigits == (std::array<uint8_t, 4>{0x0F, 5, 2, 3}));
    CHECK_EQ(static_cast<int>(b->letterCode), 0x0F);

    CHECK(fromSongCode("1234")->songDigits == (std::array<uint8_t, 4>{1, 2, 3, 4}));
    CHECK_FALSE(fromSongCode("12345").has_value());
    CHECK_FALSE(fromSongCode("ABC").has_value());
    CHECK_FALSE(fromSongCode("406E").has_value());
    CHECK_FALSE(fromSongCode("406AB").has_value());
}

TEST(verse_digits_follow_authored_layout) {
    using V = std::array<uint8_t, 2>;
    // Default right alignment.
    CHECK(verseDigits(2, V{0x0F, 1}) == (V{0x0F, 2}));
    CHECK(verseDigits(12, V{0x0F, 1}) == (V{1, 2}));
    // 512.mid style: single digit authored in the tens position.
    CHECK(verseDigits(1, V{1, 0x0F}) == (V{1, 0x0F}));
    CHECK(verseDigits(3, V{1, 0x0F}) == (V{3, 0x0F}));
    CHECK(verseDigits(10, V{1, 0x0F}) == (V{1, 0}));  // two digits need both places
    // No authored layout (523.mid).
    CHECK(verseDigits(4, V{0x0F, 0x0F}) == (V{0x0F, 4}));
}
