// PC codec/parser (mc::protocol) vs firmware codec/parser (protocol, midicontrol-fw/lib/protocol).
#include <cstdlib>

#include "fw_codec.hpp"
#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "targets.hpp"

namespace mc::fuzz {

#define REQUIRE(c) do { if (!(c)) std::abort(); } while (0)

static bool same(const protocol::Packet& a, const FwPacket& b) {
    return a.source == b.source && a.dest == b.dest && a.packetId == b.packetId &&
           static_cast<uint8_t>(a.msgType) == b.msgType && a.payload == b.payload;
}

static void diffOne(const uint8_t* d, size_t n);

void fwDifferential(const uint8_t* d, size_t n) {
    diffOne(d, n);
    // CRC cannot be guessed by the mutator: also feed a structurally valid frame built from the input
    // (src,dst,id,type,payload), once as a body and once as a whole stream with garbage around it.
    if (n >= 4) {
        protocol::Packet pk(d[0], d[1], d[2], static_cast<protocol::MsgType>(d[3]),
                            std::vector<uint8_t>(d + 4, d + (n - 4 > 130 ? 134 : n)));
        auto enc = protocol::encode(pk);
        diffOne(enc.data() + 1, enc.size() - 2);
        diffOne(enc.data(), enc.size());
        enc.insert(enc.end(), d, d + n);
        diffOne(enc.data(), enc.size());
    }
}

static void diffOne(const uint8_t* d, size_t n) {
    // 1. decodeFrame accepts/rejects identically and yields the same fields.
    auto pc = protocol::decodeFrame(d, n);
    FwPacket fw;
    bool fwOk = fwDecodeFrame(d, n, fw);
    REQUIRE(pc.has_value() == fwOk);
    if (pc) {
        REQUIRE(same(*pc, fw));
        // 2. both encoders produce the same bytes.
        REQUIRE(protocol::encode(*pc) == fwEncode(fw));
    }
    // 3. streaming parsers emit the same packet sequence.
    std::vector<protocol::Packet> pcs;
    protocol::Parser p;
    for (size_t i = 0; i < n; ++i)
        if (auto k = p.feed(d[i])) pcs.push_back(*k);
    auto fws = fwFeedAll(d, n);
    REQUIRE(pcs.size() == fws.size());
    for (size_t i = 0; i < pcs.size(); ++i) REQUIRE(same(pcs[i], fws[i]));
}

}  // namespace mc::fuzz
