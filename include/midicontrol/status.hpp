#ifndef MIDICONTROL_STATUS_HPP
#define MIDICONTROL_STATUS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <variant>
#include <vector>

// Live status of the system as the PC sees it, for the status web page.
//
// Every thread writes into one StatusRegistry (counters, latest values, a
// ring of recent events per node); the web thread reads a Snapshot, a copy
// taken under the registry's short lock. Nothing is kept across restarts.
// The wall clock is injected so tests need no real time.
namespace mc::status {

enum class Level { Debug, Info, Warn, Error };
enum class Node { Software = 0, Base = 1, Controller = 2 };
enum class Dot { Green, Yellow, Red, Grey };

const char* levelName(Level level);  // "DEBUG", "INFO", "WARN", "ERROR"
const char* nodeKey(Node node);      // "software", "base", "controller"
const char* dotName(Dot dot);        // "green", "yellow", "red", "grey"

// A field value: unset, flag, number (timestamps are ms since the epoch) or text.
using Value = std::variant<std::monostate, bool, int64_t, std::string>;

struct Event {
    int64_t timeMs = 0;
    Level level = Level::Info;
    Node node = Node::Software;
    std::string text;
};

struct SongEntry {
    std::string code;
    std::string problem;  // empty = file is fine
};

struct NodeSnapshot {
    std::map<std::string, Value> fields;
    std::map<std::string, uint64_t> counters;
    std::vector<Event> events;  // oldest first
    size_t unseenFaults = 0;    // faults not yet acknowledged (diagnostics store)
    std::string lastFault;      // JSON of the node's last fault entry, empty = none
};

struct Snapshot {
    int64_t nowMs = 0;
    std::array<NodeSnapshot, 3> nodes;  // indexed by Node
    std::vector<Event> problems;        // WARN/ERROR from all nodes, oldest first
    std::vector<SongEntry> songs;

    const NodeSnapshot& node(Node n) const { return nodes[static_cast<size_t>(n)]; }
};

class StatusRegistry {
public:
    using Clock = std::function<int64_t()>;  // wall time, ms since the epoch

    explicit StatusRegistry(Clock now, size_t ringSize = 200, size_t problemsSize = 50);

    int64_t now() const { return _now(); }

    // Record an event. WARN/ERROR are also kept in the problems list and
    // counted in the node's "warnings" / "errors" counters.
    void event(Node node, Level level, std::string text);
    void count(Node node, const std::string& counter, uint64_t n = 1);
    void set(Node node, const std::string& field, Value value);
    void setNow(Node node, const std::string& field) { set(node, field, now()); }
    void setSongs(std::vector<SongEntry> songs);
    // Diagnostics: unacknowledged fault count and the last fault's JSON ("" = none) of a node.
    void setFaults(Node node, size_t unseen, std::string lastFaultJson);

    Snapshot snapshot() const;

private:
    struct NodeState {
        std::map<std::string, Value> fields;
        std::map<std::string, uint64_t> counters;
        std::deque<Event> events;
        size_t unseenFaults = 0;
        std::string lastFault;
    };

    Clock _now;
    size_t _ringSize;
    size_t _problemsSize;
    mutable std::mutex _mutex;
    std::array<NodeState, 3> _nodes;
    std::deque<Event> _problems;
    std::vector<SongEntry> _songs;
};

// Nodes a packet to or from `addr` counts towards: BASE -> Base,
// REMOTE -> Controller, BROADCAST -> both. Anything else (e.g. a frame that
// cannot be attributed) goes to the base, which owns the serial link.
std::vector<Node> nodesForPeer(uint8_t addr);

// Health dot of a node, from the fields the app layer keeps up to date:
//   base:     connected, alive, status_answered, status_ms, status_ble, updating
//   software: song_problems, plus its WARN/ERROR in the problems list
// Base and controller additionally turn yellow (amber) while the node has unacknowledged faults.
Dot evaluateDot(Node node, const Snapshot& s);

}  // namespace mc::status

#endif  // MIDICONTROL_STATUS_HPP
