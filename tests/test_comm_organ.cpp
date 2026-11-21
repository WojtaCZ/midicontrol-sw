// CommHandler organ-power tests against a fake base on a pty (no hardware).
// A pty plays the base: it decodes PC frames, ACKs them (or drops chosen ones),
// keeps an SSR state with the base's real dedup rule (router.cpp: one last
// PacketId per source) and answers GET_STATUS with STATUS_RESP [ble, ssr, disp, pc].
#include "test_framework.hpp"
#include "midicontrol/comm.hpp"
#include "midicontrol/console_exec.hpp"
#include "midicontrol/config.hpp"
#include "midicontrol/status.hpp"
#include "midicontrol/firmware_update.hpp"
#include "midicontrol/firmware_image.hpp"
#include "midicontrol/log.hpp"
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
#include <fstream>
#include <csignal>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>

using namespace mc;
using namespace mc::protocol;
using namespace std::chrono_literals;

namespace {

struct Rx { MsgType type; uint8_t pid; std::vector<uint8_t> payload; uint8_t dest = ADDR_BASE; };

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
    std::atomic<bool> ble{false};               // STATUS_RESP ble byte
    std::atomic<int> bootId{-1};                // >= 0: STATUS_RESP is 7 B (protocol v2) with this remote boot id; -1: 4 B (v1 base)
    std::atomic<bool> bootIdValid{true};        // flags bit0 of the 7 B form
    // Packets to the remote (P1-9): the remote ACKs, the base NAKs UNREACHABLE, or the frame is lost.
    enum RemoteMode { RemoteAck, RemoteUnreachable, RemoteDrop };
    std::atomic<int> remoteMode{RemoteAck};
    std::atomic<int> dropAnchors{0};   // the next N SIGNAL_ANCHOR frames are lost on the air
    std::atomic<int> dropPlaying{0};   // the next N SIGNAL_PLAYING frames are lost on the air
    std::atomic<int> busyRemote{0};    // the next N frames to the remote (retries included) get NAK BUSY
    // Item 4b: a STATUS_RESP is held back until the next GET_STATUS arrives; then both answers go out
    // back to back (the late one first). Each answer carries the SSR state of its own GET_STATUS.
    std::atomic<bool> holdStatus{false};
    // Item 4b follow-up: the next GET_STATUS (that PacketId, retries included) is lost on the wire.
    std::atomic<int> loseNextStatus{0};
    std::atomic<int> lostStatusPid{-1};
    std::atomic<int> statusAnswers{0};
    std::atomic<int> statusPairs{0};
    std::atomic<int> protoVer{0};      // > 0: GET_INFO is answered with an INFO_RESP carrying PROTO_VER
    std::atomic<int> remoteProtoVer{0};  // > 0: the remote answers GET_INFO with an INFO_RESP carrying PROTO_VER (A6)
    std::atomic<int> organSrc{-1};     // >= 0: that INFO_RESP also carries ORGAN_SRC (A5); -1 = a v1 base (SSR tag only)

    // A4: a remote that takes a firmware transfer. 0 = off; 1 = accepts it and sends every FW_UPDATE_STATUS
    // twice with the same PacketId (the receiver's retry after a lost ACK); 2 = like 1 but DATA is NAKed
    // NO_TRANSFER (the remote lost its transfer).
    std::atomic<int> remoteFw{0};

    std::vector<Rx> log() { std::lock_guard<std::mutex> lk(_m); return _log; }

private:
    void tx(const Packet& p) { auto b = encode(p); (void)!write(_master, b.data(), b.size()); }
    void fwStatus(uint8_t st, uint8_t pid) {
        for (int i = 0; i < 2; ++i) tx(Packet(ADDR_REMOTE, ADDR_PC, pid, MsgType::FW_UPDATE_STATUS, {st}));
    }
    bool fwFrame(const Packet& p) {
        switch (p.msgType) {
        case MsgType::FW_UPDATE_START:
            tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::ACK, {p.packetId}));
            fwStatus(static_cast<uint8_t>(FwStatus::READY), 71);
            return true;
        case MsgType::FW_UPDATE_DATA:
            if (remoteFw == 2) {
                tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::NAK,
                          {p.packetId, static_cast<uint8_t>(NakReason::NO_TRANSFER)}));
                return true;
            }
            tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::ACK, {p.packetId}));
            return true;
        case MsgType::FW_UPDATE_END:
            tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::ACK, {p.packetId}));
            fwStatus(static_cast<uint8_t>(FwStatus::CRC_OK), 77);
            return true;
        default: return false;
        }
    }
    void ack(const Packet& p) { tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::ACK, {p.packetId})); }

    void handle(const Packet& p) {
        { std::lock_guard<std::mutex> lk(_m); _log.push_back({p.msgType, p.packetId, p.payload, p.dest}); }
        switch (p.msgType) {
        case MsgType::KEEPALIVE:
            tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::KEEPALIVE_ACK)); return;
        case MsgType::KEEPALIVE_ACK: case MsgType::ACK: case MsgType::NAK: return;
        default: break;
        }
        if (p.dest == ADDR_REMOTE) {  // forwarded, never executed by the base (own dedup: the remote's)
            if (p.msgType == MsgType::SIGNAL_ANCHOR && dropAnchors > 0) { --dropAnchors; return; }
            if (p.msgType == MsgType::SIGNAL_PLAYING && dropPlaying > 0) { --dropPlaying; return; }
            if (remoteFw > 0 && fwFrame(p)) return;
            if (p.msgType == MsgType::GET_INFO && remoteProtoVer > 0) {
                tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::ACK, {p.packetId}));
                tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::INFO_RESP,
                          {0x08, 1, static_cast<uint8_t>(remoteProtoVer.load())}));
                return;
            }
            if (busyRemote > 0) {
                --busyRemote;
                tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::NAK, {p.packetId, static_cast<uint8_t>(NakReason::BUSY)}));
                return;
            }
            if (remoteMode == RemoteAck) tx(Packet(ADDR_REMOTE, ADDR_PC, _id++, MsgType::ACK, {p.packetId}));
            else if (remoteMode == RemoteUnreachable)
                tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::NAK,
                          {p.packetId, static_cast<uint8_t>(NakReason::UNREACHABLE)}));
            return;
        }
        if (p.msgType == MsgType::GET_STATUS) {
            if (lostStatusPid.load() == p.packetId) return;                 // a retry of the lost request
            if (loseNextStatus > 0) { --loseNextStatus; lostStatusPid = p.packetId; return; }
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
            std::vector<uint8_t> st{static_cast<uint8_t>(ble ? 1 : 0), static_cast<uint8_t>(ssr ? 1 : 0), 1, 1};
            if (bootId >= 0) {
                st.push_back(bootIdValid ? 1 : 0);
                st.push_back(static_cast<uint8_t>(bootId & 0xFF));
                st.push_back(static_cast<uint8_t>(bootId >> 8));
            }
            if (holdStatus) {
                if (!_held) { _held = st; return; }
                tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::STATUS_RESP, *_held));
                _held.reset();
                tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::STATUS_RESP, st));
                ++statusPairs;
                return;
            }
            tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::STATUS_RESP, st));
            ++statusAnswers;
            return;
        }
        if (p.msgType == MsgType::GET_INFO) {
            ack(p);
            // A v1 base answers GET_INFO too (no PROTO_VER, no ORGAN_SRC); the SSR tag keeps the payload non-empty
            std::vector<uint8_t> info{0x21, 1, static_cast<uint8_t>(ssr ? 1 : 0)};
            if (protoVer > 0) info.insert(info.end(), {0x08, 1, static_cast<uint8_t>(protoVer.load())});
            if (organSrc >= 0) info.insert(info.end(), {0x22, 1, static_cast<uint8_t>(organSrc.load())});
            tx(Packet(ADDR_BASE, ADDR_PC, _id++, MsgType::INFO_RESP, info));
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
    std::optional<std::vector<uint8_t>> _held;
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

// ---------------------------------------------------------------------------
// Q1-1 b (owner 2026-11-14) + PCC-3: startup reset with the SSR found ON.
// The MIDI finder reports an output index that does not exist (9999): the
// notes-off is attempted (and fails harmlessly), never on a real port.
// ---------------------------------------------------------------------------
namespace {
std::optional<MidiPorts> fakePorts() {
    MidiPorts p;
    p.outIndex = 9999;
    p.inIndex = 9999;
    p.outName = "fake MIDIControl";
    p.inName = "fake MIDIControl";
    return p;
}
bool hasEvent(status::StatusRegistry& reg, status::Node n, const std::string& part) {
    auto snap = reg.snapshot();  // keep the copy alive while iterating
    for (auto& e : snap.node(n).events) if (e.text.find(part) != std::string::npos) return true;
    return false;
}
bool fieldBool(status::StatusRegistry& reg, status::Node n, const std::string& key) {
    auto snap = reg.snapshot();
    auto& f = snap.node(n).fields;
    auto it = f.find(key);
    if (it == f.end()) return false;
    if (auto* b = std::get_if<bool>(&it->second)) return *b;
    return false;
}
size_t countSetV(const std::vector<Rx>& log, uint8_t v) {
    size_t n = 0;
    for (auto& r : log) if (isSet(r, v)) ++n;
    return n;
}
}  // namespace

// PC restarts in < 6 s mid-song: the base loss guard never fired, the SSR is on and notes may sound.
// Expected: All Notes Off sent, organ-off NOT sent, idle-off timer armed.
TEST(comm_q11b_ssr_on_at_start_notes_off_no_power_off_idle_armed) {
    FakeBase base;
    base.ssr = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    mc::log::setRegistry(&reg);
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(8000ms);   // first keepalive ACK + first STATUS_RESP + retries
    auto log = base.log();
    const bool ssr = base.ssr.load();
    const bool notes = hasEvent(reg, status::Node::Software, "Startup: All Notes Off");
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    h.shutdown();
    mc::log::setRegistry(nullptr);
    std::cout << "  notes-off attempted=" << notes << " SET_CURRENT{00}=" << countSetV(log, 0)
              << " ssr=" << ssr << " idle armed=" << armed << "\n";
    CHECK(notes);
    CHECK_EQ(countSetV(log, 0), size_t(0));
    CHECK(ssr);
    CHECK(armed);
}

// A5 (Q2-33): ORGAN_SRC tells a PC-owned power-on (crashed session) from a manual one; a v1 base has no tag.
namespace {
std::string startSource(status::StatusRegistry& reg) {
    auto snap = reg.snapshot();
    auto& f = snap.node(status::Node::Base).fields;
    auto it = f.find("organ_start_source");
    if (it == f.end()) return {};
    const auto* s = std::get_if<std::string>(&it->second);
    return s ? *s : std::string{};
}
}  // namespace

TEST(comm_a5_organ_src_pc_is_treated_as_own_idle_off_armed_not_claimed) {
    FakeBase base;
    base.ssr = true;
    base.protoVer = 2;
    base.organSrc = 1;
    status::StatusRegistry reg([] { return int64_t(0); });
    mc::log::setRegistry(&reg);
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(8000ms);
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    const bool byPc = hasEvent(reg, status::Node::Software, "by the PC (previous session)");
    const std::string src = startSource(reg);
    auto log = base.log();
    h.shutdown();
    mc::log::setRegistry(nullptr);
    CHECK(armed);
    CHECK(byPc);
    CHECK_EQ(src, std::string("pc"));
    CHECK_EQ(countSetV(log, 0), size_t(0));
}

TEST(comm_a5_organ_src_menu_is_a_manual_switch_on_as_today) {
    FakeBase base;
    base.ssr = true;
    base.protoVer = 2;
    base.organSrc = 2;
    status::StatusRegistry reg([] { return int64_t(0); });
    mc::log::setRegistry(&reg);
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(8000ms);
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    const bool byPc = hasEvent(reg, status::Node::Software, "by the PC (previous session)");
    const std::string src = startSource(reg);
    h.shutdown();
    mc::log::setRegistry(nullptr);
    CHECK(armed);
    CHECK(!byPc);
    CHECK_EQ(src, std::string("menu"));
}

TEST(comm_a5_v1_base_without_the_tag_behaves_as_before) {
    FakeBase base;
    base.ssr = true;   // protoVer 0, organSrc -1: INFO_RESP without PROTO_VER / ORGAN_SRC
    status::StatusRegistry reg([] { return int64_t(0); });
    mc::log::setRegistry(&reg);
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(8000ms);
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    const bool byPc = hasEvent(reg, status::Node::Software, "by the PC (previous session)");
    const std::string src = startSource(reg);
    h.shutdown();
    mc::log::setRegistry(nullptr);
    CHECK(armed);
    CHECK(!byPc);
    CHECK_EQ(src, std::string("v1"));
}

// The idle-off timer switches the organ off once it expires (short timer for the test).
TEST(comm_q11b_idle_timer_switches_the_found_on_organ_off) {
    FakeBase base;
    base.ssr = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.organIdleOffMs = 4000;
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(3000ms);   // reset done (armed), timer not expired yet
    const size_t offsEarly = countSetV(base.log(), 0);
    std::this_thread::sleep_for(7000ms);
    auto log = base.log();
    const bool ssr = base.ssr.load();
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    h.shutdown();
    std::cout << "  offs early=" << offsEarly << " offs=" << countSetV(log, 0) << " ssr=" << ssr << "\n";
    CHECK_EQ(offsEarly, size_t(0));         // not before the reset decided and the timer ran
    CHECK_EQ(countSetV(log, 0), size_t(1));
    CHECK(!ssr);
    CHECK(!armed);
}

// SSR reported off at start: notes-off and the organ-off as before (no idle timer).
TEST(comm_q11b_ssr_off_at_start_still_powers_off) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(6000ms);
    auto log = base.log();
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    h.shutdown();
    CHECK_EQ(countSetV(log, 0), size_t(1));
    CHECK(!armed);
}

