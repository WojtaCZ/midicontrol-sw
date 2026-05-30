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

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_CONSTANTS_HPP
