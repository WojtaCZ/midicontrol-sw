#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "midicontrol/playback.hpp"
#include "midicontrol/protocol/types.hpp"
#include "midicontrol/smf.hpp"
#include "test_framework.hpp"

#ifndef MC_TESTDATA_DIR
#define MC_TESTDATA_DIR "tests/data"
#endif
#ifndef MC_SONGS_DIR
#define MC_SONGS_DIR "songs"
#endif

using namespace mc;
using mc::display::Field;
using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------
// PlaybackEngine with a fake clock (no real time, no ports)
// ---------------------------------------------------------------------------

namespace {

struct FakeClock : Timebase {
    int passStarts = 0;
    std::vector<double> waits;
    // Called on every wait; return false to abort playback.
    std::function<bool(int passStarts, double t)> hook;

    void startPass() override { ++passStarts; }
    bool waitUntil(double t) override {
        waits.push_back(t);
        return hook ? hook(passStarts, t) : true;
    }
};

struct Capture {
    std::vector<Bytes> midi;
    std::vector<Bytes> display;  // encoded NOW_PLAYING_INFO payloads
    std::vector<std::string> anchors;

    PlaybackEngine::Callbacks callbacks() {
        PlaybackEngine::Callbacks cb;
        cb.midi = [this](const Bytes& b) { midi.push_back(b); };
        cb.display = [this](const display::Update& u) {
            display.push_back(display::encodeNowPlaying(u));
        };
        cb.anchor = [this](const std::string& l) { anchors.push_back(l); };
        return cb;
    }

    int noteOns() const {
        return static_cast<int>(std::count_if(midi.begin(), midi.end(), [](const Bytes& b) {
            return b.size() == 3 && (b[0] & 0xF0) == 0x90 && b[2] != 0;
        }));
    }
    int allNotesOff() const {
        return static_cast<int>(std::count_if(midi.begin(), midi.end(), [](const Bytes& b) {
            return b.size() == 3 && (b[0] & 0xF0) == 0xB0 && b[1] == 123;
        }));
    }
    bool sawSysEx() const {
        return std::any_of(midi.begin(), midi.end(), [](const Bytes& b) {
            return !b.empty() && (b[0] == 0xF0 || b[0] == 0x4D);
        });
    }
};

smf::MidiFile fixtureSong(const char* name) { return smf::load(std::string(MC_TESTDATA_DIR) + "/" + name); }
smf::MidiFile song(const char* name) { return smf::load(std::string(MC_SONGS_DIR) + "/" + name); }

int fileNoteOns(const smf::MidiFile& mf) {
    int n = 0;
    for (const auto& ev : mf.events)
        if (ev.isNoteOn()) ++n;
    return n;
}

smf::Event ev(uint32_t tick, double sec, smf::EventKind kind, Bytes bytes) {
    smf::Event e;
    e.tick = tick;
    e.seconds = sec;
    e.kind = kind;
    e.bytes = std::move(bytes);
    return e;
}

// Full display SysEx " 401" with the given verse digits.
Bytes anchor(uint8_t v1, uint8_t v0) {
    return {0x4D, 0x43, 0x01, 0x00, 0x0F, 4, 0, 1, v1, v0, 0x0F, 0x0F};
}

// Prelude, verse 1, prelude, verse 2 - one second each, one note per section.
// The note on each anchor's tick comes before the SysEx in event order.
smf::MidiFile multiVerse() {
    smf::MidiFile mf;
    const uint8_t B = sysex::BLANK_DIGIT;
    const uint8_t verses[4][2] = {{B, B}, {B, 1}, {B, B}, {B, 2}};
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t tick = i * 480;
        const double sec = i * 1.0;
        mf.events.push_back(ev(tick, sec, smf::EventKind::Channel, {0x90, 60, 100}));
        mf.events.push_back(ev(tick, sec, smf::EventKind::SysEx, anchor(verses[i][0], verses[i][1])));
        mf.events.push_back(ev(tick + 240, sec + 0.5, smf::EventKind::Channel, {0x80, 60, 0}));
    }
    mf.events.push_back(ev(1920, 4.0, smf::EventKind::Meta, {0x2F}));
    return mf;
}

