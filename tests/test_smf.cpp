#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "midicontrol/smf.hpp"
#include "midicontrol/sysex.hpp"
#include "midicontrol/verse.hpp"
#include "test_framework.hpp"

#ifndef MC_TESTDATA_DIR
#define MC_TESTDATA_DIR "tests/data"
#endif
#ifndef MC_SONGS_DIR
#define MC_SONGS_DIR "songs"
#endif

using namespace mc;

namespace {
std::string songPath(const char* name) { return std::string(MC_SONGS_DIR) + "/" + name; }

int countNoteOns(const smf::MidiFile& mf) {
    int n = 0;
    for (auto& ev : mf.events)
        if (ev.isNoteOn()) ++n;
    return n;
}
}  // namespace

TEST(smf_parses_header_of_provided_files) {
    auto mf = smf::load(songPath("401.mid"));
    CHECK_EQ(mf.format, 1);
    CHECK_EQ(mf.division, 480);
    CHECK(!mf.events.empty());
}

TEST(smf_extracts_song_number_matching_filename) {
    struct Expect {
        const char* file;
        int song;
    };
    const Expect cases[] = {{"401.mid", 401}, {"403.mid", 403}, {"513.mid", 513},
                            {"516.mid", 516}, {"517.mid", 517}};
    for (auto& c : cases) {
        auto mf = smf::load(songPath(c.file));
        auto body = mf.firstDisplaySysEx();
        if (body.empty()) FAIL_AT(std::string(c.file) + ": no display SysEx found");
        auto info = sysex::parse(body);
        CHECK(info.has_value());
        if (info->song != c.song) {
            FAIL_AT(std::string(c.file) + ": song " + std::to_string(info->song) +
                    " != expected " + std::to_string(c.song));
        }
        CHECK_EQ(info->verse, 1);
    }
}

TEST(smf_files_contain_notes) {
    const char* files[] = {"401.mid", "403.mid", "513.mid", "516.mid", "517.mid", "523.mid"};
    for (auto* f : files) {
        auto mf = smf::load(songPath(f));
        int notes = countNoteOns(mf);
        if (notes <= 0) FAIL_AT(std::string(f) + ": expected note-on events, found 0");
    }
}

TEST(smf_523_has_display_sysex_and_old_empty_anchor_does_not) {
    // 523.mid now carries a real display anchor.
    auto fixed = smf::load(songPath("523.mid"));
    auto info = sysex::parse(fixed.firstDisplaySysEx());
    CHECK(info.has_value());
    if (info) CHECK_EQ(info->song, 523);
    CHECK(!scanVerseAnchors(fixed).empty());
    // The original 523.mid held only an empty F0 F7 with no MC header (fixture).
    auto mf = smf::load(std::string(MC_TESTDATA_DIR) + "/empty-anchor-523.mid");
    CHECK(mf.firstDisplaySysEx().empty());
    auto anchors = scanVerseAnchors(mf);
    CHECK(anchors.empty());
}

TEST(smf_verse_anchors_present_for_normal_song) {
    auto mf = smf::load(songPath("401.mid"));
    auto anchors = scanVerseAnchors(mf);
    CHECK(!anchors.empty());
    CHECK_EQ(anchors[0].verse, 1);
    // The anchor must point at a SysEx event.
    CHECK(mf.events[anchors[0].eventIndex].kind == smf::EventKind::SysEx);
}

TEST(smf_events_sorted_and_timed) {
    auto mf = smf::load(songPath("403.mid"));
    double lastSec = -1.0;
    uint32_t lastTick = 0;
    for (auto& ev : mf.events) {
        CHECK(ev.tick >= lastTick);
        CHECK(ev.seconds + 1e-9 >= lastSec);
        lastTick = ev.tick;
        lastSec = ev.seconds;
    }
    // Total duration should be positive and reasonable (> 0, < 1 hour).
    CHECK(mf.events.back().seconds > 0.0);
    CHECK(mf.events.back().seconds < 3600.0);
}

