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
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/config.hpp"
#include "midicontrol/device_info.hpp"
#include "midicontrol/dispatch.hpp"
#include "midicontrol/diag_poller.hpp"
#include "midicontrol/diag_store.hpp"
#include "midicontrol/display.hpp"
#include "midicontrol/midi_ports.hpp"
#include "midicontrol/player.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "midicontrol/protocol/types.hpp"
#include "midicontrol/serial_port.hpp"
#include "midicontrol/startup_reset.hpp"
#include "midicontrol/status.hpp"

namespace mc {

// Binary communication manager for the MIDIControl protocol and owner of the
// player.
//
// Threads:
//  - reader:    serial RX, parsing, ACK/NAK replies (Dispatcher), retries,
//               reconnect when the port disappears. Never sleeps on commands.
//  - keepalive: KEEPALIVE to the base every 2 s, base-loss detection,
//               GET_STATUS poll every 5 s, MIDI port check every 10 s.
//  - control:   the only thread that creates, starts, stops and destroys the
//               Player (command queue). Organ warm-up waits happen here.
//
// Every thread reports into the StatusRegistry (counters per peer, link and
// playback state, packet events) and stamps a heartbeat for the watchdog.
class CommHandler : private CommandSink {
public:
    // Finds the base's MIDI ports at play/record time (they re-enumerate
    // after a USB reconnect, so the index is never cached).
    using MidiPortFinder = std::function<std::optional<MidiPorts>()>;
    // Finds the base's serial device (auto-reconnect).
    using SerialFinder = std::function<std::optional<std::string>()>;

    // `diag` (optional): where the remote's DIAG records are stored; without it the remote is not polled.
    CommHandler(const Config& config, MidiPortFinder midiFinder,
                mc::status::StatusRegistry& status, mc::diag::DiagStore* diag = nullptr);
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
    // the background _pending retry machinery. The defaults are the protocol's retry budget
    // (RETRY_TIMEOUT_MS, MAX_RETRIES retries after the first send); the firmware updater passes its own.
    bool sendReliable(uint8_t dest, mc::protocol::MsgType type,
                      const std::vector<uint8_t>& payload,
                      int timeoutMs = mc::protocol::RETRY_TIMEOUT_MS,
                      int maxRetries = mc::protocol::MAX_RETRIES);

    // FW_UPDATE_STATUS mailbox (firmware updater). Status packets are ACKed by
    // the dispatcher; the latest code is kept here until taken.
    void clearFwStatus();
    // Wait up to `timeoutMs` for a status code; nullopt on timeout.
    std::optional<uint8_t> waitFwStatus(int timeoutMs);
    // Non-blocking take.
    std::optional<uint8_t> takeFwStatus() { return waitFwStatus(0); }
    // NAK reason of the last sendReliable() that ended in a NAK; empty after an ACK or a timeout.
    std::optional<uint8_t> lastNakReason();

    // High-level commands (thread-safe; playback work is queued).
    std::optional<mc::protocol::NakReason> playSong(const std::string& code);
    void stopSong();
    bool requestAddVerse();
    // false: not started (already recording, no MIDI input port, no file, spawn failed, or cancelled).
    bool recordSong();
    void organOn();
    void organOff();
    std::vector<std::string> songList() const;

    bool isPlaying() const;
    std::string sectionLabel() const;
    bool isConnected() const;
    // Protocol version the node reported in its INFO_RESP (PROTO_VER); 1 while unknown or when the node does not send it.
    int peerProtocolVersion(mc::status::Node node) const;
    bool isBaseAlive() const;

    // STATUS_RESP flags as the base reports them.
    struct BaseStatus {
        bool ble = false;
        bool relay = false;
        bool display = false;
        bool pc = false;
    };
    // Interactive `status`: send GET_STATUS and wait for the answer.
    std::optional<BaseStatus> requestStatus(int timeoutMs = 1500);

    // Liveness for the systemd watchdog: every worker thread stamped its
    // heartbeat within `maxAge` (reader/keepalive only while they run).
    bool threadsAlive(std::chrono::milliseconds maxAge) const;

    // P1-14: one gate for "a song is coming" and "an update runs". A Play claims it when it is queued
    // (before the player exists, so also through the organ warm-up); the control thread releases it when
    // no player is left and no Play is queued. tryBeginUpdate() takes the update side only when neither
    // a song nor another update holds the gate; endUpdate() gives it back.
    enum class UpdateClaim { Granted, Busy, Playing };
    UpdateClaim tryBeginUpdate();
    void endUpdate();
    bool updateClaimed() const;

