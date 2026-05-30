#include "midicontrol/comm.hpp"

#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

#include <cstdio>
#include <ctime>
#include <iostream>

#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"

extern char** environ;

namespace mc {

using namespace mc::protocol;

CommHandler::CommHandler(const Config& config, int midiOutIndex, std::string recordAlsaPort)
    : _config(config), _midiOutIndex(midiOutIndex), _recordAlsaPort(std::move(recordAlsaPort)) {}

CommHandler::~CommHandler() { disconnect(); }

bool CommHandler::connect(const std::string& devicePath) {
    if (!_serial.open(devicePath, 115200)) {
        std::cerr << "Failed to open serial port: " << devicePath << "\n";
        return false;
    }
    std::cout << "Opened serial port: " << devicePath << "\n";
    _running.store(true);
    _lastBaseKeepalive.store(Clock::now());
    _readerThread = std::thread(&CommHandler::readerLoop, this);
    _keepaliveThread = std::thread(&CommHandler::keepaliveLoop, this);
    return true;
}

void CommHandler::disconnect() {
    _running.store(false);
    if (_readerThread.joinable()) _readerThread.join();
    if (_keepaliveThread.joinable()) _keepaliveThread.join();
    if (_player) _player->stop();
    _serial.close();
}

uint8_t CommHandler::nextPacketId() { return _packetId.fetch_add(1); }

uint8_t CommHandler::send(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    Packet pkt(ADDR_PC, dest, nextPacketId(), type, payload);
    auto raw = encode(pkt);
    std::lock_guard<std::mutex> lk(_mutex);
    if (_serial.isOpen()) _serial.write(raw);
    return pkt.packetId;
}

uint8_t CommHandler::sendWithAck(uint8_t dest, MsgType type, const std::vector<uint8_t>& payload) {
    uint8_t pid = send(dest, type, payload);
    std::lock_guard<std::mutex> lk(_mutex);
    _pending[pid] = Pending{Clock::now(), 0, type, dest, payload};
    return pid;
}

void CommHandler::readerLoop() {
    while (_running.load()) {
        auto data = _serial.read(256);
        if (!data.empty()) {
            for (uint8_t byte : data) {
                auto pkt = _parser.feed(byte);
                if (pkt.has_value()) dispatchPacket(*pkt);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        checkTimeouts();
    }
}

void CommHandler::dispatchPacket(const Packet& pkt) {
    handleInternal(pkt);
    if (_callback) _callback(pkt);
}

void CommHandler::handleInternal(const Packet& pkt) {
    switch (pkt.msgType) {
        case MsgType::ACK:
            if (!pkt.payload.empty()) {
                std::lock_guard<std::mutex> lk(_mutex);
                _pending.erase(pkt.payload[0]);
            }
            break;
        case MsgType::NAK:
            if (pkt.payload.size() >= 2) {
                std::lock_guard<std::mutex> lk(_mutex);
                _pending.erase(pkt.payload[0]);
            }
            break;
        case MsgType::KEEPALIVE_ACK:
            _lastBaseKeepalive.store(Clock::now());
            break;
        default:
            break;
    }
}

void CommHandler::keepaliveLoop() {
    while (_running.load()) {
        send(ADDR_BASE, MsgType::KEEPALIVE);
        for (int i = 0; i < 20 && _running.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

bool CommHandler::isBaseAlive() const {
    auto last = _lastBaseKeepalive.load();
    return (Clock::now() - last) < std::chrono::seconds(5);
}

void CommHandler::checkTimeouts() {
    auto now = Clock::now();
    std::vector<std::pair<uint8_t, Pending>> snapshot;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        snapshot.assign(_pending.begin(), _pending.end());
    }

    for (auto& [pid, p] : snapshot) {
        if (now - p.timestamp <= std::chrono::milliseconds(RETRY_TIMEOUT_MS)) continue;
        if (p.retries < MAX_RETRIES) {
            Packet pkt(ADDR_PC, p.dest, pid, p.msgType, p.payload);
            auto raw = encode(pkt);
            std::lock_guard<std::mutex> lk(_mutex);
            if (_serial.isOpen()) _serial.write(raw);
            auto it = _pending.find(pid);
            if (it != _pending.end()) {
                it->second.timestamp = now;
                it->second.retries += 1;
            }
            std::cout << "Retry #" << (p.retries + 1) << " for PacketID=0x" << std::hex
                      << static_cast<int>(pid) << std::dec << "\n";
        } else {
            std::lock_guard<std::mutex> lk(_mutex);
            _pending.erase(pid);
            std::cout << "Timeout for PacketID=0x" << std::hex << static_cast<int>(pid) << std::dec
                      << " after " << static_cast<int>(MAX_RETRIES) << " attempts\n";
        }
    }
}

// ---- High-level commands ----

void CommHandler::playSong(const std::string& songName) {
    organOn();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    _player = std::make_unique<Player>(_midiOutIndex);
    _player->onDisplayInfo = [this](const mc::sysex::DisplayInfo& info) {
        sendNowPlayingInfo(info);
    };
    _player->onFinished = [this]() { onPlaybackFinished(); };

    std::string path = _config.playFolder + "/" + songName + ".mid";
    try {
        _player->play(path);
    } catch (const std::exception& e) {
        std::cerr << "Playback error: " << e.what() << "\n";
        _player.reset();
    }

    std::vector<uint8_t> payload(songName.begin(), songName.end());
    payload.push_back(0x00);
    sendWithAck(ADDR_BASE, MsgType::PLAY_SONG, payload);
}

void CommHandler::onPlaybackFinished() {
    _player.reset();
    organOff();
    sendWithAck(ADDR_BASE, MsgType::STOP_SONG);
}

void CommHandler::stopSong() {
    if (_player) {
        _player->stop();
        _player.reset();
    }
    if (_recorderPid > 0) {
        kill(_recorderPid, SIGTERM);
        int status = 0;
        waitpid(_recorderPid, &status, 0);
        _recorderPid = -1;
    }
    organOff();
    sendWithAck(ADDR_BASE, MsgType::STOP_SONG);
}

void CommHandler::recordSong() {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    organOn();
    std::this_thread::sleep_for(std::chrono::seconds(1));

    char buf[64];
    std::time_t t = std::time(nullptr);
    std::tm tm {};
    localtime_r(&t, &tm);
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S.mid", &tm);
    std::string filename = buf;
    std::string fullPath = _config.recFolder + "/" + filename;

    // Spawn: arecordmidi -p <alsaPort> <fullPath>
    std::string portArg = _recordAlsaPort;
    char* argv[] = {const_cast<char*>("arecordmidi"), const_cast<char*>("-p"),
                    const_cast<char*>(portArg.c_str()), const_cast<char*>(fullPath.c_str()),
                    nullptr};
    pid_t pid = -1;
    int rc = posix_spawnp(&pid, "arecordmidi", nullptr, nullptr, argv, environ);
    if (rc == 0) {
        _recorderPid = pid;
    } else {
        std::cerr << "Failed to start arecordmidi (rc=" << rc << ")\n";
    }

    std::vector<uint8_t> payload(filename.begin(), filename.end());
    payload.push_back(0x00);
    sendWithAck(ADDR_BASE, MsgType::RECORD_SONG, payload);
}

void CommHandler::getSongList() { sendWithAck(ADDR_BASE, MsgType::GET_SONG_LIST); }

void CommHandler::organOn() { sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {0x01}); }
void CommHandler::organOff() { sendWithAck(ADDR_BASE, MsgType::SET_CURRENT, {0x00}); }

void CommHandler::sendNowPlayingInfo(const mc::sysex::DisplayInfo& info) {
    // Binary-protocol display forwarding (distinct from the MIDI SysEx encoding).
    // The Base firmware consumes this packed-BCD form unchanged, so it is kept
    // byte-compatible with the original Python implementation.
    int song = info.song;
    int d3 = (song / 1000) % 10;
    int d2 = (song / 100) % 10;
    int d1 = (song / 10) % 10;
    int d0 = song % 10;
    uint8_t songH = static_cast<uint8_t>((d3 << 4) | d2);
    uint8_t songL = static_cast<uint8_t>((d1 << 4) | d0);

    int v1 = (info.verse / 10) % 10;
    int v0 = info.verse % 10;
    uint8_t verseByte = static_cast<uint8_t>((v1 << 4) | v0);

    uint8_t letterNibble = 0x0F;
    if (info.letter >= 'A' && info.letter <= 'D') {
        letterNibble = static_cast<uint8_t>(0x0A + (info.letter - 'A'));
    }

    uint8_t ledNibble = static_cast<uint8_t>(info.led);
    if (ledNibble < 1 || ledNibble > 4) ledNibble = 0x0F;

    send(ADDR_BASE, MsgType::NOW_PLAYING_INFO,
         {songH, songL, verseByte, letterNibble, ledNibble});
}

void CommHandler::repeatVerse() {
    if (_player) _player->repeatVerse();
}

bool CommHandler::toggleRepeat() { return _player ? _player->toggleRepeat() : false; }

bool CommHandler::isPlaying() const { return _player && _player->playing(); }

bool CommHandler::hasDisplayInfo() const { return _player && _player->hasInfo(); }

mc::sysex::DisplayInfo CommHandler::currentDisplayInfo() const {
    if (_player) return _player->currentInfo();
    return {};
}

}  // namespace mc