std::vector<int> verseBytes(const std::vector<Bytes>& display) {
    std::vector<int> v;
    for (const auto& d : display) v.push_back(d.size() == 5 ? d[2] : -1);
    return v;
}

}  // namespace

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------

TEST(scan_sections_from_verse_anchors) {
    auto secs = scanSections(multiVerse());
    CHECK_EQ(secs.size(), 4u);
    CHECK_EQ(secs[0].verse, 0);
    CHECK_EQ(secs[1].verse, 1);
    CHECK_EQ(secs[2].verse, 0);
    CHECK_EQ(secs[3].verse, 2);
    CHECK_EQ(secs[0].firstEvent, 0u);
    CHECK_EQ(secs[1].firstEvent, 3u);  // the note on the anchor's tick belongs to it
    CHECK_EQ(secs[3].startSeconds, 3.0);
}

TEST(scan_sections_real_files_hold_one_verse) {
    auto secs = scanSections(song("401.mid"));
    CHECK_EQ(secs.size(), 1u);
    CHECK_EQ(secs[0].verse, 1);
    secs = scanSections(song("523.mid"));
    CHECK_EQ(secs.size(), 1u);
    CHECK_EQ(secs[0].verse, 1);
    // The original 523.mid (fixture) has an empty anchor: one section without a verse.
    secs = scanSections(fixtureSong("empty-anchor-523.mid"));
    CHECK_EQ(secs.size(), 1u);
    CHECK_EQ(secs[0].verse, 0);
}

TEST(section_labels) {
    Section s;
    CHECK(sectionLabel(s, 0) == "Predehra");
    s.verse = 2;
    CHECK(sectionLabel(s, 0) == "Sloka 2");
    CHECK(sectionLabel(s, 1) == "Sloka 2 +1");
}

// ---------------------------------------------------------------------------
// PlaybackEngine
// ---------------------------------------------------------------------------

TEST(engine_plays_sections_as_authored) {
    Capture cap;
    PlaybackEngine engine(multiVerse(), "401", PlaybackOptions{}, cap.callbacks());
    FakeClock clock;

    CHECK(engine.run(clock) == PlaybackEngine::Result::Finished);
    CHECK_FALSE(engine.running());
    CHECK(cap.anchors == (std::vector<std::string>{"Predehra", "Sloka 1", "Predehra", "Sloka 2"}));
    CHECK_EQ(clock.passStarts, 1);
    CHECK_EQ(cap.noteOns(), 4);
    CHECK_FALSE(cap.sawSysEx());  // display SysEx never reaches MIDI out
    // Start display, then each later anchor as authored.
    CHECK(verseBytes(cap.display) == (std::vector<int>{0xFE, 0xF1, 0xFE, 0xF2}));
    CHECK(cap.display[0] == (Bytes{0xF4, 0x01, 0xFE, 0x0F, 0x0F}));
    CHECK_FALSE(engine.addVerse());  // finished
}

TEST(engine_sends_preamble_before_song) {
    Capture cap;
    PlaybackOptions o;
    o.preamble = {{{0xF0, 0x00, 0x4A, 0x4F, 0x48, 0x41, 0x53, 0x7F, 0xF7}, 0.1}, {{0xCB, 0}, 0.02}};
    PlaybackEngine engine(multiVerse(), "401", o, cap.callbacks());
    FakeClock clock;
    CHECK(engine.run(clock) == PlaybackEngine::Result::Finished);
    CHECK(cap.midi[0] == o.preamble[0].bytes);
    CHECK(cap.midi[1] == o.preamble[1].bytes);
    CHECK_EQ(clock.passStarts, 2);  // preamble pass + song pass
    CHECK(std::abs(clock.waits[0] - 0.1) < 1e-9);
    CHECK(std::abs(clock.waits[1] - 0.12) < 1e-9);
    CHECK_EQ(cap.noteOns(), 4);
}

