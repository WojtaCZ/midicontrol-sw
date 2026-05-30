#include <vector>

#include "midicontrol/sysex.hpp"
#include "test_framework.hpp"

using namespace mc::sysex;

TEST(parse_spec_full_display_example) {
    // From readme.md: song 1234, verse 05, letter B, green LED.
    std::vector<uint8_t> body{0x4D, 0x43, 0x01, 0x00, 0x01, 0x02,
                              0x03, 0x04, 0x00, 0x05, 0x0B, 0x02};
    auto info = parse(body);
    CHECK(info.has_value());
    CHECK_EQ(info->song, 1234);
    CHECK_EQ(info->verse, 5);
    CHECK_EQ(info->letter, 'B');
    CHECK(info->led == Led::Green);
    CHECK(info->hasSong && info->hasVerse && info->hasLetter && info->hasLed);
}

TEST(parse_song_with_leading_blank) {
    // Matches songs/401.mid: "_401" -> 401.
    std::vector<uint8_t> body{0x4D, 0x43, 0x01, 0x00, 0x0F, 0x04, 0x00,
                              0x01, 0x0F, 0x01, 0x0F, 0x0F};
    auto info = parse(body);
    CHECK(info.has_value());
    CHECK_EQ(info->song, 401);
    CHECK_EQ(info->verse, 1);
    CHECK_EQ(info->letter, LETTER_OFF);
    CHECK(info->led == Led::Off);
}

TEST(parse_partial_targets) {
    auto song = parse(std::vector<uint8_t>{0x4D, 0x43, 0x01, 0x01, 0x00, 0x00, 0x07, 0x08});
    CHECK(song.has_value());
    CHECK_EQ(song->song, 78);
    CHECK(song->hasSong && !song->hasVerse);

    auto verse = parse(std::vector<uint8_t>{0x4D, 0x43, 0x01, 0x02, 0x0F, 0x05});
    CHECK(verse.has_value());
    CHECK_EQ(verse->verse, 5);
    CHECK(verse->hasVerse && !verse->hasSong);

    auto letter = parse(std::vector<uint8_t>{0x4D, 0x43, 0x01, 0x03, 0x0C});
    CHECK(letter.has_value());
    CHECK_EQ(letter->letter, 'C');

    auto led = parse(std::vector<uint8_t>{0x4D, 0x43, 0x01, 0x04, 0x04});
    CHECK(led.has_value());
    CHECK(led->led == Led::Yellow);
}

TEST(parse_rejects_non_mc_and_short) {
    CHECK_FALSE(parse(std::vector<uint8_t>{0x00, 0x01, 0x02, 0x03}).has_value());
    CHECK_FALSE(parse(std::vector<uint8_t>{0x4D, 0x43}).has_value());          // too short
    CHECK_FALSE(parse(std::vector<uint8_t>{0x4D, 0x43, 0x01, 0x00}).has_value());  // full needs data
    CHECK_FALSE(parse(std::vector<uint8_t>{}).has_value());
}

TEST(build_full_round_trips) {
    DisplayInfo info;
    info.song = 1234;
    info.verse = 5;
    info.letter = 'B';
    info.led = Led::Green;
    auto body = buildFull(info);

    std::vector<uint8_t> expected{0x4D, 0x43, 0x01, 0x00, 0x01, 0x02,
                                  0x03, 0x04, 0x0F, 0x05, 0x0B, 0x02};
    if (body != expected) {
        FAIL_AT("built body [" + ::mc::test::toHex(body) + "] != expected [" +
                ::mc::test::toHex(expected) + "]");
    }
    auto reparsed = parse(body);
    CHECK(reparsed.has_value());
    CHECK_EQ(reparsed->song, 1234);
    CHECK_EQ(reparsed->verse, 5);
}

TEST(build_blanks_leading_zeros_keeps_units) {
    DisplayInfo info;
    info.song = 401;
    auto body = build(Target::Song, info);
    // header(4) + 4 digit bytes; 401 -> blank,4,0,1
    std::vector<uint8_t> expected{0x4D, 0x43, 0x01, 0x01, 0x0F, 0x04, 0x00, 0x01};
    if (body != expected) {
        FAIL_AT("song body [" + ::mc::test::toHex(body) + "] != [" +
                ::mc::test::toHex(expected) + "]");
    }

    DisplayInfo zero;
    zero.song = 0;
    auto zbody = build(Target::Song, zero);
    std::vector<uint8_t> zexpected{0x4D, 0x43, 0x01, 0x01, 0x0F, 0x0F, 0x0F, 0x00};
    if (zbody != zexpected) {
        FAIL_AT("zero song body [" + ::mc::test::toHex(zbody) + "] != [" +
                ::mc::test::toHex(zexpected) + "]");
    }
}

TEST(wrap_adds_f0_f7) {
    auto body = buildFull(DisplayInfo{});
    auto wrapped = wrap(body);
    CHECK_EQ(wrapped.front(), 0xF0);
    CHECK_EQ(wrapped.back(), 0xF7);
    CHECK_EQ(wrapped.size(), body.size() + 2);
}