// A PC command takes the organ over: the idle timer is disarmed (no surprise off).
TEST(comm_q11b_pc_command_disarms_the_idle_timer) {
    FakeBase base;
    base.ssr = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.organIdleOffMs = 4000;
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(3000ms);
    const bool armedBefore = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    h.organOn();                           // operator: on (e.g. web/console)
    std::this_thread::sleep_for(6000ms);
    auto log = base.log();
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    h.shutdown();
    CHECK(armedBefore);
    CHECK(!armed);
    // only the shutdown's off may appear, none from the idle timer before it
    CHECK(countSetV(log, 0) == 0);
}

// PCC-3: after the base link was lost and is back, the startup reset runs again: notes off, then the SSR rule
// (here: the organ is on at the new link -> left on, idle timer armed). A symlink stands for the device node
// so the pty behind it can disappear and come back.
TEST(comm_pcc3_reconnect_reruns_the_startup_reset) {
    status::StatusRegistry reg([] { return int64_t(0); });
    mc::log::setRegistry(&reg);
    Config cfg = testConfig();
    const auto link = std::filesystem::path(cfg.playFolder) / "fake-base-tty";
    std::filesystem::remove(link);
    auto base1 = std::make_unique<FakeBase>();
    std::filesystem::create_symlink(base1->path(), link);
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(link.string()));
    std::this_thread::sleep_for(7000ms);   // first startup reset: SSR off -> power off, confirmed by a poll
    auto countNotes = [&] {
        int n = 0;
        auto snap = reg.snapshot();
        for (auto& e : snap.node(status::Node::Software).events)
            if (e.text.find("Startup: All Notes Off") != std::string::npos) ++n;
        return n;
    };
    const int notesFirst = countNotes();
    base1.reset();                          // the base goes away (pty hung up)
    std::filesystem::remove(link);
    std::this_thread::sleep_for(1500ms);
    FakeBase base2;
    base2.ssr = true;                       // the organ is on when the link comes back
    std::filesystem::create_symlink(base2.path(), link);
    std::this_thread::sleep_for(9000ms);
    auto log2 = base2.log();
    const int notesTotal = countNotes();
    const bool armed = fieldBool(reg, status::Node::Base, "organ_idle_off_armed");
    h.shutdown();
    mc::log::setRegistry(nullptr);
    std::filesystem::remove(link);
    std::cout << "  notes-off first=" << notesFirst << " total=" << notesTotal << " offs at base2="
              << countSetV(log2, 0) << " armed=" << armed << " base2 rx=" << log2.size() << "\n";
    CHECK_EQ(notesFirst, 1);
    CHECK_EQ(notesTotal, 2);
    CHECK_EQ(countSetV(log2, 0), size_t(0));
    CHECK(armed);
}

// PCC-3 / PCC-2 across a link loss: an organ "on" the PC sent just before the loss and never saw confirmed
// must not be re-sent after the reconnect (the player was stopped by the loss; the base's loss guard may have
// switched the organ off on purpose).
TEST(comm_pcc3_unconfirmed_on_is_not_reasserted_after_reconnect) {
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    const auto link = std::filesystem::path(cfg.playFolder) / "fake-base-tty2";
    std::filesystem::remove(link);
    auto base1 = std::make_unique<FakeBase>();
    std::filesystem::create_symlink(base1->path(), link);
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(link.string()));
    std::this_thread::sleep_for(300ms);
    h.organOn();                            // e.g. a song starts ...
    std::this_thread::sleep_for(300ms);
    base1.reset();                          // ... and the link drops before a poll confirmed it
    std::filesystem::remove(link);
    std::this_thread::sleep_for(1500ms);
    FakeBase base2;                         // the base's loss guard switched the organ off
    std::filesystem::create_symlink(base2.path(), link);
    std::this_thread::sleep_for(9000ms);
    auto log2 = base2.log();
    h.shutdown();
    std::filesystem::remove(link);
    std::cout << "  SET_CURRENT{01} at base2=" << countSetV(log2, 1) << " base2 rx=" << log2.size() << "\n";
    CHECK(log2.size() > 0);
    CHECK_EQ(countSetV(log2, 1), size_t(0));
}

