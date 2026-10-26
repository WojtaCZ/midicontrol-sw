#ifndef MIDICONTROL_DIAG_STORE_HPP
#define MIDICONTROL_DIAG_STORE_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
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
// Reads are served from an in-memory tail cache (last `cacheEntries` entries per node, all fault ids and the
// last fault), loaded from disk once per node and extended on append; only a page older than the cache goes
// back to the files.
// One entry as a JSON object (the JSONL line without its newline; also the /api/diag entry shape).
std::string entryJson(const DiagEntry& e);

class DiagStore {
public:
    explicit DiagStore(std::string dir, size_t rotateBytes = 1 << 20, int keepFiles = 3, size_t cacheEntries = 2000);

    uint64_t append(DiagEntry e);  // assigns id, writes one line, rotates
    // Newest first; entries with id < beforeId (0 = no limit) and level at least as severe as minLevel.
    std::vector<DiagEntry> query(const std::string& node, char minLevel, uint64_t beforeId, size_t limit) const;
    std::optional<DiagEntry> lastFault(const std::string& node) const;
    size_t unseenFaults(const std::string& node) const;  // faults with id > acked id
    void ack(uint64_t id);  // acknowledges up to `id` on the node that fault belongs to (else on every node)
    // Last (boot_id, cursor) the remote poller reached; survives restarts (diag-remote-cursor.json).
    std::optional<std::pair<uint16_t, uint16_t>> loadRemoteCursor() const;
    void saveRemoteCursor(uint16_t boot, uint16_t cursor);
    // Replace the stored entry with this id (same node as `e`) by `e`; false if there is none.
    bool replace(uint64_t id, DiagEntry e);

    size_t diskReads() const;   // full file scans so far (tests: the cache must avoid them)
    size_t writeErrors() const; // appends that could not be written (the entry stays in memory only)

private:
    std::string path(const std::string& node, int idx) const;
    std::vector<DiagEntry> readAll(const std::string& node) const;  // oldest first
    void rotate(const std::string& node);

    struct NodeCache {
        bool complete = false;  // `tail` holds everything that is on disk
        std::deque<DiagEntry> tail;  // oldest first
        // Last `cacheEntries` E/W entries: level=E/W pages are answered from here, not by scanning the files.
        bool sevComplete = false;  // `sev` holds every E/W entry that is on disk
        std::deque<DiagEntry> sev;  // oldest first
        std::vector<uint64_t> faultIds;
        std::optional<DiagEntry> lastFault;
    };
    NodeCache& cacheFor(const std::string& node) const;  // loads on first use; mu_ held

    std::string dir_;
    size_t rotateBytes_;
    size_t cacheEntries_;
    int keepFiles_;
    mutable std::mutex mu_;
    uint64_t nextId_ = 1;
    uint64_t ackAll_ = 0;                    // legacy {"ack":N}: applies to every node
    std::map<std::string, uint64_t> ackIds_; // per node: "Mark seen" on one node must not clear another's fault
    uint64_t ackFor(const std::string& node) const;
    void saveAck() const;
    std::set<std::string> checked_;        // nodes whose file end was verified to be newline-terminated
    std::map<std::string, size_t> sizes_;  // current file size per node
    mutable std::map<std::string, NodeCache> cache_;
    mutable size_t diskReads_ = 0;
    size_t writeErrors_ = 0;
    bool writeFailing_ = false;  // a failure streak is already logged
};

}  // namespace mc::diag

#endif
