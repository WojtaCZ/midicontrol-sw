#ifndef MIDICONTROL_PLAYER_HPP
#define MIDICONTROL_PLAYER_HPP

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/smf.hpp"
#include "midicontrol/sysex.hpp"
#include "midicontrol/verse.hpp"

class RtMidiOut;

namespace mc {

// MIDI file player with inline MIDIControl SysEx handling and verse repeat.
// Faithful C++ port of player.py: SysEx messages are intercepted (not sent to
// the MIDI output) and surfaced through onDisplayInfo; channel messages are
// streamed to RtMidi with wall-clock timing.
class Player {
public:
    explicit Player(int outPortIndex);
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Callbacks (invoked from the playback thread).
    std::function<void(const mc::sysex::DisplayInfo&)> onDisplayInfo;
    std::function<void()> onFinished;  // called when playback runs to the end

    // Start playing a file in a background thread. Throws on parse error.
    void play(const std::string& filepath);
    void stop();

    bool playing() const { return _playing.load(); }
    bool repeatMode() const { return _repeatMode.load(); }

    // Jump back to the start of the current verse.
    void repeatVerse();
    // Toggle automatic verse repeat; returns the new state.
    bool toggleRepeat();

    // Current decoded display state (empty until the first SysEx is seen).
    mc::sysex::DisplayInfo currentInfo() const;
    bool hasInfo() const { return _hasInfo.load(); }

private:
    void playbackLoop();
    void playFrom(size_t startIndex, double startSeconds);
    void allNotesOff();

    int _outPortIndex;
    std::unique_ptr<RtMidiOut> _midiOut;
    std::thread _thread;

    std::atomic<bool> _playing{false};
    std::atomic<bool> _stop{false};
    std::atomic<bool> _repeatMode{false};
    std::atomic<bool> _hasInfo{false};

    // Jump request (protected by _mutex).
    std::mutex _mutex;
    bool _jumpRequested = false;
    size_t _jumpTargetIndex = 0;

    mc::smf::MidiFile _file;
    std::vector<VerseAnchor> _anchors;
    size_t _currentVerseIdx = 0;
    mc::sysex::DisplayInfo _currentInfo;
};

}  // namespace mc

#endif  // MIDICONTROL_PLAYER_HPP
