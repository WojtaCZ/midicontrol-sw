// Firmware codec side of the differential (namespace ::protocol, midicontrol-fw/lib/protocol).
// Kept in its own TU so the firmware and PC headers never meet.
#include "protocol/codec.hpp"
#include "protocol/parser.hpp"

#include "fw_codec.hpp"

namespace mc::fuzz {

bool fwDecodeFrame(const uint8_t* d, size_t n, FwPacket& out) {
    if (n > 0xFFFF) return false;
    ::protocol::Packet p{};
    if (!::protocol::decodeFrame(d, static_cast<uint16_t>(n), p)) return false;
    out.source = p.source; out.dest = p.dest; out.packetId = p.packetId;
    out.msgType = static_cast<uint8_t>(p.msgType);
    out.payload.assign(p.payload, p.payload + p.payloadLen);
    return true;
}

std::vector<uint8_t> fwEncode(const FwPacket& in) {
    ::protocol::Packet p{};
    p.source = in.source; p.dest = in.dest; p.packetId = in.packetId;
    p.msgType = static_cast<::protocol::MsgType>(in.msgType);
    p.payloadLen = static_cast<uint8_t>(in.payload.size());
    for (size_t i = 0; i < in.payload.size(); ++i) p.payload[i] = in.payload[i];
    uint8_t buf[2 * (1 + 4 + 128 + 1) + 2];
    uint16_t len = ::protocol::encode(p, buf);
    return std::vector<uint8_t>(buf, buf + len);
}

std::vector<FwPacket> fwFeedAll(const uint8_t* d, size_t n) {
    std::vector<FwPacket> res;
    ::protocol::Parser parser;
    for (size_t i = 0; i < n; ++i) {
        if (parser.feed(d[i])) {
            const auto& p = parser.getPacket();
            FwPacket f;
            f.source = p.source; f.dest = p.dest; f.packetId = p.packetId;
            f.msgType = static_cast<uint8_t>(p.msgType);
            f.payload.assign(p.payload, p.payload + p.payloadLen);
            res.push_back(std::move(f));
        }
    }
    return res;
}

}  // namespace mc::fuzz
