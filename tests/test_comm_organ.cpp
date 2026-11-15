// CommHandler organ-power tests against a fake base on a pty (no hardware).
// A pty plays the base: it decodes PC frames, ACKs them (or drops chosen ones),
// keeps an SSR state with the base's real dedup rule (router.cpp: one last
// PacketId per source) and answers GET_STATUS with STATUS_RESP [ble, ssr, disp, pc].
#include "test_framework.hpp"
#include "midicontrol/comm.hpp"
#include "midicontrol/config.hpp"
#include "midicontrol/status.hpp"
#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/parser.hpp"
#include "midicontrol/protocol/constants.hpp"

#include <pty.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>

using namespace mc;
using namespace mc::protocol;
using namespace std::chrono_literals;

namespace {

struct Rx { MsgType type; uint8_t pid; std::vector<uint8_t> payload; };

class FakeBase {
public:
    FakeBase() {
        char name[128];
        if (openpty(&_master, &_slave, name, nullptr, nullptr) != 0) throw std::runtime_error("openpty");
        termios t{}; tcgetattr(_slave, &t); cfmakeraw(&t); tcsetattr(_slave, TCSANOW, &t);
        _path = name;
        _thr = std::thread([this] { loop(); });
    }
    ~FakeBase() { _run = false; _thr.join(); close(_master); close(_slave); }
    const std::string& path() const { return _path; }

    std::atomic<bool> ackSetCurrentOn{true};   // false: SET_CURRENT{01} is executed, its ACK is lost
    std::atomic<bool> dropSetCurrentOff{false}; // true: SET_CURRENT{00} frame is lost (not executed, no ACK)
    std::atomic<bool> ssr{false};
    std::atomic<int> droppedOff{0};

    std::vector<Rx> log() { std::lock_guard<std::mutex> lk(_m); return _log; }

private:
    void tx(const Packet& p) { auto b = encode(p); (void)!write(_master, b.data(), b.size()); }
    void ack(const Packet& p) { tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::ACK, {p.packetId})); }

    void handle(const Packet& p) {
        { std::lock_guard<std::mutex> lk(_m); _log.push_back({p.msgType, p.packetId, p.payload}); }
        switch (p.msgType) {
        case MsgType::KEEPALIVE:
            tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::KEEPALIVE_ACK)); return;
        case MsgType::KEEPALIVE_ACK: case MsgType::ACK: case MsgType::NAK: return;
        default: break;
        }
        bool isSet = p.msgType == MsgType::SET_CURRENT && !p.payload.empty();
        if (isSet && p.payload[0] == 0 && dropSetCurrentOff) { ++droppedOff; return; }  // frame lost on the wire
        // router.cpp dedup: remember only the last PacketId from this source.
        if (_lastValid && _lastPid == p.packetId) { ack(p); return; }
        _lastPid = p.packetId; _lastValid = true;
        if (isSet) {
            ssr = p.payload[0] != 0;
            if (p.payload[0] != 0 && !ackSetCurrentOn) return;            // executed, ACK lost
            ack(p); return;
        }
        if (p.msgType == MsgType::GET_STATUS) {
            ack(p);
            tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::STATUS_RESP,
                      {0, static_cast<uint8_t>(ssr ? 1 : 0), 1, 1}));
            return;
        }
        ack(p);
    }
    void loop() {
        Parser parser;
        while (_run) {
            pollfd pf{_master, POLLIN, 0};
            if (poll(&pf, 1, 50) <= 0) continue;
            uint8_t buf[256];
            ssize_t n = read(_master, buf, sizeof buf);
            for (ssize_t i = 0; i < n; ++i)
                if (auto p = parser.feed(buf[i])) handle(*p);
        }
    }
    int _master = -1, _slave = -1;
    std::string _path;
    std::atomic<bool> _run{true};
    std::thread _thr;
    std::mutex _m;
    std::vector<Rx> _log;
    uint8_t _id = 1, _lastPid = 0;
    bool _lastValid = false;
};

Config testConfig() {
    auto dir = std::filesystem::temp_directory_path() / ("mc_comm_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    Config c;
    c.playFolder = dir.string();
    c.recFolder = dir.string();
    c.webEnabled = false;
    return c;
}

bool isSet(const Rx& r, uint8_t v) {
    return r.type == MsgType::SET_CURRENT && !r.payload.empty() && r.payload[0] == v;
}

}  // namespace

