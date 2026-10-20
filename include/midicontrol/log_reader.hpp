#ifndef MIDICONTROL_LOG_READER_HPP
#define MIDICONTROL_LOG_READER_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "midicontrol/diag_store.hpp"
#include "midicontrol/fault_decode.hpp"
#include "midicontrol/serial_port.hpp"

namespace mc::diag {

// Text from the base's "MIDIControl log" CDC -> DiagStore (node "base").
//
// Bytes are split into lines; each line goes through parseLogLine. Fault lines (tag "fault") are
// accumulated by BaseFaultParser into fault entries, everything else is a plain entry. E and W lines are
// also reported to `event` (the status registry). Unparseable lines are ignored.
//
// Replay window: after a reset the base first replays its pre-reset log between
//   "--- log before reset ---" and "--- end of log before reset ---"
// and then prints the boot banner (sys lines, then the fault record) - the same fault a second time, now
// complete. The window runs from the first marker to the first banner line that is neither "sys" nor a fault
// line. Only inside it is a fault with the same kind, count and fault uptime as the last stored one taken for the same
// fault and *replaces* that entry; outside it the same kind/count is a new entry, because the base's fault
// counter restarts after a power loss.
class LogIngest {
public:
    using Clock = std::function<int64_t()>;                        // wall time, unix ms
    using EventSink = std::function<void(char level, const std::string& text)>;  // may be empty

    LogIngest(DiagStore& store, Clock now, EventSink event);

    // Called after a fault entry was stored or updated (refresh /api/status).
    void setOnFault(std::function<void()> cb) { onFault_ = std::move(cb); }

    void feed(const uint8_t* data, size_t len);
    // The port went away: emit the pending fault, drop a partial line, close the replay window.
    void disconnect();

private:
    enum class Phase { None, Body, Banner };
    void onLine(std::string line);
    void store(const LogLine& l);
    void flushFault();

    DiagStore& store_;
    Clock now_;
    EventSink event_;
    std::function<void()> onFault_;
    std::string partial_;
    bool overflow_ = false;  // current line is too long: skip up to the next newline
    Phase phase_ = Phase::None;
    BaseFaultParser parser_;
    uint64_t faultUptimeMs_ = 0;  // uptime of the last fault line fed
    int64_t faultTimeMs_ = 0;     // PC time of the first fault line of the pending fault
    bool havePending_ = false;
    std::optional<DiagEntry> lastFault_;  // last fault entry stored by this ingest (with its id)
};

// A byte source that can come and go (the log CDC).
class LogSource {
public:
    virtual ~LogSource() = default;
    virtual bool isOpen() const = 0;
    virtual bool tryOpen() = 0;
    virtual std::vector<uint8_t> read() = 0;  // may block briefly; empty = nothing
    virtual bool healthy() const = 0;         // false once the port is gone
    virtual void close() = 0;
};

// Reads a LogSource into a LogIngest; reopens it like the protocol port (retry every `retryMs`).
class LogReader {
public:
    LogReader(LogSource& source, LogIngest& ingest, int retryMs = 1000) : source_(source), ingest_(ingest), retryMs_(retryMs) {}
    ~LogReader() { stop(); }

    // One iteration of the loop at `nowMs` (any monotonic ms). Returns whether the port is open.
    bool step(int64_t nowMs);

    void start();  // thread calling step() until stop()
    void stop();
    // Heartbeat for the systemd watchdog scheme: stamped on every loop iteration while the thread runs.
    bool alive(int64_t maxAgeMs) const;

private:
    static int64_t steadyMs();
    void loop();

    LogSource& source_;
    LogIngest& ingest_;
    int retryMs_;
    int64_t lastTry_ = 0;
    bool tried_ = false;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int64_t> beat_{0};
};

// The real thing: the log CDC found by `finder` (config [DIAG] LogPort overrides the auto-detection).
class SerialLogSource : public LogSource {
public:
    using Finder = std::function<std::optional<std::string>()>;
    explicit SerialLogSource(Finder finder) : finder_(std::move(finder)) {}
    bool isOpen() const override { return port_.isOpen(); }
    bool tryOpen() override;
    std::vector<uint8_t> read() override { return port_.read(256); }
    bool healthy() const override { return port_.healthy(); }
    void close() override { port_.close(); }

private:
    Finder finder_;
    mc::SerialPort port_;
};

}  // namespace mc::diag

#endif
