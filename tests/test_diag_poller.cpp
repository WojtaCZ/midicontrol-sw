#include <optional>
#include <string>
#include <vector>

#include "midicontrol/diag_poller.hpp"
#include "midicontrol/dispatch.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/protocol/diag.hpp"
#include "test_framework.hpp"

using namespace mc;
using namespace mc::diag;
namespace pd = mc::protocol::diag;
using Bytes = std::vector<uint8_t>;

namespace {

struct Rig {
    std::vector<Bytes> gets;  // DIAG_GET payloads
    std::vector<DiagEntry> entries;
    uint8_t nextPid = 40;
    DiagPoller poller;

    Rig() : poller(hooks()) {}
    DiagPoller::Hooks hooks() {
        DiagPoller::Hooks h;
        h.sendGet = [this](const Bytes& p) {
            gets.push_back(p);
            return nextPid++;
        };
        h.emit = [this](DiagEntry e) { entries.push_back(std::move(e)); };
        h.pcNowMs = [] { return int64_t(1'700'000'000'000); };
        return h;
    }
    uint8_t lastPid() const { return static_cast<uint8_t>(nextPid - 1); }
    uint16_t lastCursor() const { return static_cast<uint16_t>(gets.back()[0] | (gets.back()[1] << 8)); }
};

Bytes resp(uint16_t boot, uint16_t next, const std::vector<pd::Record>& recs) {
    Bytes b = {static_cast<uint8_t>(boot), static_cast<uint8_t>(boot >> 8), static_cast<uint8_t>(next), static_cast<uint8_t>(next >> 8),
               static_cast<uint8_t>(recs.size())};
    for (const auto& r : recs) {
        uint8_t raw[pd::RECORD_SIZE];
        pd::encodeRecord(r, raw);
        b.insert(b.end(), raw, raw + pd::RECORD_SIZE);
    }
    return b;
}

pd::Record rec(uint16_t seq, pd::Code code, uint32_t arg = 0) {
    return pd::Record{seq, static_cast<uint8_t>(code), static_cast<uint8_t>(pd::Level::INFO), 1000u + seq, arg};
}

void feed(Rig& r, const Bytes& b, int64_t now) { r.poller.onResp(b.data(), b.size(), now); }

}  // namespace

TEST(comm_diag_get_on_link_up_and_every_10_seconds) {
    Rig r;
    r.poller.tick(0, false);
    CHECK_EQ(r.gets.size(), size_t(0));  // link not up yet
    r.poller.setRemoteLink(true, 100);
    CHECK_EQ(r.gets.size(), size_t(1));
    CHECK_EQ(r.lastCursor(), uint16_t(0));
    r.poller.tick(5000, false);
    CHECK_EQ(r.gets.size(), size_t(1));
    r.poller.tick(10099, false);
    CHECK_EQ(r.gets.size(), size_t(1));
    r.poller.tick(10100, false);
    CHECK_EQ(r.gets.size(), size_t(2));
    r.poller.tick(20100, false);
    CHECK_EQ(r.gets.size(), size_t(3));
    r.poller.setRemoteLink(true, 20200);  // still up: not a new link-up
    CHECK_EQ(r.gets.size(), size_t(3));
}

TEST(comm_diag_no_get_during_firmware_update) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    CHECK_EQ(r.gets.size(), size_t(1));
    r.poller.tick(10000, true);
    r.poller.tick(30000, true);
    CHECK_EQ(r.gets.size(), size_t(1));
    r.poller.tick(30100, false);  // update over: the overdue poll goes out
    CHECK_EQ(r.gets.size(), size_t(2));
}

TEST(comm_diag_link_up_during_update_defers_the_first_get) {
    Rig r;
    r.poller.setRemoteLink(true, 0, true);
    CHECK_EQ(r.gets.size(), size_t(0));
    r.poller.tick(100, true);
    CHECK_EQ(r.gets.size(), size_t(0));
    r.poller.tick(200, false);
    CHECK_EQ(r.gets.size(), size_t(1));
}

TEST(comm_diag_cursor_follows_next_cursor) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    feed(r, resp(7, 3, {rec(1, pd::Code::BOOT, 0x02), rec(2, pd::Code::BLE_CONNECT)}), 50);
    CHECK_EQ(r.entries.size(), size_t(2));
    r.poller.tick(10000, false);
    CHECK_EQ(r.gets.size(), size_t(2));
    CHECK_EQ(r.lastCursor(), uint16_t(3));
}

