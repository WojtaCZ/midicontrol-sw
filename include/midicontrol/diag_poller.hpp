#ifndef MIDICONTROL_DIAG_POLLER_HPP
#define MIDICONTROL_DIAG_POLLER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

#include "midicontrol/diag_codes.hpp"

namespace mc::diag {

// Polling of the remote's diagnostic records (DIAG_GET / DIAG_RESP), without any I/O: the comm layer feeds
// it events and plugs in the sender. Thread-safe (link and response events come from the reader thread,
// tick() from the keepalive thread).
//
//  - GET on remote link-up, then every kPollMs while the link is up;
//  - never while a firmware update runs (tick() gets `updating`);
//  - a full page (n = 8) is followed up at once (at most kMaxFollowUps in a row), so a backlog drains
//    without waiting 10 s per page;
//  - cursor = the response's next_cursor; a changed boot_id means the cursor is meaningless: the stale page
//    is dropped and the next GET starts from 0 (no duplicates);
//  - an empty response (n = 0) and a link loss flush a FAULT whose FAULT_DETAIL records never came;
//  - NAK UNKNOWN_MSG for our GET (a remote without diagnostics) stops the polling until the next link-up.
// The ACK of every DIAG_RESP is the dispatcher's job (like STATUS_RESP).
class DiagPoller {
public:
    static constexpr int64_t kPollMs = 10'000;
    static constexpr int kMaxFollowUps = 4;  // consecutive immediate follow-ups for full pages

    struct Hooks {
        std::function<uint8_t(const std::vector<uint8_t>& payload)> sendGet;  // returns the packet id used
        std::function<void(DiagEntry)> emit;
        std::function<int64_t()> pcNowMs;  // wall clock for the entries
        // Optional: persist the last (boot_id, cursor) so a service restart does not fetch the remote's
        // current-boot ring again (it would re-raise old faults).
        std::function<std::optional<std::pair<uint16_t, uint16_t>>()> loadCursor;
        std::function<void(uint16_t boot, uint16_t cursor)> saveCursor;
    };

    explicit DiagPoller(Hooks hooks) : hooks_(std::move(hooks)) {
        if (hooks_.loadCursor) {
            if (auto c = hooks_.loadCursor()) {
                bootId_ = c->first;
                cursor_ = c->second;
            }
        }
    }

    // `updating`: a firmware update is running, the first GET of a new link waits for tick().
    void setRemoteLink(bool up, int64_t nowMs, bool updating = false);
    void tick(int64_t nowMs, bool updating);
    void onResp(const uint8_t* payload, size_t len, int64_t nowMs);
    void onNak(uint8_t pid, uint8_t reason);

private:
    void sendGet(int64_t nowMs);
    void persist();
    void emitAll(std::vector<DiagEntry>& entries);

    Hooks hooks_;
    std::mutex mu_;
    bool linkUp_ = false;
    bool stopped_ = false;
    int64_t dueMs_ = 0;
    std::optional<uint8_t> outstanding_;
    uint16_t cursor_ = 0;
    int followUps_ = 0;
    bool followUpDue_ = false;
    std::optional<uint16_t> bootId_;
    DiagRespDecoder decoder_;
};

}  // namespace mc::diag

#endif