// Fast PC restart: the base de-duplicates by the last PacketId per source. If every PC process starts its ids
// at 0, a restarted PC repeats the old process's ids and the base can drop its first real command as a
// "duplicate" (ACK, not executed). The first id must differ between processes (random seed).
TEST(comm_fast_restart_packet_ids_do_not_restart_at_the_same_value) {
    std::vector<int> firstIds;
    for (int i = 0; i < 4; ++i) {
        FakeBase base;
        status::StatusRegistry reg([] { return int64_t(0); });
        Config cfg = testConfig();
        CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
        CHECK(h.connect(base.path()));
        std::this_thread::sleep_for(300ms);
        auto log = base.log();
        h.shutdown();
        firstIds.push_back(log.empty() ? -1 : log.front().pid);
    }
    std::cout << "  first PacketIds:";
    for (int id : firstIds) std::cout << " " << id;
    std::cout << "\n";
    bool allSame = true;
    for (int id : firstIds) allSame = allSame && id == firstIds.front();
    CHECK(firstIds.front() >= 0);
    CHECK(!allSame);  // random seeds: all 4 equal by chance with p = 1/256^3
}

// ---- PCC-1: recording guard (fake arecordmidi on PATH; it logs "<pid> <file>" and sleeps) ----
namespace {
struct FakeRecorder {
    std::filesystem::path dir, recDir, log;
    std::string oldPath;
    explicit FakeRecorder(bool exitAtOnce = false) {
        dir = std::filesystem::temp_directory_path() / ("mc_rec_test_" + std::to_string(getpid()));
        std::filesystem::remove_all(dir);
        recDir = dir / "rec";
        std::filesystem::create_directories(recDir);
        log = dir / "spawned.log";
        auto script = dir / "arecordmidi";
        {
            std::ofstream s(script);
            s << "#!/bin/sh\necho \"$$ $3\" >> '" << log.string() << "'\n"
              << (exitAtOnce ? "exit 1\n" : "exec sleep 30\n");
        }
        std::filesystem::permissions(script, std::filesystem::perms::owner_all);
        const char* p = getenv("PATH");
        oldPath = p ? p : "";
        setenv("PATH", (dir.string() + ":" + oldPath).c_str(), 1);
    }
    ~FakeRecorder() {
        for (auto& l : lines()) kill(std::stoi(l.substr(0, l.find(' '))), SIGKILL);  // no orphan survives a red run
        setenv("PATH", oldPath.c_str(), 1);
        std::filesystem::remove_all(dir);
    }
    std::vector<std::string> lines() const {
        std::vector<std::string> out;
        std::ifstream f(log);
        for (std::string l; std::getline(f, l);) if (!l.empty()) out.push_back(l);
        return out;
    }
};
// The ALSA token "9999:0" cannot resolve: should the fake script ever be bypassed, the real
// arecordmidi fails instead of opening the real MIDIControl port.
std::optional<MidiPorts> recPorts() {
    auto p = fakePorts();
    p->inName = "fake 9999:0";
    return p;
}
}  // namespace

// A second Record while one runs must not start a second arecordmidi (the first would be orphaned).
TEST(comm_pcc1_second_record_is_refused_while_recording) {
    FakeRecorder fr;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.recFolder = fr.recDir.string();
    cfg.warmupMs = 0;
    CommHandler h(cfg, [] { return recPorts(); }, reg);
    CHECK(h.recordSong());
    CHECK(!h.recordSong());  // refused: "Already recording"
    std::this_thread::sleep_for(500ms);
    auto spawned = fr.lines();
    std::cout << "  arecordmidi started: " << spawned.size() << "\n";
    CHECK_EQ(spawned.size(), size_t(1));
    h.shutdown();
}