TEST(comm_diag_stops_after_nak_unknown_msg_until_next_link_up) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    r.poller.onNak(static_cast<uint8_t>(r.lastPid() + 5), 0x01);  // somebody else's NAK
    r.poller.tick(10000, false);
    CHECK_EQ(r.gets.size(), size_t(2));
    r.poller.onNak(r.lastPid(), 0x03);  // other reason: keep polling
    r.poller.tick(20000, false);
    CHECK_EQ(r.gets.size(), size_t(3));
    r.poller.onNak(r.lastPid(), 0x01);  // UNKNOWN_MSG: the remote has no diagnostics
    r.poller.tick(30000, false);
    r.poller.tick(60000, false);
    CHECK_EQ(r.gets.size(), size_t(3));
    r.poller.setRemoteLink(false, 61000);
    r.poller.setRemoteLink(true, 62000);
    CHECK_EQ(r.gets.size(), size_t(4));
}

TEST(comm_diag_pauses_after_nak_unreachable_until_next_link_up) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    r.poller.onNak(r.lastPid(), 0x06);  // UNREACHABLE: base cannot forward, no polling during the outage
    r.poller.tick(10000, false);
    r.poller.tick(60000, false);
    CHECK_EQ(r.gets.size(), size_t(1));
    r.poller.setRemoteLink(false, 61000);
    r.poller.setRemoteLink(true, 62000);  // next link-up edge resumes
    CHECK_EQ(r.gets.size(), size_t(2));
}

TEST(comm_diag_busy_nak_keeps_polling_on_the_timer) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    r.poller.onNak(r.lastPid(), 0x04);  // BUSY: link stays up, the next 10 s tick asks again
    r.poller.tick(5000, false);
    CHECK_EQ(r.gets.size(), size_t(1));
    r.poller.tick(10000, false);
    CHECK_EQ(r.gets.size(), size_t(2));
}

TEST(comm_diag_cursor_reset_on_boot_id_change) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    feed(r, resp(7, 9, {rec(8, pd::Code::BLE_CONNECT)}), 50);
    CHECK_EQ(r.entries.size(), size_t(1));
    // the remote rebooted: the answer is for the new boot, the old cursor means nothing there
    feed(r, resp(8, 2, {rec(1, pd::Code::BOOT, 0x02)}), 10050);
    CHECK_EQ(r.entries.size(), size_t(1));  // stale page dropped, it is fetched again from the start
    r.poller.tick(10060, false);
    CHECK_EQ(r.gets.size(), size_t(2));
    CHECK_EQ(r.lastCursor(), uint16_t(0));
    feed(r, resp(8, 2, {rec(0, pd::Code::BOOT, 0x02), rec(1, pd::Code::BLE_CONNECT)}), 10100);
    CHECK_EQ(r.entries.size(), size_t(3));
}

TEST(comm_diag_full_page_is_followed_up_at_once) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    std::vector<pd::Record> eight;
    for (uint16_t i = 0; i < 8; ++i) eight.push_back(rec(i, pd::Code::BLE_CONNECT));
    feed(r, resp(7, 8, eight), 50);
    r.poller.tick(100, false);
    CHECK_EQ(r.gets.size(), size_t(2));
    CHECK_EQ(r.lastCursor(), uint16_t(8));
}

TEST(comm_diag_follow_ups_are_capped_then_resume_on_the_timer) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    std::vector<pd::Record> eight;
    for (uint16_t i = 0; i < 8; ++i) eight.push_back(rec(i, pd::Code::BLE_CONNECT));
    int64_t now = 50;
    uint16_t next = 8;
    for (int i = 0; i < 10; ++i) {  // a remote that always has more
        feed(r, resp(7, next, eight), now);
        next = static_cast<uint16_t>(next + 8);
        r.poller.tick(now + 1, false);
        now += 2;
    }
    CHECK_EQ(r.gets.size(), size_t(1 + DiagPoller::kMaxFollowUps));
    r.poller.tick(10100, false);  // the regular timer goes on
    CHECK_EQ(r.gets.size(), size_t(2 + DiagPoller::kMaxFollowUps));
    feed(r, resp(7, next, eight), 10101);  // and a new run of follow-ups is allowed
    r.poller.tick(10102, false);
    CHECK_EQ(r.gets.size(), size_t(3 + DiagPoller::kMaxFollowUps));
}