TEST(smf_rejects_garbage) {
    std::vector<uint8_t> junk{'N', 'O', 'P', 'E', 0, 0, 0, 0};
    bool threw = false;
    try {
        smf::parse(junk);
    } catch (const smf::ParseError&) {
        threw = true;
    }
    CHECK(threw);
}

namespace {
// Minimal format-0 SMF around one track body.
std::vector<uint8_t> smfWithTrack(const std::vector<uint8_t>& track) {
    std::vector<uint8_t> f{'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
                           'M', 'T', 'r', 'k', 0, 0, 0, static_cast<uint8_t>(track.size())};
    f.insert(f.end(), track.begin(), track.end());
    return f;
}

const std::vector<uint8_t> kAnchorBody{0x4D, 0x43, 0x01, 0x00, 0x0F, 0x04, 0x00,
                                       0x01, 0x0F, 0x01, 0x0F, 0x0F};

void checkAnchorThenNote(const smf::MidiFile& mf) {
    CHECK_EQ(mf.events.size(), 3u);
    CHECK(mf.events[0].kind == smf::EventKind::SysEx);
    CHECK(mf.events[0].bytes == kAnchorBody);
    auto info = sysex::parse(mf.events[0].bytes);
    CHECK(info.has_value());
    CHECK_EQ(info->song, 401);
    CHECK(mf.events[1].isNoteOn());  // the following event is parsed correctly
    CHECK(mf.events[2].kind == smf::EventKind::Meta);
}
}  // namespace

TEST(smf_sysex_raw_form_without_length) {
    // As in the project files: F0 <data> F7, no length.
    std::vector<uint8_t> track{0x00, 0xF0};
    track.insert(track.end(), kAnchorBody.begin(), kAnchorBody.end());
    track.insert(track.end(), {0xF7, 0x00, 0x90, 0x3C, 0x40, 0x00, 0xFF, 0x2F, 0x00});
    checkAnchorThenNote(smf::parse(smfWithTrack(track)));
}

TEST(smf_sysex_standard_form_with_length) {
    // As re-saved by a standard editor: F0 <VLQ len incl. F7> <data> F7.
    std::vector<uint8_t> track{0x00, 0xF0, static_cast<uint8_t>(kAnchorBody.size() + 1)};
    track.insert(track.end(), kAnchorBody.begin(), kAnchorBody.end());
    track.insert(track.end(), {0xF7, 0x00, 0x90, 0x3C, 0x40, 0x00, 0xFF, 0x2F, 0x00});
    checkAnchorThenNote(smf::parse(smfWithTrack(track)));
}

TEST(smf_sysex_empty_raw_and_standard) {
    // 523.mid style "F0 F7" and standard "F0 01 F7".
    for (auto sysex : {std::vector<uint8_t>{0xF0, 0xF7}, std::vector<uint8_t>{0xF0, 0x01, 0xF7}}) {
        std::vector<uint8_t> track{0x00};
        track.insert(track.end(), sysex.begin(), sysex.end());
        track.insert(track.end(), {0x00, 0x90, 0x3C, 0x40, 0x00, 0xFF, 0x2F, 0x00});
        auto mf = smf::parse(smfWithTrack(track));
        CHECK_EQ(mf.events.size(), 3u);
        CHECK(mf.events[0].bytes.empty());
        CHECK(mf.events[1].isNoteOn());
    }
}