// Record, Stop, Record within one second: the second recording gets its own file.
TEST(comm_pcc1_records_in_the_same_second_get_unique_files) {
    FakeRecorder fr;
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.recFolder = fr.recDir.string();
    cfg.warmupMs = 0;
    CommHandler h(cfg, [] { return recPorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(100ms);
    // Start at the beginning of a wall-clock second so both records fall into the same one.
    auto now = std::chrono::system_clock::now().time_since_epoch();
    std::this_thread::sleep_for(1000ms - std::chrono::duration_cast<std::chrono::milliseconds>(now) % 1000ms + 20ms);
    h.recordSong();
    std::this_thread::sleep_for(100ms);
    h.stopSong();
    std::this_thread::sleep_for(150ms);
    h.recordSong();
    std::this_thread::sleep_for(150ms);
    auto spawned = fr.lines();
    std::vector<std::string> files;
    for (auto& l : spawned) files.push_back(l.substr(l.find(' ') + 1));
    for (auto& f : files) std::cout << "  file: " << f << "\n";
    CHECK_EQ(files.size(), size_t(2));
    CHECK(files.size() == 2 && files[0] != files[1]);
    h.shutdown();
}

// Stop pressed while a Record waits for the organ warm-up: no recorder may start afterwards.
TEST(comm_pcc1_stop_during_warmup_cancels_the_record) {
    FakeRecorder fr;
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.recFolder = fr.recDir.string();
    cfg.warmupMs = 1500;
    CommHandler h(cfg, [] { return recPorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(100ms);
    std::thread rec([&] { h.recordSong(); });
    std::this_thread::sleep_for(300ms);
    h.stopSong();
    rec.join();
    std::this_thread::sleep_for(200ms);
    auto spawned = fr.lines();
    std::cout << "  arecordmidi started after Stop: " << spawned.size() << "\n";
    CHECK_EQ(spawned.size(), size_t(0));
    h.shutdown();
}

// A recorder that dies on its own (bad port, ALSA port gone) must not block the next Record.
TEST(comm_pcc1_dead_recorder_does_not_block_the_next_record) {
    FakeRecorder fr(/*exitAtOnce=*/true);
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.recFolder = fr.recDir.string();
    cfg.warmupMs = 0;
    CommHandler h(cfg, [] { return recPorts(); }, reg);
    CHECK(h.recordSong());
    std::this_thread::sleep_for(500ms);  // the fake recorder has exited
    const bool second = h.recordSong();
    std::this_thread::sleep_for(300ms);
    std::cout << "  second Record accepted: " << second << ", started: " << fr.lines().size() << "\n";
    CHECK(second);
    CHECK_EQ(fr.lines().size(), size_t(2));
    h.shutdown();
}

// Organ safety (Q1-1 b): a dead recorder must not keep the found-on organ from the idle power-off.
TEST(comm_pcc1_dead_recorder_does_not_block_idle_off) {
    FakeRecorder fr(/*exitAtOnce=*/true);
    FakeBase base;
    base.ssr = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    cfg.recFolder = fr.recDir.string();
    cfg.warmupMs = 0;
    cfg.organIdleOffMs = 4000;
    CommHandler h(cfg, [] { return recPorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(3000ms);  // reset done, idle timer armed
    CHECK(h.recordSong());                 // restarts the timer; the recorder exits at once
    std::this_thread::sleep_for(8000ms);
    auto log = base.log();
    const bool ssr = base.ssr.load();
    h.shutdown();
    std::cout << "  offs=" << countSetV(log, 0) << " ssr=" << ssr << "\n";
    CHECK_EQ(countSetV(log, 0), size_t(1));
    CHECK(!ssr);
}

// ---------------------------------------------------------------------------
// P1-9 remote resync (shared design, PC side). Songs play into a MIDI sink: no
// RtMidi port is ever opened. The fake base's remote ACKs / NAKs / drops.
// ---------------------------------------------------------------------------
namespace {
bool isSignal(MsgType t) {
    return t == MsgType::SIGNAL_PLAYING || t == MsgType::SIGNAL_STOPPED || t == MsgType::SIGNAL_ANCHOR ||
           t == MsgType::SIGNAL_RECORDING;
}
size_t countType(const std::vector<Rx>& log, size_t from, MsgType t) {
    size_t n = 0;
    for (size_t i = from; i < log.size(); ++i) if (log[i].type == t && log[i].dest == ADDR_REMOTE) ++n;
    return n;
}
size_t countSignals(const std::vector<Rx>& log, size_t from) {
    size_t n = 0;
    for (size_t i = from; i < log.size(); ++i) if (isSignal(log[i].type)) ++n;
    return n;
}
void dumpSignals(const std::vector<Rx>& log, size_t from) {
    for (size_t i = from; i < log.size(); ++i) if (isSignal(log[i].type))
        std::cout << "  [" << i << "] " << msgTypeName(log[i].type) << " id=" << int(log[i].pid) << " \""
                  << std::string(log[i].payload.begin(), log[i].payload.end()) << "\"\n";
}
std::vector<uint8_t> lastPayload(const std::vector<Rx>& log, size_t to, MsgType t) {
    std::vector<uint8_t> out;
    for (size_t i = 0; i < to && i < log.size(); ++i) if (log[i].type == t) out = log[i].payload;
    return out;
}
Config songConfig() {
    Config c = testConfig();
    c.warmupMs = 0;
    std::filesystem::copy_file(MC_SONGS_DIR "/406A.mid", std::filesystem::path(c.playFolder) / "406A.mid",
                               std::filesystem::copy_options::overwrite_existing);
    return c;
}
// One STATUS_RESP, then give the control thread time to act on it.
void statusNow(CommHandler& h) {
    CHECK(h.requestStatus().has_value());
    std::this_thread::sleep_for(400ms);
}
template <class F> bool waitFor(F f, int ms) {
    for (int i = 0; i < ms / 50; ++i) { if (f()) return true; std::this_thread::sleep_for(50ms); }
    return f();
}
}  // namespace

// Drop mid-song: ble 1 -> 0 -> 1 while a song plays. The PC re-sends SIGNAL_PLAYING <code> and the last
// SIGNAL_ANCHOR once; the remote ACKs; a later ble=1 status sends nothing more.
TEST(comm_p19_drop_mid_song_resends_playing_and_anchor_once) {
    FakeBase base;
    base.ble = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    statusNow(h);                                    // remote known up (and told "stopped" once at PC start)
    CHECK(!h.playSong("406A").has_value());
    CHECK(waitFor([&] { return h.isPlaying() && countType(base.log(), 0, MsgType::SIGNAL_ANCHOR) > 0; }, 15000));
    base.ble = false;                                // remote drops out
    base.remoteMode = FakeBase::RemoteUnreachable;
    statusNow(h);
    const size_t mark = base.log().size();
    const auto anchorBefore = lastPayload(base.log(), mark, MsgType::SIGNAL_ANCHOR);
    base.ble = true;                                 // back
    base.remoteMode = FakeBase::RemoteAck;
    statusNow(h);
    const bool playing = h.isPlaying();
    const size_t mark2 = base.log().size();
    statusNow(h);                                    // clean now: nothing
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    CHECK(playing);                                  // the song really played (fail() would send STOPPED)
    CHECK_EQ(countType(log, mark, MsgType::SIGNAL_PLAYING), size_t(1));
    CHECK_EQ(countType(log, mark, MsgType::SIGNAL_STOPPED), size_t(0));
    bool anchorResent = false;
    for (size_t i = mark; i < mark2; ++i) if (log[i].type == MsgType::SIGNAL_ANCHOR && log[i].payload == anchorBefore) anchorResent = true;
    CHECK(anchorResent);
    CHECK_EQ(countType(log, mark2, MsgType::SIGNAL_PLAYING), size_t(0));
}

// NAK UNREACHABLE leaves the remote dirty: every further ble=1 status tries once more until it is ACKed.
TEST(comm_p19_unreachable_is_retried_on_next_ble1) {
    FakeBase base;
    base.ble = true;
    base.remoteMode = FakeBase::RemoteUnreachable;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    std::vector<size_t> perStatus;
    for (int i = 0; i < 4; ++i) {
        if (i == 2) base.remoteMode = FakeBase::RemoteAck;
        const size_t m = base.log().size();
        statusNow(h);
        perStatus.push_back(countType(base.log(), m, MsgType::SIGNAL_STOPPED));
    }
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    std::cout << "  STOPPED per status: " << perStatus[0] << " " << perStatus[1] << " " << perStatus[2] << " "
              << perStatus[3] << "\n";
    CHECK_EQ(perStatus[0], size_t(1));  // PC start: the remote may still show the old run's song
    CHECK_EQ(perStatus[1], size_t(1));  // NAKed -> again
    CHECK_EQ(perStatus[2], size_t(1));  // ACKed now
    CHECK_EQ(perStatus[3], size_t(0));  // clean
}

// NAK BUSY on a SIGNAL_* is retried like a timeout (SYSTEM.md protocol rule, PROTOCOL.md): same PacketId, same
// retry budget, and an ACK on a retry leaves the remote clean. Bench 2026-11-15 (LAT-7): a BUSY was not retried,
// so the remote showed the old state until the next 5 s status poll resynced it.
TEST(comm_signal_nak_busy_is_retried_with_the_same_id) {
    FakeBase base;
    base.ble = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    base.busyRemote = 1;                              // the PC start resync (SIGNAL_STOPPED) gets NAK BUSY once
    const size_t m = 0;
    CHECK(h.connect(base.path()));
    CHECK(waitFor([&] { return countType(base.log(), m, MsgType::SIGNAL_STOPPED) >= 2; }, 2000));
    std::this_thread::sleep_for(300ms);
    const size_t m2 = base.log().size();
    statusNow(h);                                     // ACKed on the retry: clean, nothing more
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    std::set<int> ids;
    for (size_t i = m; i < m2; ++i) if (log[i].type == MsgType::SIGNAL_STOPPED) ids.insert(log[i].pid);
    CHECK_EQ(countType(log, m, MsgType::SIGNAL_STOPPED) - countType(log, m2, MsgType::SIGNAL_STOPPED), size_t(2));
    CHECK_EQ(ids.size(), size_t(1));                  // the retry kept the PacketId
    CHECK_EQ(countType(log, m2, MsgType::SIGNAL_STOPPED), size_t(0));
}

// BUSY on every attempt: 1 send + MAX_RETRIES retries, then the remote is dirty and the next ble=1 status resyncs.
TEST(comm_signal_nak_busy_gives_up_after_the_retry_budget) {
    FakeBase base;
    base.ble = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    base.busyRemote = 1000;
    const size_t m = 0;
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(std::chrono::milliseconds((MAX_RETRIES + 1) * RETRY_TIMEOUT_MS + 500));
    const size_t m2 = base.log().size();
    base.busyRemote = 0;
    statusNow(h);                                     // dirty: one more resync, ACKed
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    CHECK_EQ(countType(log, m, MsgType::SIGNAL_STOPPED) - countType(log, m2, MsgType::SIGNAL_STOPPED),
             size_t(MAX_RETRIES + 1));
    CHECK_EQ(countType(log, m2, MsgType::SIGNAL_STOPPED), size_t(1));
}

// An idle remote that knows it is stopped gets nothing on a ble edge; a STOPPED lost on the air is re-sent once.
TEST(comm_p19_idle_clean_gets_nothing_lost_stopped_is_resent) {
    FakeBase base;
    base.ble = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    statusNow(h);                                     // one STOPPED at PC start, ACKed
    const size_t startStopped = countType(base.log(), 0, MsgType::SIGNAL_STOPPED);
    base.ble = false;
    statusNow(h);
    const size_t m1 = base.log().size();
    base.ble = true;                                  // edge, but idle and clean
    statusNow(h);
    const size_t idleSignals = countSignals(base.log(), m1);
    CHECK(!h.playSong("406A").has_value());
    CHECK(waitFor([&] { return h.isPlaying(); }, 5000));
    std::this_thread::sleep_for(300ms);
    base.remoteMode = FakeBase::RemoteDrop;          // STOPPED and its 3 retries are lost
    h.stopSong();
    std::this_thread::sleep_for(2800ms);
    base.remoteMode = FakeBase::RemoteAck;
    const size_t m2 = base.log().size();
    statusNow(h);
    const size_t m3 = base.log().size();
    statusNow(h);
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    CHECK_EQ(startStopped, size_t(1));
    CHECK_EQ(idleSignals, size_t(0));
    CHECK_EQ(countType(log, m2, MsgType::SIGNAL_STOPPED), size_t(1));
    CHECK_EQ(countSignals(log, m3), size_t(0));
}

// No resync while a firmware update runs; the first ble=1 status after it does it.
TEST(comm_p19_no_resync_during_update) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    statusNow(h);                                     // ble=0
    h.setUpdating(true);
    base.ble = true;
    statusNow(h);
    statusNow(h);
    const size_t during = countSignals(base.log(), 0);
    h.setUpdating(false);
    const size_t m = base.log().size();
    statusNow(h);
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    CHECK_EQ(during, size_t(0));
    CHECK_EQ(countType(log, m, MsgType::SIGNAL_STOPPED), size_t(1));
}

// Item 2: an un-ACKed SIGNAL_ANCHOR must not be retried after SIGNAL_STOPPED (the remote would show a
// section of a song that has ended). STOPPED / PLAYING / RECORDING supersede pending ANCHORs.
TEST(comm_item2_stopped_supersedes_pending_anchor) {
    FakeBase base;
    base.ble = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    statusNow(h);                                     // PC-start STOPPED done
    base.dropAnchors = 100;
    CHECK(!h.playSong("406A").has_value());
    CHECK(waitFor([&] { return countType(base.log(), 0, MsgType::SIGNAL_ANCHOR) > 0; }, 5000));
    const bool playing = h.isPlaying();
    h.stopSong();
    std::this_thread::sleep_for(2500ms);              // > 3 retries x 500 ms
    auto log = base.log();
    h.shutdown();
    dumpSignals(log, 0);
    size_t stopAt = log.size();
    for (size_t i = 0; i < log.size(); ++i)
        if (log[i].type == MsgType::SIGNAL_STOPPED && countType(log, 0, MsgType::SIGNAL_PLAYING) > 0 &&
            countType(std::vector<Rx>(log.begin(), log.begin() + i), 0, MsgType::SIGNAL_PLAYING) > 0) { stopAt = i; break; }
    CHECK(playing);
    CHECK(stopAt < log.size());
    CHECK_EQ(countType(log, stopAt, MsgType::SIGNAL_ANCHOR), size_t(0));
}

// Item 2 guard: the supersede is one-directional. A first SIGNAL_PLAYING lost on the air is still retried
// after the SIGNAL_ANCHOR that follows it (ANCHOR supersedes nothing).
TEST(comm_item2_anchor_does_not_supersede_playing) {
    FakeBase base;
    base.ble = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    statusNow(h);
    base.dropPlaying = 1;
    CHECK(!h.playSong("406A").has_value());
    std::this_thread::sleep_for(1500ms);
    auto log = base.log();
    const bool playing = h.isPlaying();
    h.shutdown();
    dumpSignals(log, 0);
    CHECK(playing);
    size_t firstAnchor = log.size();
    for (size_t i = 0; i < log.size(); ++i) if (log[i].type == MsgType::SIGNAL_ANCHOR) { firstAnchor = i; break; }
    CHECK(firstAnchor < log.size());
    CHECK(countType(log, firstAnchor, MsgType::SIGNAL_PLAYING) >= size_t(1));  // the retry after the anchor
}

// Item 4a (advisor): the PC switched the organ off, then the serial link was lost; during the outage the
// organist switched the organ on by hand. After the reconnect the PC must not switch it off again.
// Two variants: the "off" was confirmed by a status poll before the loss, or it was still unconfirmed.
namespace {
size_t item4aOffsAfterReconnect(bool confirmBeforeLoss, const char* linkName) {
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    const auto link = std::filesystem::path(cfg.playFolder) / linkName;
    std::filesystem::remove(link);
    auto base1 = std::make_unique<FakeBase>();
    std::filesystem::create_symlink(base1->path(), link);
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    if (!h.connect(link.string())) return size_t(999);
    std::this_thread::sleep_for(300ms);
    h.organOn();
    std::this_thread::sleep_for(300ms);
    h.organOff();
    std::this_thread::sleep_for(confirmBeforeLoss ? 6000ms : 300ms);  // 5 s poll confirms "off", or not
    base1.reset();                          // the base goes away
    std::filesystem::remove(link);
    std::this_thread::sleep_for(1500ms);
    FakeBase base2;
    base2.ssr = true;                       // switched on by hand during the outage
    std::filesystem::create_symlink(base2.path(), link);
    std::this_thread::sleep_for(9000ms);    // reconnect, startup reset, >= 1 status poll
    auto log2 = base2.log();
    h.shutdown();
    std::filesystem::remove(link);
    std::cout << "  confirmed=" << confirmBeforeLoss << " SET_CURRENT{00} at base2=" << countSetV(log2, 0)
              << " base2 rx=" << log2.size() << "\n";
    return log2.empty() ? size_t(998) : countSetV(log2, 0);
}
}  // namespace

TEST(comm_item4a_confirmed_off_then_manual_on_during_outage_is_left_on) {
    CHECK_EQ(item4aOffsAfterReconnect(true, "fake-base-tty4a1"), size_t(0));
}

// PINNED, pending an owner question (fix-pc.md, wave 2, item 4a): an "off" that was not yet confirmed by a
// status poll is still defended across the loss (PCC-2, SerialLost case), so a manual "on" made during a short
// outage is switched off once more if the first status after the reconnect arrives within the 30 s window.
// From SSR=1 alone the PC cannot tell a lost "off" from a manual "on"; re-sending "off" is the safe side.
TEST(comm_item4a_unconfirmed_off_is_still_defended_after_reconnect_pinned) {
    CHECK_EQ(item4aOffsAfterReconnect(false, "fake-base-tty4a2"), size_t(1));
}

// Item 4b, found while writing the PCC-2 test: a STATUS_RESP to a GET_STATUS sent BEFORE the last SET_CURRENT,
// arriving late (the base held it until a newer GET_STATUS went out), was checked against the newest
// generation. Its stale relay=0 "confirmed" the lost off, the next relay=1 was adopted, and the organ stayed on
// with no re-send and no fault. Each answer must be judged by the generation of its own GET_STATUS.
TEST(comm_item4b_stale_status_answer_does_not_confirm_a_lost_off) {
    FakeBase base;
    base.holdStatus = true;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    // A GET_STATUS reaches the base before the organ commands (SSR off); its answer is held back.
    CHECK(waitFor([&] {
        for (auto& r : base.log()) if (r.type == MsgType::GET_STATUS) return true;
        return false;
    }, 7000));
    h.organOn();
    std::this_thread::sleep_for(300ms);
    base.dropSetCurrentOff = true;        // every off is lost
    h.organOff();
    CHECK(waitFor([&] { return base.statusPairs.load() >= 1; }, 15000));
    std::this_thread::sleep_for(500ms);
    auto log = base.log();
    const int pairs = base.statusPairs.load();
    h.shutdown();
    std::set<uint8_t> offIds;
    for (auto& r : log) if (isSet(r, 0)) offIds.insert(r.pid);
    for (auto& r : log) if (r.type == MsgType::SET_CURRENT || r.type == MsgType::GET_STATUS)
        std::cout << "  base rx " << msgTypeName(r.type) << " id=" << int(r.pid) << " [" << mc::test::toHex(r.payload) << "]\n";
    std::cout << "  pairs=" << pairs << " distinct SET_CURRENT{00} ids=" << offIds.size() << "\n";
    CHECK_EQ(pairs, 1);
    CHECK_EQ(offIds.size(), size_t(2));   // the original off + one re-send triggered by the fresh answer
}

// Item 4b (PCC-2, advisor): two STATUS_RESPs that arrive back to back after one re-send (the first held back
// until a newer GET_STATUS went out) count as one attempt, not two.
TEST(comm_item4b_two_status_resps_after_one_resend_count_once) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    h.organOn();
    std::this_thread::sleep_for(300ms);
    base.dropSetCurrentOff = true;        // every off is lost
    h.organOff();
    auto offIdCount = [&] {
        std::set<uint8_t> ids;
        for (auto& r : base.log()) if (isSet(r, 0)) ids.insert(r.pid);
        return ids.size();
    };
    CHECK(waitFor([&] { return offIdCount() >= 2; }, 12000));  // the first re-send (1/3)
    base.holdStatus = true;
    CHECK(waitFor([&] { return base.statusPairs.load() >= 1; }, 15000));
    std::this_thread::sleep_for(500ms);
    const size_t ids = offIdCount();
    const int pairs = base.statusPairs.load();
    h.shutdown();
    std::cout << "  pairs=" << pairs << " distinct SET_CURRENT{00} ids=" << ids << "\n";
    CHECK_EQ(pairs, 1);
    CHECK_EQ(ids, size_t(3));             // original + re-send 1 + exactly one re-send for the pair
}

// Item 4b follow-up (advisor #8): a GET_STATUS lost on the wire (no ACK, no answer, retries lost too) and then a
// lost organ-off. The FIFO of request generations must not lag behind for the lost request: the PC has to
// re-send the off within a bounded number of status answers, not only after the 15 s entry expiry.
TEST(comm_item4b_lost_status_request_then_lost_off_recovers) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    CHECK(waitFor([&] { return base.statusAnswers.load() >= 1; }, 7000));
    h.organOn();
    std::this_thread::sleep_for(300ms);
    base.loseNextStatus = 1;
    CHECK(waitFor([&] { return base.lostStatusPid.load() >= 0; }, 7000));  // (1) a GET_STATUS is lost
    base.dropSetCurrentOff = true;                                           // (2) the off is lost
    h.organOff();
    const int answersAtOff = base.statusAnswers.load();
    auto offIdCount = [&] {
        std::set<uint8_t> ids;
        for (auto& r : base.log()) if (isSet(r, 0)) ids.insert(r.pid);
        return ids.size();
    };
    const auto t0 = std::chrono::steady_clock::now();
    const bool resent = waitFor([&] { return offIdCount() >= 2; }, 20000);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    const int answers = base.statusAnswers.load() - answersAtOff;
    auto log = base.log();
    h.shutdown();
    for (auto& r : log) if (r.type == MsgType::SET_CURRENT || r.type == MsgType::GET_STATUS)
        std::cout << "  base rx " << msgTypeName(r.type) << " id=" << int(r.pid) << " [" << mc::test::toHex(r.payload) << "]\n";
    std::cout << "  lost GET_STATUS id=" << base.lostStatusPid.load() << " resent=" << resent << " after " << ms
              << " ms, status answers since the off=" << answers << "\n";
    CHECK(resent);
    CHECK(answers <= 2);   // the first or second fresh answer (relay=1 vs wanted off) triggers the re-send
}

// P1-18: sendReliable's defaults are the protocol's retry budget (RETRY_TIMEOUT_MS x MAX_RETRIES retries), not
// 600 ms x 5. Every production caller (firmware_update.cpp) passes explicit values, so only the defaults change.
TEST(comm_p118_send_reliable_defaults_follow_the_protocol) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    base.remoteMode = FakeBase::RemoteDrop;           // every frame to the remote is lost
    const size_t mark = base.log().size();
    const auto t0 = std::chrono::steady_clock::now();
    const bool ok = h.sendReliable(ADDR_REMOTE, MsgType::KEEPALIVE, {});
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    auto log = base.log();
    h.shutdown();
    size_t sends = 0;
    for (size_t i = mark; i < log.size(); ++i)
        if (log[i].dest == ADDR_REMOTE && log[i].type == MsgType::KEEPALIVE) ++sends;
    CHECK(!ok);
    CHECK_EQ(sends, size_t(1 + mc::protocol::MAX_RETRIES));
    CHECK(ms >= (1 + mc::protocol::MAX_RETRIES) * mc::protocol::RETRY_TIMEOUT_MS - 50);
    CHECK(ms < (2 + mc::protocol::MAX_RETRIES) * mc::protocol::RETRY_TIMEOUT_MS);
}

// P1-10: the PC clears the indicator only when it set it. A Stop while nothing played (web/console/remote
// Stop when idle) must not blank a code that another master (the remote's own song entry) put there.
TEST(comm_p110_indicator_clear_only_after_the_pc_set_it) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    auto npi = [&](size_t from, bool clear) {
        size_t n = 0;
        auto log = base.log();
        for (size_t i = from; i < log.size(); ++i)
            if (log[i].type == MsgType::NOW_PLAYING_INFO && log[i].payload.empty() == clear) ++n;
        return n;
    };
    const size_t m0 = base.log().size();
    h.stopSong();                                     // idle Stop
    std::this_thread::sleep_for(500ms);
    const size_t idleClears = npi(m0, true);
    CHECK(!h.playSong("406A").has_value());
    CHECK(waitFor([&] { return h.isPlaying() && npi(m0, false) > 0; }, 15000));
    const size_t m1 = base.log().size();
    h.stopSong();
    std::this_thread::sleep_for(500ms);
    const size_t stopClears = npi(m1, true);
    const size_t m2 = base.log().size();
    h.stopSong();                                     // second Stop: the PC's code is already gone
    std::this_thread::sleep_for(500ms);
    const size_t againClears = npi(m2, true);
    h.shutdown();
    CHECK_EQ(idleClears, size_t(0));
    CHECK_EQ(stopClears, size_t(1));
    CHECK_EQ(againClears, size_t(0));
}

// A3: a v2 base gets the conditional clear [NPI_CLEAR_IF_OWN] (clear only if the base still shows our number),
// a v1 base keeps the empty payload (it would ignore a 1-byte NPI and never clear).
namespace {
// Plays and stops once against a base answering GET_INFO with `protoVer` (0 = v1, no tag). Returns the payloads of
// the NOW_PLAYING_INFO clears the base received after the first play update.
std::vector<std::vector<uint8_t>> clearPayloadsAfterStop(int protoVer) {
    FakeBase base;
    base.protoVer = protoVer;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(1500ms);              // the INFO poll has answered
    CHECK(!h.playSong("406A").has_value());
    CHECK(waitFor([&] {
        if (!h.isPlaying()) return false;
        for (const auto& m : base.log()) if (m.type == MsgType::NOW_PLAYING_INFO && m.payload.size() == 5) return true;
        return false;
    }, 15000));
    const size_t m1 = base.log().size();
    h.stopSong();
    std::this_thread::sleep_for(500ms);
    std::vector<std::vector<uint8_t>> out;
    auto log = base.log();
    for (size_t i = m1; i < log.size(); ++i)
        if (log[i].type == MsgType::NOW_PLAYING_INFO && log[i].payload.size() < 5) out.push_back(log[i].payload);
    h.shutdown();
    return out;
}
}  // namespace

TEST(comm_a3_v2_base_gets_the_conditional_clear) {
    const auto clears = clearPayloadsAfterStop(2);
    CHECK_EQ(clears.size(), size_t(1));
    if (clears.size() == 1) CHECK(clears[0] == std::vector<uint8_t>{0x01});
}

TEST(comm_a3_v1_base_keeps_the_empty_clear) {
    const auto clears = clearPayloadsAfterStop(0);
    CHECK_EQ(clears.size(), size_t(1));
    if (clears.size() == 1) CHECK(clears[0].empty());
}

// P1-15: the console refuses "update" while a song plays, through the real ConsoleExecutor
// (not just fwupload::updateRefusal). The image path never exists: an accepted update
// fails reading it, before ENTER_BOOTLOADER or any USB lookup.
TEST(comm_p115_console_refuses_update_while_playing) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    const std::string missing = "/nonexistent/midicontrol-p115/base.bin";
    std::string playingOut, idleOut;
    {
        ConsoleExecutor exec(h, reg);
        CHECK(!h.playSong("406A").has_value());
        CHECK(waitFor([&] { return h.isPlaying(); }, 15000));
        const auto r1 = exec.execute("update base " + missing, [&](const std::string& t) { playingOut += t; }, false);
        CHECK(r1 == console::ExecResult::Failed);
        h.stopSong();
        CHECK(waitFor([&] { return !h.isPlaying(); }, 5000));
        const auto r2 = exec.execute("update base " + missing, [&](const std::string& t) { idleOut += t; }, false);
        CHECK(r2 == console::ExecResult::Failed);
    }
    const size_t enterBl = [&] {
        size_t n = 0;
        for (const auto& m : base.log()) if (m.type == MsgType::ENTER_BOOTLOADER) ++n;
        return n;
    }();
    h.shutdown();
    CHECK(playingOut.find("refused: a song is playing") != std::string::npos);
    CHECK(idleOut.find("refused") == std::string::npos);      // idle: accepted, then the file is missing
    CHECK(idleOut.find("FAILED") != std::string::npos);
    CHECK_EQ(enterBl, size_t(0));
}

