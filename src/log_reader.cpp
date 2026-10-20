#include "midicontrol/log_reader.hpp"

#include <chrono>
#include <cstdio>

#include "midicontrol/log.hpp"
#include "midicontrol/log_line.hpp"

namespace mc::diag {

namespace {
constexpr size_t kMaxLine = 1024;  // the base cuts its lines at ~100 bytes; anything longer is noise
const char* const kReplayStart = "--- log before reset ---";
const char* const kReplayEnd = "--- end of log before reset ---";
}  // namespace

LogIngest::LogIngest(DiagStore& store, Clock now, EventSink event) : store_(store), now_(std::move(now)), event_(std::move(event)) {
    // After a service restart the base keeps reprinting its last fault in every banner: know it already.
    lastFault_ = store_.lastFault("base");
}

void LogIngest::feed(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == '\n') {
            if (!overflow_) onLine(std::move(partial_));
            partial_.clear();
            overflow_ = false;
        } else if (!overflow_) {
            partial_ += c;
            if (partial_.size() > kMaxLine) {
                partial_.clear();
                overflow_ = true;
            }
        }
    }
}

void LogIngest::disconnect() {
    flushFault();
    partial_.clear();
    overflow_ = false;
    phase_ = Phase::None;
}

void LogIngest::onLine(std::string line) {
    while (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == kReplayStart) {
        flushFault();
        phase_ = Phase::Body;
        return;
    }
    if (line == kReplayEnd) {
        flushFault();
        phase_ = Phase::Banner;
        return;
    }
    auto l = parseLogLine(line);
    if (!l) return;
    if (l->tag == "sys") {  // boot banner: logged, but does not end a pending fault or the window
        // A boot banner without a replay window is a power-on boot: the fault counter restarts, so the
        // previous fault can never be reprinted any more.
        if (phase_ == Phase::None && l->text.compare(0, 4, "boot") == 0) lastFault_.reset();
        store(*l);
        return;
    }
    if (parser_.feed(*l)) {
        if (!havePending_) {
            havePending_ = true;
            faultTimeMs_ = now_();
        }
        faultUptimeMs_ = l->uptimeMs;
        return;
    }
    flushFault();
    if (phase_ == Phase::Banner) phase_ = Phase::None;  // first ordinary line after the banner
    store(*l);
}

void LogIngest::store(const LogLine& l) {
    DiagEntry e;
    e.pcTimeMs = now_();
    e.node = "base";
    e.uptimeMs = l.uptimeMs;
    e.level = l.level;
    e.tag = l.tag;
    e.text = l.truncated ? l.text + "~" : l.text;
    store_.append(std::move(e));
    if (event_ && (l.level == 'E' || l.level == 'W')) event_(l.level, l.tag + ": " + l.text);
}

void LogIngest::flushFault() {
    auto f = parser_.take();
    const bool had = havePending_;
    havePending_ = false;
    if (!f || !had) return;
    DiagEntry e;
    e.pcTimeMs = faultTimeMs_;
    e.node = "base";
    e.uptimeMs = f->uptimeMs ? f->uptimeMs : faultUptimeMs_;
    e.level = 'E';
    e.tag = "fault";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%s #%u pc=0x%08x lr=0x%08x", f->kind.c_str(), static_cast<unsigned>(f->count), static_cast<unsigned>(f->pc),
                  static_cast<unsigned>(f->lr));
    e.text = buf;
    e.fault = *f;

    // Identity of a fault: kind + count + the fault's own uptime (the record-time line's uptime equals the
    // banner's "up N s"). Kind + count alone repeat after a power cycle.
    const bool update = phase_ != Phase::None && lastFault_ && lastFault_->fault && lastFault_->fault->kind == f->kind &&
                        lastFault_->fault->count == f->count && lastFault_->uptimeMs == e.uptimeMs;
    if (update) {
        e.pcTimeMs = lastFault_->pcTimeMs;
        if (store_.replace(lastFault_->id, e)) {
            e.id = lastFault_->id;
            lastFault_ = e;
            if (onFault_) onFault_();
            return;
        }
    }
    e.id = store_.append(e);
    lastFault_ = e;
    if (event_) event_('E', "fault: " + e.text);
    if (onFault_) onFault_();
}

// ---------------------------------------------------------------------------

int64_t LogReader::steadyMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool LogReader::step(int64_t nowMs) {
    if (!source_.isOpen()) {
        if (tried_ && nowMs - lastTry_ < retryMs_) return false;
        tried_ = true;
        lastTry_ = nowMs;
        if (!source_.tryOpen()) return false;
    }
    auto data = source_.read();
    if (!data.empty()) ingest_.feed(data.data(), data.size());
    if (!source_.healthy()) {
        source_.close();
        ingest_.disconnect();
        return false;
    }
    return true;
}

void LogReader::start() {
    if (running_.exchange(true)) return;
    beat_.store(steadyMs());
    thread_ = std::thread(&LogReader::loop, this);
}

void LogReader::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    if (source_.isOpen()) {
        source_.close();
        ingest_.disconnect();
    }
}

bool LogReader::alive(int64_t maxAgeMs) const { return !running_.load() || steadyMs() - beat_.load() <= maxAgeMs; }

void LogReader::loop() {
    while (running_.load()) {
        beat_.store(steadyMs());
        bool open = false;
        try {
            open = step(steadyMs());
        } catch (const std::exception& e) {
            mc::log::error(mc::status::Node::Base, std::string("log reader: ") + e.what());
        }
        if (!open) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

bool SerialLogSource::tryOpen() {
    auto path = finder_ ? finder_() : std::nullopt;
    if (!path) return false;
    if (!port_.open(*path, 115200)) return false;
    mc::log::info(mc::status::Node::Base, "Reading the base log from " + *path);
    return true;
}

}  // namespace mc::diag
