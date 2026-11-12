// Gap 2 / SYS-PWR-07 / SYS-NOTE-04: when the service starts (or restarts) it
// cannot know what the organ was doing, so once the base link is up it sends
// All Notes Off on every channel and then switches the organ power off. The
// indicator bus (D14) is left alone.
#include <cstdint>
#include <vector>

#include "midicontrol/startup_reset.hpp"
#include "test_framework.hpp"

using namespace mc::startup;

TEST(startup_all_notes_off_messages_cover_every_channel) {
    auto msgs = allNotesOffMessages();
    CHECK_EQ(msgs.size(), size_t(32));
    for (int ch = 0; ch < 16; ++ch) {
        const auto& a = msgs[2 * ch];
        const auto& b = msgs[2 * ch + 1];
        CHECK(a == (std::vector<uint8_t>{static_cast<uint8_t>(0xB0 | ch), 123, 0}));  // All Notes Off
        CHECK(b == (std::vector<uint8_t>{static_cast<uint8_t>(0xB0 | ch), 120, 0}));  // All Sound Off
    }
}

TEST(startup_reset_waits_for_the_base_link) {
    StartupReset r;
    auto a = r.due(-1, true);  // base not up yet
    CHECK(!a.allNotesOff);
    CHECK(!a.organOff);
    CHECK(!r.finished());
}

TEST(startup_reset_sends_notes_off_then_power_off_once) {
    StartupReset r;
    auto a = r.due(0, true);
    CHECK(a.allNotesOff);
    CHECK(a.organOff);
    r.done(a);
    CHECK(r.finished());
    auto b = r.due(10000, true);  // never again in this run
    CHECK(!b.allNotesOff);
    CHECK(!b.organOff);
}

TEST(startup_reset_waits_a_little_for_the_midi_port) {
    StartupReset r(5000);
    auto a = r.due(1000, false);  // base up, its MIDI port not enumerated yet
    CHECK(!a.allNotesOff);
    CHECK(!a.organOff);
    auto b = r.due(2000, true);  // port appeared: both, notes first
    CHECK(b.allNotesOff);
    CHECK(b.organOff);
}

TEST(startup_reset_powers_off_without_the_midi_port_after_the_grace) {
    StartupReset r(5000);
    auto a = r.due(5000, false);
    CHECK(!a.allNotesOff);  // nothing to send notes through
    CHECK(a.organOff);      // the organ is still switched off
    r.done(a);
    CHECK(r.finished());
}

TEST(startup_reset_is_cancelled_by_a_song) {
    // A song started before the reset ran owns the organ now: no power off.
    StartupReset r;
    r.cancel();
    auto a = r.due(0, true);
    CHECK(!a.allNotesOff);
    CHECK(!a.organOff);
    CHECK(r.finished());
}
