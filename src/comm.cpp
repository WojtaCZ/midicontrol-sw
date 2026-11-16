#include "midicontrol/comm.hpp"


#include <cstdio>
#include <ctime>
#include <random>
#include <sstream>

#include "midicontrol/diag_api.hpp"
#include "midicontrol/log.hpp"
#include "midicontrol/process.hpp"
#include "midicontrol/recording.hpp"
#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/songs.hpp"
#include "midicontrol/stops_file.hpp"

namespace mc {

using namespace mc::protocol;
using mc::status::Node;

namespace {

constexpr int kStatusPollMs = 5000;     // GET_STATUS to the base while connected
constexpr int kStatusAnswerMs = 2000;   // no STATUS_RESP by then = "not answered"
constexpr int kMidiCheckMs = 10000;     // MIDI port re-discovery for the status page
constexpr uint64_t kNoStatusGen = UINT64_MAX;     // Item 4b: a STATUS_RESP that answers no known GET_STATUS
constexpr int64_t kStatusGenMaxAgeMs = 15000;     // Item 4b: a request unanswered this long is not answered later
constexpr int kOrganReassertMax = 3;         // PCC-2: re-sends of a SET_CURRENT the base did not take
constexpr int64_t kOrganIntentWindowMs = 30000;  // PCC-2: how long a PC organ command is defended
std::string idleText(int64_t ms) {
    return ms >= 60000 ? std::to_string(ms / 60000) + " min" : std::to_string(ms / 1000) + " s";
}

// PM-13: reliable messages that carry a state, where only the newest one may reach the node.
// 0 = no supersede; equal non-zero values supersede each other (same destination only).
int supersedeGroup(MsgType t) {
    switch (t) {
        case MsgType::SET_CURRENT: return 1;          // organ SSR on/off
        case MsgType::SIGNAL_PLAYING:
        case MsgType::SIGNAL_RECORDING:
        case MsgType::SIGNAL_STOPPED: return 2;       // remote's playing/recording/stopped state
        default: return 0;
    }
}

bool isRemoteSignal(MsgType t) {
    return t == MsgType::SIGNAL_PLAYING || t == MsgType::SIGNAL_RECORDING || t == MsgType::SIGNAL_STOPPED ||
           t == MsgType::SIGNAL_ANCHOR;
}

std::string hexByte(uint8_t b) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", b);
    return buf;
}

std::string nodeName(uint8_t addr) {
    switch (addr) {
        case ADDR_BASE: return "base";
        case ADDR_REMOTE: return "controller";
        case ADDR_BROADCAST: return "broadcast";
        default: return hexByte(addr);
    }
}

std::string joined(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& x : v) s += (s.empty() ? "" : ", ") + x;
    return s;
}

}  // namespace

int64_t CommHandler::steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               Clock::now().time_since_epoch())
        .count();
}

CommHandler::CommHandler(const Config& config, MidiPortFinder midiFinder,
                         mc::status::StatusRegistry& status, mc::diag::DiagStore* diag)
    : _config(config), _status(status), _midiFinder(std::move(midiFinder)), _diag(diag) {
    Dispatcher::Output out;
    out.send = [this](uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
        send(dest, type, payload);
    };
    out.sendWithAck = [this](uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
        sendWithAck(dest, type, payload);
    };
    out.onAck = [this](uint8_t pid) { handleAck(pid, false); };
    out.onNak = [this](uint8_t pid, uint8_t reason) { handleAck(pid, true, reason); };
    out.onKeepaliveAck = [this]() {
        _lastBaseKeepalive.store(Clock::now());
        int64_t unset = -1;
        _baseUpMs.compare_exchange_strong(unset, steadyMs());
        if (!_startupResetDone.load() && !_updating.load() && !_startupResetQueued.exchange(true)) {
            post({Command::Kind::StartupReset, {}, {}, 0});
        }
        _status.setNow(Node::Base, "keepalive_ack_ms");
        if (_baseLostReported.exchange(false)) {
            _status.set(Node::Base, "alive", true);
            mc::log::info(Node::Base, "Base is responding again.");
        }
    };
    out.onDuplicate = [this](const Packet& pkt) {
        for (Node n : mc::status::nodesForPeer(pkt.source)) _status.count(n, "duplicates");
    };
    out.onDiagResp = [this](const Packet& pkt) {
        if (pkt.source == ADDR_REMOTE && _diagPoller) _diagPoller->onResp(pkt.payload.data(), pkt.payload.size(), steadyMs());
    };
    out.onInfoResp = [this](const Packet& pkt) { onInfoResp(pkt); };
    _dispatcher = std::make_unique<Dispatcher>(static_cast<CommandSink&>(*this), std::move(out));

    mc::devinfo::InfoPoller::Hooks ih;
    ih.sendGet = [this](mc::devinfo::InfoPoller::Target t) {
        return sendWithAck(t == mc::devinfo::InfoPoller::Base ? ADDR_BASE : ADDR_REMOTE, MsgType::GET_INFO);
    };
    ih.clear = [this](mc::devinfo::InfoPoller::Target t) {
        clearInfo(t == mc::devinfo::InfoPoller::Base ? Node::Base : Node::Controller);
    };
    ih.unsupported = [this](mc::devinfo::InfoPoller::Target t) {
        const Node n = t == mc::devinfo::InfoPoller::Base ? Node::Base : Node::Controller;
        _status.set(n, "info_unsupported", true);
        mc::log::info(n, std::string(t == mc::devinfo::InfoPoller::Base ? "Base" : "Controller") +
                             " firmware does not answer GET_INFO (older firmware): no device details.");
    };
    _infoPoller = std::make_unique<mc::devinfo::InfoPoller>(std::move(ih));

    mc::diag::DiagPoller::Hooks dh;
    dh.sendGet = [this](const std::vector<uint8_t>& payload) {
        const uint8_t pid = sendWithAck(ADDR_REMOTE, MsgType::DIAG_GET, payload);
        std::lock_guard<std::mutex> lk(_routineMutex);
        if (_diagPids.size() > 16) _diagPids.clear();  // ids wrap; only the last few matter
        _diagPids.insert(pid);
        return pid;
    };
    dh.emit = [this](mc::diag::DiagEntry e) { storeRemoteDiag(std::move(e)); };
    dh.pcNowMs = [this] { return _status.now(); };
    if (_diag) {  // no store = nothing to poll for
        dh.loadCursor = [this] { return _diag->loadRemoteCursor(); };
        dh.saveCursor = [this](uint16_t boot, uint16_t cursor) { _diag->saveRemoteCursor(boot, cursor); };
        _diagPoller = std::make_unique<mc::diag::DiagPoller>(std::move(dh));
    }

    _status.set(Node::Base, "connected", false);
    _status.set(Node::Base, "reconnects", int64_t{0});
    _status.set(Node::Base, "organ_power", false);
    _status.set(Node::Software, "playback", std::string("idle"));
    _hbControl.store(steadyMs());
    _controlThread = std::thread(&CommHandler::controlLoop, this);
}

CommHandler::~CommHandler() { shutdown(); }

// ---------------------------------------------------------------------------
// Connection management
// ---------------------------------------------------------------------------

void CommHandler::start(SerialFinder finder, const std::string& devicePath) {
    _serialFinder = std::move(finder);
    bool opened = false;
    if (!devicePath.empty()) {
        std::lock_guard<std::mutex> lk(_mutex);
        if (_serial.open(devicePath, 115200)) {
            _devicePath = devicePath;
            opened = true;
        }
    }
    if (opened) {
        sendKeepalive();  // hello with the session nonce, before any reliable frame (_connected is still false)
        _connected.store(true);
    }
    if (_connected.load()) {
        noteConnected(devicePath, false);
    } else {
        if (!devicePath.empty()) mc::log::error(Node::Base, "Failed to open serial port: " + devicePath);
        mc::log::info(Node::Base, "Waiting for the MIDIControl base...");
    }
    startIoThreads();
}

bool CommHandler::connect(const std::string& devicePath) {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (!_serial.open(devicePath, 115200)) {
            mc::log::error(Node::Base, "Failed to open serial port: " + devicePath);
            // Keep the reader running anyway: it looks for the base again
            // (finder) every second, so the service never stays disconnected.
            startIoThreads();
            return false;
        }
        _devicePath = devicePath;
    }
    sendKeepalive();  // hello with the session nonce, before any reliable frame (_connected is still false)
    _connected.store(true);
    noteConnected(devicePath, false);
    startIoThreads();
    return true;
}

void CommHandler::startIoThreads() {
    if (_running.exchange(true)) return;
    _parser.reset();
    _dispatcher->reset();
    _lastBaseKeepalive.store(Clock::now());
    _baseLostReported.store(false);
    _hbReader.store(steadyMs());
    _hbKeepalive.store(steadyMs());
    _readerThread = std::thread(&CommHandler::readerLoop, this);
    _keepaliveThread = std::thread(&CommHandler::keepaliveLoop, this);
}

