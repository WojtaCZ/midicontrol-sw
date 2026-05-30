#ifndef MIDICONTROL_PROTOCOL_TYPES_HPP
#define MIDICONTROL_PROTOCOL_TYPES_HPP

#include <cstdint>
#include <vector>

#include "midicontrol/protocol/constants.hpp"

namespace mc::protocol {

// Message types. Values are identical to the firmware enum.
enum class MsgType : uint8_t {
    ACK              = 0x01,
    NAK              = 0x02,

    PLAY_SONG        = 0x10,
    STOP_SONG        = 0x11,
    RECORD_SONG      = 0x12,
    NOW_PLAYING_INFO = 0x13,

    GET_SONG_LIST    = 0x20,
    SONG_LIST_RESP   = 0x21,

    SIGNAL_PLAYING   = 0x30,
    SIGNAL_RECORDING = 0x31,
    SIGNAL_STOPPED   = 0x32,

    SET_CURRENT      = 0x40,

    GET_STATUS       = 0x50,
    STATUS_RESP      = 0x51,

    KEEPALIVE        = 0x60,
    KEEPALIVE_ACK    = 0x61,
};

enum class NakReason : uint8_t {
    UNKNOWN_MSG     = 0x01,
    INVALID_PAYLOAD = 0x02,
    CRC_ERROR       = 0x03,
    BUSY            = 0x04,
};

// A decoded/decodable packet. Unlike the fixed-size firmware struct (which
// avoids heap allocation on the MCU), the PC side uses a std::vector payload.
struct Packet {
    uint8_t source = 0;
    uint8_t dest = 0;
    uint8_t packetId = 0;
    MsgType msgType = MsgType::ACK;
    std::vector<uint8_t> payload;

    Packet() = default;
    Packet(uint8_t src, uint8_t dst, uint8_t id, MsgType type, std::vector<uint8_t> pl = {})
        : source(src), dest(dst), packetId(id), msgType(type), payload(std::move(pl)) {}
};

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_TYPES_HPP