    // P1-13: while the base is not connected, the reader asks `probe` once a second whether the base sits
    // in its USB bootloader (an update that did not finish); not during a base update, where that is
    // expected. Reported once per edge: log, status field Base/in_bootloader, console `status`. Set
    // before start()/connect(). No probe (the default, and in tests unless given): never looked for.
    void setBootloaderProbe(std::function<bool()> probe) { _bootloaderProbe = std::move(probe); }
    bool baseInBootloader() const { return _inBootloader.load(); }

    // A base firmware update is running: the bootloader ignores KEEPALIVE and
    // GET_STATUS, so neither is sent and keepalive loss is not reported.
    void setUpdating(bool on);

    // A remote firmware update is running over BLE: no DIAG_GET (it would compete with FW_UPDATE_DATA, and the
    // remote programs its diag slot's "fetched" byte in the W25Q32 the OTA stages to). Unlike setUpdating(), base
    // keepalive and GET_STATUS continue.
    void pauseDiag(bool on);

    // Tests only: the player writes its MIDI bytes here instead of opening an RtMidi port.
    void setMidiSinkForTest(Player::MidiSink sink) { _midiSinkForTest = std::move(sink); }

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
        enum class Kind { Play, Stop, Finished, SerialLost, Shutdown, StartupReset, IdleOff, RemoteResync } kind;
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
    void handleAck(uint8_t pid, bool nak, uint8_t reason = 0);
    void checkTimeouts();
    void handleSerialLoss();
    bool tryReopen();
    uint8_t nextPacketId();
    // Unreliable, id already set; `sentOut` (optional) tells whether the frame was written.
    uint8_t sendPacket(const mc::protocol::Packet& pkt, bool* sentOut = nullptr);

    // Status instrumentation.
    void noteTx(const mc::protocol::Packet& pkt, bool retransmit);
    void noteRx(const mc::protocol::Packet& pkt);
    bool isRoutine(const mc::protocol::Packet& pkt, bool tx);
    void noteConnected(const std::string& path, bool reconnect);
    void pollStatus();
    void onInfoResp(const mc::protocol::Packet& pkt);  // INFO_RESP -> node fields (reader thread)
    void clearInfo(mc::status::Node node);               // node went away: its INFO fields are unknown
    void clearBaseReport();                              // serial lost: STATUS_RESP fields are unknown
    void storeRemoteDiag(mc::diag::DiagEntry e);  // DiagPoller output (reader / keepalive thread)
    void refreshMidiPort();
    void playbackEnded(const std::string& how, const char* counter);
    static int64_t steadyMs();

    void post(Command cmd);
    // Wait up to `ms`; true if it elapsed, false if a new command arrived.
    bool waitInterruptible(int ms);
    void doPlay(const Command& cmd);
    void doStop(bool signal);
    void finishPlayback();
    // P1-9 remote resync: every SIGNAL_* to the remote goes through sendRemoteSignal().
    void sendRemoteSignal(mc::protocol::MsgType type, const std::vector<uint8_t>& payload = {});
    // A6: SIGNAL_INFO [flags] (protocol v2 remote only); remembered for the resync.
    void sendSignalInfo(uint8_t flags);
    uint8_t sendRemoteSignalLocked(mc::protocol::MsgType type, const std::vector<uint8_t>& payload,
                                   bool& registered);  // _resyncMutex held
    uint8_t sendWithAckImpl(uint8_t dest, mc::protocol::MsgType type, const std::vector<uint8_t>& payload,
                            bool* registered);
    void noteRemoteSignalEnd(uint8_t pid, mc::protocol::MsgType type, bool acked);  // reader thread
    void doRemoteResync();
    bool releasePlayer();  // stop + destroy the player; false if there was none
    void displayClear();
    // P1-10: true while the indicator shows something this PC sent (a non-clear NOW_PLAYING_INFO);
    // displayClear() is a no-op otherwise. Kept across a serial loss: the base may still show the code.
    std::atomic<bool> _displaySet{false};
    void stopRecorder();
    bool recorderAliveLocked();  // _recorderMutex held; forgets a recorder that exited on its own

    Config _config;
    mc::status::StatusRegistry& _status;
    MidiPortFinder _midiFinder;
    SerialFinder _serialFinder;
    std::function<bool()> _bootloaderProbe;
    std::atomic<bool> _inBootloader{false};
    void noteBootloader(bool in);  // reader thread
    std::string _devicePath;

