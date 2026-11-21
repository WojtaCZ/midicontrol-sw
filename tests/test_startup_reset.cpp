// Gap 2 / SYS-PWR-07 / SYS-NOTE-04: when the service starts (or restarts) it
// cannot know what the organ was doing, so once the base link is up it sends
// All Notes Off on every channel. Then the power part: Q1-1 b (owner
// 2026-11-14) - an SSR the base reports ON is left on and the idle-off timer
// is armed; reported OFF or still unknown after the status grace: power off.
// The indicator bus (D14) is left alone.
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
    auto a = r.due(-1, true, Ssr::Off);  // base not up yet
    CHECK(!a.allNotesOff);
    CHECK(!a.organOff);
    CHECK(!r.finished());
}

TEST(startup_reset_sends_notes_off_then_power_off_once) {
    StartupReset r;
    auto a = r.due(0, true, Ssr::Off);
    CHECK(a.allNotesOff);
    CHECK(a.organOff);
    r.done(a);
    CHECK(r.finished());
    auto b = r.due(10000, true, Ssr::Off);  // never again in this run
    CHECK(!b.allNotesOff);
    CHECK(!b.organOff);
}

TEST(startup_reset_waits_a_little_for_the_midi_port) {
    StartupReset r(5000);
    auto a = r.due(1000, false, Ssr::Off);  // base up, its MIDI port not enumerated yet
    CHECK(!a.allNotesOff);
    CHECK(!a.organOff);
    auto b = r.due(2000, true, Ssr::Off);  // port appeared: both, notes first
    CHECK(b.allNotesOff);
    CHECK(b.organOff);
}

TEST(startup_reset_powers_off_without_the_midi_port_after_the_grace) {
    StartupReset r(5000);
    auto a = r.due(5000, false, Ssr::Off);
    CHECK(!a.allNotesOff);  // nothing to send notes through
    CHECK(a.notesSkipped);
    CHECK(a.organOff);      // the organ is still switched off
    r.done(a);
    CHECK(r.finished());
}

TEST(startup_reset_is_cancelled_by_a_song) {
    // A song started before the reset ran owns the organ now: no power off.
    StartupReset r;
    r.cancel();
    auto a = r.due(0, true, Ssr::Off);
    CHECK(!a.allNotesOff);
    CHECK(!a.organOff);
    CHECK(r.finished());
}

// Q1-1 b (owner 2026-11-14): a manual organ-on survives a PC start. The notes
// part still runs (a PC restart in < 6 s mid-song: the base loss guard never
// fired and notes may be sounding); only the organ-off is replaced by the
// idle-off timer.
TEST(startup_reset_ssr_on_sends_notes_off_but_no_power_off) {
    StartupReset r;
    auto a = r.due(0, true, Ssr::On);
    CHECK(a.allNotesOff);
    CHECK(!a.organOff);
    CHECK(a.armIdleOff);
    r.done(a);
    CHECK(r.finished());
}

TEST(startup_reset_waits_for_the_ssr_report_before_the_power_part) {
    StartupReset r(5000, 12000);
    auto a = r.due(0, true, Ssr::Unknown);   // notes now, power not yet decided
    CHECK(a.allNotesOff);
    CHECK(!a.organOff);
    CHECK(!a.armIdleOff);
    r.done(a);
    CHECK(!r.finished());
    auto b = r.due(2000, true, Ssr::Unknown);  // notes are not sent twice
    CHECK(!b.allNotesOff);
    CHECK(!b.organOff);
    auto c = r.due(4000, true, Ssr::On);       // report arrived: SSR on
    CHECK(!c.allNotesOff);
    CHECK(!c.organOff);
    CHECK(c.armIdleOff);
    r.done(c);
    CHECK(r.finished());
}

TEST(startup_reset_unknown_ssr_powers_off_after_the_status_grace) {
    // No STATUS_RESP at all: safety first, power off as before.
    StartupReset r(5000, 12000);
    r.done(r.due(0, true, Ssr::Unknown));
    auto a = r.due(11999, true, Ssr::Unknown);
    CHECK(!a.organOff);
    auto b = r.due(12000, true, Ssr::Unknown);
    CHECK(b.organOff);
    CHECK(!b.armIdleOff);
    r.done(b);
    CHECK(r.finished());
}

TEST(startup_reset_ssr_on_without_midi_port_waits_for_the_port_grace) {
    // Notes first: the power part is not decided before the notes part.
    StartupReset r(5000, 12000);
    auto a = r.due(1000, false, Ssr::On);
    CHECK(!a.allNotesOff);
    CHECK(!a.armIdleOff);
    auto b = r.due(5000, false, Ssr::On);
    CHECK(b.notesSkipped);
    CHECK(b.armIdleOff);
    CHECK(!b.organOff);
}
