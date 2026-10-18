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
