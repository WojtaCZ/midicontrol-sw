#ifndef MIDICONTROL_PROTOCOL_TYPES_HPP
#define MIDICONTROL_PROTOCOL_TYPES_HPP

#include <cstdint>
#include <string>
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
    REPLAY_ANCHOR    = 0x14,  // remote -> PC: "add verse" - play the current verse once more after it ends; no payload

    GET_SONG_LIST    = 0x20,
    SONG_LIST_RESP   = 0x21,

    SIGNAL_PLAYING   = 0x30,
    SIGNAL_RECORDING = 0x31,
    SIGNAL_STOPPED   = 0x32,
    SIGNAL_ANCHOR    = 0x33,  // PC -> remote: playback entered a section; payload = null-terminated ASCII label ("Sloka 2")

    SET_CURRENT      = 0x40,

    GET_STATUS       = 0x50,
    STATUS_RESP      = 0x51,
    GET_INFO         = 0x52,  // PC -> node: empty; ACKed, answered with INFO_RESP
    INFO_RESP        = 0x53,  // node -> PC: TLV fields (see device_info.hpp)

    KEEPALIVE        = 0x60,
    KEEPALIVE_ACK    = 0x61,

    // Firmware update (OTA over BLE for the remote, USB for the base)
    FW_UPDATE_START  = 0x70,  // payload: target(1) + size(4) + crc32(4) + version[...]
    FW_UPDATE_DATA   = 0x71,  // payload: offset(4) + chunk(<=120)
    FW_UPDATE_END    = 0x72,  // finalize + verify; device applies/reboots
    FW_UPDATE_STATUS = 0x73,  // device -> PC progress / result
    ENTER_BOOTLOADER = 0x74,  // base only: app reboots into its USB bootloader

    // Diagnostics (see protocol/diag.hpp)
    DIAG_GET         = 0x80,  // PC -> node: payload [cursor u16 LE]; ACKed
    DIAG_RESP        = 0x81,  // node -> PC: [boot_id u16, next_cursor u16, n u8, n x 12 B record], n <= 8
};

// FW_UPDATE_STATUS payload codes (single byte)
enum class FwStatus : uint8_t {
    READY       = 0x00,  // ready to receive (response to FW_UPDATE_START)
    IN_PROGRESS = 0x01,
    CRC_OK      = 0x02,  // image verified, will apply now
    CRC_FAIL    = 0x03,
    WRITE_ERROR = 0x04,
    BAD_SIZE    = 0x05,
};

enum class NakReason : uint8_t {
    UNKNOWN_MSG     = 0x01,
    INVALID_PAYLOAD = 0x02,
    CRC_ERROR       = 0x03,
    BUSY            = 0x04,
    NOT_PLAYING     = 0x05,  // command needs active playback (REPLAY_ANCHOR)
    UNREACHABLE     = 0x06,  // base: destination node not connected, packet not forwarded
    NOT_FOUND       = 0x07,  // PC: song code not in library
    NO_TRANSFER     = 0x08,  // FW_UPDATE_DATA/END without an active transfer (device reset or timed out)
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

// Enum identifier for logs and the status page ("REPLAY_ANCHOR"); "UNKNOWN"
// for values not in MsgType.
const char* msgTypeName(MsgType type);
// NakReason identifier ("NOT_PLAYING"); "UNKNOWN" for other values.
const char* nakReasonName(uint8_t reason);
// One-line description: "<TYPE> id=<n>", ACK adds " for=<pid>", NAK adds
// " for=<pid> <REASON>", other payloads add " [AA BB ...]" (first 16 bytes).
std::string describePacket(const Packet& pkt);

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_TYPES_HPP
