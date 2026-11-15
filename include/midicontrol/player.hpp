#ifndef MIDICONTROL_PLAYER_HPP
#define MIDICONTROL_PLAYER_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/display.hpp"
#include "midicontrol/playback.hpp"

class RtMidiOut;

namespace mc {

// Real-time song player: runs a PlaybackEngine (sections from the file's anchors, add-verse)
// on its own thread with the wall clock and an RtMidi output.
//
// Threading contract: play()/stop()/the destructor are called from ONE owner
// thread (CommHandler's control thread). Callbacks run on the playback thread
// and must not destroy the Player; onFinished should only post a message to
// the owner, which then destroys the Player (joining the finished thread).
// Open MIDI output `outPortIndex`, send `messages` and close it again (used
// when no song plays, e.g. the startup All Notes Off). False with `err` set
// when the port cannot be opened or a send fails.
bool sendMidiMessages(int outPortIndex, const std::vector<std::vector<uint8_t>>& messages,
                      std::string* err);

class Player {
public:
    // Where the MIDI bytes go instead of an RtMidi port (tests: no real port is ever opened).
    using MidiSink = std::function<void(const std::vector<uint8_t>&)>;
    explicit Player(int outPortIndex, MidiSink sink = {});
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    std::function<void(const mc::display::Update&)> onDisplay;
    std::function<void(const std::string&)> onAnchor;
    std::function<void()> onFinished;  // natural end only (not after stop())

    // Load `filepath` and start playing it in a background thread. Throws on a
    // parse error or when the MIDI port cannot be opened.
    void play(const std::string& filepath, const std::string& code, const PlaybackOptions& opts);
    void stop();

    bool playing() const { return _playing.load(); }

    // Add one extra pass of the current section. Thread-safe.
    bool addVerse();
    // Current section label ("Sloka 2 +1"), empty when idle. Thread-safe.
    std::string label() const;
    // Number of sections of the loaded song (0 before play()).
    size_t sectionCount() const { return _engine ? _engine->sections().size() : 0; }

private:
    void playbackLoop();

    int _outPortIndex;
    MidiSink _sink;
    std::unique_ptr<RtMidiOut> _midiOut;
    std::unique_ptr<PlaybackEngine> _engine;
    std::thread _thread;
    std::atomic<bool> _playing{false};
};

}  // namespace mc

#endif  // MIDICONTROL_PLAYER_HPP
