#include <optional>
#include <vector>

#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "test_framework.hpp"

using namespace mc::protocol;

namespace {
std::optional<Packet> feedAll(Parser& p, const std::vector<uint8_t>& bytes) {
    std::optional<Packet> last;
    for (uint8_t b : bytes) {
        auto r = p.feed(b);
        if (r.has_value()) last = r;
    }
    return last;
}
}  // namespace

TEST(parser_reassembles_frame) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x0A, MsgType::PLAY_SONG, {0x61, 0x62, 0x00});
    auto frame = encode(pkt);

    Parser parser;
    auto got = feedAll(parser, frame);
    CHECK(got.has_value());
    CHECK_EQ(got->packetId, 0x0A);
    CHECK(got->msgType == MsgType::PLAY_SONG);
    CHECK(got->payload == pkt.payload);
}

TEST(parser_ignores_leading_garbage) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x01, MsgType::ACK, {0x07});
    auto frame = encode(pkt);

    Parser parser;
    // Random noise before the real frame should be ignored.
    feedAll(parser, {0x55, 0xAA, 0x00, 0x7F});
    auto got = feedAll(parser, frame);
    CHECK(got.has_value());
    CHECK_EQ(got->packetId, 0x01);
}

TEST(parser_resyncs_on_new_frame_start) {
    Packet pkt(ADDR_PC, ADDR_BASE, 0x09, MsgType::STOP_SONG, {});
    auto frame = encode(pkt);

    Parser parser;
    // Start a frame, feed a few bytes, then a new FRAME_START aborts it.
    parser.feed(FRAME_START);
    parser.feed(0x99);
    parser.feed(0x88);
    auto got = feedAll(parser, frame);  // begins with its own FRAME_START
    CHECK(got.has_value());
    CHECK(got->msgType == MsgType::STOP_SONG);
}

TEST(parser_handles_two_frames_back_to_back) {
    auto f1 = encode(Packet(ADDR_PC, ADDR_BASE, 0x01, MsgType::ACK, {0x10}));
    auto f2 = encode(Packet(ADDR_PC, ADDR_BASE, 0x02, MsgType::NAK, {0x10, 0x03}));

    Parser parser;
    std::vector<Packet> packets;
    for (auto& f : {f1, f2}) {
        for (uint8_t b : f) {
            auto r = parser.feed(b);
            if (r.has_value()) packets.push_back(*r);
        }
    }
    CHECK_EQ(packets.size(), size_t(2));
    CHECK(packets[0].msgType == MsgType::ACK);
    CHECK(packets[1].msgType == MsgType::NAK);
    CHECK_EQ(packets[1].payload.size(), size_t(2));
}
