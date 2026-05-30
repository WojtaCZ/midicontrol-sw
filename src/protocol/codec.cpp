#include "midicontrol/protocol/codec.hpp"

#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/protocol/crc8.hpp"

namespace mc::protocol {

namespace {

// Append the (possibly escaped) representation of one byte to `out`.
void stuffByte(uint8_t byte, std::vector<uint8_t>& out) {
    switch (byte) {
        case FRAME_START:      out.push_back(ESC); out.push_back(ESC_FRAME_START); break;
        case FRAME_END:        out.push_back(ESC); out.push_back(ESC_FRAME_END);   break;
        case ESC:              out.push_back(ESC); out.push_back(ESC_ESC);         break;
        case FORBIDDEN_DOLLAR: out.push_back(ESC); out.push_back(ESC_DOLLAR);      break;
        case FORBIDDEN_PERCENT:out.push_back(ESC); out.push_back(ESC_PERCENT);     break;
        default:               out.push_back(byte);                                break;
    }
}

// Map an escaped byte (the one following ESC) back to its original value.
// Returns -1 if the escape sequence is invalid.
int unstuffByte(uint8_t escaped) {
    switch (escaped) {
        case ESC_FRAME_START: return FRAME_START;
        case ESC_FRAME_END:   return FRAME_END;
        case ESC_ESC:         return ESC;
        case ESC_DOLLAR:      return FORBIDDEN_DOLLAR;
        case ESC_PERCENT:     return FORBIDDEN_PERCENT;
        default:              return -1;
    }
}

}  // namespace

std::vector<uint8_t> encode(const Packet& pkt) {
    // Clamp payload to the protocol maximum (the firmware buffer cannot hold more).
    const size_t payloadLen =
        pkt.payload.size() > MAX_PAYLOAD_SIZE ? MAX_PAYLOAD_SIZE : pkt.payload.size();

    // Build the un-escaped content: Length, header, payload.
    std::vector<uint8_t> raw;
    raw.reserve(1 + HEADER_SIZE + payloadLen);
    raw.push_back(static_cast<uint8_t>(HEADER_SIZE + payloadLen));  // Length
    raw.push_back(pkt.source);
    raw.push_back(pkt.dest);
    raw.push_back(pkt.packetId);
    raw.push_back(static_cast<uint8_t>(pkt.msgType));
    raw.insert(raw.end(), pkt.payload.begin(), pkt.payload.begin() + payloadLen);

    const uint8_t checksum = crc8(raw.data(), raw.size());

    std::vector<uint8_t> out;
    out.reserve(raw.size() * 2 + 4);
    out.push_back(FRAME_START);
    for (uint8_t b : raw) {
        stuffByte(b, out);
    }
    stuffByte(checksum, out);
    out.push_back(FRAME_END);
    return out;
}

std::optional<Packet> decodeFrame(const uint8_t* raw, size_t len) {
    // Un-stuff the whole frame body first.
    std::vector<uint8_t> unstuffed;
    unstuffed.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (raw[i] == ESC) {
            if (i + 1 >= len) return std::nullopt;
            int val = unstuffByte(raw[++i]);
            if (val < 0) return std::nullopt;
            unstuffed.push_back(static_cast<uint8_t>(val));
        } else {
            unstuffed.push_back(raw[i]);
        }
    }

    // Minimum: Length(1) + Header(4) + CRC(1) = 6.
    if (unstuffed.size() < 6) return std::nullopt;

    const uint8_t length = unstuffed[0];
    if (length < HEADER_SIZE) return std::nullopt;
    const uint8_t payloadLen = static_cast<uint8_t>(length - HEADER_SIZE);
    if (payloadLen > MAX_PAYLOAD_SIZE) return std::nullopt;
    // Length byte + content + CRC.
    if (unstuffed.size() != static_cast<size_t>(1 + length + 1)) return std::nullopt;

    const uint8_t expectedCrc = crc8(unstuffed.data(), unstuffed.size() - 1);
    if (expectedCrc != unstuffed.back()) return std::nullopt;

    Packet pkt;
    pkt.source = unstuffed[1];
    pkt.dest = unstuffed[2];
    pkt.packetId = unstuffed[3];
    pkt.msgType = static_cast<MsgType>(unstuffed[4]);
    pkt.payload.assign(unstuffed.begin() + 5, unstuffed.begin() + 5 + payloadLen);
    return pkt;
}

std::optional<Packet> decodeFrame(const std::vector<uint8_t>& raw) {
    return decodeFrame(raw.data(), raw.size());
}

std::optional<Packet> decode(const std::vector<uint8_t>& frame) {
    if (frame.size() < 2 || frame.front() != FRAME_START || frame.back() != FRAME_END) {
        return std::nullopt;
    }
    return decodeFrame(frame.data() + 1, frame.size() - 2);
}

}  // namespace mc::protocol