// P1-14: a Play is accepted before the player exists (queue + organ warm-up); an update asked
// for in that window must be refused too, not only while isPlaying(). Missing image path: an
// accepted update would fail reading it, before ENTER_BOOTLOADER or any USB lookup.
TEST(comm_p114_update_refused_while_a_play_warms_up) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    cfg.warmupMs = 3000;                              // organ is off: Play waits here
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    const std::string missing = "/nonexistent/midicontrol-p114/base.bin";
    std::string warmOut;
    bool playingDuringWarmup = true;
    {
        ConsoleExecutor exec(h, reg);
        CHECK(!h.playSong("406A").has_value());
        std::this_thread::sleep_for(500ms);           // inside the warm-up
        playingDuringWarmup = h.isPlaying();
        exec.execute("update base " + missing, [&](const std::string& t) { warmOut += t; }, false);
        h.stopSong();
    }
    h.shutdown();
    CHECK(!playingDuringWarmup);                      // the window exists: isPlaying() is still false
    CHECK(warmOut.find("refused") != std::string::npos);
}

// P1-14: the play claim is given back on every way a song ends, so updates are not locked out:
// a Stop during the warm-up, a Play that fails after it was queued (file gone), a Stop while playing.
TEST(comm_p114_play_claim_released_after_stop_failure_and_end) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = songConfig();
    cfg.warmupMs = 1500;
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    auto granted = [&] {
        if (h.tryBeginUpdate() != CommHandler::UpdateClaim::Granted) return false;
        h.endUpdate();
        return true;
    };
    CHECK(granted());                                         // idle: free
    CHECK(h.tryBeginUpdate() == CommHandler::UpdateClaim::Granted);
    CHECK(h.tryBeginUpdate() == CommHandler::UpdateClaim::Busy);   // one update at a time
    h.endUpdate();

    // 1. Stop during the warm-up.
    CHECK(!h.playSong("406A").has_value());
    CHECK(h.tryBeginUpdate() == CommHandler::UpdateClaim::Playing);
    h.stopSong();
    const bool afterWarmStop = waitFor(granted, 3000);
    h.organOff();
    std::this_thread::sleep_for(300ms);

    // 2. Queued, then fails: the file disappears during the warm-up.
    const auto song = std::filesystem::path(cfg.playFolder) / "406A.mid";
    CHECK(!h.playSong("406A").has_value());
    std::filesystem::remove(song);
    CHECK(h.tryBeginUpdate() == CommHandler::UpdateClaim::Playing);
    const bool afterFail = waitFor(granted, 5000);
    std::filesystem::copy_file(MC_SONGS_DIR "/406A.mid", song, std::filesystem::copy_options::overwrite_existing);

    // 3. Plays, then stopped.
    CHECK(!h.playSong("406A").has_value());
    CHECK(waitFor([&] { return h.isPlaying(); }, 15000));
    CHECK(h.tryBeginUpdate() == CommHandler::UpdateClaim::Playing);
    h.stopSong();
    const bool afterStop = waitFor(granted, 3000);
    h.shutdown();
    CHECK(afterWarmStop);
    CHECK(afterFail);
    CHECK(afterStop);
}

