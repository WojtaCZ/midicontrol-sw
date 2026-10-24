#include "midicontrol/diag_poller.hpp"

#include "midicontrol/protocol/diag.hpp"
#include "midicontrol/protocol/types.hpp"

namespace mc::diag {

void DiagPoller::emitAll(std::vector<DiagEntry>& entries) {
    for (auto& e : entries) hooks_.emit(std::move(e));
}

void DiagPoller::persist() {
    if (hooks_.saveCursor && bootId_ && !decoder_.hasPending()) hooks_.saveCursor(*bootId_, cursor_);
}

void DiagPoller::sendGet(int64_t nowMs) {
    dueMs_ = nowMs + kPollMs;
    outstanding_ = hooks_.sendGet({static_cast<uint8_t>(cursor_), static_cast<uint8_t>(cursor_ >> 8)});
}

void DiagPoller::setRemoteLink(bool up, int64_t nowMs, bool updating) {
    std::lock_guard<std::mutex> lk(mu_);
    if (up == linkUp_) return;
    linkUp_ = up;
    outstanding_.reset();
    if (up) {
        stopped_ = false;
        if (updating || paused_) {
            dueMs_ = nowMs;
        } else {
            sendGet(nowMs);
        }
    } else if (auto e = decoder_.flush()) {
        hooks_.emit(std::move(*e));
        persist();
    }
}

void DiagPoller::tick(int64_t nowMs, bool updating) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!linkUp_ || stopped_ || updating || paused_ || nowMs < dueMs_) return;
    if (!followUpDue_) followUps_ = 0;  // a timed poll starts a new run of follow-ups
    followUpDue_ = false;
    sendGet(nowMs);
}

void DiagPoller::setPaused(bool on) {
    std::lock_guard<std::mutex> lk(mu_);
    paused_ = on;
}

void DiagPoller::onResp(const uint8_t* payload, size_t len, int64_t nowMs) {
    std::lock_guard<std::mutex> lk(mu_);
    if (len < mc::protocol::diag::RESP_HEADER_SIZE) return;
    outstanding_.reset();
    const uint16_t boot = static_cast<uint16_t>(payload[0] | (payload[1] << 8));
    if (bootId_ && *bootId_ != boot) {
        // New boot of the remote: finish what the old one left pending, restart from its first record.
        if (auto e = decoder_.flush()) hooks_.emit(std::move(*e));
        decoder_ = DiagRespDecoder{};
        bootId_ = boot;
        cursor_ = 0;
        followUps_ = 0;
        followUpDue_ = false;
        dueMs_ = nowMs;
        persist();
        return;
    }
    bootId_ = boot;
    auto d = decoder_.decode(payload, len, hooks_.pcNowMs());
    if (!d) return;  // bad length: ignore, the next poll asks again
    emitAll(d->entries);
    cursor_ = d->nextCursor;
    const uint8_t n = payload[4];
    if (n == 0) {
        if (auto e = decoder_.flush()) hooks_.emit(std::move(*e));
    } else if (n >= mc::protocol::diag::MAX_RECORDS && followUps_ < kMaxFollowUps) {
        ++followUps_;
        followUpDue_ = true;
        dueMs_ = nowMs;  // more may be waiting
    }
    persist();
}

void DiagPoller::onNak(uint8_t pid, uint8_t reason) {
    std::lock_guard<std::mutex> lk(mu_);
    if (!outstanding_ || *outstanding_ != pid) return;
    outstanding_.reset();
    // UNKNOWN_MSG: the remote has no diagnostics -> stop until the next real link-up.
    // UNREACHABLE: the base cannot forward (BLE down, or connected but the stream not yet open/secured; its STATUS
    // ble flag is set from %CONNECT%, and a drop shorter than the 5 s status period never shows ble=0). No timer
    // polling meanwhile, but forget the link state so the next STATUS_RESP with ble=1 is a fresh edge and sends one
    // GET (R27 kept, R33): at most one GET per status period while connected-but-not-open.
    // BUSY: the link holds (module briefly in command mode), dueMs_ stays, the next GET goes on the 10 s timer.
    if (reason == static_cast<uint8_t>(mc::protocol::NakReason::UNKNOWN_MSG)) {
        stopped_ = true;
    } else if (reason == static_cast<uint8_t>(mc::protocol::NakReason::UNREACHABLE)) {
        linkUp_ = false;
        // As setRemoteLink(false): a later real link loss is no edge any more and would not flush a half-received FAULT
        if (auto e = decoder_.flush()) {
            hooks_.emit(std::move(*e));
            persist();
        }
    }
}

}  // namespace mc::diag
