#include <vector>

#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "test_framework.hpp"

using namespace mc::protocol;

TEST(encode_play_song_golden_frame) {
    // PC -> Base, packetId 0x0A, PLAY_SONG, payload "ab\0".
    Packet pkt(ADDR_PC, ADDR_BASE, 0x0A, MsgType::PLAY_SONG, {0x61, 0x62, 0x00});
    auto frame = encode(pkt);

    // Source byte 0x01 equals FRAME_START, so it must be escaped to 1B 21.
    std::vector<uint8_t> expected{0x01, 0x07, 0x1B, 0x21, 0x02, 0x0A,
                                  0x10, 0x61, 0x62, 0x00, 0x59, 0x04};
    if (frame != expected) {
        FAIL_AT("frame mismatch: got [" + ::mc::test::toHex(frame) + "] expected [" +
                ::mc::test::toHex(expected) + "]");
    }
}

TEST(encode_decode_round_trip) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x42, MsgType::NOW_PLAYING_INFO,
               {0x12, 0x34, 0x0F, 0x0A, 0x02});
    auto frame = encode(pkt);
    auto decoded = decode(frame);
    CHECK(decoded.has_value());
    CHECK_EQ(decoded->source, ADDR_PC);
    CHECK_EQ(decoded->dest, ADDR_BASE);
    CHECK_EQ(decoded->packetId, 0x42);
    CHECK(decoded->msgType == MsgType::NOW_PLAYING_INFO);
    CHECK(decoded->payload == pkt.payload);
}

TEST(encode_escapes_all_forbidden_bytes) {
    // Payload containing every byte that must be stuffed.
    std::vector<uint8_t> payload{FRAME_START, FRAME_END, ESC, FORBIDDEN_DOLLAR, FORBIDDEN_PERCENT};
    Packet pkt(ADDR_REMOTE, ADDR_PC, 0x01, MsgType::STATUS_RESP, payload);
    auto frame = encode(pkt);

    // The frame body must not contain any raw forbidden byte between the markers.
    for (size_t i = 1; i + 1 < frame.size(); ++i) {
        uint8_t b = frame[i];
        CHECK(b != FRAME_START);
        CHECK(b != FRAME_END);
        CHECK(b != FORBIDDEN_DOLLAR);
        CHECK(b != FORBIDDEN_PERCENT);
    }
    // And it must round-trip exactly.
    auto decoded = decode(frame);
    CHECK(decoded.has_value());
    CHECK(decoded->payload == payload);
}

TEST(empty_payload_round_trips) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x00, MsgType::KEEPALIVE, {});
    auto decoded = decode(encode(pkt));
    CHECK(decoded.has_value());
    CHECK(decoded->msgType == MsgType::KEEPALIVE);
    CHECK(decoded->payload.empty());
}

TEST(decode_rejects_bad_crc) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x10, MsgType::ACK, {0x05});
    auto frame = encode(pkt);
    // Corrupt the CRC byte (second from the end, before FRAME_END).
    frame[frame.size() - 2] ^= 0xFF;
    auto decoded = decode(frame);
    CHECK_FALSE(decoded.has_value());
}

TEST(decode_rejects_missing_markers) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x10, MsgType::ACK, {0x05});
    auto frame = encode(pkt);
    auto noStart = std::vector<uint8_t>(frame.begin() + 1, frame.end());
    CHECK_FALSE(decode(noStart).has_value());
    auto noEnd = std::vector<uint8_t>(frame.begin(), frame.end() - 1);
    CHECK_FALSE(decode(noEnd).has_value());
}

TEST(decode_rejects_dangling_escape) {
    // ESC at the very end with no following byte is invalid.
    std::vector<uint8_t> frame{FRAME_START, 0x07, 0x01, 0x02, 0x0A, 0x10, ESC, FRAME_END};
    CHECK_FALSE(decode(frame).has_value());
}