    SerialPort _serial;
    mc::protocol::Parser _parser;
    std::unique_ptr<Dispatcher> _dispatcher;
    // Fast PC restart: the base de-duplicates by the last PacketId per source, so every
    // process starts at a random id (a restart repeating the old ids could have its first
    // command dropped as a duplicate). Two processes still collide with p = 1/256.
    std::atomic<uint8_t> _packetId{initialPacketId()};
    static uint8_t initialPacketId();
    // Protocol v2: session nonce (non-zero, random per process) sent in every KEEPALIVE to the base. A restarted PC shows a
    // different nonce, so base and remote forget the old PC's duplicate window even when the new PacketIds collide.
    const uint16_t _sessionNonce{makeSessionNonce()};
    static uint16_t makeSessionNonce();
    void sendKeepalive();

    std::mutex _mutex;  // guards _serial, _pending
    std::map<uint8_t, Pending> _pending;

    // Synchronous ACK wait used by sendReliable().
    std::mutex _ackMutex;
    std::condition_variable _ackCv;
    uint8_t _awaitPid = 0;
    bool _awaitActive = false;
    int _awaitResult = 0;  // 0 = none, 1 = ACK, 2 = NAK
    uint8_t _awaitReason = 0;  // NAK reason when _awaitResult == 2 (guarded by _ackMutex)
    std::optional<uint8_t> _fwStatus;  // guarded by _ackMutex
    std::optional<uint8_t> _lastNak;   // guarded by _ackMutex; see lastNakReason()
    // A retried FW_UPDATE_STATUS (same PacketId, same byte) must not count as a new status. Guarded by _ackMutex.
    bool _fwSeen = false;
    uint8_t _fwSeenPid = 0, _fwSeenByte = 0;
    Clock::time_point _fwSeenAt{};

    std::thread _readerThread;
    std::thread _keepaliveThread;
    std::atomic<bool> _running{false};
    std::atomic<bool> _connected{false};
    std::atomic<Clock::time_point> _lastBaseKeepalive{Clock::time_point{}};
    std::atomic<bool> _baseLostReported{false};

    PacketCallback _callback;
    uint64_t _badFrames = 0;  // reader thread: parser count already reported

    // Routine traffic (background GET_STATUS poll and its ACKs) gets no events.
    std::mutex _routineMutex;
    std::set<uint8_t> _pollPids;      // our poll GET_STATUS packet ids
    std::set<uint8_t> _pollRespPids;  // the base's STATUS_RESP ids we ACK
    bool _pollOutstanding = false;    // keepalive thread + reader (under _routineMutex)
    uint8_t _pollPid = 0;
    int64_t _pollSentMs = 0;
    // Organ power: a STATUS_RESP is the base's truth only when no SET_CURRENT went out after its GET_STATUS
    // (the base answers in order). Item 4b: one (sentMs, _setCurrentGen) entry per GET_STATUS written to the
    // base, popped by each new STATUS_RESP, so an answer to an older request is judged by that request's
    // generation. Extra entries (lost or de-duplicated requests) only make the PC ignore more answers.
    // _setCurrentGen is bumped and the entries pushed under _mutex, in the same section as the write.
    std::atomic<uint64_t> _setCurrentGen{0};
    std::deque<std::pair<int64_t, uint64_t>> _statusGens;  // _mutex
    int _lastStatusRespPid = -1;                            // _mutex; the base retries an un-ACKed STATUS_RESP
    void noteStatusRequestLocked();                         // caller holds _mutex

    // Interactive status request (requestStatus).
    std::mutex _statusMutex;
    std::condition_variable _statusCv;
    int _statusWaiters = 0;
    uint64_t _statusSeq = 0;
    BaseStatus _lastStatus;

    // Heartbeats (steady ms) for threadsAlive().
    std::atomic<int64_t> _hbReader{0};
    std::atomic<int64_t> _hbKeepalive{0};
    std::atomic<int64_t> _hbControl{0};
    std::atomic<bool> _everConnected{false};
    std::atomic<bool> _updating{false};

    // Remote diagnostics (DIAG_GET polling, see DiagPoller). Null store = not polled.
    mc::diag::DiagStore* _diag;
    std::unique_ptr<mc::diag::DiagPoller> _diagPoller;
    std::set<uint8_t> _diagPids;      // our DIAG_GET ids (under _routineMutex): routine traffic
    std::set<uint8_t> _diagRespPids;  // the remote's DIAG_RESP ids we ACK
    std::unique_ptr<mc::devinfo::InfoPoller> _infoPoller;  // GET_INFO to the base and the remote
    std::set<uint8_t> _infoRespPids;  // INFO_RESP ids we ACK (under _routineMutex): routine traffic
    std::atomic<bool> _remoteLink{false};    // last STATUS_RESP said ble=1
    std::atomic<bool> _remotePaused{false};  // remote firmware update running (pauseDiag)
    std::atomic<int64_t> _reconnects{0};