// ---- Tempo-map accuracy: event seconds against exact expected values ----
namespace {
using Bytes = std::vector<uint8_t>;

void vlq(Bytes& out, uint32_t v) {
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = v & 0x7F;
    while ((v >>= 7) != 0) tmp[n++] = 0x80 | (v & 0x7F);
    while (n > 0) out.push_back(tmp[--n]);
}

void tempoEv(Bytes& t, uint32_t delta, uint32_t usPerQ) {
    vlq(t, delta);
    t.insert(t.end(), {0xFF, 0x51, 0x03, static_cast<uint8_t>(usPerQ >> 16),
                       static_cast<uint8_t>(usPerQ >> 8), static_cast<uint8_t>(usPerQ)});
}
void noteEv(Bytes& t, uint32_t delta, uint8_t key) {
    vlq(t, delta);
    t.insert(t.end(), {0x90, key, 0x40});
}
void endTrack(Bytes& t) { t.insert(t.end(), {0x00, 0xFF, 0x2F, 0x00}); }

Bytes buildSmf(uint16_t format, uint16_t division, const std::vector<Bytes>& tracks) {
    Bytes f{'M', 'T', 'h', 'd', 0, 0, 0, 6, static_cast<uint8_t>(format >> 8),
            static_cast<uint8_t>(format), static_cast<uint8_t>(tracks.size() >> 8),
            static_cast<uint8_t>(tracks.size()), static_cast<uint8_t>(division >> 8),
            static_cast<uint8_t>(division)};
    for (auto& t : tracks) {
        uint32_t n = static_cast<uint32_t>(t.size());
        f.insert(f.end(), {'M', 'T', 'r', 'k', static_cast<uint8_t>(n >> 24),
                           static_cast<uint8_t>(n >> 16), static_cast<uint8_t>(n >> 8),
                           static_cast<uint8_t>(n)});
        f.insert(f.end(), t.begin(), t.end());
    }
    return f;
}

std::vector<double> noteSeconds(const smf::MidiFile& mf) {
    std::vector<double> v;
    for (auto& e : mf.events)
        if (e.isNoteOn()) v.push_back(e.seconds);
    return v;
}

void checkNear(double got, double want, const std::string& what) {
    if (std::fabs(got - want) > 1e-6)
        FAIL_AT(what + ": got " + std::to_string(got) + " want " + std::to_string(want));
}
}  // namespace

TEST(smf_tempo_change_mid_song_exact) {
    // 480 PPQ. 120 bpm (500000 us) for 2 beats, then 60 bpm (1000000 us), then 240 bpm (250000 us).
    Bytes t;
    noteEv(t, 0, 60);
    tempoEv(t, 960, 1000000);   // at tick 960 (1.0 s)
    noteEv(t, 480, 61);         // tick 1440: 1.0 + 1.0 = 2.0 s
    tempoEv(t, 480, 250000);    // tick 1920: 3.0 s
    noteEv(t, 240, 62);         // tick 2160: 3.0 + 0.5*0.25 = 3.125 s
    endTrack(t);
    auto s = noteSeconds(smf::parse(buildSmf(0, 480, {t})));
    CHECK_EQ(s.size(), 3u);
    checkNear(s[0], 0.0, "n0");
    checkNear(s[1], 2.0, "n1");
    checkNear(s[2], 3.125, "n2");
}

TEST(smf_tempo_change_on_a_tick_with_notes) {
    // A note on the same tick as the tempo change keeps the OLD tempo up to that tick; the next
    // note is timed at the new tempo. Tempo meta placed after the note in the same tick.
    Bytes t;
    noteEv(t, 480, 60);          // 0.5 s
    tempoEv(t, 0, 1000000);      // same tick
    noteEv(t, 0, 61);            // same tick, after the tempo meta: still 0.5 s
    noteEv(t, 480, 62);          // +1 beat at 60 bpm = 1.5 s
    endTrack(t);
    auto s = noteSeconds(smf::parse(buildSmf(0, 480, {t})));
    CHECK_EQ(s.size(), 3u);
    checkNear(s[0], 0.5, "n0");
    checkNear(s[1], 0.5, "n1");
    checkNear(s[2], 1.5, "n2");
}

TEST(smf_tempo_in_track0_notes_in_track1) {
    Bytes t0, t1;
    tempoEv(t0, 0, 400000);       // 150 bpm
    tempoEv(t0, 960, 800000);     // at tick 960: 0.8 s, then 75 bpm
    endTrack(t0);
    noteEv(t1, 480, 60);          // 0.4 s
    noteEv(t1, 480, 61);          // tick 960: 0.8 s
    noteEv(t1, 480, 62);          // tick 1440: 0.8 + 1 beat * 0.8 = 1.6 s
    noteEv(t1, 960, 63);          // tick 2400: 1.6 + 2 beats * 0.8 = 3.2 s
    endTrack(t1);
    auto s = noteSeconds(smf::parse(buildSmf(1, 480, {t0, t1})));
    CHECK_EQ(s.size(), 4u);
    checkNear(s[0], 0.4, "n0");
    checkNear(s[1], 0.8, "n1");
    checkNear(s[2], 1.6, "n2");
    checkNear(s[3], 3.2, "n3");
}

