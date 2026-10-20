#ifndef MIDICONTROL_FAULT_DECODE_HPP
#define MIDICONTROL_FAULT_DECODE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/log_line.hpp"

namespace mc::diag {

struct FaultInfo {
    std::string kind;
    uint32_t pc = 0, lr = 0, xpsr = 0, cfsr = 0, hfsr = 0, mmfar = 0, bfar = 0;
    bool hasCfsr = false;
    std::vector<std::string> cfsrBits, hfsrBits;
    std::string rev;
    uint64_t uptimeMs = 0;
    uint32_t count = 0;
    uint32_t code = 0;
};

// Names of the set MMFSR/BFSR/UFSR bits, in bit order.
std::vector<std::string> decodeCfsr(uint32_t cfsr);
// Names of the set HFSR bits (VECTTBL FORCED DEBUGEVT).
std::vector<std::string> decodeHfsr(uint32_t hfsr);

// Accumulates the base's fault lines (tag "fault", level E) into one FaultInfo.
// feed() returns true when the line belonged to a fault (consumed). Lines are:
//   record time:  "<KIND> #<n> pc=0x.. lr=0x.. code <c>"
//   dump/banner:  "<KIND> #<n> code <c> up <s>.<ms> s rev <hex>", "pc=.. lr=.. xpsr=.. ..",
//                 "r0=..", "cfsr=0x.. NAMES", "hfsr=0x.. NAMES", "mmfar=.. bfar=.. ..."
// A header line for the same kind/count as the pending record-time line merges into it.
// take() returns the pending fault (if any) and clears it; call it when a non-fault line arrives.
// MMFAR/BFAR are exposed only when CFSR.MMARVALID/BFARVALID say they are meaningful.
class BaseFaultParser {
public:
    bool feed(const LogLine& l);
    std::optional<FaultInfo> take();

private:
    std::optional<FaultInfo> cur_;
    uint32_t rawMmfar_ = 0, rawBfar_ = 0;
    bool haveFars_ = false;
    void applyFars();
};

}  // namespace mc::diag

#endif
