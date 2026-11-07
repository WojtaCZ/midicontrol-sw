#pragma once

#include <chrono>
#include <optional>

namespace mc {

// Decides when a process that has been unhealthy for too long should exit so a
// container supervisor can restart it. The first call starts the clock, so a
// monitor that never saw `alive == true` still waits a full `limit` (no
// false-positive exit at process start).
class StallMonitor {
public:
    explicit StallMonitor(std::chrono::milliseconds limit) : _limit(limit) {}

    // True once `alive` has been false continuously for >= limit; any
    // `alive == true` resets the clock.
    bool shouldExit(bool alive, std::chrono::steady_clock::time_point now) {
        if (alive) {
            _since.reset();
            return false;
        }
        if (!_since) _since = now;
        return now - *_since >= _limit;
    }

private:
    std::chrono::milliseconds _limit;
    std::optional<std::chrono::steady_clock::time_point> _since;
};

}  // namespace mc
