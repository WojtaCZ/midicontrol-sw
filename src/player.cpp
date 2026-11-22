#include <pthread.h>

#include "midicontrol/player.hpp"
#include "midicontrol/startup_reset.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <chrono>
#include <iostream>

namespace mc {

namespace {

using clock = std::chrono::steady_clock;

// Wall-clock timebase: sleeps in 10 ms slices so stop() stays responsive.
class WallClock : public Timebase {
public:
    WallClock(const PlaybackEngine& engine, PlaybackHeartbeat* hb) : _engine(engine), _hb(hb) {}

    void startPass() override { _start = clock::now(); }

    bool waitUntil(double seconds) override {
        const auto target =
            _start + std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(seconds));
        while (true) {
            if (_hb) _hb->beat(PlaybackHeartbeat::nowMs());  // B13: a long held note is not a stall
            if (_engine.stopRequested()) return false;
            auto now = clock::now();
            if (now >= target) return true;
            std::this_thread::sleep_for(
                std::min<clock::duration>(target - now, std::chrono::milliseconds(10)));
        }
    }

private:
    const PlaybackEngine& _engine;
    PlaybackHeartbeat* _hb;
    clock::time_point _start = clock::now();
};

}  // namespace

bool sendMidiMessages(int outPortIndex, const std::vector<std::vector<uint8_t>>& messages,
                      std::string* err) {
    try {
        RtMidiOut out;
        out.openPort(static_cast<unsigned int>(outPortIndex));
        for (const auto& m : messages) {
            std::vector<unsigned char> msg(m.begin(), m.end());
            out.sendMessage(&msg);
        }
        out.closePort();
        return true;
    } catch (const RtMidiError& e) {
        if (err) *err = e.getMessage();
        return false;
    }
}

bool emergencyNotesOff(int outPortIndex, std::chrono::milliseconds budget) {
    if (outPortIndex < 0) return false;
    auto done = std::make_shared<std::atomic<bool>>(false);
    auto ok = std::make_shared<std::atomic<bool>>(false);
    std::thread([outPortIndex, done, ok] {
        ok->store(sendMidiMessages(outPortIndex, mc::startup::allNotesOffMessages(), nullptr));
        done->store(true);
    }).detach();
    const auto deadline = clock::now() + budget;
    while (!done->load() && clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return done->load() && ok->load();
}

Player::Player(int outPortIndex, MidiSink sink) : _outPortIndex(outPortIndex), _sink(std::move(sink)) {}

Player::~Player() { stop(); }

void Player::play(const std::string& filepath, const std::string& code,
                  const PlaybackOptions& opts) {
    stop();

    auto file = mc::smf::load(filepath);  // throws on error

    PlaybackEngine::Callbacks cb;
    if (_sink) {
        cb.midi = _sink;  // no RtMidi port is opened
    } else {
        _midiOut = std::make_unique<RtMidiOut>();
        _midiOut->openPort(static_cast<unsigned int>(_outPortIndex));  // throws RtMidiError
        cb.midi = [this](const std::vector<uint8_t>& bytes) {
            std::vector<unsigned char> msg(bytes.begin(), bytes.end());
            try {
                _midiOut->sendMessage(&msg);
            } catch (const RtMidiError& e) {
                std::cerr << "MIDI output error: " << e.getMessage() << "\n";
            }
        };
    }
    cb.display = [this](const mc::display::Update& u) {
        if (onDisplay) onDisplay(u);
    };
    cb.anchor = [this](const std::string& label) {
        if (onAnchor) onAnchor(label);
    };
    _engine = std::make_unique<PlaybackEngine>(std::move(file), code, opts, std::move(cb));

    if (onSections) onSections(songHasSections(_engine->sections()));

    _playing.store(true);
    _thread = std::thread(&Player::playbackLoop, this);
}

void Player::stop() {
    if (_engine) _engine->stop();
    if (_thread.joinable()) {
        if (_thread.get_id() == std::this_thread::get_id()) {
            // Never join ourselves (would throw / terminate). The owner is
            // expected to stop from its own thread; this is a safety net.
            _thread.detach();
        } else {
            _thread.join();
        }
    }
    _playing.store(false);
    if (_midiOut) {
        _midiOut->closePort();
        _midiOut.reset();
    }
}

bool Player::addVerse() { return _playing.load() && _engine && _engine->addVerse(); }

std::string Player::label() const {
    return (_playing.load() && _engine) ? _engine->label() : std::string{};
}

void Player::playbackLoop() {
    pthread_setname_np(pthread_self(), "mc-play");  // findable in /proc/<pid>/task/*/comm (diagnostics, systest)
    WallClock tb(*_engine, heartbeat);
    if (heartbeat) heartbeat->beat(PlaybackHeartbeat::nowMs());
    auto result = _engine->run(tb);
    if (heartbeat) heartbeat->stop();
    _playing.store(false);
    if (result == PlaybackEngine::Result::Finished && onFinished) onFinished();
}

}  // namespace mc
