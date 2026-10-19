#ifndef MIDICONTROL_WEB_SERVER_HPP
#define MIDICONTROL_WEB_SERVER_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/http.hpp"
#include "midicontrol/songcheck.hpp"
#include "midicontrol/status.hpp"

namespace mc {

// Read-only status page on its own thread: GET / and GET /api/status.
//
// Isolation from playback: it only reads StatusRegistry snapshots (never a
// CommHandler or Player lock), multiplexes at most 8 connections with poll()
// (an idle browser preconnect can't hold up the others), drops a request
// that isn't complete within 3 s or exceeds 8 KiB, and never lets an
// exception escape. If the port
// can't be bound it logs an ERROR and retries every 10 s. The same thread
// rescans the play folder every 60 s for the song-file check.
class WebServer {
public:
    WebServer(mc::status::StatusRegistry& status, std::string bind, int port,
              std::string playFolder);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    void start();
    void stop();

private:
    void loop();
    bool tryBind();
    struct Conn {
        int fd;
        std::chrono::steady_clock::time_point started;
        std::string buf;
    };
    void acceptConn();
    // Read what's there; true when the connection is finished (answered or dropped).
    bool readConn(Conn& c);
    void respond(int fd, const mc::http::Response& r);
    void closeAll();
    void rescanSongs();

    mc::status::StatusRegistry& _status;
    std::string _bind;
    int _port;
    std::string _playFolder;
    mc::songcheck::Scanner _scanner;
    int _listenFd = -1;
    std::vector<Conn> _conns;
    bool _bindFailing = false;
    std::atomic<bool> _running{false};
    std::thread _thread;
};

}  // namespace mc

#endif  // MIDICONTROL_WEB_SERVER_HPP