// PM-13: Play -> organOn, ACK of SET_CURRENT{01} lost, Stop -> organOff within 500 ms.
// The PC's retry of the old "on" (old PacketId) reaches the base after "off" and the base
// executes it (its dedup only knows the latest id). Expected: SSR off, no "on" after "off".
TEST(comm_pm13_superseded_on_retry_must_not_follow_off) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    base.ackSetCurrentOn = false;
    h.organOn();
    std::this_thread::sleep_for(100ms);
    h.organOff();
    std::this_thread::sleep_for(2500ms);   // > 3 retries x 500 ms
    auto log = base.log();
    const bool ssrBeforeShutdown = base.ssr.load();
    h.shutdown();
    for (auto& r : log) if (r.type == MsgType::SET_CURRENT)
        std::cout << "  base rx SET_CURRENT id=" << int(r.pid) << " [" << mc::test::toHex(r.payload) << "]\n";

    size_t offAt = log.size();
    for (size_t i = 0; i < log.size(); ++i) if (isSet(log[i], 0)) { offAt = i; break; }
    CHECK(offAt < log.size());
    int onAfterOff = 0;
    for (size_t i = offAt; i < log.size(); ++i) if (isSet(log[i], 1)) ++onAfterOff;
    CHECK_EQ(onAfterOff, 0);
    CHECK(!ssrBeforeShutdown);            // organ must be unpowered after Stop
}

namespace {
int countSet(const std::vector<Rx>& log, size_t from, uint8_t v) {
    int n = 0;
    for (size_t i = from; i < log.size(); ++i) if (isSet(log[i], v)) ++n;
    return n;
}
std::string fieldText(status::StatusRegistry& reg, status::Node n, const std::string& key) {
    auto snap = reg.snapshot();
    auto& f = snap.node(n).fields;
    auto it = f.find(key);
    if (it == f.end()) return {};
    if (auto* s = std::get_if<std::string>(&it->second)) return *s;
    return {};
}
}  // namespace

// PCC-2: organOff's SET_CURRENT{00} is lost on every try; the PC gives up. The next
// authoritative STATUS_RESP reports SSR=1. Expected: the PC re-asserts its intent with a
// new PacketId so the SSR ends off.
TEST(comm_pcc2_status_mismatch_reasserts_off) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    h.organOn();
    std::this_thread::sleep_for(300ms);
    CHECK(base.ssr.load());
    base.dropSetCurrentOff = true;
    h.organOff();
    for (int i = 0; i < 100 && base.droppedOff.load() < 4; ++i) std::this_thread::sleep_for(50ms);
    CHECK_EQ(base.droppedOff.load(), 4);  // first send + 3 retries all lost -> PC gave up
    base.dropSetCurrentOff = false;       // link healthy again
    std::this_thread::sleep_for(9000ms);  // at least one authoritative 5 s status poll
    auto log = base.log();
    const bool ssrBeforeShutdown = base.ssr.load();  // Shutdown sends organOff itself
    h.shutdown();
    for (auto& r : log) if (r.type == MsgType::SET_CURRENT || r.type == MsgType::GET_STATUS)
        std::cout << "  base rx " << msgTypeName(r.type) << " id=" << int(r.pid) << " [" << mc::test::toHex(r.payload) << "]\n";
    CHECK(!ssrBeforeShutdown);            // SSR still on with nobody playing = S1
}

// PCC-2: once the base confirmed the PC's last command, a different state reported later
// (organ switched on at the base / remote) is adopted, never fought.
TEST(comm_pcc2_confirmed_then_manual_on_is_adopted) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    h.organOn();
    std::this_thread::sleep_for(300ms);
    h.organOff();
    std::this_thread::sleep_for(6000ms);  // an authoritative poll confirms "off"
    CHECK(!base.ssr.load());
    const size_t mark = base.log().size();
    base.ssr = true;                      // organist switches the organ on at the base
    std::this_thread::sleep_for(11000ms); // two more polls report SSR=1
    auto log = base.log();
    const bool ssrBeforeShutdown = base.ssr.load();
    h.shutdown();
    CHECK_EQ(countSet(log, mark, 0), 0);  // the PC did not switch it back off
    CHECK(ssrBeforeShutdown);
}

// PCC-2: the off never gets through. The PC re-sends a bounded number of times, then says
// so loudly (status field + ERROR) instead of silently adopting "on".
TEST(comm_pcc2_gives_up_loudly) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    h.organOn();
    std::this_thread::sleep_for(300ms);
    base.dropSetCurrentOff = true;        // every off is lost, for good
    h.organOff();
    std::this_thread::sleep_for(32000ms);
    const int dropped = base.droppedOff.load();
    const std::string fault = fieldText(reg, status::Node::Base, "organ_power_fault");
    h.shutdown();
    std::cout << "  dropped offs=" << dropped << " fault=\"" << fault << "\"\n";
    CHECK(dropped > 4);                   // re-sent after the first give-up
    CHECK(dropped <= 4 * 4 + 4);          // bounded (1 + 3 re-sends, 4 frames each; slack for shutdown)
    CHECK(!fault.empty());
}
