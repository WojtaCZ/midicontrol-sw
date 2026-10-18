#include "midicontrol/comm.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cstdio>
#include <ctime>
#include <iostream>
#include <sstream>

#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/songs.hpp"

extern char** environ;

namespace mc {

using namespace mc::protocol;

namespace {

std::string hexByte(uint8_t b) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "0x%02X", b);
    return buf;
}

}  // namespace

CommHandler::CommHandler(const Config& config, MidiPortFinder midiFinder)
    : _config(config), _midiFinder(std::move(midiFinder)) {
    Dispatcher::Output out;
    out.send = [this](uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
        send(dest, type, payload);
    };
    out.sendWithAck = [this](uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
        sendWithAck(dest, type, payload);
    };
    out.onAck = [this](uint8_t pid) { handleAck(pid, false); };
    out.onNak = [this](uint8_t pid, uint8_t reason) {
        std::cout << "  <- NAK for PacketID=" << hexByte(pid) << ", reason=" << hexByte(reason)
                  << "\n";
        handleAck(pid, true);
    };
    out.onKeepaliveAck = [this]() {
        _lastBaseKeepalive.store(Clock::now());
        if (_baseLostReported.exchange(false)) std::cout << "Base is responding again.\n";
    };
    _dispatcher = std::make_unique<Dispatcher>(static_cast<CommandSink&>(*this), std::move(out));

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
            std::cout << "Opened serial port: " << devicePath << "\n";
        } else {
            std::cerr << "Failed to open serial port: " << devicePath << "\n";
        }
    }
    if (!_connected.load()) std::cout << "Waiting for the MIDIControl base...\n";
    startIoThreads();
}

bool CommHandler::connect(const std::string& devicePath) {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (!_serial.open(devicePath, 115200)) {
            std::cerr << "Failed to open serial port: " << devicePath << "\n";
            return false;
        }
        _devicePath = devicePath;
    }
    std::cout << "Opened serial port: " << devicePath << "\n";
    _connected.store(true);
    startIoThreads();
    return true;
}

void CommHandler::startIoThreads() {
    if (_running.exchange(true)) return;
    _parser.reset();
    _dispatcher->reset();
    _lastBaseKeepalive.store(Clock::now());
    _baseLostReported.store(false);
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
    std::lock_guard<std::mutex> lk(_mutex);
    _serial.close();
    _pending.clear();
    _connected.store(false);
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

void CommHandler::handleSerialLoss() {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _serial.close();
        _pending.clear();
    }
    _connected.store(false);
    _parser.reset();
    _dispatcher->reset();
    std::cerr << "Serial link to the base lost; waiting for it to come back...\n";
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
    std::cout << "Connected to the base on " << *path << "\n";
    return true;
}

// ---------------------------------------------------------------------------
// Sending
// ---------------------------------------------------------------------------

uint8_t CommHandler::nextPacketId() { return _packetId.fetch_add(1); }

uint8_t CommHandler::send(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    Packet pkt(ADDR_PC, dest, nextPacketId(), type, payload);
    auto raw = encode(pkt);
    std::lock_guard<std::mutex> lk(_mutex);
    if (_serial.isOpen()) _serial.write(raw);
    return pkt.packetId;
}

uint8_t CommHandler::sendWithAck(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    // Register before writing so a fast ACK cannot arrive before the entry exists.
    uint8_t pid = nextPacketId();
    Packet pkt(ADDR_PC, dest, pid, type, payload);
    auto raw = encode(pkt);
    std::lock_guard<std::mutex> lk(_mutex);
    if (!_serial.isOpen()) return pid;  // nothing to retry while disconnected
    _pending[pid] = Pending{Clock::now(), 0, type, dest, payload};
    _serial.write(raw);
    return pid;
}

