#ifndef MIDICONTROL_PROTOCOL_CODEC_HPP
#define MIDICONTROL_PROTOCOL_CODEC_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "midicontrol/protocol/types.hpp"

namespace mc::protocol {

// Encode a packet into the full wire format, including FRAME_START/FRAME_END,
// byte stuffing and the trailing CRC-8. Returns the bytes ready to send.
std::vector<uint8_t> encode(const Packet& pkt);

// Decode the raw bytes *between* FRAME_START and FRAME_END (still escaped) back
// into a Packet. Returns std::nullopt on any framing / length / CRC error.
std::optional<Packet> decodeFrame(const uint8_t* raw, size_t len);
std::optional<Packet> decodeFrame(const std::vector<uint8_t>& raw);

// Decode a complete frame that still includes the FRAME_START/FRAME_END bytes.
std::optional<Packet> decode(const std::vector<uint8_t>& frame);

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_CODEC_HPP
