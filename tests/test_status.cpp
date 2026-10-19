#include <cstdint>
#include <string>
#include <vector>

#include "midicontrol/status.hpp"
#include "test_framework.hpp"

using namespace mc::status;

namespace {
struct Rig {
    int64_t t = 1'000'000;
    StatusRegistry r{[this] { return t; }};
};

int64_t num(const NodeSnapshot& n, const std::string& key) {
    auto it = n.counters.find(key);
    return it == n.counters.end() ? -1 : static_cast<int64_t>(it->second);
}

const NodeSnapshot& node(const Snapshot& s, Node n) { return s.nodes[static_cast<size_t>(n)]; }
}  // namespace

TEST(status_counters_per_node) {
    Rig g;
    g.r.count(Node::Base, "tx");
    g.r.count(Node::Base, "tx");
    g.r.count(Node::Controller, "tx");
    auto s = g.r.snapshot();
    CHECK_EQ(num(node(s, Node::Base), "tx"), 2);
    CHECK_EQ(num(node(s, Node::Controller), "tx"), 1);
    CHECK_EQ(s.nowMs, int64_t(1'000'000));
}

TEST(status_ring_wraps_at_200) {
    Rig g;
    for (int i = 0; i < 205; ++i) g.r.event(Node::Base, Level::Debug, "e" + std::to_string(i));
    auto s = g.r.snapshot();
    const auto& ev = node(s, Node::Base).events;
    CHECK_EQ(ev.size(), size_t(200));
    CHECK_EQ(ev.front().text, "e5");
    CHECK_EQ(ev.back().text, "e204");
    CHECK(ev.back().node == Node::Base);
    CHECK_EQ(ev.back().timeMs, int64_t(1'000'000));
}

TEST(status_problems_keep_warnings) {
    Rig g;
    g.r.event(Node::Base, Level::Warn, "lost");
    for (int i = 0; i < 300; ++i) g.r.event(Node::Base, Level::Debug, "x");
    auto s = g.r.snapshot();
    CHECK_EQ(node(s, Node::Base).events.size(), size_t(200));
    CHECK_EQ(s.problems.size(), size_t(1));
    CHECK_EQ(s.problems[0].text, "lost");
    CHECK_EQ(num(node(s, Node::Base), "warnings"), 1);

    for (int i = 0; i < 60; ++i) g.r.event(Node::Software, Level::Error, "e");
    s = g.r.snapshot();
    CHECK_EQ(s.problems.size(), size_t(50));
    CHECK_EQ(num(node(s, Node::Software), "errors"), 60);
}

TEST(status_snapshot_is_a_copy) {
    Rig g;
    g.r.set(Node::Base, "device", std::string("/dev/ttyACM0"));
    auto before = g.r.snapshot();
    g.r.event(Node::Base, Level::Info, "later");
    g.r.set(Node::Base, "device", std::string("/dev/ttyACM1"));
    CHECK(node(before, Node::Base).events.empty());
    CHECK(std::get<std::string>(node(before, Node::Base).fields.at("device")) == "/dev/ttyACM0");
}

TEST(status_set_now_and_songs) {
    Rig g;
    g.r.setNow(Node::Controller, "last_rx_ms");
    g.r.setSongs({{"401", ""}, {"523", "empty display anchor"}});
    auto s = g.r.snapshot();
    CHECK(std::get<int64_t>(node(s, Node::Controller).fields.at("last_rx_ms")) == 1'000'000);
    CHECK_EQ(s.songs.size(), size_t(2));
    CHECK_EQ(s.songs[1].problem, "empty display anchor");
}

TEST(status_nodes_for_peer) {
    CHECK(nodesForPeer(0x02) == std::vector<Node>{Node::Base});
    CHECK(nodesForPeer(0x03) == std::vector<Node>{Node::Controller});
    CHECK(nodesForPeer(0xFF) == (std::vector<Node>{Node::Base, Node::Controller}));
    CHECK(nodesForPeer(0x01) == std::vector<Node>{Node::Base});
}

TEST(status_names) {
    CHECK_EQ(std::string(levelName(Level::Warn)), "WARN");
    CHECK_EQ(std::string(nodeKey(Node::Controller)), "controller");
    CHECK_EQ(std::string(dotName(Dot::Grey)), "grey");
}

TEST(status_dot_base) {
    Rig g;
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Yellow);  // nothing known yet = waiting
    g.r.set(Node::Base, "connected", false);
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Yellow);
    g.r.set(Node::Base, "connected", true);
    g.r.set(Node::Base, "alive", false);
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Red);
    g.r.set(Node::Base, "alive", true);
    g.r.set(Node::Base, "status_answered", false);
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Yellow);
    g.r.set(Node::Base, "status_answered", true);
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Green);
}

TEST(status_dot_controller) {
    Rig g;
    g.r.set(Node::Base, "status_ms", g.t);
    g.r.set(Node::Base, "status_ble", true);
    CHECK(evaluateDot(Node::Controller, g.r.snapshot()) == Dot::Grey);  // base not connected
    g.r.set(Node::Base, "connected", true);
    CHECK(evaluateDot(Node::Controller, g.r.snapshot()) == Dot::Green);
    g.t += 5000;
    g.r.set(Node::Base, "status_ble", false);
    CHECK(evaluateDot(Node::Controller, g.r.snapshot()) == Dot::Yellow);
    g.t += 11001;  // 16 s old
    CHECK(evaluateDot(Node::Controller, g.r.snapshot()) == Dot::Grey);

    Rig h;
    h.r.set(Node::Base, "connected", true);
    CHECK(evaluateDot(Node::Controller, h.r.snapshot()) == Dot::Grey);  // never a STATUS_RESP
}

TEST(status_dot_software) {
    Rig g;
    CHECK(evaluateDot(Node::Software, g.r.snapshot()) == Dot::Green);
    g.r.event(Node::Base, Level::Error, "base trouble");  // other node: no effect
    CHECK(evaluateDot(Node::Software, g.r.snapshot()) == Dot::Green);
    g.r.event(Node::Software, Level::Warn, "w");
    CHECK(evaluateDot(Node::Software, g.r.snapshot()) == Dot::Yellow);
    g.t += 11 * 60 * 1000;
    CHECK(evaluateDot(Node::Software, g.r.snapshot()) == Dot::Green);
    g.r.event(Node::Software, Level::Error, "e");
    CHECK(evaluateDot(Node::Software, g.r.snapshot()) == Dot::Red);
    g.t += 11 * 60 * 1000;
    g.r.set(Node::Software, "song_problems", int64_t(1));
    CHECK(evaluateDot(Node::Software, g.r.snapshot()) == Dot::Yellow);
}

TEST(status_dot_base_updating) {
    Rig g;
    g.r.set(Node::Base, "connected", true);
    g.r.set(Node::Base, "alive", false);  // the bootloader ignores keepalives
    g.r.set(Node::Base, "updating", true);
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Yellow);
    g.r.set(Node::Base, "connected", false);  // re-enumerating
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Yellow);
    g.r.set(Node::Base, "updating", false);
    g.r.set(Node::Base, "connected", true);
    CHECK(evaluateDot(Node::Base, g.r.snapshot()) == Dot::Red);
}