    // P1-9: "remote state dirty" until the remote ACKs a re-sent state. Lock order: _resyncMutex before
    // _mutex (sendRemoteSignal holds it across sendWithAck so an ACK cannot be matched before it is awaited).
    // A new PC process starts dirty and "may play": the remote can still show a song from the previous run.
    std::mutex _resyncMutex;
    bool _remoteDirty = true;
    std::optional<uint16_t> _lastRemoteBootId;  // A2: remote boot id of the current ble=1 stretch (under _resyncMutex)
    bool _remoteMayPlay = true;      // the remote may think a song plays (cleared by an ACKed SIGNAL_STOPPED)
    std::set<uint8_t> _resyncAwait;  // pids of the current resync attempt
    std::string _lastAnchor;         // last SIGNAL_ANCHOR label of the current song
    std::optional<uint8_t> _lastSignalInfo;  // last SIGNAL_INFO flags byte sent for the current song (A6)
    std::atomic<bool> _resyncQueued{false};
    Player::MidiSink _midiSinkForTest;

    // Song being played: sections visited (playback thread appends, control
    // thread reads after the player has been joined).
    std::mutex _songMutex;
    std::string _songCode;
    std::vector<std::string> _songSections;

    // Control thread + queue.
    std::thread _controlThread;
    mutable std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Command> _queue;
    // P1-14 gate, guarded by _queueMutex (a Play is claimed in the same lock that queues it).
    bool _playClaimed = false;
    bool _updateClaimed = false;
    void releasePlayClaimIfIdle();  // control thread, after each command

    // Player: created/destroyed only on the control thread; _playerMutex
    // guards the pointer for readers on other threads.
    mutable std::mutex _playerMutex;
    std::unique_ptr<Player> _player;
    uint64_t _generation = 0;  // control thread only
    std::atomic<bool> _organOn{false};
    // PCC-2: the organ state the PC last commanded and not yet confirmed by an authoritative STATUS_RESP.
    // organOn/organOff set it and send under _organMutex, so a re-send can never overtake a newer command
    // (the supersede rule makes the last sender win). Lock order: _organMutex before _mutex.
    std::mutex _organMutex;
    int _organIntent = -1;          // -1 none (adopt what the base reports), 0 off, 1 on
    int64_t _organIntentMs = 0;     // steady ms of the command
    int _organReasserts = 0;        // re-sends for this intent
    int _organFault = -1;           // SSR state (0/1) shown as organ_power_fault after giving up; -1 none
    void setOrganLocked(bool on);   // caller holds _organMutex
    void reconcileOrgan(bool relay, uint64_t statusGen);  // authoritative STATUS_RESP (reader thread)

    // Startup reset (gap 2 / SYS-PWR-07 / SYS-NOTE-04): notes off + organ off
    // once the base link is up. The keepalive ACK posts Kind::StartupReset
    // while it is not finished; the control thread decides and performs it.
    void doStartupReset();
    mc::startup::StartupReset _startupReset;  // control thread only
    std::atomic<bool> _startupResetDone{false};
    std::atomic<bool> _startupResetQueued{false};
    std::atomic<bool> _organClaimed{false};  // organOn() ran: never power off at startup
    std::atomic<int64_t> _baseUpMs{-1};      // steady ms of the first keepalive ACK
    std::atomic<int> _ssrReported{-1};       // last authoritative STATUS_RESP relay: -1 unknown, 0 off, 1 on

    // Q1-1 b idle-off timer: armed when the startup reset finds the SSR on
    // (manual organ-on survives a PC start). Disarmed when the PC switches the
    // organ (organOn/organOff), a song starts, or the base reports it off.
    // The keepalive thread posts Kind::IdleOff when it expires; the control
    // thread re-checks and powers off.
    void armIdleOff();
    void disarmIdleOff();
    void doIdleOff();
    std::atomic<int64_t> _idleOffArmedMs{-1};  // steady ms when armed, -1 = not armed
    std::atomic<bool> _idleOffQueued{false};

    std::mutex _recorderMutex;
    int _recorderPid = -1;
    bool _recorderStarting = false;   // PCC-1: a Record is in its warm-up (guarded by _recorderMutex)
    bool _recorderCancelled = false;  // stopRecorder ran during that warm-up
};

}  // namespace mc

#endif  // MIDICONTROL_COMM_HPP
