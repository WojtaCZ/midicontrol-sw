#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <memory>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "midicontrol/comm.hpp"
#include "midicontrol/config.hpp"
#include "midicontrol/firmware_update.hpp"
#include "midicontrol/log.hpp"
#include "midicontrol/midi_ports.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/protocol/types.hpp"
#include "midicontrol/serial_port.hpp"
#include "midicontrol/status.hpp"
#include "midicontrol/web_server.hpp"

using namespace mc;
using mc::protocol::MsgType;
using mc::protocol::NakReason;
using mc::protocol::Packet;

namespace {

const char* kUsage =
    "Usage: midicontrol [--config PATH] [--headless]\n"
    "\n"
    "  -c, --config PATH  config file (default: $MIDICONTROL_CONFIG, then\n"
    "                     $XDG_CONFIG_HOME/midicontrol/config.conf, then ./config.conf)\n"
    "      --headless     no interactive prompt; run until SIGINT/SIGTERM\n"
    "                     (implied when stdin is not a terminal)\n"
    "  -h, --help         this help\n";

const char* kCommands =
    "Commands: play <code>, stop, verse (add verse), list, record, on, off, status,\n"
    "          update <base|remote> <name-update.bin>, quit\n";

const char* nakText(NakReason r) {
    switch (r) {
        case NakReason::INVALID_PAYLOAD: return "invalid song code";
        case NakReason::NOT_FOUND: return "song not found";
        case NakReason::NOT_PLAYING: return "nothing is playing";
        default: return "refused";
    }
}

bool fileExists(const std::string& p) {
    struct stat st {};
    return stat(p.c_str(), &st) == 0;
}

// Block until SIGINT/SIGTERM (headless mode). The signals must already be
// blocked in every thread (done before any thread is started).
void waitForTermination(const sigset_t& set) {
    int sig = 0;
    sigwait(&set, &sig);
    std::cout << "Received signal " << sig << ", shutting down.\n";
}

void interactive(CommHandler& handler) {
    std::cout << "\nMIDIControl - interactive control\n" << kCommands << "\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;

        size_t a = line.find_first_not_of(" \t");
        if (a == std::string::npos) continue;
        size_t b = line.find_last_not_of(" \t");
        line = line.substr(a, b - a + 1);

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);

        if (cmd == "play" && !rest.empty()) {
            if (auto nak = handler.playSong(rest)) {
                std::cout << "Cannot play " << rest << ": " << nakText(*nak) << "\n";
            }
        } else if (cmd == "stop") {
            handler.stopSong();
        } else if (cmd == "verse" || cmd == "repeat") {
            if (handler.requestAddVerse()) {
                std::cout << "Verse added: " << handler.sectionLabel() << "\n";
            } else {
                std::cout << "Nothing is playing\n";
            }
        } else if (cmd == "record") {
            handler.recordSong();
        } else if (cmd == "list") {
            auto songs = handler.songList();
            std::cout << songs.size() << " songs:";
            for (const auto& s : songs) std::cout << " " << s;
            std::cout << "\n";
        } else if (cmd == "on") {
            handler.organOn();
        } else if (cmd == "off") {
            handler.organOff();
        } else if (cmd == "status") {
            std::cout << "connected=" << handler.isConnected()
                      << " base_alive=" << handler.isBaseAlive()
                      << " playing=" << handler.isPlaying();
            auto label = handler.sectionLabel();
            if (!label.empty()) std::cout << " section=\"" << label << "\"";
            std::cout << "\n";
            if (auto st = handler.requestStatus()) {
                std::cout << "Base: BLE=" << st->ble << ", relay=" << st->relay
                          << ", display=" << st->display << ", pc_alive=" << st->pc << "\n";
            } else {
                std::cout << "Base did not answer GET_STATUS\n";
            }
        } else if (cmd == "update") {
            std::istringstream uss(rest);
            std::string target, path;
            uss >> target;
            std::getline(uss, path);
            if (!path.empty() && path[0] == ' ') path.erase(0, 1);
            std::transform(target.begin(), target.end(), target.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if ((target != "base" && target != "remote") || path.empty()) {
                std::cout << "Usage: update <base|remote> <name-update.bin>\n"
                             "  (the trailered -update.bin from the firmware build; a plain\n"
                             "  .bin is refused by the bootloader)\n";
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
            std::cout << "Unknown command: " << line << "\n" << kCommands;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string configArg;
    bool headless = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configArg = argv[++i];
        } else if (arg.rfind("--config=", 0) == 0) {
            configArg = arg.substr(9);
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << kUsage;
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n" << kUsage;
            return 2;
        }
    }
    if (!isatty(STDIN_FILENO)) headless = true;

    const std::string configPath = resolveConfigPath(
        configArg, [](const char* name) { return std::getenv(name); }, fileExists);
    Config config;
    try {
        config = loadConfig(configPath);
        std::cout << "Using config file " << configPath << "\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }

    // In headless mode SIGINT/SIGTERM are taken synchronously by sigwait();
    // block them before any thread exists so every thread inherits the mask.
    sigset_t termSignals;
    sigemptyset(&termSignals);
    sigaddset(&termSignals, SIGINT);
    sigaddset(&termSignals, SIGTERM);
    if (headless) pthread_sigmask(SIG_BLOCK, &termSignals, nullptr);

    mc::status::StatusRegistry status([] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    });
    mc::log::setRegistry(&status);
    CommHandler handler(config, [] { return findMidiControlPorts(); }, status);
    status.set(mc::status::Node::Software, "version", std::string(MC_GIT_HASH));
    status.set(mc::status::Node::Software, "build_date", std::string(MC_BUILD_DATE));
    status.setNow(mc::status::Node::Software, "started_ms");
    status.set(mc::status::Node::Software, "config_path", configPath);
    status.set(mc::status::Node::Software, "play_folder", config.playFolder);
    handler.start([] { return findMidiControlPort(); }, findMidiControlPort().value_or(""));

    std::unique_ptr<WebServer> web;
    if (config.webEnabled) {
        web = std::make_unique<WebServer>(status, config.webBind, config.webPort, config.playFolder);
        web->start();
    }

    if (headless) {
        std::cout << "Running headless; send SIGTERM or SIGINT to stop.\n";
        waitForTermination(termSignals);
    } else {
        interactive(handler);
    }

    if (web) web->stop();
    handler.shutdown();
    std::cout << "Done.\n";
    return 0;
}