bool CommHandler::sendReliable(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload,
                               int timeoutMs, int maxRetries) {
    // Same PacketId on every retry, so the receiver can suppress duplicates.
    const uint8_t pid = nextPacketId();
    const auto raw = encode(Packet(ADDR_PC, dest, pid, type, payload));
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        {
            std::lock_guard<std::mutex> lk(_ackMutex);
            _awaitPid = pid;
            _awaitResult = 0;
            _awaitActive = true;
        }
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_serial.isOpen()) _serial.write(raw);
        }

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
    return false;
}

void CommHandler::handleAck(uint8_t pid, bool nak) {
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _pending.erase(pid);
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
    std::lock_guard<std::mutex> lk(_mutex);
    for (auto it = _pending.begin(); it != _pending.end();) {
        Pending& p = it->second;
        if (now - p.timestamp <= std::chrono::milliseconds(RETRY_TIMEOUT_MS)) {
            ++it;
            continue;
        }
        if (p.retries < MAX_RETRIES) {
            // Retransmit with the SAME PacketId (receiver de-duplicates).
            auto raw = encode(Packet(ADDR_PC, p.dest, it->first, p.msgType, p.payload));
            if (_serial.isOpen()) _serial.write(raw);
            p.timestamp = now;
            p.retries += 1;
            ++it;
        } else {
            std::cout << "Timeout for PacketID=" << hexByte(it->first) << " (type "
                      << hexByte(static_cast<uint8_t>(p.msgType)) << " to "
                      << hexByte(p.dest) << ") after " << static_cast<int>(MAX_RETRIES)
                      << " retries\n";
            it = _pending.erase(it);
        }
    }
}

// ---------------------------------------------------------------------------
// I/O threads
// ---------------------------------------------------------------------------

