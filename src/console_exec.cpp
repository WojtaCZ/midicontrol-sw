#include "midicontrol/console_exec.hpp"

#include <sstream>

#include "midicontrol/firmware_update.hpp"
#include "midicontrol/log.hpp"

namespace mc {

using console::Command;
using console::ExecResult;
using mc::protocol::NakReason;
using mc::status::Node;

namespace {

const char* nakText(NakReason r) {
    switch (r) {
        case NakReason::INVALID_PAYLOAD: return "invalid song code";
        case NakReason::NOT_FOUND: return "song not found";
        case NakReason::NOT_PLAYING: return "nothing is playing";
        default: return "refused";
    }
}

}  // namespace

ExecResult ConsoleExecutor::execute(const std::string& line, const console::Reply& out, bool remote) {
    const Command c = console::parse(line);
    using Kind = Command::Kind;
    if (c.kind == Kind::Empty) return ExecResult::Ok;
    if (c.kind == Kind::Quit) return ExecResult::Quit;

    // Every command is an event on the Software tab; a socket client's also
    // goes to the journal (the local prompt already shows what was typed).
    const std::string trimmed = line.substr(line.find_first_not_of(" \t"));
    if (remote) {
        mc::log::info(Node::Software, "console: " + trimmed);
    } else {
        _status.event(Node::Software, mc::status::Level::Info, "console: " + trimmed);
    }

    switch (c.kind) {
        case Kind::Play:
            if (auto nak = _handler.playSong(c.arg)) {
                out("Cannot play " + c.arg + ": " + nakText(*nak) + "\n");
                return ExecResult::Failed;
            }
            return ExecResult::Ok;
        case Kind::Stop:
            _handler.stopSong();
            return ExecResult::Ok;
        case Kind::Verse:
            if (_handler.requestAddVerse()) {
                out("Verse added: " + _handler.sectionLabel() + "\n");
                return ExecResult::Ok;
            }
            out("Nothing is playing\n");
            return ExecResult::Failed;
        case Kind::Record:
            _handler.recordSong();
            return ExecResult::Ok;
        case Kind::List: {
            auto songs = _handler.songList();
            std::string s = std::to_string(songs.size()) + " songs:";
            for (const auto& code : songs) s += " " + code;
            out(s + "\n");
            return ExecResult::Ok;
        }
        case Kind::On:
            _handler.organOn();
            return ExecResult::Ok;
        case Kind::Off:
            _handler.organOff();
            return ExecResult::Ok;
        case Kind::Status: {
            std::ostringstream os;
            os << "connected=" << _handler.isConnected() << " base_alive=" << _handler.isBaseAlive()
               << " playing=" << _handler.isPlaying();
            auto label = _handler.sectionLabel();
            if (!label.empty()) os << " section=\"" << label << "\"";
            out(os.str() + "\n");
            if (auto st = _handler.requestStatus()) {
                std::ostringstream bs;
                bs << "Base: BLE=" << st->ble << ", relay=" << st->relay << ", display=" << st->display
                   << ", pc_alive=" << st->pc << "\n";
                out(bs.str());
                return ExecResult::Ok;
            }
            out("Base did not answer GET_STATUS\n");
            return ExecResult::Failed;
        }
        case Kind::Update:
            return update(c, out, remote);
        case Kind::Usage:
            out(c.arg);
            return ExecResult::Failed;
        case Kind::Unknown:
            out("Unknown command: " + c.arg + "\n" + console::kCommandsHelp);
            return ExecResult::Failed;
        case Kind::Empty:
        case Kind::Quit:
            break;
    }
    return ExecResult::Ok;
}

ExecResult ConsoleExecutor::update(const Command& c, const console::Reply& out, bool remote) {
    std::unique_lock<std::mutex> lk(_updateMutex, std::try_to_lock);
    if (!lk.owns_lock()) {
        out("busy: a firmware update is already running\n");
        return ExecResult::Failed;
    }
    _status.set(Node::Software, "activity", "firmware update to " + c.target + " in progress");
    auto progress = [&out](std::size_t done, std::size_t total) {
        int pct = total ? static_cast<int>(done * 100 / total) : 100;
        int bars = pct / 5;
        out("\r  [" + std::string(static_cast<size_t>(bars), '#') +
            std::string(static_cast<size_t>(20 - bars), ' ') + "] " + std::to_string(pct) + "%  (" +
            std::to_string(done) + "/" + std::to_string(total) + ")");
    };
    // The bar is drawn with "\r"; a message starts on a fresh line.
    FirmwareUpdater::Say say;
    if (remote) say = [&out](const std::string& text) { out("\n" + text + "\n"); };
    FirmwareUpdater updater(_handler, say);
    bool ok = false;
    try {
        ok = c.target == "base" ? updater.updateBase(c.arg, progress)
                                : updater.updateRemote(c.arg, progress);
    } catch (const std::exception& e) {
        mc::log::error(Node::Software, std::string("firmware update: ") + e.what());
    }
    _status.set(Node::Software, "activity", std::string());
    out(std::string("\n") + (ok ? "Firmware update complete." : "Firmware update FAILED.") + "\n");
    return ok ? ExecResult::Ok : ExecResult::Failed;
}

}  // namespace mc
