#include "midicontrol/playback_guard.hpp"
#include "test_framework.hpp"

using mc::PlaybackHeartbeat;
using mc::PlaybackStallGuard;

TEST(playback_heartbeat_idle_is_minus_one) {
    PlaybackHeartbeat hb;
    CHECK(hb.ageMs(1000) == -1);
    hb.beat(1000);
    CHECK(hb.ageMs(1500) == 500);
    hb.stop();
    CHECK(hb.ageMs(5000) == -1);
}

TEST(playback_guard_ignores_idle_and_fresh) {
    PlaybackStallGuard g(2000);
    CHECK(!g.shouldSilence(-1));
    CHECK(!g.shouldSilence(0));
    CHECK(!g.shouldSilence(2000));
    CHECK(!g.stalled(-1));
    CHECK(!g.stalled(2000));
}

TEST(playback_guard_fires_once_per_stall) {
    PlaybackStallGuard g(2000);
    CHECK(g.stalled(2001));
    CHECK(g.shouldSilence(2001));
    CHECK(!g.shouldSilence(2500));
    CHECK(!g.shouldSilence(60000));
    CHECK(g.stalled(60000));
}

TEST(playback_guard_rearms_after_beat_or_song_end) {
    PlaybackStallGuard g(2000);
    CHECK(g.shouldSilence(3000));
    CHECK(!g.shouldSilence(10));   // thread beat again
    CHECK(g.shouldSilence(2100));  // new stall episode
    CHECK(!g.shouldSilence(-1));   // song ended
    CHECK(g.shouldSilence(2100));
}

TEST(playback_heartbeat_feeds_guard) {
    PlaybackHeartbeat hb;
    PlaybackStallGuard g(2000);
    hb.beat(10000);
    CHECK(!g.shouldSilence(hb.ageMs(11990)));
    CHECK(g.shouldSilence(hb.ageMs(12500)));
}
