#include <string>

#include "midicontrol/smf.hpp"
#include "midicontrol/sysex.hpp"
#include "midicontrol/verse.hpp"
#include "test_framework.hpp"

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

TEST(smf_523_has_no_display_sysex) {
    // 523.mid contains only an empty F0 F7 with no MC header.
    auto mf = smf::load(songPath("523.mid"));
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