void CommHandler::readerLoop() {
    auto lastReopen = Clock::time_point{};
    auto lastHealth = Clock::now();
    while (_running.load()) {
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
    _dispatcher->handle(pkt);
    if (pkt.msgType == MsgType::FW_UPDATE_STATUS && pkt.dest == ADDR_PC && !pkt.payload.empty()) {
        std::lock_guard<std::mutex> lk(_ackMutex);
        _fwStatus = pkt.payload[0];
        _ackCv.notify_all();
    }
    if (_callback) _callback(pkt);
}

void CommHandler::keepaliveLoop() {
    while (_running.load()) {
        if (_connected.load()) {
            send(ADDR_BASE, MsgType::KEEPALIVE);
            auto silent = Clock::now() - _lastBaseKeepalive.load();
            if (silent > std::chrono::milliseconds(KEEPALIVE_LOSS_MS) &&
                !_baseLostReported.exchange(true)) {
                std::cerr << "Base is not answering keepalives (no reply for "
                          << KEEPALIVE_LOSS_MS / 1000 << " s).\n";
            }
        }
        for (int i = 0; i < KEEPALIVE_PERIOD_MS / 100 && _running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

// ---------------------------------------------------------------------------
// CommandSink (reader thread) - validate, answer, queue
// ---------------------------------------------------------------------------

std::optional<NakReason> CommHandler::playSong(const std::string& code, uint8_t requester) {
    std::cout << "  <- PLAY_SONG \"" << code << "\" from " << hexByte(requester) << "\n";
    return playSong(code);
}

void CommHandler::stopSong(uint8_t requester) {
    std::cout << "  <- STOP_SONG from " << hexByte(requester) << "\n";
    stopSong();
}

std::optional<NakReason> CommHandler::addVerse() {
    if (requestAddVerse()) {
        std::cout << "  <- Add verse: " << sectionLabel() << "\n";
        return std::nullopt;
    }
    return NakReason::NOT_PLAYING;
}

std::vector<uint8_t> CommHandler::songListPage(size_t offset) {
    size_t count = 0;
    auto page = mc::songs::buildSongListPage(mc::songs::listSongs(_config.playFolder), offset,
                                             count, MAX_PAYLOAD_SIZE);
    std::cout << "  <- Song list request (offset=" << offset << "), sending " << count
              << " songs, has_more=" << static_cast<int>(page[0]) << "\n";
    return page;
}

// ---------------------------------------------------------------------------
// High-level commands
// ---------------------------------------------------------------------------

std::optional<NakReason> CommHandler::playSong(const std::string& rawCode) {
    auto code = mc::songs::sanitizeCode(rawCode);
    if (!code) {
        std::cerr << "Invalid song code: \"" << rawCode << "\"\n";
        return NakReason::INVALID_PAYLOAD;
    }
    auto path = mc::songs::findSongFile(_config.playFolder, *code);
    if (!path) {
        std::cerr << "Song not found: " << *code << " (in " << _config.playFolder << ")\n";
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
    sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {0x01});
}

void CommHandler::organOff() {
    _organOn.store(false);
    sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {0x00});
}

void CommHandler::displayClear() {
    send(ADDR_BASE, MsgType::NOW_PLAYING_INFO,
         mc::display::encodeNowPlaying(mc::display::clearUpdate()));
}

void CommHandler::recordSong() {
    auto ports = _midiFinder ? _midiFinder() : std::nullopt;
    if (!ports) {
        std::cerr << "MIDIControl MIDI input port not found; cannot record.\n";
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

    char* argv[] = {const_cast<char*>("arecordmidi"), const_cast<char*>("-p"),
                    const_cast<char*>(alsaPort.c_str()), const_cast<char*>(fullPath.c_str()),
                    nullptr};
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "arecordmidi", nullptr, nullptr, argv, environ);
    if (rc == 0) {
        std::lock_guard<std::mutex> lk(_recorderMutex);
        _recorderPid = pid;
        std::cout << "Recording to " << fullPath << "\n";
    } else {
        std::cerr << "Failed to start arecordmidi (rc=" << rc << ")\n";
    }
}

void CommHandler::stopRecorder() {
    std::lock_guard<std::mutex> lk(_recorderMutex);
    if (_recorderPid > 0) {
        kill(_recorderPid, SIGTERM);
        int status = 0;
        waitpid(_recorderPid, &status, 0);
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
    std::unique_lock<std::mutex> lk(_queueMutex);
    bool interrupted = _queueCv.wait_for(lk, std::chrono::milliseconds(ms), [&] {
        for (const auto& c : _queue) {
            if (c.kind != Command::Kind::Finished) return true;  // stale Finished don't count
        }
        return false;
    });
    return !interrupted;
}

void CommHandler::controlLoop() {
    while (true) {
        Command cmd;
        {
            std::unique_lock<std::mutex> lk(_queueMutex);
            _queueCv.wait(lk, [&] { return !_queue.empty(); });
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
                if (releasePlayer()) std::cerr << "Playback stopped (base disconnected).\n";
                stopRecorder();
                _organOn.store(false);
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

void CommHandler::doPlay(const Command& cmd) {
    releasePlayer();  // a new song replaces the current one

    if (!_organOn.load()) {
        organOn();
        std::cout << "Organ power on, warming up " << _config.warmupMs << " ms...\n";
        if (!waitInterruptible(_config.warmupMs)) {
            std::cout << "Play " << cmd.code << " superseded during warm-up.\n";
            return;
        }
    }

    auto fail = [&](const std::string& why) {
        std::cerr << "Cannot play " << cmd.code << ": " << why << "\n";
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
        std::cout << "  -> " << label << "\n";
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
    {
        std::lock_guard<std::mutex> lk(_playerMutex);
        _player = std::move(player);
    }
    std::cout << "Playing " << cmd.code << "\n";
}

void CommHandler::finishPlayback() {
    if (!releasePlayer()) return;  // already stopped by a Stop that raced the end
    displayClear();
    sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_STOPPED);
    if (!_config.keepPowerOn) organOff();
    std::cout << "Playback finished.\n";
}

void CommHandler::doStop(bool signal) {
    bool wasPlaying = releasePlayer();
    stopRecorder();
    displayClear();
    if (signal) sendWithAck(ADDR_REMOTE, MsgType::SIGNAL_STOPPED);
    if (!_config.keepPowerOn) organOff();
    if (wasPlaying) std::cout << "Playback stopped.\n";
}

}  // namespace mc