TEST(smf_smpte_division_exact) {
    // 25 fps, 40 ticks/frame = 1000 ticks/s; tempo events must not matter.
    Bytes t;
    tempoEv(t, 0, 1000000);
    noteEv(t, 250, 60);
    noteEv(t, 1500, 61);
    endTrack(t);
    uint16_t div = static_cast<uint16_t>((static_cast<uint8_t>(-25) << 8) | 40);
    auto mf = smf::parse(buildSmf(0, div, {t}));
    auto s = noteSeconds(mf);
    CHECK_EQ(s.size(), 2u);
    checkNear(s[0], 0.25, "n0");
    checkNear(s[1], 1.75, "n1");
    // 30 fps, 80 ticks/frame = 2400 ticks/s.
    Bytes u;
    noteEv(u, 2400, 60);
    endTrack(u);
    div = static_cast<uint16_t>((static_cast<uint8_t>(-30) << 8) | 80);
    checkNear(noteSeconds(smf::parse(buildSmf(0, div, {u})))[0], 1.0, "30fps");
}

TEST(smf_large_delta_vlq) {
    // Four-byte VLQ delta: 0x0FFFFFFF ticks at 480 PPQ, 120 bpm.
    Bytes t;
    noteEv(t, 268435455u, 60);
    endTrack(t);
    auto s = noteSeconds(smf::parse(buildSmf(0, 480, {t})));
    CHECK_EQ(s.size(), 1u);
    checkNear(s[0], 268435455.0 / 480.0 * 0.5, "large delta");
    // Multi-byte VLQ boundaries.
    Bytes u;
    for (uint32_t d : {127u, 128u, 16383u, 16384u, 2097151u, 2097152u}) noteEv(u, d, 60);
    endTrack(u);
    auto v = noteSeconds(smf::parse(buildSmf(0, 480, {u})));
    CHECK_EQ(v.size(), 6u);
    uint64_t tick = 0;
    size_t i = 0;
    for (uint32_t d : {127u, 128u, 16383u, 16384u, 2097151u, 2097152u}) {
        tick += d;
        checkNear(v[i++], static_cast<double>(tick) / 480.0 * 0.5, "vlq boundary");
    }
}

TEST(smf_no_cumulative_drift_over_10000_beats) {
    // 10 000 beats, a note every beat, with a tempo change every 100 beats cycling through values that
    // have exact microsecond sums. Expected time is computed in integer microseconds.
    const uint32_t tempos[] = {500000, 400000, 625000, 480000};
    Bytes t;
    std::vector<int64_t> wantUs;
    int64_t us = 0;
    uint32_t cur = 500000;
    for (int beat = 0; beat < 10000; ++beat) {
        if (beat % 100 == 0) {
            cur = tempos[(beat / 100) % 4];
            tempoEv(t, 0, cur);
        }
        noteEv(t, beat == 0 ? 0 : 480, 60);
        if (beat > 0) us += cur;   // the tempo meta sits right after the previous note, so this gap ran at `cur`
        wantUs.push_back(us);
    }
    endTrack(t);
    auto s = noteSeconds(smf::parse(buildSmf(0, 480, {t})));
    CHECK_EQ(s.size(), 10000u);
    double worst = 0;
    for (size_t i = 0; i < s.size(); ++i)
        worst = std::max(worst, std::fabs(s[i] - static_cast<double>(wantUs[i]) / 1e6));
    if (worst > 1e-6) FAIL_AT("drift: worst error " + std::to_string(worst) + " s over 10000 beats");
    // Last note is ~ 5000 s in: absolute error stays far below 1 us.
    CHECK(worst < 1e-6);
}
