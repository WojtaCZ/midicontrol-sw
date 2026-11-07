#include <chrono>

#include "midicontrol/stall_monitor.hpp"
#include "test_framework.hpp"

using namespace std::chrono;
using mc::StallMonitor;

namespace {
const steady_clock::time_point t0{};  // arbitrary epoch
steady_clock::time_point at(milliseconds ms) { return t0 + ms; }
}  // namespace

TEST(stall_never_exits_while_alive) {
    StallMonitor m(seconds(30));
    for (int s = 0; s <= 600; s += 5) CHECK(!m.shouldExit(true, at(seconds(s))));
}

TEST(stall_exits_after_limit_of_not_alive) {
    StallMonitor m(seconds(30));
    CHECK(!m.shouldExit(false, at(seconds(0))));
    CHECK(!m.shouldExit(false, at(milliseconds(29900))));
    CHECK(m.shouldExit(false, at(seconds(30))));
}

TEST(stall_alive_resets_clock) {
    StallMonitor m(seconds(30));
    CHECK(!m.shouldExit(false, at(seconds(0))));
    CHECK(!m.shouldExit(false, at(seconds(20))));
    CHECK(!m.shouldExit(true, at(seconds(21))));
    CHECK(!m.shouldExit(false, at(seconds(22))));
    CHECK(!m.shouldExit(false, at(seconds(51))));
    CHECK(m.shouldExit(false, at(seconds(52))));
}

TEST(stall_startup_grace) {
    StallMonitor m(seconds(30));
    CHECK(!m.shouldExit(false, at(seconds(0))));
    CHECK(!m.shouldExit(false, at(seconds(29))));
}
