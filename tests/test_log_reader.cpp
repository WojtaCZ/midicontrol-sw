#include <sys/stat.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "midicontrol/log_reader.hpp"
#include "test_framework.hpp"

using namespace mc::diag;

namespace {

struct TmpDir {
    std::string path;
    TmpDir() {
        char t[] = "/tmp/mclogXXXXXX";
        path = mkdtemp(t);
    }
    ~TmpDir() {
        std::string cmd = "rm -rf '" + path + "'";
        (void)!system(cmd.c_str());
    }
};

struct Events {
    std::vector<std::string> list;
    LogIngest::EventSink sink() {
        return [this](char lvl, const std::string& text) { list.push_back(std::string(1, lvl) + " " + text); };
    }
};

void feedLines(LogIngest& in, const std::vector<std::string>& lines) {
    for (const auto& l : lines) {
        const std::string s = l + "\r\n";
        in.feed(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }
}

std::vector<std::string> texts(const std::vector<DiagEntry>& v) {
    std::vector<std::string> o;
    for (auto it = v.rbegin(); it != v.rend(); ++it) o.push_back(it->text);  // oldest first
    return o;
}

// A scripted log port: a list of connection sessions, each a list of chunks; a session ends (unplug) when its chunks run out.
struct FakeSource : LogSource {
    struct Session {
        std::vector<std::string> chunks;
    };
    std::vector<Session> sessions;
    size_t next = 0, chunk = 0;
    bool open_ = false, healthy_ = true;
    int opens = 0;
    bool isOpen() const override { return open_; }
    bool tryOpen() override {
        if (next >= sessions.size()) return false;
        open_ = true;
        healthy_ = true;
        chunk = 0;
        ++opens;
        return true;
    }
    std::vector<uint8_t> read() override {
        const Session& s = sessions[next];
        if (chunk < s.chunks.size()) {
            const std::string& c = s.chunks[chunk++];
            return std::vector<uint8_t>(c.begin(), c.end());
        }
        healthy_ = false;  // unplugged
        return {};
    }
    bool healthy() const override { return healthy_; }
    void close() override {
        open_ = false;
        ++next;
    }
};

}  // namespace

TEST(test_log_reader_reconnect) {
    TmpDir t;
    DiagStore store(t.path);
    Events ev;
    LogIngest ingest(store, [] { return int64_t(1'700'000'000'000); }, ev.sink());
    FakeSource src;
    src.sessions.push_back({{"[    1.000] I ble: one\r\n[    2.000] W ble: tw"}});
    src.sessions.push_back({{"o again\r\n[    3.000] E ble: three\r\n"}});
    LogReader reader(src, ingest, 0);
    int64_t now = 0;
    for (int i = 0; i < 20; ++i) reader.step(now += 1000);  // must not crash on a closed port either
    CHECK_EQ(src.opens, 2);
    auto got = texts(store.query("base", 'D', 0, 50));
    CHECK_EQ(got.size(), size_t(2));
    CHECK_EQ(got[0], std::string("one"));
    CHECK_EQ(got[1], std::string("three"));  // "tw" + "o again" never glued together, no duplicates
    CHECK_EQ(ev.list.size(), size_t(1));
    CHECK_EQ(ev.list[0], std::string("E ble: three"));
}

TEST(log_reader_waits_between_open_attempts) {
    TmpDir t;
    DiagStore store(t.path);
    LogIngest ingest(store, [] { return int64_t(0); }, nullptr);
    struct Never : FakeSource {
        int tries = 0;
        bool tryOpen() override {
            ++tries;
            return false;
        }
    } src;
    LogReader reader(src, ingest, 1000);
    for (int64_t n = 0; n <= 2500; n += 100) reader.step(n);
    CHECK_EQ(src.tries, 3);  // t = 0, 1000, 2000
}

TEST(log_ingest_replay_window_updates_one_fault_entry) {
    TmpDir t;
    DiagStore store(t.path);
    Events ev;
    LogIngest in(store, [] { return int64_t(1'700'000'000'000); }, ev.sink());
    feedLines(in, {"--- log before reset ---", "[    5.000] I ble: before", "[    6.000] E fault: HARDFAULT #4 pc=0x08001234 lr=0x08005678 code 0",
                   "--- end of log before reset ---", "[    0.010] I sys: MIDIControl base fw main rev abc1234",
                   "[    0.011] I sys: reset: IWDG (csr 0x24000000)", "[    0.012] E fault: HARDFAULT #4 code 0 up 6.000 s rev abc1234",
                   "[    0.013] E fault: cfsr=0x00000400 IMPRECISERR", "[    0.500] I ble: ready"});
    CHECK_EQ(store.unseenFaults("base"), size_t(1));
    auto f = store.lastFault("base");
    CHECK(f.has_value());
    CHECK(f->fault.has_value());
    CHECK_EQ(f->fault->pc, uint32_t(0x08001234));  // kept from the record-time line
    CHECK(f->fault->hasCfsr);                       // added by the banner
    CHECK_EQ(f->fault->rev, std::string("abc1234"));
    int faults = 0;
    for (const auto& e : store.query("base", 'D', 0, 50)) faults += e.fault ? 1 : 0;
    CHECK_EQ(faults, 1);
    auto got = texts(store.query("base", 'D', 0, 50));
    CHECK_EQ(got.size(), size_t(5));  // before, fault, 2 x sys, ready
}

TEST(log_ingest_same_fault_after_power_cycle_is_a_new_entry) {
    TmpDir t;
    DiagStore store(t.path);
    LogIngest in(store, [] { return int64_t(1'700'000'000'000); }, nullptr);
    feedLines(in, {"--- log before reset ---", "[    6.000] E fault: HARDFAULT #1 pc=0x08001234 lr=0x08005678 code 0",
                   "--- end of log before reset ---", "[    0.010] I sys: boot", "[    0.012] E fault: HARDFAULT #1 code 0 up 6.000 s rev abc1234",
                   "[    0.500] I ble: ready"});
    CHECK_EQ(store.unseenFaults("base"), size_t(1));
    in.disconnect();  // power cycle: the count restarts, no replay marker this time
    feedLines(in, {"[    0.010] I sys: boot", "[    0.012] E fault: HARDFAULT #1 code 0 up 2.000 s rev abc1234", "[    0.500] I ble: ready"});
    CHECK_EQ(store.unseenFaults("base"), size_t(2));
}

TEST(log_ingest_live_fault_then_replay_is_one_entry) {
    TmpDir t;
    DiagStore store(t.path);
    LogIngest in(store, [] { return int64_t(1); }, nullptr);
    // fault seen live (PC had the log open) ...
    feedLines(in, {"[    6.000] E fault: HARDFAULT #4 pc=0x08001234 lr=0x08005678 code 0", "[    6.100] I ble: x"});
    in.disconnect();  // the base resets, USB drops
    feedLines(in, {"--- log before reset ---", "[    6.000] E fault: HARDFAULT #4 pc=0x08001234 lr=0x08005678 code 0", "--- end of log before reset ---",
                   "[    0.012] E fault: HARDFAULT #4 code 0 up 6.000 s rev abc1234", "[    0.500] I ble: ready"});
    CHECK_EQ(store.unseenFaults("base"), size_t(1));
}

TEST(log_ingest_disconnect_flushes_pending_fault_and_drops_partial_line) {
    TmpDir t;
    DiagStore store(t.path);
    LogIngest in(store, [] { return int64_t(1); }, nullptr);
    feedLines(in, {"[    6.000] E fault: HARDFAULT #2 pc=0x08001234 lr=0x08005678 code 0"});
    const std::string partial = "[    7.000] I ble: hal";
    in.feed(reinterpret_cast<const uint8_t*>(partial.data()), partial.size());
    CHECK_EQ(store.unseenFaults("base"), size_t(0));  // still pending
    in.disconnect();
    CHECK_EQ(store.unseenFaults("base"), size_t(1));
    CHECK_EQ(store.query("base", 'D', 0, 10).size(), size_t(1));  // the partial line is gone
    feedLines(in, {"f a line"});  // would be glued to the partial if it was kept
    CHECK_EQ(store.query("base", 'D', 0, 10).size(), size_t(1));
}

TEST(log_ingest_warnings_and_errors_become_events) {
    TmpDir t;
    DiagStore store(t.path);
    Events ev;
    LogIngest in(store, [] { return int64_t(1); }, ev.sink());
    feedLines(in, {"[    1.000] I ble: fine", "[    2.000] W ble: cmd failed: C", "[    3.000] E usb: gone", "garbage line", "[    4.000] D ble: dbg"});
    CHECK_EQ(ev.list.size(), size_t(2));
    CHECK_EQ(ev.list[0], std::string("W ble: cmd failed: C"));
    CHECK_EQ(ev.list[1], std::string("E usb: gone"));
    CHECK_EQ(store.query("base", 'D', 0, 10).size(), size_t(4));  // the garbage line is not stored
}

TEST(log_ingest_reports_stored_and_updated_faults) {
    TmpDir t;
    DiagStore store(t.path);
    LogIngest in(store, [] { return int64_t(1); }, nullptr);
    int changed = 0;
    in.setOnFault([&] { ++changed; });
    feedLines(in, {"--- log before reset ---", "[    6.000] E fault: HARDFAULT #4 pc=0x08001234 lr=0x08005678 code 0", "--- end of log before reset ---"});
    CHECK_EQ(changed, 1);
    feedLines(in, {"[    0.012] E fault: HARDFAULT #4 code 0 up 6.000 s rev abc1234", "[    0.500] I ble: ready"});
    CHECK_EQ(changed, 2);  // the update refreshes the status too
}

// Review finding: the same kind/count after a power cycle inside a later replay window must not overwrite the
// old (acked) fault entry.
TEST(log_ingest_power_cycle_then_same_count_in_replay_is_a_new_entry) {
    TmpDir t;
    DiagStore store(t.path);
    LogIngest in(store, [] { return int64_t(1); }, nullptr);
    feedLines(in, {"--- log before reset ---", "[    6.000] E fault: HARDFAULT #1 pc=0x08001234 lr=0x08005678 code 0", "--- end of log before reset ---",
                   "[    0.010] I sys: boot", "[    0.012] E fault: HARDFAULT #1 code 0 up 6.000 s rev abc", "[    0.500] I ble: ready"});
    CHECK_EQ(store.unseenFaults("base"), size_t(1));
    store.ack(store.lastFault("base")->id);
    CHECK_EQ(store.unseenFaults("base"), size_t(0));
    in.disconnect();  // power cycle: the count restarts
    feedLines(in, {"[    0.010] I sys: boot", "[    0.020] I sys: reset: power-on", "[    0.500] I ble: ready"});
    in.disconnect();
    feedLines(in, {"--- log before reset ---", "[   99.000] E fault: HARDFAULT #1 pc=0x08009999 lr=0x08001111 code 0", "--- end of log before reset ---",
                   "[    0.010] I sys: boot", "[    0.012] E fault: HARDFAULT #1 code 0 up 99.000 s rev abc", "[    0.500] I ble: ready"});
    int faults = 0;
    for (const auto& e : store.query("base", 'D', 0, 100)) faults += e.fault ? 1 : 0;
    CHECK_EQ(faults, 2);
    CHECK_EQ(store.unseenFaults("base"), size_t(1));
    CHECK_EQ(store.lastFault("base")->fault->pc, uint32_t(0x08009999));
}

// Finding 4 (base): a restarted service must not re-raise a fault that the next reset reprints.
TEST(log_ingest_restart_seeds_fault_identity_from_store) {
    TmpDir t;
    {
        DiagStore store(t.path);
        LogIngest in(store, [] { return int64_t(1); }, nullptr);
        feedLines(in, {"--- log before reset ---", "[    6.000] E fault: HARDFAULT #1 pc=0x08001234 lr=0x08005678 code 0", "--- end of log before reset ---",
                       "[    0.010] I sys: boot", "[    0.012] E fault: HARDFAULT #1 code 0 up 6.000 s rev abc", "[    0.500] I ble: ready"});
        store.ack(store.lastFault("base")->id);
    }
    DiagStore store(t.path);  // service restart
    LogIngest in(store, [] { return int64_t(2); }, nullptr);
    // a later plain reset reprints the old record in its banner
    feedLines(in, {"--- log before reset ---", "[   50.000] I ble: x", "--- end of log before reset ---", "[    0.010] I sys: boot",
                   "[    0.012] E fault: HARDFAULT #1 code 0 up 6.000 s rev abc", "[    0.500] I ble: ready"});
    int faults = 0;
    for (const auto& e : store.query("base", 'D', 0, 100)) faults += e.fault ? 1 : 0;
    CHECK_EQ(faults, 1);
    CHECK_EQ(store.unseenFaults("base"), size_t(0));
}

TEST(log_ingest_replayed_body_lines_raise_no_status_events) {
    TmpDir t;
    DiagStore store(t.path);
    Events ev;
    LogIngest in(store, [] { return int64_t(1); }, ev.sink());
    feedLines(in, {"--- log before reset ---", "[    5.000] W ble: old warning", "--- end of log before reset ---", "[    0.010] I sys: boot",
                   "[    0.020] W ble: new warning"});
    CHECK_EQ(ev.list.size(), size_t(1));
    CHECK_EQ(ev.list[0], std::string("W ble: new warning"));
    CHECK_EQ(store.query("base", 'W', 0, 10).size(), size_t(2));  // both are still stored
}
