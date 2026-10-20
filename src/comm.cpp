#include "midicontrol/comm.hpp"


#include <cstdio>
#include <ctime>
#include <sstream>

#include "midicontrol/diag_api.hpp"
#include "midicontrol/log.hpp"
#include "midicontrol/process.hpp"
#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/songs.hpp"

namespace mc {

using namespace mc::protocol;
using mc::status::Node;

namespace {

constexpr int kStatusPollMs = 5000;     // GET_STATUS to the base while connected
constexpr int kStatusAnswerMs = 2000;   // no STATUS_RESP by then = "not answered"
constexpr int kMidiCheckMs = 10000;     // MIDI port re-discovery for the status page

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
        if (pkt.source == ADDR_REMOTE) _diagPoller->onResp(pkt.payload.data(), pkt.payload.size(), steadyMs());
    };
    _dispatcher = std::make_unique<Dispatcher>(static_cast<CommandSink&>(*this), std::move(out));

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
    _diagPoller = std::make_unique<mc::diag::DiagPoller>(std::move(dh));

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
    if (!devicePath.empty()) {
        std::lock_guard<std::mutex> lk(_mutex);
        if (_serial.open(devicePath, 115200)) {
            _devicePath = devicePath;
            _connected.store(true);
        }
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
    _diagPoller->setRemoteLink(false, steadyMs());
    std::lock_guard<std::mutex> lk(_mutex);
    _serial.close();
    _pending.clear();
    _connected.store(false);
    _status.set(Node::Base, "connected", false);
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
    }
    _connected.store(false);
    _parser.reset();
    _dispatcher->reset();
    _diagPoller->setRemoteLink(false, steadyMs());  // flushes a half-received FAULT
    _status.set(Node::Base, "connected", false);
    mc::log::warn(Node::Base, "Serial link to the base lost; waiting for it to come back...");
    post({Command::Kind::SerialLost, {}, {}, 0});
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
    _lastBaseKeepalive.store(Clock::now());
    _baseLostReported.store(false);
    _connected.store(true);
    noteConnected(*path, _everConnected.load());
    return true;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

uint8_t CommHandler::nextPacketId() { return _packetId.fetch_add(1); }

uint8_t CommHandler::send(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    return sendPacket(Packet(ADDR_PC, dest, nextPacketId(), type, payload));
}

uint8_t CommHandler::sendPacket(const Packet& pkt) {
    auto raw = encode(pkt);
    bool sent = false;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (_serial.isOpen()) sent = _serial.write(raw);
    }
    if (sent) noteTx(pkt, false);
    return pkt.packetId;
}

uint8_t CommHandler::sendWithAck(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    // Register before writing so a fast ACK cannot arrive before the entry exists.
    uint8_t pid = nextPacketId();
    Packet pkt(ADDR_PC, dest, pid, type, payload);
    auto raw = encode(pkt);
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (!_serial.isOpen()) return pid;  // nothing to retry while disconnected
        _pending[pid] = Pending{Clock::now(), 0, type, dest, payload};
        _serial.write(raw);
    }
    if (dest == ADDR_REMOTE && type != MsgType::DIAG_GET) {
        _status.set(Node::Controller, "last_tx", std::string(msgTypeName(type)));
        _status.setNow(Node::Controller, "last_tx_ms");
        _status.set(Node::Controller, "last_tx_result", std::string("pending"));
    }
    noteTx(pkt, false);
    return pid;
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
        _awaitActive = false;
        lk.unlock();

        if (signalled && result == 1) return true;   // ACK
        if (signalled && result == 2) return false;  // NAK — caller aborts
        // timeout: retry
    }
    for (Node n : mc::status::nodesForPeer(dest)) _status.count(n, "gave_up");
    return false;
}

