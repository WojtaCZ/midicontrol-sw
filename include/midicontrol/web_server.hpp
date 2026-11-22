#ifndef MIDICONTROL_WEB_SERVER_HPP
#define MIDICONTROL_WEB_SERVER_HPP

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/firmware_upload.hpp"
#include "midicontrol/http.hpp"
#include "midicontrol/organ.hpp"
#include "midicontrol/songcheck.hpp"
#include "midicontrol/stops_file.hpp"
#include "midicontrol/diag_store.hpp"
#include "midicontrol/status.hpp"

namespace mc {

// Status page on its own thread: GET / and GET /api/status; the writes are the
// organ-stops store (GET /api/stops, PUT /api/stops/<code>) and, when a starter is
// given, the firmware upload (POST /api/firmware/<base|remote>, which only
// validates the file and hands it to the starter; the update runs elsewhere).
//
// Isolation from playback: it only reads StatusRegistry snapshots (never a
// CommHandler or Player lock), multiplexes at most 8 connections with poll()
// (an idle browser preconnect can't hold up the others), drops a request
// that isn't complete within 3 s (a firmware upload: 60 s) or exceeds 8 KiB
// (a firmware upload: 512 KiB), and never lets an
// exception escape. If the port
// can't be bound it logs an ERROR and retries every 10 s. The same thread
// rescans the play folder every 60 s for the song-file check.
class WebServer {
public:
    // `diag` (optional) serves GET /api/diag; `firmware` (optional) enables POST /api/firmware/<target>.
    WebServer(mc::status::StatusRegistry& status, std::string bind, int port,
              std::string playFolder, const mc::organ::OrganConfig& organ,
              mc::diag::DiagStore* diag = nullptr, mc::fwupload::Start firmware = nullptr,
              std::vector<std::string> allowedHosts = {});
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
    mc::organ::OrganConfig _organ;
    mc::stops::Store _stops;  // owned here; doPlay reads the song's .conf itself, never through this
    mc::diag::DiagStore* _diag;
    std::vector<std::string> _allowedHosts;  // B7: extra Host names accepted on writes
    mc::fwupload::Start _firmware;
    mc::songcheck::Scanner _scanner;
    int _listenFd = -1;
    std::vector<Conn> _conns;
    bool _bindFailing = false;
    std::atomic<bool> _running{false};
    std::thread _thread;
};

}  // namespace mc

#endif  // MIDICONTROL_WEB_SERVER_HPP