TEST(engine_preamble_stop_aborts_before_song) {
    Capture cap;
    PlaybackOptions o;
    o.preamble = {{{0xF0, 0xF7}, 0.1}, {{0xCB, 0}, 0.02}};
    PlaybackEngine engine(multiVerse(), "401", o, cap.callbacks());
    FakeClock clock;
    clock.hook = [](int, double) { return false; };
    CHECK(engine.run(clock) == PlaybackEngine::Result::Stopped);
    CHECK_EQ(cap.noteOns(), 0);
    CHECK(cap.anchors.empty());
    CHECK(cap.display.empty());
    CHECK_EQ(cap.allNotesOff(), 0);
    CHECK_FALSE(engine.running());
}

TEST(engine_drops_program_changes_when_asked) {
    auto countPc = [](bool drop) {
        auto mf = multiVerse();
        mf.events.insert(mf.events.begin(), ev(0, 0.0, smf::EventKind::Channel, {0xC0, 0}));
        Capture cap;
        PlaybackOptions o;
        o.dropProgramChanges = drop;
        PlaybackEngine engine(mf, "401", o, cap.callbacks());
        FakeClock clock;
        engine.run(clock);
        return std::count_if(cap.midi.begin(), cap.midi.end(),
                             [](const Bytes& b) { return (b[0] & 0xF0) == 0xC0; });
    };
    CHECK_EQ(countPc(false), 1);
    CHECK_EQ(countPc(true), 0);
}

TEST(engine_add_verse_replays_section_and_keeps_number) {
    Capture cap;
    PlaybackEngine engine(multiVerse(), "401", PlaybackOptions{}, cap.callbacks());
    FakeClock clock;
    bool pressed = false;
    clock.hook = [&](int pass, double t) {
        if (pass == 1 && t > 1.2 && !pressed) {  // during "Sloka 1"
            pressed = true;
            CHECK(engine.addVerse());
        }
        return true;
    };

    CHECK(engine.run(clock) == PlaybackEngine::Result::Finished);
    CHECK(cap.anchors == (std::vector<std::string>{"Predehra", "Sloka 1", "Sloka 1 +1", "Sloka 1",
                                                   "Predehra", "Sloka 2"}));
    CHECK_EQ(clock.passStarts, 2);
    CHECK_EQ(cap.noteOns(), 5);
    // The repeat shows verse 1 again; the number never advances on its own.
    CHECK(verseBytes(cap.display) == (std::vector<int>{0xFE, 0xF1, 0xF1, 0xFE, 0xF2}));
    // The repeat is timed from the section's anchor: its note-off at +0.5 s.
    CHECK(std::find(clock.waits.begin(), clock.waits.end(), 0.5) != clock.waits.end());
}

TEST(engine_last_section_repeat_and_stop) {
    auto mf = song("407.mid");
    Capture cap;
    PlaybackEngine engine(mf, "407", PlaybackOptions{}, cap.callbacks());
    FakeClock clock;
    clock.hook = [&](int pass, double t) {
        if (pass == 1 && t > 1.0 && engine.label() == "Sloka 1") engine.addVerse();
        if (pass == 2 && t > 2.0) engine.stop();  // stop during the extra pass
        return true;
    };
    CHECK(engine.run(clock) == PlaybackEngine::Result::Stopped);
    CHECK(cap.anchors == (std::vector<std::string>{"Sloka 1", "Sloka 1 +1", "Sloka 1"}));
    CHECK_EQ(cap.allNotesOff(), 2 * 16);  // before the repeat and at the stop
    CHECK_FALSE(engine.addVerse());
}