void CommHandler::stopIoThreads() {
    _running.store(false);
    if (_readerThread.joinable()) _readerThread.join();
    if (_keepaliveThread.joinable()) _keepaliveThread.join();
}

void CommHandler::disconnect() {
    stopIoThreads();
    if (_diagPoller) _diagPoller->setRemoteLink(false, steadyMs());
    std::lock_guard<std::mutex> lk(_mutex);
    _serial.close();
    _pending.clear();
    _statusGens.clear();  // no answer to an earlier request can come over a new connection
    _lastStatusRespPid = -1;
    _connected.store(false);
    _status.set(Node::Base, "connected", false);
    clearBaseReport();
    _infoPoller->tick(steadyMs(), false, false);
}

bool CommHandler::reconnect(const std::string& devicePath) {
    disconnect();
    return connect(devicePath);
}

void CommHandler::shutdown() {
    if (_controlThread.joinable()) {
        post({Command::Kind::Shutdown, {}, {}, 0});
        _controlThread.join();
    }
    disconnect();
}

bool CommHandler::isConnected() const { return _connected.load(); }

bool CommHandler::isBaseAlive() const {
    if (!_connected.load()) return false;
    return (Clock::now() - _lastBaseKeepalive.load()) <
           std::chrono::milliseconds(KEEPALIVE_LOSS_MS);
}

bool CommHandler::threadsAlive(std::chrono::milliseconds maxAge) const {
    const int64_t now = steadyMs();
    const int64_t limit = maxAge.count();
    if (now - _hbControl.load() > limit) return false;
    if (!_running.load()) return true;  // I/O threads stopped on purpose (disconnect)
    return now - _hbReader.load() <= limit && now - _hbKeepalive.load() <= limit;
}

void CommHandler::pauseDiag(bool on) {
    _remotePaused.store(on);  // no GET_INFO to the remote during its update either
    if (_diagPoller) _diagPoller->setPaused(on);
}

void CommHandler::setUpdating(bool on) {
    if (!on) {
        // Start the loss timer afresh once the application is back.
        _lastBaseKeepalive.store(Clock::now());
        _baseLostReported.store(false);
    }
    _updating.store(on);
    _status.set(Node::Base, "updating", on);
}

void CommHandler::noteConnected(const std::string& path, bool reconnect) {
    _everConnected.store(true);
    _status.set(Node::Base, "connected", true);
    _status.set(Node::Base, "alive", true);
    _status.set(Node::Base, "device", path);
    _status.set(Node::Base, "vidpid", usbIdOf(path));
    _status.set(Node::Base, "status_answered", std::monostate{});
    _status.setNow(Node::Base, "connected_since_ms");
    if (reconnect) {
        _status.set(Node::Base, "reconnects", ++_reconnects);
        mc::log::info(Node::Base, "Connected to the base on " + path);
    } else {
        mc::log::info(Node::Base, "Opened serial port: " + path);
    }
}

void CommHandler::handleSerialLoss() {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _serial.close();
        _pending.clear();
        _statusGens.clear();  // no answer to an earlier request can come over a new connection
        _lastStatusRespPid = -1;
    }
    _connected.store(false);
    _parser.reset();
    _dispatcher->reset();
    if (_diagPoller) _diagPoller->setRemoteLink(false, steadyMs());  // flushes a half-received FAULT
    _status.set(Node::Base, "connected", false);
    clearBaseReport();
    _infoPoller->tick(steadyMs(), false, false);  // down now: a reconnect faster than the 2 s tick is still an edge
    mc::log::warn(Node::Base, "Serial link to the base lost; waiting for it to come back...");
    post({Command::Kind::SerialLost, {}, {}, 0});
}

void CommHandler::noteBootloader(bool in) {
    if (_inBootloader.exchange(in) == in) return;
    _status.set(Node::Base, "in_bootloader", in);
    if (in) {
        mc::log::error(Node::Base, "The base is in its USB bootloader (ABCD:1235): a firmware update did not "
                                   "finish. Re-flash the base (console: update base <file>).");
    } else {
        mc::log::info(Node::Base, "The base left its bootloader.");
    }
}

bool CommHandler::tryReopen() {
    std::optional<std::string> path;
    if (_serialFinder) {
        path = _serialFinder();
    } else if (!_devicePath.empty()) {
        path = _devicePath;
    }
    if (!path) return false;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (!_serial.open(*path, 115200)) return false;
        _devicePath = *path;
    }
    _parser.reset();
    _dispatcher->reset();
    sendKeepalive();  // hello with the session nonce, before any reliable frame
    _lastBaseKeepalive.store(Clock::now());
    _baseLostReported.store(false);
    _connected.store(true);
    noteConnected(*path, _everConnected.load());
    return true;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

uint8_t CommHandler::initialPacketId() {
    uint32_t seed = 0;
    try {
        std::random_device rd;
        seed = rd();
    } catch (...) {
        // no entropy source: the clock below still differs between restarts
    }
    seed ^= static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    seed ^= static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count() >> 10);
    return static_cast<uint8_t>(seed ^ (seed >> 8) ^ (seed >> 16) ^ (seed >> 24));
}

void CommHandler::noteStatusRequestLocked() {
    const int64_t now = steadyMs();
    while (!_statusGens.empty() && now - _statusGens.front().first > kStatusGenMaxAgeMs) _statusGens.pop_front();
    _statusGens.emplace_back(now, _setCurrentGen.load());
}

uint16_t CommHandler::makeSessionNonce() {
    uint32_t seed = 0;
    try {
        std::random_device rd;
        seed = rd();
    } catch (...) {
        // no entropy source: the clocks below still differ between restarts
    }
    seed ^= static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    seed ^= static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count() >> 7);
    const uint16_t n = static_cast<uint16_t>(seed ^ (seed >> 16));
    return n == 0 ? uint16_t(1) : n;  // 0 means "no nonce" on the wire
}

void CommHandler::sendKeepalive() {
    send(ADDR_BASE, MsgType::KEEPALIVE,
         {static_cast<uint8_t>(_sessionNonce & 0xFF), static_cast<uint8_t>(_sessionNonce >> 8)});
}

int CommHandler::peerProtocolVersion(Node node) const {
    const auto snap = _status.snapshot();
    const auto& f = snap.node(node).fields;
    const auto it = f.find("proto_ver");
    if (it == f.end()) return 1;
    const auto* v = std::get_if<int64_t>(&it->second);
    return v && *v >= 1 ? static_cast<int>(*v) : 1;
}

uint8_t CommHandler::nextPacketId() { return _packetId.fetch_add(1); }

uint8_t CommHandler::send(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    return sendPacket(Packet(ADDR_PC, dest, nextPacketId(), type, payload));
}

uint8_t CommHandler::sendPacket(const Packet& pkt, bool* sentOut) {
    auto raw = encode(pkt);
    bool sent = false;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (_serial.isOpen()) {
            const bool statusReq = pkt.msgType == MsgType::GET_STATUS && pkt.dest == ADDR_BASE;
            if (statusReq) noteStatusRequestLocked();
            sent = _serial.write(raw);
            if (statusReq && !sent) _statusGens.pop_back();
        }
    }
    if (sent) noteTx(pkt, false);
    if (sentOut) *sentOut = sent;
    return pkt.packetId;
}

uint8_t CommHandler::sendWithAck(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    return sendWithAckImpl(dest, type, payload, nullptr);
}

