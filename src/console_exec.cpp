#include "midicontrol/console_exec.hpp"

#include <sstream>

#include "midicontrol/diag_api.hpp"
#include "midicontrol/firmware_upload.hpp"
#include "midicontrol/log.hpp"
#include "midicontrol/protocol/constants.hpp"

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
        case NakReason::NO_TRANSFER: return "no firmware transfer is running";
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
            if (_handler.recordSong()) return ExecResult::Ok;
            out("Recording not started (see the log)\n");
            return ExecResult::Failed;
        case Kind::List: {
            auto songs = _handler.songList();
            std::string s = std::to_string(songs.size()) + " songs:";
            for (const auto& code : songs) s += " " + code;
            out(s + "\n");
            return ExecResult::Ok;
        }
        case Kind::On:
            _handler.organOnManual();
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
            if (_handler.baseInBootloader()) {
                out("Base is in its USB bootloader (ABCD:1235): a firmware update did not finish; "
                    "re-flash it with: update base <file>\n");
            }
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
        case Kind::Diag:
            if (!_diag) {
                out("Diagnostics are not available\n");
                return ExecResult::Failed;
            }
            out(mc::diag::formatDiag(*_diag));
            return ExecResult::Ok;
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

void ConsoleExecutor::waitForUpload() {
    std::lock_guard<std::mutex> lk(_workerMutex);
    if (_worker.joinable()) _worker.join();
}

bool ConsoleExecutor::runUpdate(const std::string& target, const Run& run, const FirmwareUpdater::Say& say,
                                const FirmwareUpdater::Progress& progress) {
    const auto set = [this](const char* field, mc::status::Value v) { _status.set(Node::Software, field, std::move(v)); };
    set("fw_update_target", target);
    set("fw_update_state", std::string("running"));
    set("fw_update_pct", int64_t{0});
    set("fw_update_msg", std::string());
    _status.setNow(Node::Software, "fw_update_ms");
    _status.set(Node::Software, "activity", "firmware update to " + target + " in progress");
    mc::fwupload::TransferProgress transfer;
    auto bar = [&](std::size_t done, std::size_t total) {
        if (auto step = transfer.update(done, total)) {
            set("fw_update_pct", static_cast<int64_t>(step->pct));
            set("fw_update_msg", step->msg);
        }
        if (progress) progress(done, total);
    };
    auto tell = [&](const std::string& text) {
        set("fw_update_msg", text);
        if (say) say(text);
    };
    FirmwareUpdater updater(_handler, tell);
    bool ok = false;
    try {
        ok = run(updater, bar);
    } catch (const std::exception& e) {
        tell(std::string("firmware update: ") + e.what());
        mc::log::error(Node::Software, std::string("firmware update: ") + e.what());
    }
    set("fw_update_state", std::string(ok ? "done" : "failed"));
    _status.setNow(Node::Software, "fw_update_ms");
    _status.set(Node::Software, "activity", std::string());
    return ok;
}

ExecResult ConsoleExecutor::update(const Command& c, const console::Reply& out, bool remote) {
    // Same refusals as the status page upload (startUpload): never reboot the base under a song.
    // The busy wording is unchanged ("busy: ...") for scripts that match it.
    // P1-14: the claim is taken under the same gate a Play takes when it is queued, so an update
    // cannot start while a song warms up the organ (isPlaying() is still false then).
    const auto claim = _handler.tryBeginUpdate();
    const bool busy = claim == CommHandler::UpdateClaim::Busy;
    const std::string why =
        mc::fwupload::updateRefusal(busy, claim == CommHandler::UpdateClaim::Playing || _handler.isPlaying(), true);
    if (!why.empty()) {
        if (claim == CommHandler::UpdateClaim::Granted) _handler.endUpdate();
        out((busy ? "busy: " : "refused: ") + why + "\n");
        return ExecResult::Failed;
    }
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
    const bool ok = runUpdate(c.target, [&c](FirmwareUpdater& u, FirmwareUpdater::Progress p) {
        return c.target == "base" ? u.updateBase(c.arg, p) : u.updateRemote(c.arg, p);
    }, say, progress);
    _handler.endUpdate();
    out(std::string("\n") + (ok ? "Firmware update complete." : "Firmware update FAILED.") + "\n");
    return ok ? ExecResult::Ok : ExecResult::Failed;
}

std::string ConsoleExecutor::startUpload(mc::fwupload::Accepted upload) {
    // Busy, then playing, then base connection (see fwupload::updateRefusal).
    // P1-14: claimed under the play gate (see update()).
    const auto claim = _handler.tryBeginUpdate();
    const std::string why = mc::fwupload::updateRefusal(
        claim == CommHandler::UpdateClaim::Busy,
        claim == CommHandler::UpdateClaim::Playing || _handler.isPlaying(), _handler.isBaseAlive());
    if (!why.empty()) {
        if (claim == CommHandler::UpdateClaim::Granted) _handler.endUpdate();
        return why;
    }
    std::lock_guard<std::mutex> lk(_workerMutex);
    if (_worker.joinable()) _worker.join();  // the previous upload has finished: the gate was free
    const bool base = upload.board == mc::protocol::ADDR_BASE;
    const std::string target = base ? "base" : "remote";
    mc::log::info(Node::Software, "status page: firmware update to " + target + " requested");
    try {
        _worker = std::thread([this, base, target, image = std::move(upload.image)] {
            const std::string name = "uploaded " + target + " image";
            runUpdate(target, [&](FirmwareUpdater& u, FirmwareUpdater::Progress p) {
                return base ? u.updateBase(image, name, p) : u.updateRemote(image, name, p);
            }, {}, {});
            _handler.endUpdate();
        });
    } catch (const std::exception& e) {
        _handler.endUpdate();
        return std::string("cannot start the update: ") + e.what();
    }
    return {};
}

}  // namespace mc
