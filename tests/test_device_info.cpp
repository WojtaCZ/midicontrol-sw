#include <string>
#include <vector>

#include "midicontrol/device_info.hpp"
#include "test_framework.hpp"

using namespace mc;
using Bytes = std::vector<uint8_t>;

namespace {

const mc::status::Value* find(const std::vector<std::pair<std::string, mc::status::Value>>& f, const std::string& k) {
    for (const auto& [key, v] : f)
        if (key == k) return &v;
    return nullptr;
}

template <typename T>
T get(const std::vector<std::pair<std::string, mc::status::Value>>& f, const std::string& k) {
    const auto* v = find(f, k);
    if (!v || !std::holds_alternative<T>(*v)) throw mc::test::Failure{"missing or wrong type: " + k};
    return std::get<T>(*v);
}

void add(Bytes& b, uint8_t tag, const std::string& s) {
    b.push_back(tag);
    b.push_back(static_cast<uint8_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

}  // namespace

TEST(devinfo_base_fields) {
    Bytes p;
    add(p, 0x01, "c880b14");
    add(p, 0x02, "main");
    add(p, 0x03, "2026/10/25 16:03:47 UTC");
    p.insert(p.end(), {0x04, 1, 1});                          // dirty
    p.insert(p.end(), {0x05, 4, 0x40, 0x42, 0x0F, 0x00});     // uptime 1 000 000 ms
    p.insert(p.end(), {0x06, 1, 0x14});                       // PIN|SOFTWARE
    p.insert(p.end(), {0x07, 1, 2});
    p.insert(p.end(), {0x10, 1, 0x01 | 0x02 | 0x04 | 0x10 | 0x20 | 0x40});
    add(p, 0x11, "D88039FE5996");
    add(p, 0x12, "MIDIControlBase");
    p.insert(p.end(), {0x13, 1, 4});
    p.insert(p.end(), {0x20, 1, 0});
    p.insert(p.end(), {0x21, 1, 1});
    auto f = devinfo::decode(p.data(), p.size());
    CHECK_EQ(get<std::string>(f, "fw_revision"), std::string("c880b14"));
    CHECK_EQ(get<std::string>(f, "fw_branch"), std::string("main"));
    CHECK_EQ(get<std::string>(f, "fw_built"), std::string("2026/10/25 16:03:47 UTC"));
    CHECK(get<bool>(f, "fw_dirty"));
    CHECK_EQ(get<int64_t>(f, "uptime_ms"), 1000000);
    CHECK_EQ(get<std::string>(f, "reset_cause"), std::string("PIN|SOFTWARE"));
    CHECK_EQ(get<int64_t>(f, "fault_count"), 2);
    CHECK(get<bool>(f, "ble_connected"));
    CHECK(get<bool>(f, "ble_stream_open"));
    CHECK(get<bool>(f, "ble_secured"));
    CHECK(!get<bool>(f, "ble_bonded"));
    CHECK(get<bool>(f, "ble_module_up"));
    CHECK(get<bool>(f, "ble_peer_alive"));
    CHECK(get<bool>(f, "ble_pc_alive"));
    CHECK_EQ(get<std::string>(f, "ble_peer"), std::string("D8:80:39:FE:59:96"));
    CHECK_EQ(get<std::string>(f, "ble_name"), std::string("MIDIControlBase"));
    CHECK_EQ(get<std::string>(f, "ble_pairing"), std::string("done"));
    CHECK(!get<bool>(f, "display"));
    CHECK(get<bool>(f, "ssr"));
    for (const auto& [k, v] : f) {
        bool known = false;
        for (const auto& key : devinfo::fieldKeys()) known |= key == k;
        CHECK(known);
    }
}

TEST(devinfo_remote_battery_fields) {
    Bytes p = {0x30, 2, 0x8B, 0x0F,  // 3979 mV
               0x31, 1, 3, 0x32, 1, 0x01 | 0x04, 0x33, 2, 0x88, 0x13,  // 5000 mV
               0x34, 1, 81,  // 81 %
               0x06, 1, 0x00};
    auto f = devinfo::decode(p.data(), p.size());
    CHECK_EQ(get<int64_t>(f, "battery_mv"), 3979);
    CHECK_EQ(get<int64_t>(f, "battery_level"), 3);
    CHECK_EQ(get<int64_t>(f, "battery_percent"), 81);
    CHECK(get<bool>(f, "charging"));
    CHECK(!get<bool>(f, "charge_done"));
    CHECK(get<bool>(f, "vbus"));
    CHECK(!get<bool>(f, "battery_critical"));
    CHECK_EQ(get<int64_t>(f, "vbus_mv"), 5000);
    CHECK_EQ(get<std::string>(f, "reset_cause"), std::string("none"));
}

TEST(devinfo_unknown_tags_skipped_bad_lengths_ignored) {
    Bytes p = {0x7E, 3, 1, 2, 3,      // unknown tag from newer firmware
               0x05, 2, 1, 2,         // uptime with a wrong length: ignored
               0x31, 1, 4};
    auto f = devinfo::decode(p.data(), p.size());
    CHECK_EQ(f.size(), 1u);
    CHECK_EQ(get<int64_t>(f, "battery_level"), 4);
}

TEST(devinfo_truncated_payload_is_rejected) {
    Bytes p = {0x31, 1, 4, 0x01, 5, 'a', 'b'};
    CHECK(devinfo::decode(p.data(), p.size()).empty());
    Bytes q = {0x31};
    CHECK(devinfo::decode(q.data(), q.size()).empty());
}

TEST(devinfo_mac_and_pairing_names) {
    CHECK_EQ(devinfo::formatMac("D88039FE5996"), std::string("D8:80:39:FE:59:96"));
    CHECK_EQ(devinfo::formatMac("abc"), std::string("abc"));
    CHECK_EQ(std::string(devinfo::pairingName(1)), std::string("scanning"));
    CHECK_EQ(std::string(devinfo::pairingName(9)), std::string("unknown"));
}

namespace {
struct PollRig {
    std::vector<int> gets;  // Target per GET sent
    std::vector<int> clears, unsupported;
    uint8_t nextPid = 10;
    devinfo::InfoPoller poller{hooks()};
    devinfo::InfoPoller::Hooks hooks() {
        devinfo::InfoPoller::Hooks h;
        h.sendGet = [this](devinfo::InfoPoller::Target t) { gets.push_back(t); return nextPid++; };
        h.clear = [this](devinfo::InfoPoller::Target t) { clears.push_back(t); };
        h.unsupported = [this](devinfo::InfoPoller::Target t) { unsupported.push_back(t); };
        return h;
    }
};
constexpr uint8_t kUnknownMsg = 0x01, kUnreachable = 0x06;
}  // namespace

TEST(infopoller_get_on_up_edge_then_every_period) {
    PollRig r;
    r.poller.tick(0, false, false);
    CHECK(r.gets.empty());
    r.poller.tick(1000, true, false);
    CHECK(r.gets == std::vector<int>{0});
    r.poller.tick(1000 + devinfo::InfoPoller::kPeriodMs - 1, true, false);
    CHECK_EQ(r.gets.size(), 1u);
    r.poller.tick(1000 + devinfo::InfoPoller::kPeriodMs, true, true);  // base due + remote up edge
    CHECK((r.gets == std::vector<int>{0, 0, 1}));
    CHECK(r.poller.owns(11));
    CHECK(r.poller.owns(12));
    CHECK(!r.poller.owns(13));
}

TEST(infopoller_down_clears_fields) {
    PollRig r;
    r.poller.tick(0, true, true);
    r.poller.tick(100, true, false);
    CHECK(r.clears == std::vector<int>{1});
    r.poller.tick(200, false, false);
    CHECK((r.clears == std::vector<int>{1, 0}));
    r.poller.tick(300, false, false);
    CHECK_EQ(r.clears.size(), 2u);  // only on the edge
}

TEST(infopoller_unknown_msg_stops_until_next_up_edge) {
    PollRig r;
    r.poller.tick(0, false, true);  // GET id 10 to the remote
    r.poller.onNak(10, kUnknownMsg, 50);
    r.poller.onNak(10, kUnknownMsg, 60);  // duplicate NAK: reported once
    CHECK(r.unsupported == std::vector<int>{1});
    r.poller.tick(devinfo::InfoPoller::kPeriodMs * 3, false, true);
    CHECK_EQ(r.gets.size(), 1u);
    r.poller.tick(devinfo::InfoPoller::kPeriodMs * 3 + 1, false, false);
    r.poller.tick(devinfo::InfoPoller::kPeriodMs * 3 + 2, false, true);  // link back: try again
    CHECK_EQ(r.gets.size(), 2u);
}

TEST(infopoller_unreachable_retries_sooner_foreign_nak_ignored) {
    PollRig r;
    r.poller.tick(0, false, true);  // id 10
    r.poller.onNak(99, kUnreachable, 100);  // not ours
    r.poller.tick(devinfo::InfoPoller::kRetryMs + 100, false, true);
    CHECK_EQ(r.gets.size(), 1u);
    r.poller.onNak(10, kUnreachable, 100);
    r.poller.tick(devinfo::InfoPoller::kRetryMs + 100, false, true);
    CHECK_EQ(r.gets.size(), 2u);
}

TEST(devinfo_link_quality_fields) {
    Bytes p = {0x14, 2, 0xD3, 0xD0,                    // own -45 dBm, peer -48 dBm
               0x15, 6, 16, 0, 250, 0, 0, 0x33,        // 20 ms, timeout 2500 ms, latency 0, Coded both ways
               0x17, 6, 2, 1, 3, 4, 0x07, 0x10};       // losses 2, err 1, reboots 3, retries 4, reconnect 4103 ms
    auto f = devinfo::decode(p.data(), p.size());
    CHECK_EQ(get<int64_t>(f, "link_rssi"), -45);
    CHECK_EQ(get<int64_t>(f, "link_peer_rssi"), -48);
    CHECK_EQ(get<int64_t>(f, "link_interval_ms"), 20);
    CHECK_EQ(get<int64_t>(f, "link_latency"), 0);
    CHECK_EQ(get<int64_t>(f, "link_timeout_ms"), 2500);
    CHECK_EQ(get<std::string>(f, "link_phy"), std::string("Coded"));
    CHECK_EQ(get<int64_t>(f, "link_losses"), 2);
    CHECK_EQ(get<int64_t>(f, "link_err_conn"), 1);
    CHECK_EQ(get<int64_t>(f, "link_module_reboots"), 3);
    CHECK_EQ(get<int64_t>(f, "link_secure_retries"), 4);
    CHECK_EQ(get<int64_t>(f, "link_reconnect_ms"), 4103);
    for (const auto& [k, v] : f) {
        bool known = false;
        for (const auto& key : devinfo::fieldKeys()) known |= key == k;
        CHECK(known);
    }
}

TEST(devinfo_unknown_link_values_are_left_out) {
    Bytes p = {0x14, 2, 0x80, 0x80, 0x15, 6, 0, 0, 0, 0, 0, 0};
    auto f = devinfo::decode(p.data(), p.size());
    CHECK(f.empty());
}

// Protocol v2 (A1): PROTO_VER (tag 0x08, u8). A node that does not send it speaks protocol v1.
TEST(devinfo_proto_ver_is_decoded) {
    Bytes p;
    add(p, 0x01, "c880b14");
    p.insert(p.end(), {0x08, 1, 2});
    auto f = devinfo::decode(p.data(), p.size());
    CHECK_EQ(get<int64_t>(f, "proto_ver"), int64_t(2));
    bool known = false;
    for (const auto& key : devinfo::fieldKeys()) known |= key == "proto_ver";
    CHECK(known);
}

TEST(devinfo_proto_ver_with_wrong_length_is_ignored) {
    Bytes p{0x08, 2, 2, 0};
    auto f = devinfo::decode(p.data(), p.size());
    CHECK(find(f, "proto_ver") == nullptr);
}
