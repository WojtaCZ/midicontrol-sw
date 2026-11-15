// Seeded-random fallback: runs every target on mutated seeds + pure random bytes, deterministic,
// a few seconds. Usage: mc_fuzz_fallback [iterations-per-target] [seed]. Also replays files in
// tests/fuzz/regress/<target>/ (crash regressions) when MC_FUZZ_REGRESS_DIR is defined.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "targets.hpp"

namespace {
using Bytes = std::vector<uint8_t>;

Bytes bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

std::vector<Bytes> seedsFor(const std::string& t) {
    std::vector<Bytes> v;
    if (t == "parser_feed" || t == "decode_frame" || t == "fw_differential") {
        // valid frames (with and without FRAME_START/END; decodeFrame wants the body only)
        v.push_back({0x24, 0x05, 0x01, 0x02, 0x03, 0x01, 0x00, 0x25});
        v.push_back({0x06, 0x01, 0x02, 0x03, 0x10, 0x31, 0x32, 0x00});
        v.push_back({0x24, 0x7D, 0x7E, 0x25, 0x24});
    } else if (t == "parse_request") {
        v.push_back(bytes("GET /api/status HTTP/1.1\r\nHost: x\r\n\r\n"));
        v.push_back(bytes("POST /api/stops/12 HTTP/1.1\r\nContent-Length: 5\r\nContent-Type: Text/Plain\r\n\r\nhello"));
        v.push_back(bytes("POST /api/firmware/x HTTP/1.0\r\ncontent-length: 99999\r\nTransfer-Encoding: chunked\r\n\r\n"));
    } else if (t == "smf_parse") {
        v.push_back({'M','T','h','d',0,0,0,6,0,1,0,1,0x01,0xE0,'M','T','r','k',0,0,0,12,
                     0x00,0xFF,0x51,0x03,0x07,0xA1,0x20,0x00,0x90,0x3C,0x40,0x00});
        v.push_back({'M','T','h','d',0,0,0,6,0,0,0,1,0x00,0x60,'M','T','r','k',0,0,0,4,0x00,0xFF,0x2F,0x00});
    } else if (t == "parse_config") {
        v.push_back(bytes("[general]\nport = /dev/ttyACM0\nsongs = ./songs\n# c\n; c\nkey=\n=v\n[x\n"));
    } else if (t == "parse_log_line") {
        v.push_back(bytes("2026-11-15T12:00:00 [fault] code=0x12 pc=0x08001234 lr=0x08000020"));
        v.push_back(bytes("123456 I diag seq=5 code=7 arg=0xdeadbeef"));
        v.push_back(bytes("\x1b[0m [  12.345] boot: ok\r"));
    } else if (t == "sanitize_code") {
        v.push_back(bytes("0012a")); v.push_back(bytes("  1999D\0\0")); v.push_back(bytes("../../etc"));
    } else if (t == "decode_record") {
        v.push_back(Bytes(12, 0xA5));
    } else if (t == "sysex_parse") {
        v.push_back({0x7D, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08});
    } else if (t == "stops_parse") {
        v.push_back(bytes("Stops: 1 2 3 24 99 x\nName = y\n# c\nStops 5,6\n"));
    }
    if (v.empty()) v.push_back({0});
    return v;
}

void mutate(Bytes& b, std::mt19937_64& r) {
    int k = 1 + static_cast<int>(r() % 4);
    while (k--) {
        switch (r() % 6) {
            case 0: if (!b.empty()) b[r() % b.size()] = static_cast<uint8_t>(r()); break;
            case 1: b.insert(b.begin() + (b.empty() ? 0 : r() % (b.size() + 1)), static_cast<uint8_t>(r())); break;
            case 2: if (!b.empty()) b.erase(b.begin() + r() % b.size()); break;
            case 3: if (!b.empty()) b[r() % b.size()] ^= static_cast<uint8_t>(1u << (r() % 8)); break;
            case 4: if (!b.empty() && b.size() < 4096) { size_t a = r() % b.size(); b.insert(b.end(), b.begin() + a, b.end()); } break;
            case 5: if (b.size() > 1) b.resize(1 + r() % b.size()); break;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const unsigned long iters = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 20000;
    const unsigned long seed = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 1;
    size_t count;
    const auto* all = mc::fuzz::targets(count);
    for (size_t t = 0; t < count; ++t) {
        std::mt19937_64 rng(seed * 1000003 + t);
        const auto seeds = seedsFor(all[t].name);
        unsigned long runs = 0;
#ifdef MC_FUZZ_REGRESS_DIR
        namespace fs = std::filesystem;
        fs::path dir = fs::path(MC_FUZZ_REGRESS_DIR) / all[t].name;
        if (fs::is_directory(dir))
            for (auto& e : fs::directory_iterator(dir)) {
                std::ifstream f(e.path(), std::ios::binary);
                Bytes b((std::istreambuf_iterator<char>(f)), {});
                all[t].fn(b.data(), b.size());
                ++runs;
            }
#endif
        for (unsigned long i = 0; i < iters; ++i, ++runs) {
            Bytes b;
            if (i % 8 == 0) {
                b.resize(rng() % 300);
            } else {
                b = seeds[rng() % seeds.size()];
                mutate(b, rng);
            }
            if (i % 8 == 0) for (auto& x : b) x = static_cast<uint8_t>(rng());
            all[t].fn(b.data(), b.size());
        }
        std::printf("fuzz %-16s %lu inputs ok\n", all[t].name, runs);
    }
    return 0;
}