TEST(engine_real_file_plays_once) {
    auto mf = song("401.mid");
    const int notes = fileNoteOns(mf);
    Capture cap;
    PlaybackEngine engine(mf, "401", PlaybackOptions{}, cap.callbacks());
    FakeClock clock;
    CHECK(engine.run(clock) == PlaybackEngine::Result::Finished);
    CHECK(cap.anchors == (std::vector<std::string>{"Sloka 1"}));
    CHECK_EQ(cap.noteOns(), notes);
    CHECK_EQ(cap.display.size(), 1u);  // the head anchor is not re-sent
    CHECK(cap.display[0] == (Bytes{0xF4, 0x01, 0xF1, 0x0F, 0x0F}));
}

TEST(engine_abort_from_timebase) {
    auto mf = song("401.mid");
    Capture cap;
    PlaybackEngine engine(mf, "401", PlaybackOptions{}, cap.callbacks());
    FakeClock clock;
    clock.hook = [](int, double t) { return t < 3.0; };
    CHECK(engine.run(clock) == PlaybackEngine::Result::Stopped);
    CHECK_EQ(cap.anchors.size(), 1u);
}

TEST(engine_shifted_digits_letter_and_523_fallback) {
    auto first = [](const char* file, const char* code) {
        Capture cap;
        PlaybackEngine engine(std::string(file).rfind("empty-anchor", 0) == 0 ? fixtureSong(file) : song(file), code, PlaybackOptions{}, cap.callbacks());
        FakeClock clock;
        engine.run(clock);
        return cap.display.at(0);
    };
    CHECK(first("512.mid", "512") == (Bytes{0xF5, 0x12, 0x1F, 0x0F, 0x0F}));   // verse "1 "
    CHECK(first("513.mid", "513") == (Bytes{0x51, 0x3F, 0xF1, 0x0F, 0x0F}));   // song "513 "
    CHECK(first("406A.mid", "406A") == (Bytes{0xF4, 0x06, 0xF1, 0x0A, 0x0F}));
    // The old 523.mid's anchor is an empty F0 F7 (fixture): the song number comes from the code.
    CHECK(first("empty-anchor-523.mid", "523") == (Bytes{0xF5, 0x23, 0xFE, 0x0F, 0x0F}));
}

TEST(engine_forwards_partial_sysex_and_waits_gap_before_repeat) {
    smf::MidiFile mf;
    mf.events.push_back(ev(0, 0.0, smf::EventKind::SysEx, anchor(0x0F, 1)));
    mf.events.push_back(ev(0, 0.0, smf::EventKind::Channel, {0x90, 60, 100}));
    mf.events.push_back(ev(480, 0.5, smf::EventKind::SysEx, {0x4D, 0x43, 0x01, 0x03, 0x0C}));
    mf.events.push_back(ev(960, 1.0, smf::EventKind::Channel, {0x80, 60, 0}));

    Capture cap;
    PlaybackOptions opts;
    opts.gapSeconds = 0.25;
    PlaybackEngine engine(mf, "401", opts, cap.callbacks());
    FakeClock clock;
    clock.hook = [&](int pass, double t) {
        if (pass == 1 && t == 0.5) engine.addVerse();
        return true;
    };
    CHECK(engine.run(clock) == PlaybackEngine::Result::Finished);

    // Start display, letter partial, then the letter partial again in the repeat.
    CHECK_EQ(cap.display.size(), 3u);
    CHECK(cap.display[1] == (Bytes{0xFF, 0xFF, 0xFF, 0x0C, 0xFF}));
    CHECK(std::find(clock.waits.begin(), clock.waits.end(), 0.25) != clock.waits.end());
    CHECK_EQ(clock.passStarts, 3);  // start, gap, repeat
    CHECK_EQ(cap.noteOns(), 2);
}

TEST(song_has_sections_a6) {
    CHECK(songHasSections(scanSections(multiVerse())));
    CHECK(songHasSections(scanSections(song("401.mid"))));    // one section, but it carries verse 1
    CHECK(songHasSections(scanSections(song("523.mid"))));    // fixed anchor carries verse 1
    CHECK(!songHasSections(scanSections(fixtureSong("empty-anchor-523.mid"))));  // empty anchor: no verse markers
    CHECK(!songHasSections({}));
}
