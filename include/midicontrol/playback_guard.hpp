#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace mc {

// B13: stuck notes must be silent within 10 s of a playback stall (owner 2026-11-22).

// Lock-free heartbeat of the playback thread. The playback thread beats while a song runs (in the
// 10 ms wait slices, so a long held note is not a stall); the watchdog only reads the atomic, so a
// thread hung while holding any lock cannot block the watchdog too.
class PlaybackHeartbeat {
public:
    static int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void beat(int64_t nowMs) { _ms.store(nowMs, std::memory_order_relaxed); }
    void stop() { _ms.store(-1, std::memory_order_relaxed); }  // no song running

    // Age of the last beat in ms, or -1 when no song runs.
    int64_t ageMs(int64_t nowMs) const {
        const int64_t v = _ms.load(std::memory_order_relaxed);
        return v < 0 ? -1 : nowMs - v;
    }

private:
    std::atomic<int64_t> _ms{-1};
};

// Decides when the watchdog silences the organ after a playback stall. Fires once per stall
// episode; a fresh beat (or the end of the song) re-arms it.
class PlaybackStallGuard {
public:
    explicit PlaybackStallGuard(int64_t limitMs) : _limitMs(limitMs) {}

    bool stalled(int64_t ageMs) const { return ageMs > _limitMs; }

    bool shouldSilence(int64_t ageMs) {
        if (!stalled(ageMs)) {
            _fired = false;
            return false;
        }
        if (_fired) return false;
        _fired = true;
        return true;
    }

private:
    int64_t _limitMs;
    bool _fired = false;
};

}  // namespace mc