// P1-13: a base left in its bootloader is reported (status field, console `status`) while it is not
// connected, and not during a base update. The probe is a fake: no real USB is scanned.
TEST(comm_p113_base_in_bootloader_is_reported) {
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    std::atomic<bool> inBl{false};
    h.setBootloaderProbe([&] { return inBl.load(); });
    h.setUpdating(true);                              // during a base update the bootloader is expected
    CHECK(!h.connect("/nonexistent/midicontrol-p113-tty"));
    inBl = true;
    std::this_thread::sleep_for(2500ms);
    const bool reportedDuringUpdate = h.baseInBootloader();
    h.setUpdating(false);
    const bool reported = waitFor([&] { return fieldBool(reg, status::Node::Base, "in_bootloader"); }, 3000);
    std::string statusOut;
    {
        ConsoleExecutor exec(h, reg);
        exec.execute("status", [&](const std::string& t) { statusOut += t; }, false);
    }
    inBl = false;
    const bool cleared = waitFor([&] { return !fieldBool(reg, status::Node::Base, "in_bootloader"); }, 3000);
    h.shutdown();
    CHECK(!reportedDuringUpdate);
    CHECK(reported);
    CHECK(statusOut.find("USB bootloader") != std::string::npos);
    CHECK(cleared);
}