uint8_t CommHandler::sendWithAckImpl(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload,
                                     bool* registered) {
    if (registered) *registered = false;
    // Register before writing so a fast ACK cannot arrive before the entry exists.
    uint8_t pid = nextPacketId();
    Packet pkt(ADDR_PC, dest, pid, type, payload);
    auto raw = encode(pkt);
    int superseded = 0;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        // Item 4b: every SET_CURRENT counts, sent or not; STATUS_RESPs to earlier GET_STATUS no longer tell
        // the SSR state. Under _mutex so no GET_STATUS write can fall between the bump and this write.
        if (type == MsgType::SET_CURRENT && dest == ADDR_BASE) ++_setCurrentGen;
        if (!_serial.isOpen()) return pid;  // nothing to retry while disconnected
        // PM-13 supersede rule, at the single send point for reliable packets: a newer SET_CURRENT
        // (or SIGNAL_PLAYING/SIGNAL_STOPPED) drops every older un-ACKed one of the same group to the same
        // node. Otherwise the old frame's retry (old PacketId) can reach the node after the new one, and
        // the node, which remembers only the last PacketId per source, executes the stale state last.
        // Erase, register and write under one lock so checkTimeouts cannot retransmit in between.
        const int group = supersedeGroup(type);
        if (group != 0) {
            for (auto it = _pending.begin(); it != _pending.end();) {
                // Item 2: a state signal (group 2) also drops a pending SIGNAL_ANCHOR, whose section belongs
                // to the old state. One-directional: an ANCHOR supersedes nothing (a lost PLAYING must still
                // be retried after the anchor that follows it).
                const bool staleAnchor = group == 2 && it->second.msgType == MsgType::SIGNAL_ANCHOR;
                if (it->second.dest == dest && (supersedeGroup(it->second.msgType) == group || staleAnchor)) {
                    it = _pending.erase(it);
                    ++superseded;
                } else {
                    ++it;
                }
            }
        }
        _pending[pid] = Pending{Clock::now(), 0, type, dest, payload};
        if (type == MsgType::GET_STATUS && dest == ADDR_BASE) noteStatusRequestLocked();
        _serial.write(raw);
        if (registered) *registered = true;
    }
    if (superseded > 0) {
        for (Node n : mc::status::nodesForPeer(dest)) {
            mc::log::debug(n, std::string(msgTypeName(type)) + " supersedes " + std::to_string(superseded) +
                                  " pending older one(s)");
        }
    }
    if (dest == ADDR_REMOTE && type != MsgType::DIAG_GET && type != MsgType::GET_INFO) {
        _status.set(Node::Controller, "last_tx", std::string(msgTypeName(type)));
        _status.setNow(Node::Controller, "last_tx_ms");
        _status.set(Node::Controller, "last_tx_result", std::string("pending"));
    }
    noteTx(pkt, false);
    return pid;
}

// P1-9: the single send point for SIGNAL_* to the remote. Holding _resyncMutex across the send keeps the
// last anchor and the resync bookkeeping in step with what went on the wire (an ACK waits for it).
uint8_t CommHandler::sendRemoteSignalLocked(MsgType type, const std::vector<uint8_t>& payload, bool& registered) {
    const uint8_t pid = sendWithAckImpl(ADDR_REMOTE, type, payload, &registered);
    if (type == MsgType::SIGNAL_PLAYING) {
        _remoteMayPlay = true;
        _lastAnchor.clear();
    } else if (type == MsgType::SIGNAL_ANCHOR) {
        _lastAnchor.assign(payload.begin(), payload.end());
    }
    if (!registered) _remoteDirty = true;  // no link: the remote did not get it
    return pid;
}

void CommHandler::sendRemoteSignal(MsgType type, const std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lk(_resyncMutex);
    bool registered = false;
    sendRemoteSignalLocked(type, payload, registered);
}

void CommHandler::noteRemoteSignalEnd(uint8_t pid, MsgType type, bool acked) {
    std::lock_guard<std::mutex> lk(_resyncMutex);
    if (!acked) {
        // NAK (UNREACHABLE, BUSY, ...) or no answer after the retries: the remote's state is unknown.
        _remoteDirty = true;
        _resyncAwait.clear();  // the running attempt failed; a late ACK of its other packets proves nothing
        return;
    }
    if (type == MsgType::SIGNAL_STOPPED) _remoteMayPlay = false;
    if (_resyncAwait.erase(pid) > 0 && _resyncAwait.empty()) {
        _remoteDirty = false;
        mc::log::info(Node::Controller, "Remote state resynced (ACKed).");
    }
}

void CommHandler::doRemoteResync() {
    _resyncQueued.store(false);
    // No send during a firmware update (base or remote); the next STATUS_RESP with ble=1 tries again.
    if (_updating.load() || _remotePaused.load() || !_remoteLink.load()) return;
    const bool playing = isPlaying();
    std::string code;
    {
        std::lock_guard<std::mutex> lk(_songMutex);
        code = _songCode;
    }
    std::lock_guard<std::mutex> lk(_resyncMutex);
    if (!_remoteDirty) return;
    _resyncAwait.clear();
    std::vector<std::pair<uint8_t, bool>> sent;
    std::string what;
    if (playing) {
        bool reg = false;
        const std::vector<uint8_t> anchor(_lastAnchor.begin(), _lastAnchor.end());  // PLAYING clears it
        sent.emplace_back(sendRemoteSignalLocked(MsgType::SIGNAL_PLAYING,
                                                 std::vector<uint8_t>(code.begin(), code.end()), reg), reg);
        what = "SIGNAL_PLAYING " + code;
        if (!anchor.empty()) {
            sent.emplace_back(sendRemoteSignalLocked(MsgType::SIGNAL_ANCHOR, anchor, reg), reg);
            what += " + SIGNAL_ANCHOR";
        }
    } else if (_remoteMayPlay) {
        bool reg = false;
        sent.emplace_back(sendRemoteSignalLocked(MsgType::SIGNAL_STOPPED, {}, reg), reg);
        what = "SIGNAL_STOPPED";
    } else {
        _remoteDirty = false;  // idle and known to be stopped: nothing to tell it
        return;
    }
    bool all = true;
    for (const auto& [pid, reg] : sent) {
        if (reg) _resyncAwait.insert(pid);
        all = all && reg;
    }
    if (!all) _resyncAwait.clear();  // part of it never went out: stay dirty
    mc::log::info(Node::Controller, "Remote resync: " + what);
}

bool CommHandler::sendReliable(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload,
                               int timeoutMs, int maxRetries) {
    // Same PacketId on every retry, so the receiver can suppress duplicates.
    const uint8_t pid = nextPacketId();
    const Packet pkt(ADDR_PC, dest, pid, type, payload);
    const auto raw = encode(pkt);
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        {
            std::lock_guard<std::mutex> lk(_ackMutex);
            _awaitPid = pid;
            _awaitResult = 0;
            _awaitActive = true;
        }
        bool sent = false;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_serial.isOpen()) sent = _serial.write(raw);
        }
        if (sent) noteTx(pkt, attempt > 0);

        std::unique_lock<std::mutex> lk(_ackMutex);
        bool signalled = _ackCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                         [&] { return _awaitResult != 0; });
        int result = _awaitResult;
        const uint8_t reason = _awaitReason;
        _awaitActive = false;
        lk.unlock();

        if (signalled && result == 1) return true;   // ACK
        if (signalled && result == 2 && reason == static_cast<uint8_t>(NakReason::BUSY)) {
            // The base could not forward right now (BLE module briefly in command mode / TX full): the link is
            // usable, so retry like a timeout instead of failing a whole firmware update.
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }
        if (signalled && result == 2) return false;  // NAK — caller aborts
        // timeout: retry
    }
    for (Node n : mc::status::nodesForPeer(dest)) _status.count(n, "gave_up");
    return false;
}

void CommHandler::handleAck(uint8_t pid, bool nak, uint8_t reason) {
    std::optional<Pending> pending;
    std::optional<Pending> busyRetry;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        auto it = _pending.find(pid);
        if (it != _pending.end()) {
            Pending& p = it->second;
            // Protocol rule (SYSTEM.md, PROTOCOL.md): NAK BUSY is retried like a timeout, same PacketId, same
            // budget. Kept pending with a fresh timestamp, checkTimeouts resends it after RETRY_TIMEOUT_MS and
            // spends one retry; the budget used up falls through to the NAK path below. Background polls keep
            // their own BUSY rules (DiagPoller: next tick; InfoPoller; the 5 s GET_STATUS poll). Bench
            // 2026-11-15 (LAT-7): an un-retried SIGNAL_* BUSY left the remote on the old state for ~3.6 s.
            const bool background = p.msgType == MsgType::DIAG_GET || p.msgType == MsgType::GET_INFO ||
                                    p.msgType == MsgType::GET_STATUS;
            if (nak && reason == static_cast<uint8_t>(NakReason::BUSY) && !background && p.retries < MAX_RETRIES) {
                p.timestamp = Clock::now();
                busyRetry = p;
            } else {
                pending = p;
                _pending.erase(it);
            }
        }
    }
    if (busyRetry) {
        for (Node n : mc::status::nodesForPeer(busyRetry->dest)) {
            _status.count(n, "busy_retries");
            mc::log::info(n, "NAK BUSY for " + std::string(msgTypeName(busyRetry->msgType)) + " id=" +
                                 std::to_string(pid) + " to " + nodeName(busyRetry->dest) + ": retrying (" +
                                 std::to_string(busyRetry->retries + 1) + "/" + std::to_string(MAX_RETRIES) + ")");
        }
        return;
    }
    if (nak && _diagPoller) _diagPoller->onNak(pid, reason);  // stops the polling after NAK UNKNOWN_MSG/UNREACHABLE (BUSY only retries on the timer)
    const bool infoGet = pending && pending->msgType == MsgType::GET_INFO;
    if (nak && infoGet) _infoPoller->onNak(pid, reason, steadyMs());  // reports UNKNOWN_MSG itself, once
    bool pollNak = false;
    if (nak) {
        std::lock_guard<std::mutex> lk(_routineMutex);
        if (_pollOutstanding && pid == _pollPid) {
            _pollOutstanding = false;
            pollNak = true;
        }
    }
    if (pollNak) {
        _status.set(Node::Base, "status_answered", false);
    } else if (nak && !infoGet) {
        std::string what = pending ? std::string(msgTypeName(pending->msgType)) + " id=" +
                                         std::to_string(pid) + " to " + nodeName(pending->dest)
                                   : "id=" + std::to_string(pid);
        mc::log::info(pending ? mc::status::nodesForPeer(pending->dest)[0] : Node::Base,
                      "NAK for " + what + ": " + nakReasonName(reason));
    }
    if (pending && pending->dest == ADDR_REMOTE && pending->msgType != MsgType::DIAG_GET && !infoGet) {
        _status.set(Node::Controller, "last_tx_result",
                    nak ? "NAK " + std::string(nakReasonName(reason)) : std::string("ACK"));
    }
    if (pending && pending->dest == ADDR_REMOTE && isRemoteSignal(pending->msgType)) {
        noteRemoteSignalEnd(pid, pending->msgType, !nak);
    }
    std::lock_guard<std::mutex> lk(_ackMutex);
    if (_awaitActive && pid == _awaitPid) {
        _awaitResult = nak ? 2 : 1;
        _awaitReason = reason;
        _ackCv.notify_all();
    }
}

