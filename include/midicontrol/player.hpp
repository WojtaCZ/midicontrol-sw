#ifndef MIDICONTROL_PLAYER_HPP
#define MIDICONTROL_PLAYER_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

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
class Player {
public:
    explicit Player(int outPortIndex);
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
    std::unique_ptr<RtMidiOut> _midiOut;
    std::unique_ptr<PlaybackEngine> _engine;
    std::thread _thread;
    std::atomic<bool> _playing{false};
};

}  // namespace mc

#endif  // MIDICONTROL_PLAYER_HPP
