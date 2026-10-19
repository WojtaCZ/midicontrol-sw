#include "midicontrol/control_socket.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <iostream>

#include "midicontrol/log.hpp"

namespace mc {

using mc::status::Node;

namespace {

constexpr size_t kMaxClients = 4;
constexpr size_t kMaxLine = 4096;
constexpr int kSendTimeoutS = 5;

bool makeAddress(const std::string& path, sockaddr_un& addr) {
    if (path.size() >= sizeof(addr.sun_path)) return false;
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return true;
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

ControlServer::ControlServer(console::Executor exec, std::string socketPath)
    : _exec(std::move(exec)), _path(std::move(socketPath)) {}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start() {
    sockaddr_un addr{};
    if (!makeAddress(_path, addr)) {
        mc::log::error(Node::Software, "control socket path too long: " + _path);
        return false;
    }
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        mc::log::error(Node::Software, std::string("control socket: ") + std::strerror(errno));
        return false;
    }
    ::unlink(_path.c_str());  // stale file from a previous run (we hold the lock)
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::chmod(_path.c_str(), 0600) != 0 || ::listen(fd, 4) != 0) {
        mc::log::error(Node::Software, "control socket " + _path + ": " + std::strerror(errno));
        ::close(fd);
        return false;
    }
    _listenFd = fd;
    _running.store(true);
    _thread = std::thread(&ControlServer::acceptLoop, this);
    mc::log::info(Node::Software, "Control socket " + _path);
    return true;
}

void ControlServer::stop() {
    if (!_running.exchange(false)) return;
    if (_thread.joinable()) _thread.join();
    ::close(_listenFd);
    _listenFd = -1;
    ::unlink(_path.c_str());
    std::unique_lock<std::mutex> lk(_mutex);
    for (int fd : _clients) ::shutdown(fd, SHUT_RDWR);  // wakes their reads
    // A running command (e.g. a firmware update) is allowed to finish.
    _cv.wait(lk, [&] { return _clients.empty(); });
}

void ControlServer::acceptLoop() {
    while (_running.load()) {
        pollfd p{_listenFd, POLLIN, 0};
        if (::poll(&p, 1, 500) <= 0 || !(p.revents & POLLIN)) continue;
        int fd = ::accept4(_listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) continue;
        timeval tv{};
        tv.tv_sec = kSendTimeoutS;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_clients.size() >= kMaxClients) {
                sendAll(fd, "busy: too many clients\n" + console::endMarker(false));
                ::close(fd);
                continue;
            }
            _clients.insert(fd);
        }
        try {
            std::thread(&ControlServer::serveClient, this, fd).detach();
        } catch (const std::exception& e) {
            mc::log::error(Node::Software, std::string("control socket: ") + e.what());
            std::lock_guard<std::mutex> lk(_mutex);
            _clients.erase(fd);
            ::close(fd);
        }
    }
}

void ControlServer::serveClient(int fd) {
    bool alive = true;     // false once a write failed: output is discarded
    bool atLineStart = true;
    auto out = [&](const std::string& text) {
        if (text.empty()) return;
        atLineStart = text.back() == '\n';
        if (alive && !sendAll(fd, text)) alive = false;
    };
    std::string buf;
    char chunk[512];
    try {
        while (alive) {
            ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            buf.append(chunk, static_cast<size_t>(n));
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                atLineStart = true;
                console::ExecResult r = console::ExecResult::Failed;
                try {
                    r = _exec(line, out);
                } catch (const std::exception& e) {
                    mc::log::error(Node::Software, std::string("console command failed: ") + e.what());
                    out(std::string("error: ") + e.what() + "\n");
                }
                if (!atLineStart) out("\n");
                out(console::endMarker(r != console::ExecResult::Failed));
            }
            if (buf.size() > kMaxLine) {
                out("error: line too long\n" + console::endMarker(false));
                break;
            }
        }
    } catch (...) {
        // never let anything escape a client thread
    }
    ::close(fd);
    std::lock_guard<std::mutex> lk(_mutex);
    _clients.erase(fd);
    _cv.notify_all();
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

namespace {

// Print the reply up to the end marker. nullopt if the connection dropped.
std::optional<bool> readReply(int fd) {
    bool atLineStart = true;
    bool inMarker = false;
    std::string marker;
    char chunk[1024];
    while (true) {
        ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return std::nullopt;
        std::string text;
        for (ssize_t i = 0; i < n; ++i) {
            char ch = chunk[i];
            if (inMarker) {
                if (ch == '\n') {
                    std::cout << text << std::flush;
                    return marker == "OK";
                }
                marker += ch;
                continue;
            }
            if (atLineStart && ch == console::kEndOfReply) {
                inMarker = true;
                continue;
            }
            text += ch;
            atLineStart = ch == '\n';
        }
        std::cout << text << std::flush;
    }
}

std::string currentDir() {
    char buf[PATH_MAX];
    return getcwd(buf, sizeof(buf)) ? std::string(buf) : std::string(".");
}

}  // namespace

std::optional<int> runClient(const std::string& socketPath, const std::optional<std::string>& oneShot) {
    sockaddr_un addr{};
    if (!makeAddress(socketPath, addr)) return std::nullopt;
    int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return std::nullopt;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return std::nullopt;
    }
    const std::string cwd = currentDir();

    if (oneShot) {
        int rc = 1;
        if (sendAll(fd, console::absolutize(*oneShot, cwd) + "\n")) {
            auto ok = readReply(fd);
            if (!ok) std::cerr << "Connection to the midicontrol service lost.\n";
            rc = ok && *ok ? 0 : 1;
        }
        ::close(fd);
        return rc;
    }

    std::cout << "\nMIDIControl - attached to the running service (quit detaches)\n"
              << console::kCommandsHelp << "\n";
    std::string line;
    int rc = 0;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        auto kind = console::parse(line).kind;
        if (kind == console::Command::Kind::Quit) break;
        if (kind == console::Command::Kind::Empty) continue;
        if (!sendAll(fd, console::absolutize(line, cwd) + "\n") || !readReply(fd)) {
            std::cerr << "Connection to the midicontrol service lost.\n";
            rc = 1;
            break;
        }
    }
    ::close(fd);
    return rc;
}

}  // namespace mc