// Review finding: after a service restart the remote's current-boot ring must not be fetched again.
TEST(comm_diag_cursor_is_persisted_and_resumed_after_restart) {
    std::optional<std::pair<uint16_t, uint16_t>> saved;
    auto mkHooks = [&](Rig& r) {
        auto h = r.hooks();
        h.loadCursor = [&] { return saved; };
        h.saveCursor = [&](uint16_t b, uint16_t c) { saved = std::make_pair(b, c); };
        return h;
    };
    {
        Rig r;
        DiagPoller p(mkHooks(r));
        p.setRemoteLink(true, 0);
        Bytes b = resp(7, 3, {rec(1, pd::Code::BOOT, 0x02), rec(2, pd::Code::BLE_CONNECT)});
        p.onResp(b.data(), b.size(), 50);
        CHECK(saved.has_value());
        CHECK_EQ(saved->first, uint16_t(7));
        CHECK_EQ(saved->second, uint16_t(3));
    }
    Rig r2;  // restarted service
    DiagPoller p2(mkHooks(r2));
    p2.setRemoteLink(true, 0);
    CHECK_EQ(r2.lastCursor(), uint16_t(3));  // resumes, not from 0
    Bytes none = resp(7, 3, {});
    p2.onResp(none.data(), none.size(), 10);
    CHECK_EQ(r2.entries.size(), size_t(0));
    // the remote rebooted meanwhile: new boot id -> from the start again
    Bytes nb = resp(8, 2, {rec(1, pd::Code::BOOT, 0x02)});
    p2.onResp(nb.data(), nb.size(), 20);
    CHECK_EQ(saved->first, uint16_t(8));
    CHECK_EQ(saved->second, uint16_t(0));
}

TEST(comm_diag_pending_fault_flushed_on_empty_response) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    feed(r, resp(7, 1, {rec(0, pd::Code::FAULT, 1)}), 50);
    CHECK_EQ(r.entries.size(), size_t(0));  // waiting for its FAULT_DETAIL records
    feed(r, resp(7, 1, {}), 10050);
    CHECK_EQ(r.entries.size(), size_t(1));
    CHECK(r.entries[0].fault.has_value());
}

TEST(comm_diag_pending_fault_flushed_on_link_down) {
    Rig r;
    r.poller.setRemoteLink(true, 0);
    feed(r, resp(7, 1, {rec(0, pd::Code::FAULT, 1)}), 50);
    CHECK_EQ(r.entries.size(), size_t(0));
    r.poller.setRemoteLink(false, 500);
    CHECK_EQ(r.entries.size(), size_t(1));
}

TEST(comm_diag_resp_is_acked_and_delivered_by_dispatcher) {
    struct Sink : CommandSink {
        std::optional<mc::protocol::NakReason> playSong(const std::string&, uint8_t) override { return std::nullopt; }
        void stopSong(uint8_t) override {}
        std::optional<mc::protocol::NakReason> addVerse() override { return std::nullopt; }
        std::vector<uint8_t> songListPage(size_t) override { return {}; }
    } sink;
    struct Sent {
        uint8_t dest;
        mc::protocol::MsgType type;
        Bytes payload;
    };
    std::vector<Sent> sent;
    std::vector<Bytes> delivered;
    Dispatcher::Output out;
    out.send = [&](uint8_t d, mc::protocol::MsgType t, const Bytes& p) { sent.push_back({d, t, p}); };
    out.onDiagResp = [&](const mc::protocol::Packet& p) { delivered.push_back(p.payload); };
    Dispatcher disp(sink, out);
    const Bytes payload = resp(7, 1, {rec(0, pd::Code::BOOT, 2)});
    mc::protocol::Packet pkt(mc::protocol::ADDR_REMOTE, mc::protocol::ADDR_PC, 77, mc::protocol::MsgType::DIAG_RESP, payload);
    disp.handle(pkt);
    CHECK_EQ(sent.size(), size_t(1));
    CHECK(sent[0].type == mc::protocol::MsgType::ACK);
    CHECK_EQ(sent[0].dest, mc::protocol::ADDR_REMOTE);
    CHECK(sent[0].payload == Bytes{77});
    CHECK_EQ(delivered.size(), size_t(1));
    disp.handle(pkt);  // retransmission: re-ACKed, not delivered twice
    CHECK_EQ(sent.size(), size_t(2));
    CHECK_EQ(delivered.size(), size_t(1));
}
