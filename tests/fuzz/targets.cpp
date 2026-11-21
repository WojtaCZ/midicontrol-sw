#include "targets.hpp"

#include <cstdlib>
#include <string>
#include <vector>

#include "midicontrol/config.hpp"
#include "midicontrol/http.hpp"
#include "midicontrol/log_line.hpp"
#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/diag.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "midicontrol/smf.hpp"
#include "midicontrol/songs.hpp"
#include "midicontrol/stops_file.hpp"
#include "midicontrol/sysex.hpp"

namespace mc::fuzz {

namespace {

#define REQUIRE(c) do { if (!(c)) std::abort(); } while (0)

std::string str(const uint8_t* d, size_t n) { return std::string(reinterpret_cast<const char*>(d), n); }

void tParserFeed(const uint8_t* d, size_t n) {
    protocol::Parser p;
    for (size_t i = 0; i < n; ++i) {
        auto pk = p.feed(d[i]);
        if (pk) {
            REQUIRE(pk->payload.size() <= protocol::MAX_PAYLOAD_SIZE);
            // A packet the parser accepted must survive an encode/decode round trip.
            auto back = protocol::decode(protocol::encode(*pk));
            REQUIRE(back && back->payload == pk->payload && back->msgType == pk->msgType);
        }
    }
    p.reset();
}

void tDecodeFrame(const uint8_t* d, size_t n) {
    if (n >= 4) {  // structurally valid frame from the input (the CRC is not guessable by mutation)
        protocol::Packet pk(d[0], d[1], d[2], static_cast<protocol::MsgType>(d[3]),
                            std::vector<uint8_t>(d + 4, d + (n - 4 > 130 ? 134 : n)));
        const auto e = protocol::encode(pk);
        auto r = protocol::decodeFrame(e.data() + 1, e.size() - 2);
        if (pk.payload.size() <= protocol::MAX_PAYLOAD_SIZE)
            REQUIRE(r && r->payload == pk.payload && r->source == pk.source);
        else
            REQUIRE(r && r->payload.size() == protocol::MAX_PAYLOAD_SIZE);
    }
    auto pk = protocol::decodeFrame(d, n);
    if (pk) {
        const auto enc = protocol::encode(*pk);
        auto back = protocol::decodeFrame(enc.data() + 1, enc.size() - 2);
        REQUIRE(back && back->payload == pk->payload);
    }
}

void tParseRequest(const uint8_t* d, size_t n) {
    http::Request r;
    auto res = http::parseRequest(str(d, n), r);
    if (res == http::ParseResult::Ok) REQUIRE(r.body.size() <= http::kMaxFirmwareBody);
    // Tiny limit exercises the TooLarge paths.
    http::Request r2;
    http::parseRequest(str(d, n), r2, 64);
}

void tSmf(const uint8_t* d, size_t n) {
    try {
        smf::parse(std::vector<uint8_t>(d, d + n));
    } catch (const std::exception&) {
    }
}

void tConfig(const uint8_t* d, size_t n) {
    try {
        parseConfig(str(d, n));
    } catch (const std::exception&) {
    }
}

void tLogLine(const uint8_t* d, size_t n) {
    diag::parseLogLine(std::string_view(reinterpret_cast<const char*>(d), n));
}

void tSanitizeCode(const uint8_t* d, size_t n) {
    auto a = songs::sanitizeCode(str(d, n));
    if (a) REQUIRE(songs::validCode(*a));
    auto b = songs::sanitizeCode(std::vector<uint8_t>(d, d + n));
    if (b) REQUIRE(songs::validCode(*b));
}

void tDecodeRecord(const uint8_t* d, size_t n) {
    if (n < 12) return;
    auto r = protocol::diag::decodeRecord(d);
    uint8_t out[12];
    protocol::diag::encodeRecord(r, out);
    for (int i = 0; i < 12; ++i) REQUIRE(out[i] == d[i]);
}

void tSysex(const uint8_t* d, size_t n) { sysex::parse(d, n); }

void tStops(const uint8_t* d, size_t n) {
    std::vector<std::string> w;
    try {
        stops::parse(str(d, n), 24, "f", &w);
    } catch (const std::exception&) {
    }
}

const Target kTargets[] = {
    {"parser_feed", tParserFeed}, {"decode_frame", tDecodeFrame}, {"parse_request", tParseRequest},
    {"smf_parse", tSmf},          {"parse_config", tConfig},      {"parse_log_line", tLogLine},
    {"sanitize_code", tSanitizeCode}, {"decode_record", tDecodeRecord}, {"sysex_parse", tSysex},
    {"stops_parse", tStops},
#ifdef MC_FUZZ_FW_CODEC
    {"fw_differential", fwDifferential},
#endif
};

}  // namespace

const Target* targets(size_t& count) {
    count = sizeof(kTargets) / sizeof(kTargets[0]);
    return kTargets;
}

}  // namespace mc::fuzz
