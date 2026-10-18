#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "midicontrol/comm.hpp"
#include "midicontrol/config.hpp"
#include "midicontrol/firmware_update.hpp"
#include "midicontrol/midi_ports.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/protocol/types.hpp"
#include "midicontrol/serial_port.hpp"

namespace fs = std::filesystem;
using namespace mc;
using mc::protocol::MsgType;
using mc::protocol::Packet;

namespace {

std::string payloadToString(const std::vector<uint8_t>& payload) {
    std::string s(payload.begin(), payload.end());
    // strip trailing NULs
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

// Build a SONG_LIST_RESP payload for the requested offset.
std::vector<uint8_t> buildSongListResponse(const std::string& folder, size_t offset,
                                           size_t& sentCount) {
    std::vector<std::string> names;
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext == ".mid") names.push_back(p.stem().string());
    }
    std::sort(names.begin(), names.end());

    std::vector<uint8_t> result;
    result.push_back(0x00);  // has_more placeholder
    sentCount = 0;
    for (size_t i = offset; i < names.size(); ++i) {
        const std::string& name = names[i];
        size_t needed = name.size() + (sentCount > 0 ? 1 : 0);
        if (result.size() + needed > 128) {
            result[0] = 0x01;  // has_more = true
            break;
        }
        if (sentCount > 0) result.push_back(0x3B);  // ';'
        result.insert(result.end(), name.begin(), name.end());
        ++sentCount;
    }
    return result;
}

void printDisplayInfo(const sysex::DisplayInfo& info) {
    char letter = info.letter == sysex::LETTER_OFF ? '-' : info.letter;
    std::cout << "  song=" << info.song << ", verse=" << info.verse << ", letter=" << letter
              << ", led=" << static_cast<int>(info.led) << "\n";
}

}  // namespace

