#ifndef MIDICONTROL_DIAG_STORE_HPP
#define MIDICONTROL_DIAG_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/diag_codes.hpp"

namespace mc::diag {

// Rotating JSONL store, one file set per node: diag-<node>.jsonl (newest), .1, .2 ...
// (keepFiles files in total). One JSON object per line:
//   {"id":N,"t":<pc unix ms>,"node":"base","boot":N,"up":<node uptime ms>,"lvl":"E","tag":"..","text":"..",
//    "fault":{"kind":"..","pc":N,"lr":N,"xpsr":N,["cfsr":N,"cfsr_bits":[..],]"hfsr":N,"hfsr_bits":[..],
//             "mmfar":N,"bfar":N,"rev":"..","up":N,"count":N,"code":N}}
// Ids are global (shared by all nodes) and monotonic across restarts. Fault acknowledgement is kept in
// diag-ack.json. Thread-safe. Level order: E < W < I < D.
class DiagStore {
public:
    explicit DiagStore(std::string dir, size_t rotateBytes = 1 << 20, int keepFiles = 3);

    uint64_t append(DiagEntry e);  // assigns id, writes one line, rotates
    // Newest first; entries with id < beforeId (0 = no limit) and level at least as severe as minLevel.
    std::vector<DiagEntry> query(const std::string& node, char minLevel, uint64_t beforeId, size_t limit) const;
    std::optional<DiagEntry> lastFault(const std::string& node) const;
    size_t unseenFaults(const std::string& node) const;  // faults with id > acked id
    void ack(uint64_t id);

private:
    std::string path(const std::string& node, int idx) const;
    std::vector<DiagEntry> readAll(const std::string& node) const;  // oldest first
    void rotate(const std::string& node);

    std::string dir_;
    size_t rotateBytes_;
    int keepFiles_;
    mutable std::mutex mu_;
    uint64_t nextId_ = 1;
    uint64_t ackId_ = 0;
    std::map<std::string, size_t> sizes_;  // current file size per node
};

}  // namespace mc::diag

#endif
