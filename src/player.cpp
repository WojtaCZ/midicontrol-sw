#include "midicontrol/player.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <chrono>

namespace mc {

using clock = std::chrono::steady_clock;

Player::Player(int outPortIndex) : _outPortIndex(outPortIndex) {}

Player::~Player() { stop(); }

void Player::play(const std::string& filepath) {
    stop();

    _file = mc::smf::load(filepath);  // throws on error
    _anchors = scanVerseAnchors(_file);
    _currentVerseIdx = 0;
    _repeatMode.store(false);
    _hasInfo.store(false);
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _jumpRequested = false;
        _currentInfo = mc::sysex::DisplayInfo{};
    }
    _stop.store(false);
    _playing.store(true);

    _thread = std::thread(&Player::playbackLoop, this);
}

void Player::stop() {
    if (!_playing.load() && !_thread.joinable()) return;
    _stop.store(true);
    if (_thread.joinable()) _thread.join();
    _playing.store(false);
}

void Player::repeatVerse() {
    if (!_playing.load() || _anchors.empty()) return;
    std::lock_guard<std::mutex> lk(_mutex);
    _jumpTargetIndex = _anchors[_currentVerseIdx].eventIndex;
    _jumpRequested = true;
}

bool Player::toggleRepeat() {
    bool now = !_repeatMode.load();
    _repeatMode.store(now);
    return now;
}

mc::sysex::DisplayInfo Player::currentInfo() const {
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(_mutex));
    return _currentInfo;
}

void Player::allNotesOff() {
    if (!_midiOut) return;
    for (int ch = 0; ch < 16; ++ch) {
        std::vector<unsigned char> msg{static_cast<unsigned char>(0xB0 | ch), 123, 0};
        _midiOut->sendMessage(&msg);
    }
}

void Player::playbackLoop() {
    _midiOut = std::make_unique<RtMidiOut>();
    try {
        _midiOut->openPort(static_cast<unsigned int>(_outPortIndex));
    } catch (RtMidiError&) {
        _playing.store(false);
        return;
    }

    playFrom(0, 0.0);

    allNotesOff();
    _midiOut->closePort();
    _midiOut.reset();

    bool finishedNaturally = !_stop.load();
    _playing.store(false);
    if (finishedNaturally && onFinished) onFinished();
}

void Player::playFrom(size_t startIndex, double startSeconds) {
    const auto playbackStart = clock::now();
    const double timeOffset = startSeconds;

    auto takeJump = [this](size_t& target) -> bool {
        std::lock_guard<std::mutex> lk(_mutex);
        if (!_jumpRequested) return false;
        _jumpRequested = false;
        target = _jumpTargetIndex;
        return true;
    };
    auto jumpPending = [this]() -> bool {
        std::lock_guard<std::mutex> lk(_mutex);
        return _jumpRequested;
    };

    for (size_t idx = startIndex; idx < _file.events.size(); ++idx) {
        if (_stop.load()) return;

        // Handle a pending jump before processing this event.
        size_t jumpTarget = 0;
        if (takeJump(jumpTarget)) {
            allNotesOff();
            playFrom(jumpTarget, _file.events[jumpTarget].seconds);
            return;
        }

        const auto& ev = _file.events[idx];

        // Sleep until this event's wall-clock time, staying responsive.
        const auto targetWall =
            playbackStart + std::chrono::duration_cast<clock::duration>(
                                std::chrono::duration<double>(ev.seconds - timeOffset));
        bool sawJump = false;
        while (true) {
            auto now = clock::now();
            if (now >= targetWall) break;
            auto remaining = targetWall - now;
            auto slice = std::min<clock::duration>(remaining, std::chrono::milliseconds(10));
            std::this_thread::sleep_for(slice);
            if (_stop.load()) return;
            if (jumpPending()) { sawJump = true; break; }
        }
        // If a jump arrived while waiting, re-enter the jump branch for this idx.
        if (sawJump) {
            --idx;  // counteract the loop ++idx
            continue;
        }

        // Process the event.
        if (ev.kind == mc::smf::EventKind::SysEx) {
            auto info = mc::sysex::parse(ev.bytes);
            if (info.has_value()) {
                {
                    std::lock_guard<std::mutex> lk(_mutex);
                    _currentInfo = *info;
                }
                _hasInfo.store(true);
                // Track which verse we are in.
                for (size_t i = 0; i < _anchors.size(); ++i) {
                    if (_anchors[i].eventIndex == idx) {
                        _currentVerseIdx = i;
                        break;
                    }
                }
                if (onDisplayInfo) onDisplayInfo(*info);
            }
            // SysEx is never forwarded to the MIDI output.
        } else if (ev.kind == mc::smf::EventKind::Channel) {
            std::vector<unsigned char> msg(ev.bytes.begin(), ev.bytes.end());
            _midiOut->sendMessage(&msg);
        }
        // Meta events are ignored for output.

        // Automatic verse repeat: when we reach the next verse anchor, jump back.
        if (_repeatMode.load() && !_anchors.empty()) {
            size_t nextVerseIdx = _currentVerseIdx + 1;
            if (nextVerseIdx < _anchors.size() && idx + 1 >= _anchors[nextVerseIdx].eventIndex) {
                allNotesOff();
                const auto& cur = _anchors[_currentVerseIdx];
                playFrom(cur.eventIndex, cur.seconds);
                return;
            }
        }
    }
}

}  // namespace mc
