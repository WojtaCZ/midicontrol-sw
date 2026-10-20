#ifndef MIDICONTROL_CONSOLE_EXEC_HPP
#define MIDICONTROL_CONSOLE_EXEC_HPP

#include <mutex>
#include <string>

#include "midicontrol/comm.hpp"
#include "midicontrol/console.hpp"
#include "midicontrol/diag_store.hpp"
#include "midicontrol/status.hpp"

namespace mc {

// Runs terminal commands against the handler, for the local prompt and for
// clients of the control socket alike. Thread-safe: each client calls it on
// its own thread; only one firmware update runs at a time.
class ConsoleExecutor {
public:
    // `diag` (optional) backs the `diag` command.
    ConsoleExecutor(CommHandler& handler, mc::status::StatusRegistry& status, mc::diag::DiagStore* diag = nullptr)
        : _handler(handler), _status(status), _diag(diag) {}

    // `remote`: `out` is a socket client rather than this process's stdout
    // (log lines such as firmware-update progress are then copied to it).
    console::ExecResult execute(const std::string& line, const console::Reply& out, bool remote);

private:
    console::ExecResult update(const console::Command& c, const console::Reply& out, bool remote);

    CommHandler& _handler;
    mc::status::StatusRegistry& _status;
    mc::diag::DiagStore* _diag;
    std::mutex _updateMutex;
};

}  // namespace mc

#endif  // MIDICONTROL_CONSOLE_EXEC_HPP
