#include "midicontrol/status.hpp"

#include "midicontrol/protocol/constants.hpp"

namespace mc::status {

namespace {

constexpr int64_t kRecentProblemMs = 10 * 60 * 1000;  // software dot window
constexpr int64_t kStatusFreshMs = 15 * 1000;         // controller: STATUS_RESP age

template <typename T>
const T* field(const NodeSnapshot& n, const std::string& key) {
    auto it = n.fields.find(key);
    if (it == n.fields.end()) return nullptr;
    return std::get_if<T>(&it->second);
}

bool flag(const NodeSnapshot& n, const std::string& key, bool fallback) {
    const bool* b = field<bool>(n, key);
    return b ? *b : fallback;
}

}  // namespace

const char* levelName(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

const char* nodeKey(Node node) {
    switch (node) {
        case Node::Software: return "software";
        case Node::Base: return "base";
        case Node::Controller: return "controller";
    }
    return "software";
}

const char* dotName(Dot dot) {
    switch (dot) {
        case Dot::Green: return "green";
        case Dot::Yellow: return "yellow";
        case Dot::Red: return "red";
        case Dot::Grey: return "grey";
    }
    return "grey";
}

StatusRegistry::StatusRegistry(Clock now, size_t ringSize, size_t problemsSize)
    : _now(std::move(now)), _ringSize(ringSize), _problemsSize(problemsSize) {}

void StatusRegistry::event(Node node, Level level, std::string text) {
    Event ev{now(), level, node, std::move(text)};
    std::lock_guard<std::mutex> lk(_mutex);
    NodeState& n = _nodes[static_cast<size_t>(node)];
    if (level == Level::Warn || level == Level::Error) {
        n.counters[level == Level::Warn ? "warnings" : "errors"] += 1;
        _problems.push_back(ev);
        while (_problems.size() > _problemsSize) _problems.pop_front();
    }
    n.events.push_back(std::move(ev));
    while (n.events.size() > _ringSize) n.events.pop_front();
}

void StatusRegistry::count(Node node, const std::string& counter, uint64_t n) {
    std::lock_guard<std::mutex> lk(_mutex);
    _nodes[static_cast<size_t>(node)].counters[counter] += n;
}

void StatusRegistry::set(Node node, const std::string& key, Value value) {
    std::lock_guard<std::mutex> lk(_mutex);
    _nodes[static_cast<size_t>(node)].fields[key] = std::move(value);
}

void StatusRegistry::setSongs(std::vector<SongEntry> songs) {
    std::lock_guard<std::mutex> lk(_mutex);
    _songs = std::move(songs);
}

Snapshot StatusRegistry::snapshot() const {
    Snapshot s;
    s.nowMs = now();
    std::lock_guard<std::mutex> lk(_mutex);
    for (size_t i = 0; i < _nodes.size(); ++i) {
        s.nodes[i].fields = _nodes[i].fields;
        s.nodes[i].counters = _nodes[i].counters;
        s.nodes[i].events.assign(_nodes[i].events.begin(), _nodes[i].events.end());
    }
    s.problems.assign(_problems.begin(), _problems.end());
    s.songs = _songs;
    return s;
}

std::vector<Node> nodesForPeer(uint8_t addr) {
    switch (addr) {
        case mc::protocol::ADDR_REMOTE: return {Node::Controller};
        case mc::protocol::ADDR_BROADCAST: return {Node::Base, Node::Controller};
        default: return {Node::Base};
    }
}

Dot evaluateDot(Node node, const Snapshot& s) {
    const NodeSnapshot& base = s.node(Node::Base);
    switch (node) {
        case Node::Software: {
            bool warn = false;
            for (const auto& ev : s.problems) {
                if (ev.node != Node::Software || s.nowMs - ev.timeMs > kRecentProblemMs) continue;
                if (ev.level == Level::Error) return Dot::Red;
                warn = true;
            }
            const int64_t* songProblems = field<int64_t>(s.node(Node::Software), "song_problems");
            if (warn || (songProblems && *songProblems > 0)) return Dot::Yellow;
            return Dot::Green;
        }
        case Node::Base:
            if (!flag(base, "connected", false)) return Dot::Yellow;  // waiting for the base
            if (!flag(base, "alive", true)) return Dot::Red;
            if (!flag(base, "status_answered", true)) return Dot::Yellow;
            return Dot::Green;
        case Node::Controller: {
            // The PC only knows what the base reports; it can't tell a fault
            // from "switched off", so there is no red here.
            const int64_t* statusMs = field<int64_t>(base, "status_ms");
            if (!flag(base, "connected", false) || !statusMs || s.nowMs - *statusMs > kStatusFreshMs) {
                return Dot::Grey;
            }
            return flag(base, "status_ble", false) ? Dot::Green : Dot::Yellow;
        }
    }
    return Dot::Grey;
}

}  // namespace mc::status