void CommHandler::clearFwStatus() {
    std::lock_guard<std::mutex> lk(_ackMutex);
    _fwStatus.reset();
}

std::optional<uint8_t> CommHandler::waitFwStatus(int timeoutMs) {
    std::unique_lock<std::mutex> lk(_ackMutex);
    _ackCv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return _fwStatus.has_value(); });
    auto s = _fwStatus;
    _fwStatus.reset();
    return s;
}

void CommHandler::checkTimeouts() {
    auto now = Clock::now();
    std::vector<Packet> resent;
    std::vector<std::pair<uint8_t, Pending>> gaveUp;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        for (auto it = _pending.begin(); it != _pending.end();) {
            Pending& p = it->second;
            if (now - p.timestamp <= std::chrono::milliseconds(RETRY_TIMEOUT_MS)) {
                ++it;
                continue;
            }
            if (p.retries < MAX_RETRIES) {
                // Retransmit with the SAME PacketId (receiver de-duplicates).
                Packet pkt(ADDR_PC, p.dest, it->first, p.msgType, p.payload);
                // A retried GET_STATUS may be answered again (the base keeps only its last PacketId): one
                // more entry, so a second answer cannot take a newer request's generation.
                if (p.msgType == MsgType::GET_STATUS && p.dest == ADDR_BASE && _serial.isOpen()) {
                    noteStatusRequestLocked();
                }
                if (_serial.isOpen() && _serial.write(encode(pkt))) resent.push_back(std::move(pkt));
                p.timestamp = now;
                p.retries += 1;
                ++it;
            } else {
                gaveUp.emplace_back(it->first, p);
                it = _pending.erase(it);
            }
        }
    }
    for (const auto& pkt : resent) noteTx(pkt, true);
    for (const auto& [pid, p] : gaveUp) {
        // Background polls (DIAG_GET, GET_INFO) time out whenever the remote drops before the base notices: counted,
        // logged as debug, and they don't overwrite the result of the last real command shown on the page.
        const bool background = p.msgType == MsgType::DIAG_GET || p.msgType == MsgType::GET_INFO;
        for (Node n : mc::status::nodesForPeer(p.dest)) {
            _status.count(n, "gave_up");
            const std::string text = std::string(msgTypeName(p.msgType)) + " id=" + std::to_string(pid) + " to " +
                                     nodeName(p.dest) + " not acknowledged after " + std::to_string(MAX_RETRIES) +
                                     " retries";
            if (background) {
                mc::log::debug(n, text);
            } else {
                mc::log::warn(n, text);
            }
        }
        if (p.dest == ADDR_REMOTE && !background) _status.set(Node::Controller, "last_tx_result", std::string("no answer"));
        if (p.dest == ADDR_REMOTE && isRemoteSignal(p.msgType)) noteRemoteSignalEnd(pid, p.msgType, false);
    }
}

// ---------------------------------------------------------------------------
// Status instrumentation
// ---------------------------------------------------------------------------

bool CommHandler::isRoutine(const Packet& pkt, bool tx) {
    std::lock_guard<std::mutex> lk(_routineMutex);
    auto bounded = [](std::set<uint8_t>& s, uint8_t pid) {
        if (s.size() > 16) s.clear();  // ids wrap; only the last few matter
        s.insert(pid);
    };
    switch (pkt.msgType) {
        case MsgType::KEEPALIVE:
        case MsgType::KEEPALIVE_ACK:
            return true;
        case MsgType::GET_STATUS:
            return tx && _pollPids.count(pkt.packetId);
        case MsgType::STATUS_RESP: {
            if (tx) return false;
            std::lock_guard<std::mutex> slk(_statusMutex);
            if (_statusWaiters > 0) return false;  // answer to the interactive `status`
            bounded(_pollRespPids, pkt.packetId);
            return true;
        }
        case MsgType::DIAG_GET:
            return tx;  // our own background poll
        case MsgType::GET_INFO:
            return tx;  // only the InfoPoller sends it
        case MsgType::INFO_RESP:
            if (tx) return false;
            bounded(_infoRespPids, pkt.packetId);
            return true;
        case MsgType::DIAG_RESP:
            if (tx) return false;
            bounded(_diagRespPids, pkt.packetId);
            return true;
        case MsgType::ACK:
            if (pkt.payload.empty()) return false;
            if (tx) {
                return _pollRespPids.count(pkt.payload[0]) > 0 || _diagRespPids.count(pkt.payload[0]) > 0 ||
                       _infoRespPids.count(pkt.payload[0]) > 0;
            }
            return _pollPids.count(pkt.payload[0]) > 0 || _diagPids.count(pkt.payload[0]) > 0 ||
                   _infoPoller->owns(pkt.payload[0]);
        default:
            return false;
    }
}

void CommHandler::noteTx(const Packet& pkt, bool retransmit) {
    const bool routine = isRoutine(pkt, true);
    for (Node n : mc::status::nodesForPeer(pkt.dest)) {
        _status.count(n, "tx");
        if (retransmit) _status.count(n, "retries");
        if (pkt.msgType == MsgType::NAK) _status.count(n, "nak_sent");
    }
    if (!routine && pkt.dest == ADDR_REMOTE && !pkt.payload.empty() &&
        (pkt.msgType == MsgType::ACK || pkt.msgType == MsgType::NAK)) {
        _status.set(Node::Controller, "last_cmd_reply",
                    pkt.msgType == MsgType::ACK || pkt.payload.size() < 2
                        ? std::string(msgTypeName(pkt.msgType))
                        : "NAK " + std::string(nakReasonName(pkt.payload[1])));
    }
    if (routine) return;
    std::string text = "TX " + describePacket(pkt);
    if (retransmit) text += " (retry)";
    for (Node n : mc::status::nodesForPeer(pkt.dest)) mc::log::debug(n, text);
}

