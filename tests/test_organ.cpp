#include <cmath>
#include <string>
#include <vector>

#include "midicontrol/organ.hpp"
#include "test_framework.hpp"

TEST(organ_defaults_are_opus_910) {
    auto o = mc::organ::defaultOrgan();
    CHECK_EQ(o.stopCount(), 42);
    CHECK_EQ(o.groups.size(), 4u);
    CHECK_EQ(o.groups[0].label, std::string("ACCESSORIES")); CHECK_EQ(o.groups[0].last, 7);
    CHECK_EQ(o.groups[3].label, std::string("SWELL")); CHECK_EQ(o.groups[3].first, 30);
    CHECK_EQ(o.stops[0].name, std::string("Swell to Great")); CHECK_EQ(o.stops[0].st, std::string("60"));
    CHECK_EQ(o.stops[7].name, std::string("Double Bass 16'")); CHECK_EQ(o.stops[7].st, std::string("01"));
    CHECK_EQ(o.stops[41].name, std::string("MIDI to Swell")); CHECK_EQ(o.stops[41].st, std::string("47"));
    CHECK(o.allOffSysex == (std::vector<uint8_t>{0xF0,0x00,0x4A,0x4F,0x48,0x41,0x53,0x7F,0xF7}));
}
TEST(organ_program_is_octal_st_plus_offset) {
    auto o = mc::organ::defaultOrgan();
    CHECK_EQ(mc::organ::programFor(o, 8), 0);    // ST01
    CHECK_EQ(mc::organ::programFor(o, 15), 7);   // ST10 = 8 decimal
    CHECK_EQ(mc::organ::programFor(o, 17), 11);  // ST14 = 12
    CHECK_EQ(mc::organ::programFor(o, 1), 47);   // ST60 = 48
    CHECK_EQ(mc::organ::programFor(o, 42), 38);  // ST47 = 39
}
TEST(organ_parse_overrides) {
    auto o = mc::organ::parseOrgan({{"Channel","3"},{"ProgramOffset","0"},{"Program17","19"},
                                    {"Name1","Coupler"},{"ST2","70"},{"StopDelayMs","5"},{"DropSongProgramChanges","false"}});
    CHECK_EQ(o.channel, 3); CHECK_EQ(mc::organ::programFor(o, 8), 1);
    CHECK_EQ(mc::organ::programFor(o, 17), 19);  // override wins
    CHECK_EQ(mc::organ::programFor(o, 2), 56);   // ST70 octal
    CHECK_EQ(o.stops[0].name, std::string("Coupler")); CHECK_FALSE(o.dropSongProgramChanges);
}
TEST(organ_parse_rejects_bad_values) {
    using mc::organ::parseOrgan;
    CHECK_THROWS(parseOrgan({{"Channel","17"}}));
    CHECK_THROWS(parseOrgan({{"ST3","18"}}));            // 8 is not an octal digit
    CHECK_THROWS(parseOrgan({{"Program5","128"}}));
    CHECK_THROWS(parseOrgan({{"AllOffSysex","F0 ZZ F7"}}));
    CHECK_THROWS(parseOrgan({{"Groups","A:1-7, B:9-12"}})); // gap at 8
    CHECK_THROWS(parseOrgan({{"ProgramOffset","-2"}}));     // ST01 would map to -1
}
TEST(organ_parse_groups_beyond_defaults) {
    using mc::organ::parseOrgan;
    CHECK_THROWS(parseOrgan({{"Groups","A:1-43"}}));  // stop 43 has no ST and no program
    auto o = parseOrgan({{"Groups","A:1-43"},{"Program43","5"}});
    CHECK_EQ(o.stopCount(), 43);
    CHECK_EQ(o.stops[42].name, std::string("Stop 43"));
    CHECK_EQ(mc::organ::programFor(o, 43), 5);
    CHECK_THROWS(parseOrgan({{"Name43","x"}}));  // beyond the stop count
}
TEST(organ_preamble_bytes_and_delays) {
    auto o = mc::organ::defaultOrgan();
    auto p = mc::organ::buildPreamble(o, {8, 17});
    CHECK_EQ(p.size(), 3u);
    CHECK(p[0].bytes == o.allOffSysex); CHECK(std::abs(p[0].delayAfterSeconds - 0.100) < 1e-9);
    CHECK(p[1].bytes == (std::vector<uint8_t>{0xCB, 0})); CHECK(std::abs(p[1].delayAfterSeconds - 0.020) < 1e-9);
    CHECK(p[2].bytes == (std::vector<uint8_t>{0xCB, 11}));
    CHECK_EQ(mc::organ::buildPreamble(o, {}).size(), 1u);  // saved empty = all-off only
}
