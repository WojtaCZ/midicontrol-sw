#ifndef MIDICONTROL_COMM_HPP
#define MIDICONTROL_COMM_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/config.hpp"
#include "midicontrol/dispatch.hpp"
#include "midicontrol/display.hpp"
#include "midicontrol/midi_ports.hpp"
#include "midicontrol/player.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "midicontrol/protocol/types.hpp"
#include "midicontrol/serial_port.hpp"

namespace mc {

// Binary communication manager for the MIDIControl protocol and owner of the
// player.
//
// Threads:
//  - reader:    serial RX, parsing, ACK/NAK replies (Dispatcher), retries,
//               reconnect when the port disappears. Never sleeps on commands.
//  - keepalive: KEEPALIVE to the base every 2 s, base-loss detection.
//  - control:   the only thread that creates, starts, stops and destroys the
//               Player (command queue). Organ warm-up waits happen here.
class CommHandler : private CommandSink {
public:
    // Finds the base's MIDI ports at play/record time (they re-enumerate
    // after a USB reconnect, so the index is never cached).
    using MidiPortFinder = std::function<std::optional<MidiPorts>()>;
    // Finds the base's serial device (auto-reconnect).
    using SerialFinder = std::function<std::optional<std::string>()>;

    CommHandler(const Config& config, MidiPortFinder midiFinder);
    ~CommHandler();

    // Start the worker threads. With a device path the port is opened right
    // away; without one (or when it is lost later) the reader keeps calling
    // `finder` every second until the base appears.
    void start(SerialFinder finder, const std::string& devicePath = {});

    // Open a specific serial device and start the I/O threads (used by the
    // firmware updater together with reconnect()).
    bool connect(const std::string& devicePath);
    void disconnect();
    bool reconnect(const std::string& devicePath);

    // Stop playback, switch the organ off and close everything.
    void shutdown();

    using PacketCallback = std::function<void(const mc::protocol::Packet&)>;
    // Observer for every received packet (logging). Called on the reader thread.
    void onPacket(PacketCallback cb) { _callback = std::move(cb); }

    // Low-level send.
    uint8_t send(uint8_t dest, mc::protocol::MsgType type, const std::vector<uint8_t>& payload = {});
    uint8_t sendWithAck(uint8_t dest, mc::protocol::MsgType type,
                        const std::vector<uint8_t>& payload = {});

    // Synchronous reliable send: transmits the packet and blocks until a matching
    // ACK arrives (true) or a NAK / the retry budget is exhausted (false). Used by
    // the firmware-update flow, which needs per-chunk confirmation. Does not use
    // the background _pending retry machinery.
    bool sendReliable(uint8_t dest, mc::protocol::MsgType type,
                      const std::vector<uint8_t>& payload, int timeoutMs = 600,
                      int maxRetries = 5);

    // FW_UPDATE_STATUS mailbox (firmware updater). Status packets are ACKed by
    // the dispatcher; the latest code is kept here until taken.
    void clearFwStatus();
    // Wait up to `timeoutMs` for a status code; nullopt on timeout.
    std::optional<uint8_t> waitFwStatus(int timeoutMs);
    // Non-blocking take.
    std::optional<uint8_t> takeFwStatus() { return waitFwStatus(0); }

    // High-level commands (thread-safe; playback work is queued).
    std::optional<mc::protocol::NakReason> playSong(const std::string& code);
    void stopSong();
    bool requestAddVerse();
    void recordSong();
    void organOn();
    void organOff();
    std::vector<std::string> songList() const;

    bool isPlaying() const;
    std::string sectionLabel() const;
    bool isConnected() const;
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

    struct Command {
        enum class Kind { Play, Stop, Finished, SerialLost, Shutdown } kind;
        std::string code;
        std::string path;
        uint64_t generation = 0;
    };

    // CommandSink (reader thread).
    std::optional<mc::protocol::NakReason> playSong(const std::string& code,
                                                    uint8_t requester) override;
    void stopSong(uint8_t requester) override;
    std::optional<mc::protocol::NakReason> addVerse() override;
    std::vector<uint8_t> songListPage(size_t offset) override;

    void startIoThreads();
    void stopIoThreads();
    void readerLoop();
    void keepaliveLoop();
    void controlLoop();
    void dispatchPacket(const mc::protocol::Packet& pkt);
    void handleAck(uint8_t pid, bool nak);
    void checkTimeouts();
    void handleSerialLoss();
    bool tryReopen();
    uint8_t nextPacketId();

    void post(Command cmd);
    // Wait up to `ms`; true if it elapsed, false if a new command arrived.
    bool waitInterruptible(int ms);
    void doPlay(const Command& cmd);
    void doStop(bool signal);
    void finishPlayback();
    bool releasePlayer();  // stop + destroy the player; false if there was none
    void displayClear();
    void stopRecorder();

    Config _config;
    MidiPortFinder _midiFinder;
    SerialFinder _serialFinder;
    std::string _devicePath;

    SerialPort _serial;
    mc::protocol::Parser _parser;
    std::unique_ptr<Dispatcher> _dispatcher;
    std::atomic<uint8_t> _packetId{0};

    std::mutex _mutex;  // guards _serial, _pending
    std::map<uint8_t, Pending> _pending;

    // Synchronous ACK wait used by sendReliable().
    std::mutex _ackMutex;
    std::condition_variable _ackCv;
    uint8_t _awaitPid = 0;
    bool _awaitActive = false;
    int _awaitResult = 0;  // 0 = none, 1 = ACK, 2 = NAK
    std::optional<uint8_t> _fwStatus;  // guarded by _ackMutex

    std::thread _readerThread;
    std::thread _keepaliveThread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _connected{false};
    std::atomic<Clock::time_point> _lastBaseKeepalive{Clock::time_point{}};
    std::atomic<bool> _baseLostReported{false};

    PacketCallback _callback;

    // Control thread + queue.
    std::thread _controlThread;
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Command> _queue;

    // Player: created/destroyed only on the control thread; _playerMutex
    // guards the pointer for readers on other threads.
    mutable std::mutex _playerMutex;
    std::unique_ptr<Player> _player;
    uint64_t _generation = 0;  // control thread only
    std::atomic<bool> _organOn{false};

    std::mutex _recorderMutex;
    int _recorderPid = -1;
};

}  // namespace mc

#endif  // MIDICONTROL_COMM_HPP
