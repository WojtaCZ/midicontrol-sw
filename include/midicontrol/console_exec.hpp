#ifndef MIDICONTROL_CONSOLE_EXEC_HPP
#define MIDICONTROL_CONSOLE_EXEC_HPP

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "midicontrol/comm.hpp"
#include "midicontrol/console.hpp"
#include "midicontrol/diag_store.hpp"
#include "midicontrol/firmware_update.hpp"
#include "midicontrol/firmware_upload.hpp"
#include "midicontrol/status.hpp"

namespace mc {

// Runs terminal commands against the handler, for the local prompt and for
// clients of the control socket alike, and the status page's firmware uploads.
// Thread-safe: each client calls it on its own thread; only one firmware update
// runs at a time, whoever started it. Every update publishes its progress in the
// Software node's fields fw_update_target / _state (running, done, failed) /
// _pct / _msg / _ms, which the page shows.
class ConsoleExecutor {
public:
    // `diag` (optional) backs the `diag` command.
    ConsoleExecutor(CommHandler& handler, mc::status::StatusRegistry& status, mc::diag::DiagStore* diag = nullptr)
        : _handler(handler), _status(status), _diag(diag) {}
    ~ConsoleExecutor() { waitForUpload(); }

    // Blocks until an upload started by startUpload has finished; call before CommHandler::shutdown,
    // so stopping the service never abandons a half-flashed device.
    void waitForUpload();

    ConsoleExecutor(const ConsoleExecutor&) = delete;
    ConsoleExecutor& operator=(const ConsoleExecutor&) = delete;

    // Status page upload (already validated by fwupload::handlePost): starts the update on
    // a worker thread; "" when started, else why not (fwupload::Start).
    std::string startUpload(mc::fwupload::Accepted upload);

    // `remote`: `out` is a socket client rather than this process's stdout
    // (log lines such as firmware-update progress are then copied to it).
    console::ExecResult execute(const std::string& line, const console::Reply& out, bool remote);

private:
    console::ExecResult update(const console::Command& c, const console::Reply& out, bool remote);
    using Run = std::function<bool(FirmwareUpdater&, FirmwareUpdater::Progress)>;
    // One update with the status fields kept current; the caller holds _updating.
    bool runUpdate(const std::string& target, const Run& run, const FirmwareUpdater::Say& say,
                   const FirmwareUpdater::Progress& progress);

    CommHandler& _handler;
    mc::status::StatusRegistry& _status;
    mc::diag::DiagStore* _diag;
    std::atomic<bool> _updating{false};
    std::mutex _workerMutex;  // guards _worker (web thread starts it, main joins it)
    std::thread _worker;      // the last upload; joined before the next one starts
};

}  // namespace mc

#endif  // MIDICONTROL_CONSOLE_EXEC_HPP
