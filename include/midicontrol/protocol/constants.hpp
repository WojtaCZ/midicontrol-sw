#ifndef MIDICONTROL_PROTOCOL_CONSTANTS_HPP
#define MIDICONTROL_PROTOCOL_CONSTANTS_HPP

#include <cstdint>

// MIDIControl binary protocol constants.
//
// This is a faithful port of the firmware library lib/protocol/constants.hpp.
// The wire format must stay byte-for-byte identical to the embedded side, so
// the values below are copied verbatim from the firmware.
namespace mc::protocol {

// Frame delimiters (never appear un-escaped inside a frame body).
inline constexpr uint8_t FRAME_START = 0x01;  // SOH
inline constexpr uint8_t FRAME_END   = 0x04;  // EOT

// Escape prefix.
inline constexpr uint8_t ESC = 0x1B;

// Escaped sequences (the byte that follows ESC).
inline constexpr uint8_t ESC_FRAME_START = 0x21;  // ESC 0x21 -> 0x01
inline constexpr uint8_t ESC_FRAME_END   = 0x22;  // ESC 0x22 -> 0x04
inline constexpr uint8_t ESC_ESC         = 0x23;  // ESC 0x23 -> 0x1B
inline constexpr uint8_t ESC_DOLLAR      = 0x26;  // ESC 0x26 -> 0x24 ($)
inline constexpr uint8_t ESC_PERCENT     = 0x27;  // ESC 0x27 -> 0x25 (%)

// Bytes forbidden on the wire (reserved by the RN4870 BLE module).
inline constexpr uint8_t FORBIDDEN_DOLLAR  = 0x24;  // $ - RN4870 command mode
inline constexpr uint8_t FORBIDDEN_PERCENT = 0x25;  // % - RN4870 status delimiter

// Protocol version (INFO tag PROTO_VER). Version 1 is implicit: a node without PROTO_VER is v1.
// 2: PROTO_VER in INFO_RESP, KEEPALIVE session nonce, 8-id duplicate window, ORGAN_SRC INFO tag, SIGNAL_INFO (incl.
//    BASE_UPDATING), KEEPALIVE_ACK boot id + 7-byte STATUS_RESP, NPI_CLEAR_IF_OWN, FW_UPDATE_STATUS ACK/retry + NAK
//    NO_TRANSFER, GET_SONG_LIST u16 offset, RECORD_SONG Base->PC + SIGNAL_RECORDING.
inline constexpr uint8_t PROTOCOL_VERSION = 2;

// Node addresses.
inline constexpr uint8_t ADDR_PC        = 0x01;
inline constexpr uint8_t ADDR_BASE      = 0x02;
inline constexpr uint8_t ADDR_REMOTE    = 0x03;
inline constexpr uint8_t ADDR_BROADCAST = 0xFF;

// Sizes.
inline constexpr uint8_t  MAX_PAYLOAD_SIZE = 128;
inline constexpr uint8_t  HEADER_SIZE      = 4;  // source + dest + packetId + msgType
// FRAME_START + worst-case (every byte escaped) + FRAME_END.
inline constexpr uint16_t MAX_FRAME_SIZE   = 2 + (1 + HEADER_SIZE + MAX_PAYLOAD_SIZE + 1) * 2;

// Reliable-delivery retry policy.
inline constexpr uint8_t  MAX_RETRIES     = 3;
inline constexpr uint16_t RETRY_TIMEOUT_MS = 500;

// Keepalive: PC -> Base and Base -> Remote; the peer is considered lost after
// KEEPALIVE_LOSS_MS without a KEEPALIVE_ACK.
inline constexpr uint16_t KEEPALIVE_PERIOD_MS = 2000;
inline constexpr uint16_t KEEPALIVE_LOSS_MS   = 6000;

// Song code in PLAY_SONG / SIGNAL_PLAYING: ASCII [0-9A-Z], e.g. "406A".
inline constexpr uint8_t MAX_SONG_CODE_LEN = 8;

// NOW_PLAYING_INFO field values.
inline constexpr uint8_t NPI_NO_CHANGE   = 0xFF;  // field unchanged
inline constexpr uint8_t NPI_BLANK       = 0xFE;  // blank the field (song number / verse)
inline constexpr uint8_t NPI_DIGIT_BLANK = 0x0F;  // BCD nibble: blank digit
// One-byte payload (protocol v2): clear the indicator only if this system wrote the number last.
inline constexpr uint8_t NPI_CLEAR_IF_OWN = 0x01;

// SIGNAL_INFO flags byte (protocol v2, MsgType 0x34). Receivers read the bytes and bits they know and ignore the
// rest; bits 2-7 are reserved and sent as 0. The sender always sends the complete flags byte.
inline constexpr uint8_t SIGNAL_INFO_SONG_HAS_SECTIONS = 0x01;  // bit0 (A6): the song file has verse markers; 0 = the remote hides add-verse
inline constexpr uint8_t SIGNAL_INFO_BASE_UPDATING     = 0x02;  // bit1 (A8): sent by the base before it reboots into its bootloader; the remote shows "Zakladna: aktualizace"

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_CONSTANTS_HPP
