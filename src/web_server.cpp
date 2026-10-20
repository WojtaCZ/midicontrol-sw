#include "midicontrol/web_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

#include "midicontrol/diag_api.hpp"
#include "midicontrol/http.hpp"
#include "midicontrol/log.hpp"
#include "midicontrol/status_json.hpp"
#include "midicontrol/web_page.hpp"

namespace mc {

using mc::status::Node;

namespace {

constexpr int kBindRetryMs = 10000;
constexpr int kRescanMs = 60000;
constexpr int kSendTimeoutMs = 2000;      // writing one response
constexpr int kRequestDeadlineMs = 3000;  // whole request head
constexpr size_t kMaxConns = 8;

using Clock = std::chrono::steady_clock;

int64_t msSince(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

void sendAll(int fd, const std::string& data) {
    // The socket is non-blocking: wait for room with poll, bounded in time.
    const auto started = Clock::now();
    size_t sent = 0;
    while (sent < data.size() && msSince(started) < kSendTimeoutMs) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n > 0) {
            sent += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            pollfd p{fd, POLLOUT, 0};
            ::poll(&p, 1, 200);
            continue;
        }
        return;  // a gone client: give up on it
    }
}

}  // namespace

WebServer::WebServer(mc::status::StatusRegistry& status, std::string bind, int port,
                     std::string playFolder, mc::diag::DiagStore* diag)
    : _status(status), _bind(std::move(bind)), _port(port), _playFolder(std::move(playFolder)), _diag(diag) {}

WebServer::~WebServer() { stop(); }

void WebServer::start() {
    if (_running.exchange(true)) return;
    try {
        _thread = std::thread(&WebServer::loop, this);
    } catch (const std::exception& e) {
        _running.store(false);
        mc::log::error(Node::Software, std::string("status page: cannot start thread: ") + e.what());
    }
}

void WebServer::stop() {
    _running.store(false);
    if (_thread.joinable()) _thread.join();
    closeAll();
    if (_listenFd >= 0) {
        ::close(_listenFd);
        _listenFd = -1;
    }
}

bool WebServer::tryBind() {
    auto fail = [&](const std::string& why) {
        // One ERROR per failure streak; the retries every 10 s are DEBUG.
        const std::string text = "status page: cannot listen on " + _bind + ":" +
                                 std::to_string(_port) + ": " + why + " (retrying every 10 s)";
        if (_bindFailing) {
            mc::log::debug(Node::Software, text);
        } else {
            mc::log::error(Node::Software, text);
        }
        _bindFailing = true;
        return false;
    };
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(_port));
    if (inet_pton(AF_INET, _bind.c_str(), &addr.sin_addr) != 1) return fail("invalid Bind address");

    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return fail(std::strerror(errno));
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || ::listen(fd, 8) != 0) {
        std::string why = std::strerror(errno);
        ::close(fd);
        return fail(why);
    }
    _listenFd = fd;
    _bindFailing = false;
    mc::log::info(Node::Software, "Status page on http://" + _bind + ":" + std::to_string(_port) + "/");
    return true;
}

void WebServer::acceptConn() {
    int fd = ::accept4(_listenFd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (fd < 0) return;
    if (_conns.size() >= kMaxConns) {
        ::close(fd);  // connection limit
        return;
    }
    _conns.push_back(Conn{fd, Clock::now(), {}});
}

void WebServer::respond(int fd, const mc::http::Response& r) { sendAll(fd, mc::http::serialize(r)); }

bool WebServer::readConn(Conn& c) {
    char chunk[2048];
    while (true) {
        ssize_t n = ::recv(c.fd, chunk, sizeof(chunk), 0);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return false;  // wait for more
        if (n <= 0) return true;  // closed by the client or an error
        c.buf.append(chunk, static_cast<size_t>(n));
        mc::http::Request req;
        switch (mc::http::parseRequest(c.buf, req)) {
            case mc::http::ParseResult::Incomplete:
                continue;
            case mc::http::ParseResult::Ok: {
                mc::http::DiagHandler diag;
                if (_diag) {
                    diag = [this](const std::string& query) {
                        return mc::diag::apiDiag(*_diag, query, [this] { mc::diag::publishFaultStatus(*_diag, _status); });
                    };
                }
                respond(c.fd, mc::http::route(req, mc::web::statusPageHtml(), [this] {
                    return mc::status::toJson(_status.snapshot());
                }, diag));
                return true;
            }
            case mc::http::ParseResult::TooLarge:
                respond(c.fd, mc::http::errorResponse(413));
                return true;
            case mc::http::ParseResult::Bad:
                respond(c.fd, mc::http::errorResponse(400));
                return true;
        }
    }
}

void WebServer::closeAll() {
    for (auto& c : _conns) ::close(c.fd);
    _conns.clear();
}

void WebServer::rescanSongs() {
    auto result = _scanner.rescan(_playFolder);
    std::vector<mc::status::SongEntry> songs;
    int64_t problems = 0;
    for (const auto& f : result.all) {
        std::string text;
        for (const auto& p : f.problems) text += (text.empty() ? "" : "; ") + p;
        if (!text.empty()) ++problems;
        songs.push_back({f.code, text});
    }
    _status.setSongs(std::move(songs));
    _status.set(Node::Software, "song_count", static_cast<int64_t>(result.all.size()));
    _status.set(Node::Software, "song_problems", problems);
    for (const auto& [file, problem] : result.newProblems) {
        mc::log::warn(Node::Software, "song " + file + ": " + problem);
    }
}

void WebServer::loop() {
    auto lastBind = Clock::time_point{};
    auto lastScan = Clock::time_point{};
    bool scanned = false;
    while (_running.load()) {
        try {
            if (!scanned || msSince(lastScan) >= kRescanMs) {
                scanned = true;
                lastScan = Clock::now();
                rescanSongs();
            }
            if (_listenFd < 0) {
                if (lastBind == Clock::time_point{} || msSince(lastBind) >= kBindRetryMs) {
                    lastBind = Clock::now();
                    tryBind();
                }
                if (_listenFd < 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    continue;
                }
            }
            std::vector<pollfd> fds;
            fds.push_back({_listenFd, POLLIN, 0});
            for (const auto& c : _conns) fds.push_back({c.fd, POLLIN, 0});
            int r = ::poll(fds.data(), fds.size(), 500);
            if (r < 0) continue;
            // Connections first (fds[i + 1] belongs to _conns[i]), then accept.
            std::vector<Conn> keep;
            for (size_t i = 0; i < _conns.size(); ++i) {
                Conn& c = _conns[i];
                bool done = false;
                if (fds[i + 1].revents & (POLLIN | POLLHUP | POLLERR)) done = readConn(c);
                if (!done && msSince(c.started) >= kRequestDeadlineMs) {
                    if (!c.buf.empty()) respond(c.fd, mc::http::errorResponse(408));
                    done = true;  // an idle preconnect is just closed
                }
                if (done) {
                    ::close(c.fd);
                } else {
                    keep.push_back(std::move(c));
                }
            }
            _conns = std::move(keep);
            if (fds[0].revents & POLLIN) acceptConn();
        } catch (const std::exception& e) {
            mc::log::error(Node::Software, std::string("status page: ") + e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        } catch (...) {
            mc::log::error(Node::Software, "status page: unknown error");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

}  // namespace mc