// Protocol v2 (A1): the first frame the PC sends after the port opens is a KEEPALIVE with its session nonce, before any
// reliable frame; the nonce is non-zero and stays the same for the whole process.
TEST(comm_a1_hello_keepalive_with_nonce_precedes_everything) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    h.organOn();                       // a reliable frame right after open
    std::this_thread::sleep_for(2600ms);  // one more periodic KEEPALIVE
    auto log = base.log();
    h.shutdown();
    CHECK(!log.empty());
    CHECK(log[0].type == MsgType::KEEPALIVE);
    CHECK_EQ(log[0].payload.size(), size_t(2));
    const unsigned nonce = log[0].payload.size() == 2 ? (log[0].payload[0] | (log[0].payload[1] << 8)) : 0;
    CHECK(nonce != 0);
    int keepalives = 0;
    for (auto& r : log) {
        if (r.type != MsgType::KEEPALIVE) continue;
        ++keepalives;
        CHECK_EQ(r.payload.size(), size_t(2));
        if (r.payload.size() == 2) CHECK_EQ(unsigned(r.payload[0] | (r.payload[1] << 8)), nonce);
    }
    CHECK(keepalives >= 2);
}

TEST(comm_a1_peer_protocol_version_defaults_to_1_and_follows_info) {
    FakeBase base;
    base.protoVer = 2;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK_EQ(h.peerProtocolVersion(status::Node::Base), 1);       // nothing known yet: v1
    CHECK_EQ(h.peerProtocolVersion(status::Node::Controller), 1);
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(1200ms);
    CHECK_EQ(h.peerProtocolVersion(status::Node::Base), 2);
    CHECK_EQ(h.peerProtocolVersion(status::Node::Controller), 1);  // no INFO from the remote
    h.shutdown();
}

// A2: remote boot id in STATUS_RESP (protocol v2). A reboot shorter than the status poll shows no ble 0->1 edge; the
// changed id makes the PC resync the remote exactly once. The resync only has something to say while a song plays
// (an idle PC already told the remote "stopped"), so the rig plays a song and counts SIGNAL_PLAYING.
namespace {
struct BootIdRig {
    FakeBase base;
    status::StatusRegistry reg{[] { return int64_t(0); }};
    Config cfg = songConfig();
    std::unique_ptr<CommHandler> h;
    BootIdRig() {
        base.ble = true;
        h = std::make_unique<CommHandler>(cfg, [] { return fakePorts(); }, reg);
        h->setMidiSinkForTest([](const std::vector<uint8_t>&) {});
        CHECK(h->connect(base.path()));
        std::this_thread::sleep_for(300ms);
        statusNow(*h);  // first ble=1 status: the PC-start resync, ACKed
        CHECK(!h->playSong("406A").has_value());
        CHECK(waitFor([&] { return h->isPlaying() && countType(base.log(), 0, MsgType::SIGNAL_ANCHOR) > 0; }, 15000));
    }
    // SIGNAL_PLAYING resends caused by the status that follows `setup`
    size_t resendsAfter(const std::function<void()>& setup) {
        const size_t m = base.log().size();
        setup();
        statusNow(*h);
        return countType(base.log(), m, MsgType::SIGNAL_PLAYING);
    }
};
}  // namespace

TEST(comm_a2_old_base_four_bytes_never_resyncs) {
    BootIdRig r;
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));
    r.h->shutdown();
}
TEST(comm_a2_same_boot_id_no_resync) {
    BootIdRig r;
    r.base.bootId = 0x1234;
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));  // first id: stored
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));  // same id
    r.h->shutdown();
}
TEST(comm_a2_changed_boot_id_resyncs_exactly_once) {
    BootIdRig r;
    r.base.bootId = 0x1234;
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));
    CHECK_EQ(r.resendsAfter([&] { r.base.bootId = 0x4321; }), size_t(1));  // reboot: SIGNAL_PLAYING again
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));                           // the new id is the reference now
    r.h->shutdown();
}
TEST(comm_a2_first_id_after_link_up_only_stored) {
    BootIdRig r;
    r.base.bootId = 0x1111;
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));
    r.h->shutdown();
}
TEST(comm_a2_id_change_with_ble0_does_nothing_and_forgets_reference) {
    BootIdRig r;
    r.base.bootId = 0x1234;
    statusNow(*r.h);                                                       // stored
    r.base.remoteMode = FakeBase::RemoteUnreachable;
    r.base.ble = false;
    CHECK_EQ(r.resendsAfter([&] { r.base.bootId = 0x4321; }), size_t(0));  // ble=0: ignored
    r.base.remoteMode = FakeBase::RemoteAck;
    CHECK_EQ(r.resendsAfter([&] { r.base.ble = true; }), size_t(1));       // the 0->1 edge resyncs (not the id change)
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));                           // id was just stored: no second resync
    r.h->shutdown();
}
TEST(comm_a2_invalid_flag_ignores_the_id) {
    BootIdRig r;
    r.base.bootId = 0x1234;
    r.base.bootIdValid = false;
    CHECK_EQ(r.resendsAfter([] {}), size_t(0));
    CHECK_EQ(r.resendsAfter([&] { r.base.bootId = 0x4321; }), size_t(0));  // valid=0: the id means nothing
    r.h->shutdown();
}

namespace {
void put32le(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
// An update file for the remote (board 3), same layout as midicontrol-bootloader tools/fw_trailer.py.
std::vector<uint8_t> remoteUpdateFile() {
    std::vector<uint8_t> f;
    for (size_t i = 0; i < 100; ++i) f.push_back(static_cast<uint8_t>(i * 7));
    while (f.size() % 8) f.push_back(0xFF);
    std::vector<uint8_t> t;
    put32le(t, static_cast<uint32_t>(f.size()));
    put32le(t, mc::crc32(f.data(), f.size()));
    put32le(t, (1u << 16) | (2u << 8) | 3u);
    t.push_back(0x03);
    t.push_back(static_cast<uint8_t>(mc::fwimage::kFormat));
    t.push_back(0);
    t.push_back(0);
    put32le(t, mc::crc32(t.data(), 16));
    put32le(t, mc::fwimage::kMagic);
    f.insert(f.end(), t.begin(), t.end());
    return f;
}
}  // namespace

// A4 (PM-7): the device resends an un-ACKed FW_UPDATE_STATUS with the same PacketId. Both copies are
// ACKed, but the copy of a status the updater already took is not a new status.
TEST(comm_a4_repeated_fw_status_is_acked_but_counted_once) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    base.remoteFw = 1;
    FirmwareUpdater up(h);
    CHECK(up.updateRemote(remoteUpdateFile(), "remote-update.bin"));
    std::this_thread::sleep_for(300ms);   // the repeated CRC_OK copy has arrived by now
    CHECK(!h.takeFwStatus().has_value());
    int ackedCopies = 0;
    for (auto& r : base.log())
        if (r.type == MsgType::ACK && r.payload.size() == 1 && r.payload[0] == 77) ++ackedCopies;
    h.shutdown();
    CHECK_EQ(ackedCopies, 2);
}

