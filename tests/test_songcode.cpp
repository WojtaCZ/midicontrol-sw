// PCW-1 / Q1-2 a (owner 2026-11-14): a song code is 1-4 digits, value <= 1999, optional single
// A-D suffix (case-insensitive). One rule for every site: protocol PLAY_SONG, CLI/console play,
// song list (web, SONG_LIST_RESP), stops editor PUT, songcheck, display.
// Moved from the step-2 verification red test (verify-pc.md, test_red_songcode.cpp).
#include "test_framework.hpp"
#include "midicontrol/songs.hpp"
#include "midicontrol/display.hpp"

using mc::songs::sanitizeCode;

TEST(pcw1_valid_codes_accepted) {
    CHECK(sanitizeCode(std::string("1")).has_value());
    CHECK(sanitizeCode(std::string("406A")).has_value());
    CHECK(sanitizeCode(std::string("406b")) == std::optional<std::string>("406B"));  // case-insensitive
    CHECK(sanitizeCode(std::string("1999D")).has_value());
    CHECK(sanitizeCode(std::string("0")).has_value());
}

TEST(pcw1_letters_only_rejected) {
    CHECK(!sanitizeCode(std::string("ABC")).has_value());
    CHECK(!sanitizeCode(std::string("REJSTRIK")).has_value());
}

TEST(pcw1_over_1999_rejected) {
    CHECK(!sanitizeCode(std::string("2000")).has_value());
    CHECK(!sanitizeCode(std::string("9999")).has_value());
    CHECK(!sanitizeCode(std::string("2000A")).has_value());
}

TEST(pcw1_five_digits_rejected) {
    CHECK(!sanitizeCode(std::string("12345")).has_value());
    CHECK(!sanitizeCode(std::string("00001")).has_value());
}

TEST(pcw1_bad_suffix_rejected) {
    CHECK(!sanitizeCode(std::string("1999E")).has_value());
    CHECK(!sanitizeCode(std::string("12AB")).has_value());
    CHECK(!sanitizeCode(std::string("A12")).has_value());
}

TEST(pcw1_display_parser_caps_1999) {  // P1-4: same rule in fromSongCode
    CHECK(!mc::display::fromSongCode("2000").has_value());
    CHECK(!mc::display::fromSongCode("9999A").has_value());
    CHECK(mc::display::fromSongCode("1999D").has_value());
}

TEST(pcw1_validator_is_shared) {
    CHECK(mc::songs::validCode("406A"));
    CHECK(mc::songs::validCode("406a"));
    CHECK(!mc::songs::validCode("2000"));
    CHECK(!mc::songs::validCode(""));
}

// B4 (Q2-15): leading zeros do not name another song.
TEST(b4_normalize_code_table) {
    using mc::songs::normalizeCode;
    CHECK(normalizeCode("0406") == "406");
    CHECK(normalizeCode("0406B") == "406B");
    CHECK(normalizeCode("0000") == "0");
    CHECK(normalizeCode("0") == "0");
    CHECK(normalizeCode("12") == "12");
    CHECK(normalizeCode("0406", false) == "0406");
    CHECK(normalizeCode("ABC") == "ABC");
    CHECK(sanitizeCode(std::string(" 0406 ")) == std::optional<std::string>("406"));
    CHECK(sanitizeCode(std::string("0007a")) == std::optional<std::string>("7A"));
    CHECK(!sanitizeCode(std::string("00001")).has_value());
    mc::songs::setNormalise(false);
    CHECK(sanitizeCode(std::string("0406")) == std::optional<std::string>("0406"));
    mc::songs::setNormalise(true);
}