void CommHandler::handleAck(uint8_t pid, bool nak, uint8_t reason) {
    std::optional<Pending> pending;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        auto it = _pending.find(pid);
        if (it != _pending.end()) {
            pending = it->second;
            _pending.erase(it);
        }
    }
    if (nak) _diagPoller->onNak(pid, reason);  // stops the polling after NAK UNKNOWN_MSG
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
    } else if (nak) {
        std::string what = pending ? std::string(msgTypeName(pending->msgType)) + " id=" +
                                         std::to_string(pid) + " to " + nodeName(pending->dest)
                                   : "id=" + std::to_string(pid);
        mc::log::info(pending ? mc::status::nodesForPeer(pending->dest)[0] : Node::Base,
                      "NAK for " + what + ": " + nakReasonName(reason));
    }
    if (pending && pending->dest == ADDR_REMOTE && pending->msgType != MsgType::DIAG_GET) {
        _status.set(Node::Controller, "last_tx_result",
                    nak ? "NAK " + std::string(nakReasonName(reason)) : std::string("ACK"));
    }
    std::lock_guard<std::mutex> lk(_ackMutex);
    if (_awaitActive && pid == _awaitPid) {
        _awaitResult = nak ? 2 : 1;
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
        for (Node n : mc::status::nodesForPeer(p.dest)) {
            _status.count(n, "gave_up");
            mc::log::warn(n, std::string(msgTypeName(p.msgType)) + " id=" + std::to_string(pid) +
                                 " to " + nodeName(p.dest) + " not acknowledged after " +
                                 std::to_string(MAX_RETRIES) + " retries");
        }
        if (p.dest == ADDR_REMOTE) _status.set(Node::Controller, "last_tx_result", std::string("no answer"));
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
        case MsgType::DIAG_RESP:
            if (tx) return false;
            bounded(_diagRespPids, pkt.packetId);
            return true;
        case MsgType::ACK:
            if (pkt.payload.empty()) return false;
            if (tx) return _pollRespPids.count(pkt.payload[0]) > 0 || _diagRespPids.count(pkt.payload[0]) > 0;
            return _pollPids.count(pkt.payload[0]) > 0 || _diagPids.count(pkt.payload[0]) > 0;
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
        if (pkt.msgType != MsgType::ACK && pkt.msgType != MsgType::NAK && pkt.msgType != MsgType::DIAG_RESP &&
            pkt.dest == ADDR_PC) {
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
        _diagPoller->setRemoteLink(st.ble, steadyMs(), _updating.load());  // remote reachable -> DIAG_GET
        _status.set(Node::Base, "status_relay", st.relay);
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
                if (tryReopen()) continue;
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
        if (now - lastHealth >= std::chrono::milliseconds(500)) {
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
            send(ADDR_BASE, MsgType::KEEPALIVE);
            auto silent = Clock::now() - _lastBaseKeepalive.load();
            if (silent > std::chrono::milliseconds(KEEPALIVE_LOSS_MS) &&
                !_baseLostReported.exchange(true)) {
                _status.set(Node::Base, "alive", false);
                mc::log::warn(Node::Base, "Base is not answering keepalives (no reply for " +
                                              std::to_string(KEEPALIVE_LOSS_MS / 1000) + " s).");
            }
            pollStatus();
            _diagPoller->tick(steadyMs(), false);  // while updating this block is skipped: no DIAG_GET
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
    _organOn.store(true);
    _status.set(Node::Base, "organ_power", true);
    sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {0x01});
}

void CommHandler::organOff() {
    _organOn.store(false);
    _status.set(Node::Base, "organ_power", false);
    sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {0x00});
}

void CommHandler::displayClear() {
    send(ADDR_BASE, MsgType::NOW_PLAYING_INFO,
         mc::display::encodeNowPlaying(mc::display::clearUpdate()));
}

void CommHandler::recordSong() {
    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    if (!ports) {
        mc::log::error(Node::Software, "MIDIControl MIDI input port not found; cannot record.");
        return;
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

    char buf[64];
    std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_r(&t, &tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.mid", &tm);
    std::string fullPath = _config.recFolder + "/" + buf;

    const int pid = mc::proc::spawn({"arecordmidi", "-p", alsaPort, fullPath});
    if (pid > 0) {
        std::lock_guard<std::mutex> lk(_recorderMutex);
        _recorderPid = pid;
        mc::log::info(Node::Software, "Recording to " + fullPath);
    } else {
        mc::log::error(Node::Software, "Failed to start arecordmidi (rc=" + std::to_string(-pid) + ")");
    }
}

void CommHandler::stopRecorder() {
    std::lock_guard<std::mutex> lk(_recorderMutex);
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
        _queue.push_back(std::move(cmd));
    }
    _queueCv.notify_all();
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
                if (c.kind != Command::Kind::Finished) return true;  // stale Finished don't count
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
                _status.set(Node::Base, "organ_power", false);
                break;
            case Command::Kind::Shutdown:
                if (releasePlayer()) {
                    displayClear();
                    sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_STOPPED);
                }
                stopRecorder();
                if (_organOn.load()) organOff();
                return;
        }
    }
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
    _status.set(Node::Software, "last_song", code);
    _status.set(Node::Software, "last_sections", joined(sections));
    _status.set(Node::Software, "last_end", how);
    if (counter) _status.count(Node::Software, counter);
}

void CommHandler::doPlay(const Command& cmd) {
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
        sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_STOPPED);
        if (!_config.keepPowerOn) organOff();
    };

    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    if (!ports) {
        fail("MIDIControl MIDI output port not found");
        return;
    }

    auto player = std::make_unique<Player>(ports->outIndex);
    const uint64_t gen = ++_generation;
    player->onDisplay = [this](const mc::display::Update& u) {
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
        sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_ANCHOR, payload);
    };
    player->onFinished = [this, gen]() { post({Command::Kind::Finished, {}, {}, gen}); };

    PlaybackOptions opts;
    opts.gapSeconds = _config.verseGapMs / 1000.0;

    // SIGNAL_PLAYING goes out before the first SIGNAL_ANCHOR.
    sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_PLAYING,
                std::vector<uint8_t>(cmd.code.begin(), cmd.code.end()));
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
    sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_STOPPED);
    if (!_config.keepPowerOn) organOff();
    playbackEnded("ended normally", "songs_ended");
    mc::log::info(Node::Software, "Playback finished" +
                                      std::string(_config.keepPowerOn ? "." : ", organ power off."));
}

void CommHandler::doStop(bool signal) {
    bool wasPlaying = releasePlayer();
    stopRecorder();
    displayClear();
    if (signal) sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_STOPPED);
    if (!_config.keepPowerOn) organOff();
    if (wasPlaying) {
        playbackEnded("stopped", "songs_stopped");
        mc::log::info(Node::Software, "Playback stopped.");
    }
}

}  // namespace mc