void CommHandler::noteRx(const Packet& pkt) {
    for (Node n : mc::status::nodesForPeer(pkt.source)) {
        _status.count(n, "rx");
        if (pkt.msgType == MsgType::NAK) _status.count(n, "nak_received");
    }
    if (pkt.source == ADDR_REMOTE) {
        _status.setNow(Node::Controller, "last_rx_ms");
        // Commands only: replies to our polls (DIAG_RESP, INFO_RESP) are not something the user did on the remote
        if (pkt.msgType != MsgType::ACK && pkt.msgType != MsgType::NAK && pkt.msgType != MsgType::DIAG_RESP &&
            pkt.msgType != MsgType::INFO_RESP && pkt.dest == ADDR_PC) {
            const std::string name = msgTypeName(pkt.msgType);
            _status.set(Node::Controller, "last_cmd", name);
            _status.setNow(Node::Controller, "last_cmd_ms");
            _status.set(Node::Controller, "last_cmd_reply", std::string("none"));
            _status.count(Node::Controller, "cmd." + name);
        }
    }
    if (pkt.msgType == MsgType::STATUS_RESP && pkt.source == ADDR_BASE && pkt.payload.size() >= 3) {
        BaseStatus st;
        st.ble = pkt.payload[0] != 0;
        st.relay = pkt.payload[1] != 0;
        st.display = pkt.payload[2] != 0;
        st.pc = pkt.payload.size() >= 4 && pkt.payload[3] != 0;
        _status.set(Node::Base, "status_ble", st.ble);
        if (_diagPoller) _diagPoller->setRemoteLink(st.ble, steadyMs(), _updating.load());  // remote reachable -> DIAG_GET
        _status.set(Node::Base, "status_relay", st.relay);
        const bool wasLink = _remoteLink.exchange(st.ble);
        if (st.ble) {
            // P1-9: a ble 0->1 edge makes the remote's state suspect; while dirty every ble=1 status tries once.
            bool dirty;
            {
                std::lock_guard<std::mutex> lk(_resyncMutex);
                if (!wasLink) {
                    _remoteDirty = true;
                    _lastRemoteBootId.reset();  // a new stretch: the first id is only stored
                }
                // A2 (protocol v2): a 7 B STATUS_RESP (v1 base: 4 B, so this never fires) carries the remote's boot id.
                // A changed id is a reboot shorter than the status poll (no ble 0->1 edge): resync like an edge.
                if (pkt.payload.size() >= 7 && (pkt.payload[4] & 0x01) != 0) {
                    const uint16_t id = static_cast<uint16_t>(pkt.payload[5] | (pkt.payload[6] << 8));
                    if (_lastRemoteBootId && *_lastRemoteBootId != id) {
                        mc::log::info(Node::Controller, "Remote rebooted (boot id " + std::to_string(*_lastRemoteBootId) + " -> " +
                                                            std::to_string(id) + ")");
                        _remoteDirty = true;
                    }
                    _lastRemoteBootId = id;
                }
                dirty = _remoteDirty;
            }
            if (dirty && !_resyncQueued.exchange(true)) post({Command::Kind::RemoteResync, {}, {}, 0});
        } else {
            std::lock_guard<std::mutex> lk(_resyncMutex);
            _lastRemoteBootId.reset();
        }
        // Item 4b: judged by the generation of the GET_STATUS it answers (FIFO; the base answers in order).
        // A repeat of the last STATUS_RESP (same PacketId) or an answer with no request left is not authoritative.
        uint64_t statusGen = kNoStatusGen;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (pkt.packetId != _lastStatusRespPid) {
                _lastStatusRespPid = pkt.packetId;
                if (!_statusGens.empty()) {
                    statusGen = _statusGens.front().second;
                    _statusGens.pop_front();
                }
            }
        }
        reconcileOrgan(st.relay, statusGen);
        _status.set(Node::Base, "status_display", st.display);
        _status.set(Node::Base, "status_pc", st.pc);
        _status.setNow(Node::Base, "status_ms");
        _status.set(Node::Base, "status_answered", true);
        {
            std::lock_guard<std::mutex> lk(_routineMutex);
            _pollOutstanding = false;
        }
        // Routine check first: it decides by whether someone is waiting.
        const bool routine = isRoutine(pkt, false);
        {
            std::lock_guard<std::mutex> lk(_statusMutex);
            _lastStatus = st;
            ++_statusSeq;
        }
        _statusCv.notify_all();
        if (!routine) {
            for (Node n : mc::status::nodesForPeer(pkt.source)) mc::log::debug(n, "RX " + describePacket(pkt));
        }
        return;
    }
    if (isRoutine(pkt, false)) return;
    for (Node n : mc::status::nodesForPeer(pkt.source)) mc::log::debug(n, "RX " + describePacket(pkt));
}

void CommHandler::pollStatus() {
    const int64_t now = steadyMs();
    bool unanswered = false;
    {
        std::lock_guard<std::mutex> lk(_routineMutex);
        if (_pollOutstanding && now - _pollSentMs > kStatusAnswerMs) {
            _pollOutstanding = false;
            unanswered = true;
        }
        if (now - _pollSentMs < kStatusPollMs) {
            if (!unanswered) return;
        } else {
            // Marked before sending, so a fast STATUS_RESP can't be overtaken.
            _pollOutstanding = true;
            _pollSentMs = now;
            unanswered = false;
            if (_pollPids.size() > 16) _pollPids.clear();  // ids wrap; only the last few matter
        }
    }
    if (unanswered) {
        _status.set(Node::Base, "status_answered", false);
        return;
    }
    // Register the id before sending, so the poll and its ACK are recognised
    // as routine and a fast NAK finds _pollPid already set.
    const uint8_t pid = nextPacketId();
    {
        std::lock_guard<std::mutex> lk(_routineMutex);
        _pollPids.insert(pid);
        _pollPid = pid;
    }
    sendPacket(Packet(ADDR_PC, ADDR_BASE, pid, MsgType::GET_STATUS));
}

void CommHandler::storeRemoteDiag(mc::diag::DiagEntry e) {
    if (!_diag) return;
    if (e.level == 'E' || e.level == 'W') {
        _status.event(Node::Controller, e.level == 'E' ? mc::status::Level::Error : mc::status::Level::Warn, e.tag + ": " + e.text);
    }
    const bool fault = e.fault.has_value();
    _diag->append(std::move(e));
    if (fault) mc::diag::publishFaultStatus(*_diag, _status);
}

void CommHandler::onInfoResp(const Packet& pkt) {
    const bool fromBase = pkt.source == ADDR_BASE;
    if (!fromBase && pkt.source != ADDR_REMOTE) return;
    const Node node = fromBase ? Node::Base : Node::Controller;
    auto fields = mc::devinfo::decode(pkt.payload.data(), pkt.payload.size());
    if (fields.empty()) {
        mc::log::warn(node, "Malformed INFO_RESP (" + std::to_string(pkt.payload.size()) + " bytes) ignored");
        return;
    }
    for (auto& [key, value] : fields) _status.set(node, key, std::move(value));
    _status.set(node, "info_unsupported", false);
    _status.setNow(node, "info_ms");
    _infoPoller->onResp(fromBase ? mc::devinfo::InfoPoller::Base : mc::devinfo::InfoPoller::Remote, steadyMs());
}

void CommHandler::clearInfo(Node node) {
    for (const auto& key : mc::devinfo::fieldKeys()) _status.set(node, key, std::monostate{});
    _status.set(node, "info_ms", std::monostate{});
}

void CommHandler::clearBaseReport() {
    _remoteLink.store(false);
    {
        std::lock_guard<std::mutex> lk(_resyncMutex);
        _lastRemoteBootId.reset();
    }
    _ssrReported.store(-1);
    for (const char* key : {"status_ble", "status_relay", "status_display", "status_pc", "status_ms"}) {
        _status.set(Node::Base, key, std::monostate{});
    }
    _status.set(Node::Base, "organ_power", std::monostate{});
}

void CommHandler::refreshMidiPort() {
    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    _status.set(Node::Base, "midi_port_found", ports.has_value());
    _status.set(Node::Base, "midi_out", ports ? ports->outName : std::string());
}

std::optional<CommHandler::BaseStatus> CommHandler::requestStatus(int timeoutMs) {
    std::unique_lock<std::mutex> lk(_statusMutex);
    ++_statusWaiters;
    const uint64_t seq = _statusSeq;
    lk.unlock();
    sendWithAck(ADDR_BASE, MsgType::GET_STATUS);
    lk.lock();
    bool got = _statusCv.wait_for(lk, std::chrono::milliseconds(timeoutMs),
                                  [&] { return _statusSeq != seq; });
    --_statusWaiters;
    if (!got) return std::nullopt;
    return _lastStatus;
}

// ---------------------------------------------------------------------------
// I/O threads
// ---------------------------------------------------------------------------