int main() {
    Config config;
    try {
        config = loadConfig("config.conf");
        std::cout << "Reading config file config.conf.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    auto ports = findMidiControlPorts();
    if (!ports) {
        std::cerr << "MIDIControl MIDI ports not found!\n";
        return 1;
    }
    std::cout << "Found MIDIControl output port: " << ports->outName << "\n";
    std::cout << "Found MIDIControl input port: " << ports->inName << "\n";

    auto devicePath = findMidiControlPort();
    if (!devicePath) {
        std::cerr << "MIDIControl serial port not found!\n";
        return 1;
    }
    std::cout << "Found MIDIControl serial port: " << *devicePath << "\n";

    // arecordmidi expects an ALSA client:port; use the trailing token of the name.
    std::string alsaPort;
    {
        std::istringstream iss(ports->inName);
        std::string tok;
        while (iss >> tok) alsaPort = tok;
    }

    CommHandler handler(config, ports->outIndex, alsaPort);

    handler.onPacket([&handler, &config](const Packet& pkt) {
        switch (pkt.msgType) {
            case MsgType::KEEPALIVE_ACK:
                return;  // handled internally
            case MsgType::ACK:
                if (!pkt.payload.empty())
                    std::cout << "  <- ACK for PacketID=0x" << std::hex
                              << static_cast<int>(pkt.payload[0]) << std::dec << "\n";
                break;
            case MsgType::NAK:
                if (pkt.payload.size() >= 2)
                    std::cout << "  <- NAK for PacketID=0x" << std::hex
                              << static_cast<int>(pkt.payload[0]) << ", reason=0x"
                              << static_cast<int>(pkt.payload[1]) << std::dec << "\n";
                break;
            case MsgType::SIGNAL_PLAYING:
                std::cout << "  <- Playing: " << payloadToString(pkt.payload) << "\n";
                break;
            case MsgType::SIGNAL_RECORDING:
                std::cout << "  <- Recording: " << payloadToString(pkt.payload) << "\n";
                break;
            case MsgType::SIGNAL_STOPPED:
                std::cout << "  <- Stopped\n";
                break;
            case MsgType::SONG_LIST_RESP:
                if (pkt.payload.size() > 1) {
                    std::cout << "  <- Song list (has_more=" << static_cast<int>(pkt.payload[0])
                              << "): "
                              << payloadToString({pkt.payload.begin() + 1, pkt.payload.end()})
                              << "\n";
                } else {
                    std::cout << "  <- Song list: (empty)\n";
                }
                break;
            case MsgType::STATUS_RESP:
                if (pkt.payload.size() >= 3) {
                    std::cout << "  <- Status: BLE=" << bool(pkt.payload[0])
                              << ", current=" << bool(pkt.payload[1])
                              << ", display=" << bool(pkt.payload[2]) << ", pc_alive="
                              << (pkt.payload.size() >= 4 ? bool(pkt.payload[3]) : false) << "\n";
                }
                break;
            case MsgType::GET_SONG_LIST: {
                size_t offset = pkt.payload.empty() ? 0 : pkt.payload[0];
                size_t count = 0;
                auto resp = buildSongListResponse(config.playFolder, offset, count);
                std::cout << "  <- Song list request (offset=" << offset << "), sending " << count
                          << " songs, has_more=" << static_cast<int>(resp[0]) << "\n";
                handler.sendWithAck(mc::protocol::ADDR_BASE, MsgType::SONG_LIST_RESP, resp);
                break;
            }
            case MsgType::PLAY_SONG: {
                std::string song = payloadToString(pkt.payload);
                std::cout << "  <- Base requested play: " << song << "\n";
                handler.playSong(song);
                break;
            }
            case MsgType::STOP_SONG:
                std::cout << "  <- Base stopped playback\n";
                handler.stopSong();
                break;
            default:
                break;
        }
    });

    if (!handler.connect(*devicePath)) {
        std::cerr << "Cannot connect to MIDIControl!\n";
        return 1;
    }

    std::cout << "\nMIDIControl - interactive control\n";
    std::cout << "Commands: play <song>, stop, record, list, on, off, status,\n";
    std::cout << "          repeat, repeatmode, update <base|remote> <file.bin>, quit\n\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;

        // trim
        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t");
        line = line.substr(a, b - a + 1);
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

        if (cmd == "play" && !rest.empty()) {
            std::cout << "Playing: " << rest << "\n";
            handler.playSong(rest);
        } else if (cmd == "stop") {
            std::cout << "Stopping...\n";
            handler.stopSong();
        } else if (cmd == "record") {
            std::cout << "Recording...\n";
            handler.recordSong();
        } else if (cmd == "list") {
            std::cout << "Requesting song list...\n";
            handler.getSongList();
        } else if (cmd == "on") {
            std::cout << "Turning on power...\n";
            handler.organOn();
        } else if (cmd == "off") {
            std::cout << "Turning off power...\n";
            handler.organOff();
        } else if (cmd == "repeat") {
            if (handler.isPlaying()) {
                handler.repeatVerse();
                std::cout << "Jumping to start of current verse\n";
            } else {
                std::cout << "Nothing is playing\n";
            }
        } else if (cmd == "repeatmode") {
            if (handler.isPlaying()) {
                bool en = handler.toggleRepeat();
                std::cout << "Verse repeat: " << (en ? "on" : "off") << "\n";
            } else {
                std::cout << "Nothing is playing\n";
            }
        } else if (cmd == "status") {
            std::cout << "base_alive=" << handler.isBaseAlive() << "\n";
            std::cout << "playing=" << handler.isPlaying() << "\n";
            if (handler.hasDisplayInfo()) printDisplayInfo(handler.currentDisplayInfo());
            std::cout << "Requesting status...\n";
            handler.sendWithAck(mc::protocol::ADDR_BASE, MsgType::GET_STATUS);
        } else if (cmd == "update") {
            std::istringstream uss(rest);
            std::string target, path;
            uss >> target;
            std::getline(uss, path);
            if (!path.empty() && path[0] == ' ') path.erase(0, 1);
            std::transform(target.begin(), target.end(), target.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if ((target != "base" && target != "remote") || path.empty()) {
                std::cout << "Usage: update <base|remote> <firmware.bin>\n";
            } else {
                auto progress = [](std::size_t done, std::size_t total) {
                    int pct = total ? static_cast<int>(done * 100 / total) : 100;
                    int bars = pct / 5;
                    std::cout << "\r  [" << std::string(bars, '#') << std::string(20 - bars, ' ')
                              << "] " << pct << "%  (" << done << "/" << total << ")" << std::flush;
                };
                FirmwareUpdater updater(handler);
                bool ok = (target == "base") ? updater.updateBase(path, progress)
                                             : updater.updateRemote(path, progress);
                std::cout << "\n" << (ok ? "Firmware update complete." : "Firmware update FAILED.")
                          << "\n";
            }
        } else if (cmd == "quit" || cmd == "exit") {
            break;
        } else {
            std::cout << "Unknown command: " << line << "\n";
            std::cout << "Commands: play <song>, stop, record, list, on, off, status,\n";
            std::cout << "          repeat, repeatmode, update <base|remote> <file.bin>, quit\n";
        }
    }

    handler.disconnect();
    std::cout << "Done.\n";
    return 0;
}
