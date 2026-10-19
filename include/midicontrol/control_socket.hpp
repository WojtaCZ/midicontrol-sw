#ifndef MIDICONTROL_CONTROL_SOCKET_HPP
#define MIDICONTROL_CONTROL_SOCKET_HPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>

#include "midicontrol/console.hpp"

namespace mc {

// Unix control socket of the running service ($XDG_RUNTIME_DIR/midicontrol.sock,
// mode 0600). Each client gets its own thread; commands run there, so a slow
// one (firmware update) never blocks the others. A client that disconnects
// mid-command never cancels it: further output is simply discarded.
// Protocol: see console.hpp.
class ControlServer {
public:
    ControlServer(console::Executor exec, std::string socketPath);
    ~ControlServer();
    ControlServer(const ControlServer&) = delete;
    ControlServer& operator=(const ControlServer&) = delete;

    // Call only while holding the instance lock: a stale socket file is removed.
    bool start();
    // Stops accepting, disconnects clients and waits for running commands.
    void stop();

private:
    void acceptLoop();
    void serveClient(int fd);

    console::Executor _exec;
    std::string _path;
    int _listenFd = -1;
    std::atomic<bool> _running{false};
    std::thread _thread;

    std::mutex _mutex;  // guards the two below
    std::condition_variable _cv;
    std::set<int> _clients;
};

// The thin client: attaches to a running service. Interactive (oneShot unset):
// same prompt and commands as standalone, `quit` detaches. One-shot: sends the
// line, prints the reply and returns 0 on success, 1 on failure. nullopt when
// no service answers on the socket.
std::optional<int> runClient(const std::string& socketPath, const std::optional<std::string>& oneShot);

}  // namespace mc

#endif  // MIDICONTROL_CONTROL_SOCKET_HPP
