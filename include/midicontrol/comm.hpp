#ifndef MIDICONTROL_COMM_HPP
#define MIDICONTROL_COMM_HPP

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/config.hpp"
#include "midicontrol/player.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "midicontrol/protocol/types.hpp"
#include "midicontrol/serial_port.hpp"
#include "midicontrol/sysex.hpp"

namespace mc {

// Binary communication manager for the MIDIControl protocol. Owns the serial
// link, a reader thread, a keepalive thread, ACK/retry tracking, and the
// MIDI player. Faithful C++ port of comm.py.
class CommHandler {
public:
    CommHandler(const Config& config, int midiOutIndex, std::string recordAlsaPort);
    ~CommHandler();

    // Open the serial port and start the worker threads.
    bool connect(const std::string& devicePath);
    void disconnect();

    using PacketCallback = std::function<void(const mc::protocol::Packet&)>;
    void onPacket(PacketCallback cb) { _callback = std::move(cb); }

    // Low-level send.
    uint8_t send(uint8_t dest, mc::protocol::MsgType type, const std::vector<uint8_t>& payload = {});
    uint8_t sendWithAck(uint8_t dest, mc::protocol::MsgType type,
                        const std::vector<uint8_t>& payload = {});

    // High-level commands (mirror comm.py).
    void playSong(const std::string& songName);
    void stopSong();
    void recordSong();
    void getSongList();
    void organOn();
    void organOff();

    void sendNowPlayingInfo(const mc::sysex::DisplayInfo& info);

    // Player passthrough.
    void repeatVerse();
    bool toggleRepeat();
    bool isPlaying() const;
    bool hasDisplayInfo() const;
    mc::sysex::DisplayInfo currentDisplayInfo() const;

    bool isBaseAlive() const;

private:
    using Clock = std::chrono::steady_clock;

    struct Pending {
        Clock::time_point timestamp;
        int retries;
        mc::protocol::MsgType msgType;
        uint8_t dest;
        std::vector<uint8_t> payload;
    };

    void readerLoop();
    void keepaliveLoop();
    void dispatchPacket(const mc::protocol::Packet& pkt);
    void handleInternal(const mc::protocol::Packet& pkt);
    void checkTimeouts();
    uint8_t nextPacketId();
    void onPlaybackFinished();

    Config _config;
    int _midiOutIndex;
    std::string _recordAlsaPort;

    SerialPort _serial;
    mc::protocol::Parser _parser;
    std::atomic<uint8_t> _packetId{0};

    std::mutex _mutex;  // guards _serial writes, _pending
    std::map<uint8_t, Pending> _pending;

    std::thread _readerThread;
    std::thread _keepaliveThread;
    std::atomic<bool> _running{false};
    std::atomic<Clock::time_point> _lastBaseKeepalive{Clock::time_point{}};

    PacketCallback _callback;

    std::unique_ptr<Player> _player;
    int _recorderPid = -1;
};

}  // namespace mc

#endif  // MIDICONTROL_COMM_HPP