// A4 (PM-8): NAK NO_TRANSFER is final (no retries) and the updater says what happened.
TEST(comm_a4_no_transfer_nak_fails_at_once_with_a_clear_text) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = testConfig();
    CommHandler h(cfg, [] { return std::optional<MidiPorts>{}; }, reg);
    CHECK(h.connect(base.path()));
    base.remoteFw = 2;
    std::vector<std::string> said;
    FirmwareUpdater up(h, [&](const std::string& t) { said.push_back(t); });
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!up.updateRemote(remoteUpdateFile(), "remote-update.bin"));
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(h.lastNakReason() == std::optional<uint8_t>(static_cast<uint8_t>(NakReason::NO_TRANSFER)));
    int dataFrames = 0;
    for (auto& r : base.log()) if (r.type == MsgType::FW_UPDATE_DATA) ++dataFrames;
    h.shutdown();
    CHECK_EQ(dataFrames, 1);              // not retried
    CHECK(ms < 4000);                     // no 1.5 s x 9 chunk retries
    bool clear = false;
    for (auto& t : said)
        if (t.find("device lost the transfer (reset or timeout); start the update again") != std::string::npos) clear = true;
    CHECK(clear);
}


// ---- A6: SIGNAL_INFO [flags], SONG_HAS_SECTIONS ----
namespace {
Config infoConfig(const char* song) {
    Config c = songConfig();
    std::filesystem::copy_file(std::filesystem::path(MC_SONGS_DIR) / song, std::filesystem::path(c.playFolder) / song,
                               std::filesystem::copy_options::overwrite_existing);
    return c;
}
// Every packet to the remote that is a signal or SIGNAL_INFO, in order.
std::vector<Rx> sigAndInfo(const std::vector<Rx>& log, size_t from = 0) {
    std::vector<Rx> out;
    for (size_t i = from; i < log.size(); ++i)
        if (log[i].dest == ADDR_REMOTE && (isSignal(log[i].type) || log[i].type == MsgType::SIGNAL_INFO))
            out.push_back(log[i]);
    return out;
}
bool remoteKnown(CommHandler& h, FakeBase& base, int ver) {
    base.remoteProtoVer = ver;
    base.ble = true;
    statusNow(h);
    return waitFor([&] { return h.peerProtocolVersion(mc::status::Node::Controller) == ver || ver == 1; }, 5000);
}
}  // namespace

// A song with sections: SIGNAL_INFO bit0=1 right after SIGNAL_PLAYING, before the first ANCHOR; group 3 does not drop PLAYING.
TEST(comm_a6_signal_info_sections_after_playing_before_anchor) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = infoConfig("401.mid");
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    CHECK(remoteKnown(h, base, 2));
    const size_t m = base.log().size();
    CHECK(!h.playSong("401").has_value());
    CHECK(waitFor([&] { return countType(base.log(), m, MsgType::SIGNAL_ANCHOR) > 0; }, 15000));
    auto seq = sigAndInfo(base.log(), m);
    h.stopSong();
    h.shutdown();
    CHECK(seq.size() >= 3);
    CHECK(seq[0].type == MsgType::SIGNAL_PLAYING);
    CHECK(seq[1].type == MsgType::SIGNAL_INFO);
    CHECK(seq[1].payload == std::vector<uint8_t>{SIGNAL_INFO_SONG_HAS_SECTIONS});
    CHECK(seq[2].type == MsgType::SIGNAL_ANCHOR);
}

// A song without sections: bit0 = 0 (reserved bits are 0 as well).
TEST(comm_a6_signal_info_no_sections_is_zero) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = infoConfig("523.mid");
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    CHECK(remoteKnown(h, base, 2));
    const size_t m = base.log().size();
    CHECK(!h.playSong("523").has_value());
    CHECK(waitFor([&] { return countType(base.log(), m, MsgType::SIGNAL_INFO) > 0; }, 15000));
    auto seq = sigAndInfo(base.log(), m);
    h.stopSong();
    h.shutdown();
    CHECK(seq.size() >= 2);
    CHECK(seq[0].type == MsgType::SIGNAL_PLAYING);
    CHECK(seq[1].type == MsgType::SIGNAL_INFO);
    CHECK(seq[1].payload == std::vector<uint8_t>{0});
}

// A resync after a drop resends PLAYING, SIGNAL_INFO (same flags), then the ANCHOR.
TEST(comm_a6_resync_resends_signal_info) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = infoConfig("523.mid");
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    CHECK(remoteKnown(h, base, 2));
    CHECK(!h.playSong("523").has_value());
    CHECK(waitFor([&] { return countType(base.log(), 0, MsgType::SIGNAL_INFO) > 0; }, 15000));
    std::this_thread::sleep_for(300ms);
    base.ble = false;
    statusNow(h);
    const size_t m = base.log().size();
    base.ble = true;                                  // edge while playing: resync
    statusNow(h);
    std::this_thread::sleep_for(500ms);
    auto seq = sigAndInfo(base.log(), m);
    h.stopSong();
    h.shutdown();
    CHECK(seq.size() >= 2);
    CHECK(seq[0].type == MsgType::SIGNAL_PLAYING);
    CHECK(seq[1].type == MsgType::SIGNAL_INFO);
    CHECK(seq[1].payload == std::vector<uint8_t>{0});
}

// A v1 remote (no PROTO_VER) never gets SIGNAL_INFO; PLAYING and ANCHOR still go out.
TEST(comm_a6_v1_remote_gets_no_signal_info) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = infoConfig("401.mid");
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    base.ble = true;                                  // remoteProtoVer stays 0: the remote does not report one
    statusNow(h);
    CHECK_EQ(h.peerProtocolVersion(mc::status::Node::Controller), 1);
    CHECK(!h.playSong("401").has_value());
    CHECK(waitFor([&] { return countType(base.log(), 0, MsgType::SIGNAL_ANCHOR) > 0; }, 15000));
    auto log = base.log();
    h.stopSong();
    h.shutdown();
    CHECK_EQ(countType(log, 0, MsgType::SIGNAL_INFO), size_t(0));
    CHECK(countType(log, 0, MsgType::SIGNAL_PLAYING) >= 1);
}

// Fix round 1: the remote's PROTO_VER is unknown when the song starts. The flags are recorded anyway and go out
// as soon as the INFO poll reports v2 (never from the reader thread: through the RemoteResync command).
namespace {
void relinkWhileVersionUnknown(CommHandler& h, FakeBase& base, int verNow) {
    base.ble = false;
    statusNow(h);
    std::this_thread::sleep_for(2600ms);            // the info poller sees the link state on its 2 s tick
    base.remoteProtoVer = verNow;   // the next GET_INFO (link edge) is answered with this version
    base.ble = true;
    statusNow(h);
}
}  // namespace

TEST(comm_a6_signal_info_sent_when_version_becomes_known_v2) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = infoConfig("401.mid");
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    base.ble = true;                                  // link up, but no INFO_RESP: version unknown
    statusNow(h);
    CHECK_EQ(h.peerProtocolVersion(mc::status::Node::Controller), 1);
    CHECK(!h.playSong("401").has_value());
    CHECK(waitFor([&] { return countType(base.log(), 0, MsgType::SIGNAL_ANCHOR) > 0; }, 15000));
    CHECK_EQ(countType(base.log(), 0, MsgType::SIGNAL_INFO), size_t(0));
    const size_t m = base.log().size();
    relinkWhileVersionUnknown(h, base, 2);
    CHECK(waitFor([&] { return countType(base.log(), m, MsgType::SIGNAL_INFO) > 0; }, 8000));
    CHECK_EQ(h.peerProtocolVersion(mc::status::Node::Controller), 2);
    auto seq = sigAndInfo(base.log(), m);
    h.stopSong();
    h.shutdown();
    bool found = false;
    for (auto& r : seq)
        if (r.type == MsgType::SIGNAL_INFO && r.payload == std::vector<uint8_t>{SIGNAL_INFO_SONG_HAS_SECTIONS}) found = true;
    CHECK(found);
}

TEST(comm_a6_signal_info_not_sent_when_version_becomes_known_v1) {
    FakeBase base;
    status::StatusRegistry reg([] { return int64_t(0); });
    Config cfg = infoConfig("401.mid");
    CommHandler h(cfg, [] { return fakePorts(); }, reg);
    h.setMidiSinkForTest([](const std::vector<uint8_t>&) {});
    CHECK(h.connect(base.path()));
    std::this_thread::sleep_for(300ms);
    base.ble = true;
    statusNow(h);
    CHECK(!h.playSong("401").has_value());
    CHECK(waitFor([&] { return countType(base.log(), 0, MsgType::SIGNAL_ANCHOR) > 0; }, 15000));
    relinkWhileVersionUnknown(h, base, 1);            // INFO_RESP says PROTO_VER 1
    std::this_thread::sleep_for(3500ms);
    CHECK_EQ(h.peerProtocolVersion(mc::status::Node::Controller), 1);
    auto log = base.log();
    h.stopSong();
    h.shutdown();
    CHECK_EQ(countType(log, 0, MsgType::SIGNAL_INFO), size_t(0));
}