void CommHandler::readerLoop() {
    auto lastReopen = Clock::time_point{};
    auto lastHealth = Clock::now();
    while (_running.load()) {
        _hbReader.store(steadyMs());
        bool open;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            open = _serial.isOpen();
        }
        if (!open) {
            auto now = Clock::now();
            if (now - lastReopen >= std::chrono::seconds(1)) {
                lastReopen = now;
                if (tryReopen()) {
                    noteBootloader(false);
                    continue;
                }
                if (_bootloaderProbe && !_updating.load()) noteBootloader(_bootloaderProbe());
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Reads are only issued from this thread; close happens here too
        // (or after this thread is joined), so no lock is needed.
        auto data = _serial.read(256);
        for (uint8_t byte : data) {
            auto pkt = _parser.feed(byte);
            if (pkt.has_value()) dispatchPacket(*pkt);
        }
        if (_parser.badFrames() != _badFrames) {
            _status.count(Node::Base, "crc_errors", _parser.badFrames() - _badFrames);
            _badFrames = _parser.badFrames();
        }

        auto now = Clock::now();
        // A read that returned nothing on a failed port must not spin until the 500 ms tick.
        if (now - lastHealth >= std::chrono::milliseconds(500) || (data.empty() && !_serial.healthy())) {
            lastHealth = now;
            if (!_serial.healthy()) {
                handleSerialLoss();
                continue;
            }
        }
        checkTimeouts();
    }
}

void CommHandler::dispatchPacket(const Packet& pkt) {
    noteRx(pkt);  // before the dispatcher, so its ACK is recognised as routine
    _dispatcher->handle(pkt);
    if (pkt.msgType == MsgType::FW_UPDATE_STATUS && pkt.dest == ADDR_PC && !pkt.payload.empty()) {
        std::lock_guard<std::mutex> lk(_ackMutex);
        _fwStatus = pkt.payload[0];
        _ackCv.notify_all();
    }
    if (_callback) _callback(pkt);
}

void CommHandler::keepaliveLoop() {
    int64_t lastMidiCheck = 0;
    while (_running.load()) {
        _hbKeepalive.store(steadyMs());
        if (_connected.load() && !_updating.load()) {
            sendKeepalive();
            auto silent = Clock::now() - _lastBaseKeepalive.load();
            if (silent > std::chrono::milliseconds(KEEPALIVE_LOSS_MS) &&
                !_baseLostReported.exchange(true)) {
                _status.set(Node::Base, "alive", false);
                mc::log::warn(Node::Base, "Base is not answering keepalives (no reply for " +
                                              std::to_string(KEEPALIVE_LOSS_MS / 1000) + " s).");
            }
            pollStatus();
            const int64_t idleArmed = _idleOffArmedMs.load();
            if (idleArmed >= 0 && steadyMs() - idleArmed >= _config.organIdleOffMs &&
                !_idleOffQueued.exchange(true)) {
                post({Command::Kind::IdleOff, {}, {}, 0});
            }
            if (_diagPoller) _diagPoller->tick(steadyMs(), false);  // while updating this block is skipped: no DIAG_GET
            _infoPoller->tick(steadyMs(), true, _remoteLink.load() && !_remotePaused.load());
        } else {
            _infoPoller->tick(steadyMs(), false, false);  // base gone or updating: its details are void
        }
        if (steadyMs() - lastMidiCheck >= kMidiCheckMs) {
            lastMidiCheck = steadyMs();
            refreshMidiPort();
        }
        for (int i = 0; i < KEEPALIVE_PERIOD_MS / 100 && _running.load(); ++i) {
            _hbKeepalive.store(steadyMs());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ---------------------------------------------------------------------------
// CommandSink (reader thread) - validate, answer, queue
// ---------------------------------------------------------------------------

std::optional<NakReason> CommHandler::playSong(const std::string& code, uint8_t requester) {
    mc::log::info(Node::Software, "PLAY_SONG \"" + code + "\" from " + nodeName(requester));
    return playSong(code);
}

void CommHandler::stopSong(uint8_t requester) {
    mc::log::info(Node::Software, "STOP_SONG from " + nodeName(requester));
    stopSong();
}

std::optional<NakReason> CommHandler::addVerse() {
    if (requestAddVerse()) {
        mc::log::info(Node::Software, "Add verse: " + sectionLabel());
        return std::nullopt;
    }
    return NakReason::NOT_PLAYING;
}

std::vector<uint8_t> CommHandler::songListPage(size_t offset) {
    size_t count = 0;
    auto page = mc::songs::buildSongListPage(mc::songs::listSongs(_config.playFolder), offset,
                                             count, MAX_PAYLOAD_SIZE);
    mc::log::info(Node::Software, "Song list request (offset=" + std::to_string(offset) +
                                      "), sending " + std::to_string(count) +
                                      " songs, has_more=" + std::to_string(page[0]));
    return page;
}

// ---------------------------------------------------------------------------
// High-level commands
// ---------------------------------------------------------------------------

std::optional<NakReason> CommHandler::playSong(const std::string& rawCode) {
    auto code = mc::songs::sanitizeCode(rawCode);
    if (!code) {
        mc::log::warn(Node::Software, "Invalid song code: \"" + rawCode + "\"");
        return NakReason::INVALID_PAYLOAD;
    }
    auto path = mc::songs::findSongFile(_config.playFolder, *code);
    if (!path) {
        mc::log::warn(Node::Software, "Song not found: " + *code + " (in " + _config.playFolder + ")");
        return NakReason::NOT_FOUND;
    }
    post({Command::Kind::Play, *code, *path, 0});
    return std::nullopt;
}

void CommHandler::stopSong() { post({Command::Kind::Stop, {}, {}, 0}); }

bool CommHandler::requestAddVerse() {
    std::lock_guard<std::mutex> lk(_playerMutex);
    return _player && _player->addVerse();
}

bool CommHandler::isPlaying() const {
    std::lock_guard<std::mutex> lk(_playerMutex);
    return _player && _player->playing();
}

std::string CommHandler::sectionLabel() const {
    std::lock_guard<std::mutex> lk(_playerMutex);
    return _player ? _player->label() : std::string{};
}

std::vector<std::string> CommHandler::songList() const {
    return mc::songs::listSongs(_config.playFolder);
}

void CommHandler::organOn() {
    _organClaimed.store(true);
    std::lock_guard<std::mutex> lk(_organMutex);
    setOrganLocked(true);
}

void CommHandler::organOff() {
    std::lock_guard<std::mutex> lk(_organMutex);
    setOrganLocked(false);
}

void CommHandler::setOrganLocked(bool on) {
    disarmIdleOff();  // the PC switches the organ now: its own rules apply
    _organOn.store(on);
    _status.set(Node::Base, "organ_power", on);
    _organIntent = on ? 1 : 0;
    _organIntentMs = steadyMs();
    _organReasserts = 0;
    sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {static_cast<uint8_t>(on ? 0x01 : 0x00)});
}

// PCC-2. An authoritative STATUS_RESP (its GET_STATUS went out after the last SET_CURRENT) tells the SSR state.
// While the PC's last command is unconfirmed and young (kOrganIntentWindowMs), a different state means the
// command was lost (all retries gone, or a stale frame won): re-send it with a new PacketId, at most
// kOrganReassertMax times, then report loudly. A confirmed command, or one older than the window, is history:
// whatever the base reports is adopted (someone switched the organ at the base or the remote; never fought).
// Does not decide anything about KeepPowerOn: it only repeats what the PC itself last commanded.
void CommHandler::reconcileOrgan(bool relay, uint64_t statusGen) {
    std::lock_guard<std::mutex> lk(_organMutex);
    if (statusGen != _setCurrentGen.load()) return;  // the answer may predate the last SET_CURRENT
    _ssrReported.store(relay ? 1 : 0);
    if (!relay) disarmIdleOff();  // switched off (base menu, remote, loss guard): nothing left to time out
    // The base's SSR, whoever switched it (PC, base menu, remote): shown on the page and used to decide
    // whether a song needs the warm-up.
    _organOn.store(relay);
    _status.set(Node::Base, "organ_power", relay);
    if (_organFault >= 0 && (relay ? 1 : 0) != _organFault) {
        _organFault = -1;
        _status.set(Node::Base, "organ_power_fault", std::monostate{});
    }
    if (_organIntent < 0) return;
    const bool want = _organIntent == 1;
    const std::string wantText = want ? "on" : "off";
    if (relay == want) {
        if (_organReasserts > 0) {
            mc::log::info(Node::Base, "Organ power: the base now reports " + wantText + " after " +
                                          std::to_string(_organReasserts) + " re-send(s).");
        }
        _organIntent = -1;
        return;
    }
    const int64_t age = steadyMs() - _organIntentMs;
    if (_organReasserts == 0 && age > kOrganIntentWindowMs) {
        _organIntent = -1;  // too old to be the PC's doing: adopt
        return;
    }
    if (_organReasserts >= kOrganReassertMax || age > kOrganIntentWindowMs) {
        _organIntent = -1;
        _organFault = relay ? 1 : 0;
        const std::string text =
            "Organ power: the PC switched the organ " + wantText + " but the base still reports " +
            (relay ? "ON" : "off") + " after " + std::to_string(_organReasserts) + " re-send(s); giving up" +
            (relay ? ". THE ORGAN IS STILL POWERED." : ".");
        _status.set(Node::Base, "organ_power_fault", text);
        if (relay) {
            mc::log::error(Node::Base, text);
        } else {
            mc::log::warn(Node::Base, text);
        }
        return;
    }
    ++_organReasserts;
    mc::log::warn(Node::Base, "Organ power: the base reports " + std::string(relay ? "on" : "off") +
                                  ", the PC switched it " + wantText + "; re-sending (" +
                                  std::to_string(_organReasserts) + "/" + std::to_string(kOrganReassertMax) + ").");
    sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {static_cast<uint8_t>(want ? 0x01 : 0x00)});
}

void CommHandler::displayClear() {
    // P1-10: clear only a code this PC put on the indicator; an idle Stop must not blank the remote's entry.
    // A clear that was not written (serial closed) leaves the flag set, so a later Stop still clears.
    if (!_displaySet.exchange(false)) return;
    bool sent = false;
    sendPacket(Packet(ADDR_PC, ADDR_BASE, nextPacketId(), MsgType::NOW_PLAYING_INFO,
                      mc::display::encodeNowPlaying(mc::display::clearUpdate())), &sent);
    if (!sent) _displaySet.store(true);
}

bool CommHandler::recordSong() {
    // PCC-1: one recorder at a time. A second Record (web/console) while one runs or warms up is
    // refused; overwriting _recorderPid would orphan the first arecordmidi (it keeps the ALSA port).
    {
        std::lock_guard<std::mutex> lk(_recorderMutex);
        if (recorderAliveLocked() || _recorderStarting) {
            mc::log::warn(Node::Software, "Already recording; Stop first.");
            return false;
        }
        _recorderStarting = true;
        _recorderCancelled = false;
    }
    struct StartingGuard {
        CommHandler& h;
        ~StartingGuard() {
            std::lock_guard<std::mutex> lk(h._recorderMutex);
            h._recorderStarting = false;
        }
    } startingGuard{*this};

    _organClaimed.store(true);  // the operator uses the organ: no startup power off
    if (_idleOffArmedMs.load() >= 0) armIdleOff();  // SYS-REC-05: recording restarts the idle timer
    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    if (!ports) {
        mc::log::error(Node::Software, "MIDIControl MIDI input port not found; cannot record.");
        return false;
    }
    // arecordmidi expects an ALSA client:port; use the trailing token of the name.
    std::string alsaPort;
    {
        std::istringstream iss(ports->inName);
        std::string tok;
        while (iss >> tok) alsaPort = tok;
    }

    if (!_organOn.load()) {
        organOn();
        std::this_thread::sleep_for(std::chrono::milliseconds(_config.warmupMs));
    }

    // Unique per start, also within one second (the name has 1 s resolution).
    const std::string fullPath = mc::recording::reservePath(_config.recFolder, std::time(nullptr));
    if (fullPath.empty()) {
        mc::log::error(Node::Software, "Cannot create a recording file in " + _config.recFolder);
        return false;
    }

    // Spawn and store the pid under one lock: a Stop/SerialLost during the warm-up cancels the start,
    // and stopRecorder can never miss a recorder that is being started.
    std::lock_guard<std::mutex> lk(_recorderMutex);
    if (_recorderCancelled) {
        std::remove(fullPath.c_str());
        mc::log::info(Node::Software, "Recording cancelled during the organ warm-up.");
        return false;
    }
    const int pid = mc::proc::spawn({"arecordmidi", "-p", alsaPort, fullPath});
    if (pid > 0) {
        _recorderPid = pid;
        mc::log::info(Node::Software, "Recording to " + fullPath);
        return true;
    }
    std::remove(fullPath.c_str());
    mc::log::error(Node::Software, "Failed to start arecordmidi (rc=" + std::to_string(-pid) + ")");
    return false;
}

bool CommHandler::recorderAliveLocked() {
    if (_recorderPid > 0 && !mc::proc::running(_recorderPid)) {
        mc::log::warn(Node::Software, "arecordmidi exited on its own; recording ended.");
        _recorderPid = -1;
    }
    return _recorderPid > 0;
}

void CommHandler::stopRecorder() {
    std::lock_guard<std::mutex> lk(_recorderMutex);
    if (_recorderStarting) _recorderCancelled = true;  // a Record still in its warm-up must not start
    if (_recorderPid > 0) {
        // Bounded: a recorder that ignores SIGTERM can't hang the control thread.
        if (mc::proc::terminate(_recorderPid) == mc::proc::Ended::Killed) {
            mc::log::warn(Node::Software, "arecordmidi did not stop on SIGTERM; killed");
        }
        _recorderPid = -1;
    }
}

// ---------------------------------------------------------------------------
// Control thread
// ---------------------------------------------------------------------------

void CommHandler::post(Command cmd) {
    {
        std::lock_guard<std::mutex> lk(_queueMutex);
        if (cmd.kind == Command::Kind::Play) _playClaimed = true;
        _queue.push_back(std::move(cmd));
    }
    _queueCv.notify_all();
}

CommHandler::UpdateClaim CommHandler::tryBeginUpdate() {
    std::lock_guard<std::mutex> lk(_queueMutex);
    if (_updateClaimed) return UpdateClaim::Busy;
    if (_playClaimed) return UpdateClaim::Playing;
    _updateClaimed = true;
    return UpdateClaim::Granted;
}

void CommHandler::endUpdate() {
    std::lock_guard<std::mutex> lk(_queueMutex);
    _updateClaimed = false;
}

bool CommHandler::updateClaimed() const {
    std::lock_guard<std::mutex> lk(_queueMutex);
    return _updateClaimed;
}

void CommHandler::releasePlayClaimIfIdle() {
    bool havePlayer;
    {
        std::lock_guard<std::mutex> lk(_playerMutex);  // only this thread changes _player
        havePlayer = _player != nullptr;
    }
    if (havePlayer) return;
    std::lock_guard<std::mutex> lk(_queueMutex);
    for (const auto& c : _queue)
        if (c.kind == Command::Kind::Play) return;
    _playClaimed = false;
}

bool CommHandler::waitInterruptible(int ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(ms);
    std::unique_lock<std::mutex> lk(_queueMutex);
    // In slices of at most 1 s, so the heartbeat keeps ticking during long warm-ups.
    while (true) {
        _hbControl.store(steadyMs());
        const auto slice = std::min(deadline, Clock::now() + std::chrono::seconds(1));
        bool interrupted = _queueCv.wait_until(lk, slice, [&] {
            for (const auto& c : _queue) {
                // Stale Finished, the startup check and the idle check don't interrupt a warm-up.
                if (c.kind != Command::Kind::Finished && c.kind != Command::Kind::StartupReset &&
                    c.kind != Command::Kind::IdleOff && c.kind != Command::Kind::RemoteResync) return true;
            }
            return false;
        });
        if (interrupted) return false;
        if (Clock::now() >= deadline) return true;
    }
}

void CommHandler::controlLoop() {
    while (true) {
        Command cmd;
        {
            std::unique_lock<std::mutex> lk(_queueMutex);
            // Tick while idle so the watchdog can tell idle from hung.
            while (!_queueCv.wait_for(lk, std::chrono::seconds(1), [&] { return !_queue.empty(); })) {
                _hbControl.store(steadyMs());
            }
            _hbControl.store(steadyMs());
            cmd = std::move(_queue.front());
            _queue.pop_front();
        }
        switch (cmd.kind) {
            case Command::Kind::Play:
                doPlay(cmd);
                break;
            case Command::Kind::Stop:
                doStop(true);
                break;
            case Command::Kind::Finished:
                if (cmd.generation == _generation) finishPlayback();
                break;
            case Command::Kind::SerialLost:
                // The base (and its MIDI port) went away: nothing can be
                // signalled, just stop playing. Power state is unknown now.
                if (releasePlayer()) {
                    mc::log::warn(Node::Software, "Playback stopped (base disconnected).");
                    playbackEnded("stopped (base disconnected)", "songs_stopped");
                }
                stopRecorder();
                _organOn.store(false);
                _status.set(Node::Base, "organ_power", std::monostate{});  // unknown until the base reports again
                // PCC-3: the organ state is unknown now; run the startup reset again when the base is back
                // (notes off, then the SSR rule: reported on -> idle timer, off/unknown -> power off).
                disarmIdleOff();
                {
                    // PCC-2 never re-sends an unconfirmed "on" across a loss (nothing plays now; the base's
                    // loss guard may have cut the SSR on purpose). An unconfirmed "off" is still defended.
                    std::lock_guard<std::mutex> lk(_organMutex);
                    if (_organIntent == 1) _organIntent = -1;
                }
                _startupReset = mc::startup::StartupReset();
                _organClaimed.store(false);
                _baseUpMs.store(-1);
                _startupResetDone.store(false);
                break;
            case Command::Kind::StartupReset:
                _startupResetQueued.store(false);
                doStartupReset();
                break;
            case Command::Kind::IdleOff:
                _idleOffQueued.store(false);
                doIdleOff();
                break;
            case Command::Kind::RemoteResync:
                doRemoteResync();
                break;
            case Command::Kind::Shutdown:
                if (releasePlayer()) {
                    displayClear();
                    sendRemoteSignal(MsgType::SIGNAL_STOPPED);
                }
                stopRecorder();
                if (_organOn.load()) organOff();
                return;
        }
        releasePlayClaimIfIdle();
    }
}

void CommHandler::doStartupReset() {
    if (_startupReset.finished()) return;
    // Someone switched the organ on (a song, a recording) before the reset
    // ran: the operator owns it now.
    if (_organClaimed.load()) {
        _startupReset.cancel();
        _startupResetDone.store(true);
        return;
    }
    const int64_t upMs = _baseUpMs.load();
    const int64_t since = upMs < 0 ? -1 : steadyMs() - upMs;
    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    const int rep = _ssrReported.load();
    const auto ssr = rep < 0 ? mc::startup::Ssr::Unknown : (rep ? mc::startup::Ssr::On : mc::startup::Ssr::Off);
    auto due = _startupReset.due(since, ports.has_value(), ssr);
    mc::startup::Actions performed;
    if (due.allNotesOff) {
        std::string err;
        if (sendMidiMessages(ports->outIndex, mc::startup::allNotesOffMessages(), &err)) {
            mc::log::info(Node::Software, "Startup: All Notes Off sent on all 16 channels.");
        } else {
            mc::log::warn(Node::Software, "Startup: All Notes Off not sent: " + err);
        }
        performed.allNotesOff = true;
    } else if (due.notesSkipped) {
        mc::log::warn(Node::Software, "Startup: MIDI output port not found; no All Notes Off.");
        performed.notesSkipped = true;
    }
    if (due.organOff) {
        organOff();  // indicator bus untouched (no display clear)
        mc::log::info(Node::Software, std::string("Startup: organ power off") +
                                          (ssr == mc::startup::Ssr::Unknown ? " (SSR state not reported)." : "."));
        performed.organOff = true;
    } else if (due.armIdleOff) {
        // Q1-1 b (owner 2026-11-14): a manual organ-on survives a PC start.
        armIdleOff();
        mc::log::info(Node::Software, "Startup: organ found switched on; left on, idle power-off in " +
                                          idleText(_config.organIdleOffMs) + ".");
        performed.armIdleOff = true;
    }
    _startupReset.done(performed);
    if (_startupReset.finished()) _startupResetDone.store(true);
}

void CommHandler::armIdleOff() {
    _idleOffArmedMs.store(steadyMs());
    _status.set(Node::Base, "organ_idle_off_armed", true);
}

void CommHandler::disarmIdleOff() {
    if (_idleOffArmedMs.exchange(-1) >= 0) _status.set(Node::Base, "organ_idle_off_armed", false);
}

void CommHandler::doIdleOff() {
    const int64_t armed = _idleOffArmedMs.load();
    if (armed < 0 || steadyMs() - armed < _config.organIdleOffMs) return;  // disarmed or restarted meanwhile
    bool busy = false;
    {
        std::lock_guard<std::mutex> lk(_playerMutex);
        busy = _player != nullptr;
    }
    {
        std::lock_guard<std::mutex> lk(_recorderMutex);
        busy = busy || recorderAliveLocked();  // a dead recorder must not keep the organ on
    }
    if (busy) {
        armIdleOff();  // in use: start counting again
        return;
    }
    mc::log::info(Node::Software, "Organ idle for " + idleText(_config.organIdleOffMs) + ": power off.");
    organOff();  // disarms the timer
}

bool CommHandler::releasePlayer() {
    std::unique_ptr<Player> p;
    {
        std::lock_guard<std::mutex> lk(_playerMutex);
        p = std::move(_player);
    }
    if (!p) return false;
    p->stop();  // joins the playback thread; we are never on it
    return true;
}

void CommHandler::playbackEnded(const std::string& how, const char* counter) {
    std::string code;
    std::vector<std::string> sections;
    {
        std::lock_guard<std::mutex> lk(_songMutex);
        code = _songCode;
        sections = _songSections;
    }
    _status.set(Node::Software, "playback", std::string("idle"));
    _status.set(Node::Software, "song", std::string());
    _status.set(Node::Software, "section", std::string());
    _status.set(Node::Software, "stops", std::string());
    _status.set(Node::Software, "last_song", code);
    _status.set(Node::Software, "last_sections", joined(sections));
    _status.set(Node::Software, "last_end", how);
    if (counter) _status.count(Node::Software, counter);
}

void CommHandler::doPlay(const Command& cmd) {
    _organClaimed.store(true);  // a song owns the organ now: no startup power off
    disarmIdleOff();            // after the song KeepPowerOn decides (2026-10-26, unchanged)
    if (releasePlayer()) playbackEnded("stopped", "songs_stopped");  // a new song replaces it
    {
        std::lock_guard<std::mutex> lk(_songMutex);
        _songCode = cmd.code;
        _songSections.clear();
    }
    _status.set(Node::Software, "song", cmd.code);
    _status.set(Node::Software, "section", std::string());

    if (!_organOn.load()) {
        organOn();
        _status.set(Node::Software, "playback", std::string("warming up"));
        mc::log::info(Node::Software, "Organ power on, warming up " +
                                          std::to_string(_config.warmupMs) + " ms...");
        if (!waitInterruptible(_config.warmupMs)) {
            mc::log::info(Node::Software, "Play " + cmd.code + " superseded during warm-up.");
            _status.set(Node::Software, "playback", std::string("idle"));
            return;
        }
    }

    auto fail = [&](const std::string& why) {
        mc::log::error(Node::Software, "Cannot play " + cmd.code + ": " + why);
        playbackEnded("failed: " + why, nullptr);
        displayClear();
        sendRemoteSignal(MsgType::SIGNAL_STOPPED);
        if (!_config.keepPowerOn) organOff();
    };

    // The song's stops (<CODE>.conf is read here, never through the web server's store).
    // Any trouble with the file leaves the song "not set": it must still play.
    PlaybackOptions opts;
    opts.gapSeconds = _config.verseGapMs / 1000.0;
    mc::stops::Stops songStops;
    try {
        std::vector<std::string> warnings;
        songStops = mc::stops::load(_config.playFolder, cmd.code, _config.organ.stopCount(), &warnings);
        for (const auto& w : warnings) mc::log::warn(Node::Software, w);
    } catch (const std::exception& e) {
        mc::log::warn(Node::Software, std::string("Stops not read, treating ") + cmd.code + " as not set: " + e.what());
    }
    mc::stops::SongPlan plan = mc::stops::planFor(_config.organ, songStops);
    opts.preamble = std::move(plan.preamble);
    opts.dropProgramChanges = plan.dropProgramChanges;
    mc::log::info(Node::Software, "Stops for " + cmd.code + ": " + plan.text);
    _status.set(Node::Software, "stops", plan.text);

    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    if (!ports) {
        fail("MIDIControl MIDI output port not found");
        return;
    }

    auto player = std::make_unique<Player>(ports->outIndex, _midiSinkForTest);
    const uint64_t gen = ++_generation;
    player->onDisplay = [this](const mc::display::Update& u) {
        _displaySet.store(!u.clear);
        send(ADDR_BASE, MsgType::NOW_PLAYING_INFO, mc::display::encodeNowPlaying(u));
    };
    player->onAnchor = [this](const std::string& label) {
        mc::log::info(Node::Software, "  -> " + label);
        _status.set(Node::Software, "section", label);
        {
            // "+k" labels are add-verse presses on the current section, not new ones.
            std::lock_guard<std::mutex> lk(_songMutex);
            if (label.find(" +") == std::string::npos) _songSections.push_back(label);
        }
        std::vector<uint8_t> payload(label.begin(), label.end());
        if (payload.size() > MAX_PAYLOAD_SIZE - 1) payload.resize(MAX_PAYLOAD_SIZE - 1);
        payload.push_back(0x00);
        sendRemoteSignal(MsgType::SIGNAL_ANCHOR, payload);  // remembered for a resync
    };
    player->onFinished = [this, gen]() { post({Command::Kind::Finished, {}, {}, gen}); };

    // SIGNAL_PLAYING goes out before the first SIGNAL_ANCHOR.
    sendRemoteSignal(MsgType::SIGNAL_PLAYING, std::vector<uint8_t>(cmd.code.begin(), cmd.code.end()));
    try {
        player->play(cmd.path, cmd.code, opts);
    } catch (const std::exception& e) {
        fail(e.what());
        return;
    }
    const size_t sections = player->sectionCount();
    {
        std::lock_guard<std::mutex> lk(_playerMutex);
        _player = std::move(player);
    }
    _status.set(Node::Software, "playback", std::string("playing"));
    mc::log::info(Node::Software, "Playing " + cmd.code + ": " + std::to_string(sections) +
                                      (sections == 1 ? " section" : " sections"));
}

void CommHandler::finishPlayback() {
    if (!releasePlayer()) return;  // already stopped by a Stop that raced the end
    displayClear();
    sendRemoteSignal(MsgType::SIGNAL_STOPPED);
    if (!_config.keepPowerOn) organOff();
    playbackEnded("ended normally", "songs_ended");
    mc::log::info(Node::Software, "Playback finished" +
                                      std::string(_config.keepPowerOn ? "." : ", organ power off."));
}

void CommHandler::doStop(bool signal) {
    bool wasPlaying = releasePlayer();
    stopRecorder();
    displayClear();
    if (signal) sendRemoteSignal(MsgType::SIGNAL_STOPPED);
    if (!_config.keepPowerOn) organOff();
    if (wasPlaying) {
        playbackEnded("stopped", "songs_stopped");
        mc::log::info(Node::Software, "Playback stopped.");
    }
}

}  // namespace mc
