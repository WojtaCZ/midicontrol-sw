#ifndef MIDICONTROL_DIAG_CODES_HPP
#define MIDICONTROL_DIAG_CODES_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/fault_decode.hpp"

namespace mc::diag {

// One stored diagnostic event (base log line / fault, or a remote DIAG record).
struct DiagEntry {
    uint64_t id = 0;
    int64_t pcTimeMs = 0;  // PC wall clock (unix ms) when received
    std::string node;      // "base" | "remote"
    uint32_t boot = 0;     // remote boot_id (0 for base)
    uint64_t uptimeMs = 0; // node uptime
    char level = 'I';
    std::string tag, text;
    std::optional<FaultInfo> fault;
};

// DIAG_RESP payload: [boot_id u16, next_cursor u16, n u8, n x 12 B record].
struct DiagRespDecoded {
    uint16_t bootId = 0, nextCursor = 0;
    std::vector<DiagEntry> entries;
};

// Stateful decoder: a FAULT whose FAULT_DETAIL records arrive in the next response is held back until
// complete (or until another record / a boot_id change / flush() ends it).
class DiagRespDecoder {
public:
    std::optional<DiagRespDecoded> decode(const uint8_t* payload, size_t len, int64_t pcTimeMs);
    std::optional<DiagEntry> flush();  // emits an incomplete pending FAULT, if any
    bool hasPending() const { return pending_.has_value(); }

private:
    std::optional<DiagEntry> pending_;
    int detail_ = 0;
};

// Stateless wrapper: one response on its own (an incomplete FAULT is emitted as it is).
// Records become entries (node "remote", tag "diag"). A FAULT record followed by FAULT_DETAIL
// records (pc, lr, xpsr, ipsr) folds into one entry carrying a FaultInfo. nullopt on a bad length.
std::optional<DiagRespDecoded> decodeDiagResp(const uint8_t* payload, size_t len, int64_t pcTimeMs);

// Human text of one record, e.g. BOOT -> "boot, reset: PIN|SOFTWARE", LOW_BATTERY -> "low battery 3412 mV".
std::string diagCodeText(uint8_t code, uint32_t arg);

}  // namespace mc::diag

#endif
