#include <string>

#include "midicontrol/protocol/diag.hpp"
#include "midicontrol/protocol/types.hpp"
#include "test_framework.hpp"

using namespace mc;
using protocol::MsgType;
using protocol::Packet;

TEST(names_msg_type) {
    CHECK_EQ(std::string(protocol::msgTypeName(MsgType::REPLAY_ANCHOR)), "REPLAY_ANCHOR");
    CHECK_EQ(std::string(protocol::msgTypeName(MsgType::KEEPALIVE_ACK)), "KEEPALIVE_ACK");
    CHECK_EQ(std::string(protocol::msgTypeName(MsgType::DIAG_GET)), "DIAG_GET");
    CHECK_EQ(std::string(protocol::msgTypeName(MsgType::DIAG_RESP)), "DIAG_RESP");
    CHECK_EQ(static_cast<int>(MsgType::DIAG_GET), 0x80);
    CHECK_EQ(static_cast<int>(MsgType::DIAG_RESP), 0x81);
    CHECK_EQ(std::string(protocol::msgTypeName(static_cast<MsgType>(0x99))), "UNKNOWN");
    CHECK_EQ(std::string(protocol::nakReasonName(0x05)), "NOT_PLAYING");
    CHECK_EQ(std::string(protocol::nakReasonName(0x08)), "NO_TRANSFER");
    CHECK_EQ(std::string(protocol::nakReasonName(0x77)), "UNKNOWN");
}

TEST(names_describe_packet) {
    CHECK_EQ(protocol::describePacket(Packet(3, 1, 8, MsgType::REPLAY_ANCHOR)),
             "REPLAY_ANCHOR id=8");
    CHECK_EQ(protocol::describePacket(Packet(1, 3, 9, MsgType::NAK, {8, 0x05})),
             "NAK id=9 for=8 NOT_PLAYING");
    CHECK_EQ(protocol::describePacket(Packet(2, 1, 4, MsgType::ACK, {3})), "ACK id=4 for=3");
    CHECK_EQ(protocol::describePacket(
                 Packet(1, 2, 17, MsgType::NOW_PLAYING_INFO, {0x40, 0x6A, 0x01, 0xFF, 0xFF})),
             "NOW_PLAYING_INFO id=17 [40 6A 01 FF FF]");
    std::vector<uint8_t> big(20, 0xAB);
    CHECK_EQ(protocol::describePacket(Packet(1, 2, 1, MsgType::FW_UPDATE_DATA, big)),
             "FW_UPDATE_DATA id=1 [AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB AB ...]");
}

TEST(diag_record_roundtrip) {
    using namespace protocol::diag;
    const Record r{0xBEEF, static_cast<uint8_t>(Code::RECORDS_LOST), static_cast<uint8_t>(Level::DEBUG),
                   0x01020304u, 0xDEADBEEFu};
    uint8_t raw[RECORD_SIZE];
    encodeRecord(r, raw);
    CHECK_EQ(static_cast<int>(raw[0]), 0xEF);
    CHECK_EQ(static_cast<int>(raw[1]), 0xBE);
    CHECK_EQ(static_cast<int>(raw[4]), 0x04);
    CHECK_EQ(static_cast<int>(raw[11]), 0xDE);
    const Record q = decodeRecord(raw);
    CHECK_EQ(static_cast<int>(q.seq), 0xBEEF);
    CHECK_EQ(static_cast<int>(q.code), 16);
    CHECK_EQ(static_cast<int>(q.level), 4);
    CHECK_EQ(static_cast<unsigned long>(q.uptimeMs), 0x01020304ul);
    CHECK_EQ(static_cast<unsigned long>(q.arg), 0xDEADBEEFul);
    CHECK_EQ(static_cast<int>(MAX_RESP_PAYLOAD), 101);
}
