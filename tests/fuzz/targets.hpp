// Fuzz targets shared by the libFuzzer binaries and the seeded-random ctest fallback.
// Each target must never crash/UB/hang on any input and must keep its invariants
// (violations call std::abort so both drivers report them).
#ifndef MC_FUZZ_TARGETS_HPP
#define MC_FUZZ_TARGETS_HPP

#include <cstddef>
#include <cstdint>

namespace mc::fuzz {

struct Target {
    const char* name;
    void (*fn)(const uint8_t*, size_t);
};

// Differential vs the firmware codec; defined only when built with MC_FUZZ_FW_CODEC.
void fwDifferential(const uint8_t* d, size_t n);

const Target* targets(size_t& count);

}  // namespace mc::fuzz

#endif
