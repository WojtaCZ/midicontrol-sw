#ifndef MIDICONTROL_PLAYBACK_HPP
#define MIDICONTROL_PLAYBACK_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "midicontrol/display.hpp"
#include "midicontrol/smf.hpp"

// Song structure and real-time playback logic, independent of RtMidi, the
// serial link and the wall clock (both are injected), so it is unit-testable.
//
// Song model: the MIDI file defines the structure. Every display SysEx that
// carries a verse field is an anchor and starts a section, which runs to the
// next anchor (or the end of the file). A blank verse (or verse 0) marks the
// prelude:
//
//     Predehra -> Sloka 1 -> Predehra -> Sloka 2 -> ...   (as authored)
//
// "Add verse" (REPLAY_ANCHOR) replays the current section once more when it
// reaches its end, before playback continues; presses accumulate, and the
// last section can be repeated too. The indicator keeps the verse number of
// the section being repeated.
namespace mc {

// One raw MIDI message to send, then wait `delayAfterSeconds` before the next one.
struct MidiStep {
    std::vector<uint8_t> bytes;
    double delayAfterSeconds = 0.0;
};

// One section of the song, from its anchor to the next one.
struct Section {
    size_t firstEvent = 0;  // index into MidiFile::events (first event at the anchor's tick)
    uint32_t tick = 0;
    double startSeconds = 0.0;
    int verse = 0;  // 0 = prelude (blank verse field or no verse anchor yet)
};

// Split a parsed file into sections at its verse anchors. Always returns at
// least one section starting at the first event. Anchors on the same tick are
// merged; anchors before the first note define the first section.
std::vector<Section> scanSections(const mc::smf::MidiFile& mf);

// True when the file has verse markers: more than one section, or a first section that carries a verse number.
// A song with only a prelude-like head (no verse anchor, or only blank ones) has no sections (A6).
bool songHasSections(const std::vector<Section>& sections);

// Label for SIGNAL_ANCHOR: "Predehra", "Sloka 2", "Sloka 2 +1".
std::string sectionLabel(const Section& s, int pendingRepeats);

// Wall-clock abstraction for the engine. Times are seconds from the start of
// the current pass.
class Timebase {
public:
    virtual ~Timebase() = default;
    virtual void startPass() = 0;
    // Block until `seconds` after startPass(). Return false to abort playback.
    virtual bool waitUntil(double seconds) = 0;
};

struct PlaybackOptions {
    double gapSeconds = 0.0;  // silence before a repeated section
    std::vector<MidiStep> preamble;  // sent before the song, through the Timebase
    bool dropProgramChanges = false;  // skip 0xCn channel events from the file
};

// Plays a parsed song file section by section on the calling thread.
//
// Display: at the start a full NOW_PLAYING_INFO is emitted from the file's
// leading anchor SysEx - digits exactly as authored, the song number falling
// back to the song code when the anchor is missing or empty (e.g. 523.mid),
// and every field defined so nothing of the previous song stays on the
// indicator. Display SysEx later in the file is forwarded as a partial update
// (untouched fields = no change). SysEx is never sent to MIDI out.
class PlaybackEngine {
public:
    struct Callbacks {
        std::function<void(const std::vector<uint8_t>&)> midi;        // MIDI message (channel or SysEx)
        std::function<void(const mc::display::Update&)> display;      // NOW_PLAYING_INFO
        std::function<void(const std::string&)> anchor;               // SIGNAL_ANCHOR label
    };

    enum class Result { Finished, Stopped };

    PlaybackEngine(mc::smf::MidiFile file, std::string code, PlaybackOptions opts, Callbacks cb);

    // Run the whole song. Blocks until it ends or stop() is called.
    Result run(Timebase& tb);

    // Thread-safe controls.
    bool addVerse();  // false once stopped or finished
    void stop() { _stop.store(true); }
    bool stopRequested() const { return _stop.load(); }
    bool running() const { return _running.load(); }
    std::string label() const;

    const std::vector<Section>& sections() const { return _sections; }

    // The display update emitted when playback starts (exposed for tests).
    const mc::display::Update& startDisplay() const { return _base; }

private:
    void allNotesOff();
    void announce();  // SIGNAL_ANCHOR for the current section

    mc::smf::MidiFile _file;
    std::string _code;
    PlaybackOptions _opts;
    Callbacks _cb;

    // Authored display (leading anchor) with fallbacks applied.
    mc::display::Update _base;
    std::vector<bool> _isHeadAnchor;  // per event: already part of _base
    std::vector<Section> _sections;

    mutable std::mutex _mutex;  // guards the three below
    size_t _current = 0;
    int _pending = 0;
    bool _finished = false;
    std::atomic<bool> _stop{false};
    std::atomic<bool> _running{false};
};

}  // namespace mc

#endif  // MIDICONTROL_PLAYBACK_HPP
