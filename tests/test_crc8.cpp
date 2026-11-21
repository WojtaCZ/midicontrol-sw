#include <vector>

#include "midicontrol/protocol/crc8.hpp"
#include "test_framework.hpp"

using mc::protocol::crc8;

TEST(crc8_empty_is_zero) {
    CHECK_EQ(crc8(nullptr, 0), 0x00);
}

TEST(crc8_play_song_vector) {
    // From PROTOCOL.md: CRC-8 (poly 0x31, non-reflected) over {07 01 02 0A 10 61 62 00} == 0x59.
    std::vector<uint8_t> content{0x07, 0x01, 0x02, 0x0A, 0x10, 0x61, 0x62, 0x00};
    CHECK_EQ(crc8(content.data(), content.size()), 0x59);
}

TEST(crc8_single_byte_matches_table) {
    // crc8({0x00}) follows the table: table[0 ^ 0] == table[0] == 0x00.
    uint8_t zero = 0x00;
    CHECK_EQ(crc8(&zero, 1), 0x00);
    // crc8({0x01}) == table[1] == 0x31.
    uint8_t one = 0x01;
    CHECK_EQ(crc8(&one, 1), 0x31);
}

TEST(crc8_check_value_is_0xA2) {
    // Catalogue check value of this variant (poly 0x31, init 0, non-reflected); CRC-8/MAXIM would give 0xA1.
    const uint8_t s[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    CHECK_EQ(crc8(s, sizeof s), 0xA2);
}
