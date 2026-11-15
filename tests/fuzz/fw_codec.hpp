#ifndef MC_FUZZ_FW_CODEC_HPP
#define MC_FUZZ_FW_CODEC_HPP
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mc::fuzz {
struct FwPacket {
    uint8_t source = 0, dest = 0, packetId = 0, msgType = 0;
    std::vector<uint8_t> payload;
};
bool fwDecodeFrame(const uint8_t* d, size_t n, FwPacket& out);
std::vector<uint8_t> fwEncode(const FwPacket& in);
std::vector<FwPacket> fwFeedAll(const uint8_t* d, size_t n);
}  // namespace mc::fuzz
#endif
