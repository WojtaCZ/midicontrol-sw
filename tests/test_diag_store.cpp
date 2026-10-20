#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "midicontrol/diag_store.hpp"
#include "test_framework.hpp"

using namespace mc::diag;

namespace {
struct TmpDir {
    std::string path;
    TmpDir() {
        char t[] = "/tmp/mcdiagXXXXXX";
        path = mkdtemp(t);
    }
    ~TmpDir() {
        std::string cmd = "rm -rf '" + path + "'";
        (void)!system(cmd.c_str());
    }
};

DiagEntry mk(const std::string& node, char lvl, const std::string& text, bool fault = false) {
    DiagEntry e;
    e.pcTimeMs = 1'700'000'000'000;
    e.node = node;
    e.boot = 4;
    e.uptimeMs = 1234;
    e.level = lvl;
    e.tag = "t";
    e.text = text;
    if (fault) {
        FaultInfo f;
        f.kind = "HARDFAULT";
        f.pc = 0x08001234;
        f.cfsr = 0x8200;
        f.hasCfsr = true;
        f.cfsrBits = {"PRECISERR", "BFARVALID"};
        f.bfar = 0x40021000;
        e.fault = f;
    }
    return e;
}

long fsize(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 ? long(st.st_size) : -1;
}
}  // namespace

TEST(store_roundtrip_with_escaping_and_fault) {
    TmpDir d;
    DiagStore s(d.path);
    s.append(mk("base", 'E', "quote \" back\\ nl\n tab\t ctl\x01 utf8 \xc4\x9b", true));
    DiagStore s2(d.path);
    auto q = s2.query("base", 'D', 0, 10);
    CHECK_EQ(q.size(), size_t(1));
    CHECK_EQ(q[0].text, std::string("quote \" back\\ nl\n tab\t ctl\x01 utf8 \xc4\x9b"));
    CHECK_EQ(q[0].pcTimeMs, int64_t(1'700'000'000'000));
    CHECK_EQ(q[0].boot, uint32_t(4));
    CHECK_EQ(q[0].uptimeMs, uint64_t(1234));
    CHECK(q[0].fault.has_value());
    CHECK_EQ(q[0].fault->pc, uint32_t(0x08001234));
    CHECK_EQ(q[0].fault->bfar, uint32_t(0x40021000));
    CHECK((q[0].fault->cfsrBits == std::vector<std::string>{"PRECISERR", "BFARVALID"}));
}

TEST(store_rotation_bounds_files_and_size) {
    TmpDir d;
    uint64_t last = 0;
    {
        DiagStore s(d.path);
        for (int i = 0; i < 10000; ++i) last = s.append(mk("base", 'I', "entry number " + std::to_string(i) + " padding padding padding padding"));
    }
    CHECK_EQ(last, uint64_t(10000));
    int n = 0;
    for (const char* suf : {"", ".1", ".2", ".3"}) {
        long sz = fsize(d.path + "/diag-base.jsonl" + suf);
        if (sz >= 0) {
            ++n;
            CHECK(sz <= (1 << 20));
        }
    }
    CHECK(n <= 3);
    CHECK(n >= 2);
    DiagStore s(d.path);
    auto q = s.query("base", 'D', 0, 5);
    CHECK_EQ(q.size(), size_t(5));
    CHECK_EQ(q[0].id, uint64_t(10000));
    CHECK_EQ(q[4].id, uint64_t(9996));
    CHECK(q[0].text.find("9999") != std::string::npos);
    CHECK_EQ(s.append(mk("base", 'I', "next")), uint64_t(10001));
}

TEST(store_ids_monotonic_across_nodes_and_reopen) {
    TmpDir d;
    uint64_t a, b;
    {
        DiagStore s(d.path);
        a = s.append(mk("base", 'I', "a"));
        b = s.append(mk("remote", 'I', "b"));
    }
    CHECK(b > a);
    DiagStore s(d.path);
    CHECK(s.append(mk("base", 'I', "c")) > b);
}

TEST(store_query_filters_and_pages) {
    TmpDir d;
    DiagStore s(d.path);
    for (char l : {'D', 'I', 'W', 'E', 'I'}) s.append(mk("base", l, std::string("b") + l));
    s.append(mk("remote", 'E', "r"));
    auto w = s.query("base", 'W', 0, 10);
    CHECK_EQ(w.size(), size_t(2));
    CHECK_EQ(w[0].level, 'E');
    CHECK_EQ(w[1].level, 'W');
    auto all = s.query("base", 'D', 0, 10);
    CHECK_EQ(all.size(), size_t(5));
    CHECK(all[0].id > all[1].id);
    auto page = s.query("base", 'D', all[1].id, 2);
    CHECK_EQ(page.size(), size_t(2));
    CHECK_EQ(page[0].id, all[2].id);
    CHECK_EQ(s.query("remote", 'D', 0, 10).size(), size_t(1));
    CHECK_EQ(s.query("base", 'D', 0, 2).size(), size_t(2));
}

TEST(store_unseen_faults_and_ack_persist) {
    TmpDir d;
    uint64_t f1, f2;
    {
        DiagStore s(d.path);
        CHECK(!s.lastFault("base").has_value());
        f1 = s.append(mk("base", 'E', "f1", true));
        s.append(mk("base", 'I', "plain"));
        f2 = s.append(mk("base", 'E', "f2", true));
        s.append(mk("remote", 'E', "rf", true));
        CHECK_EQ(s.unseenFaults("base"), size_t(2));
        CHECK_EQ(s.lastFault("base")->id, f2);
        s.ack(f1);
        CHECK_EQ(s.unseenFaults("base"), size_t(1));
    }
    DiagStore s(d.path);
    CHECK_EQ(s.unseenFaults("base"), size_t(1));
    s.ack(f2);
    CHECK_EQ(s.unseenFaults("base"), size_t(0));
    CHECK_EQ(s.unseenFaults("remote"), size_t(1));  // remote fault id is above the ack
    s.ack(f2 + 1);
    CHECK_EQ(s.unseenFaults("remote"), size_t(0));
}

TEST(store_tolerates_garbage_lines) {
    TmpDir d;
    {
        DiagStore s(d.path);
        s.append(mk("base", 'I', "ok"));
    }
    {
        std::ofstream f(d.path + "/diag-base.jsonl", std::ios::app);
        f << "not json\n{\"id\":\n\n";
    }
    DiagStore s(d.path);
    CHECK_EQ(s.query("base", 'D', 0, 10).size(), size_t(1));
}

TEST(store_append_after_partial_last_line) {
    TmpDir d;
    {
        DiagStore s(d.path);
        s.append(mk("base", 'I', "one"));
    }
    {
        std::ofstream f(d.path + "/diag-base.jsonl", std::ios::app);
        f << "{\"id\":5,\"t\":1,\"no";  // crash mid-write, no newline
    }
    DiagStore s(d.path);
    s.append(mk("base", 'I', "two"));
    auto q = s.query("base", 'D', 0, 10);
    CHECK_EQ(q.size(), size_t(2));
    CHECK_EQ(q[0].text, std::string("two"));
}

TEST(store_replace_updates_entry_and_persists) {
    TmpDir t;
    uint64_t id1, id2;
    {
        DiagStore s(t.path);
        id1 = s.append(mk("base", 'E', "first", true));
        id2 = s.append(mk("base", 'I', "other"));
        DiagEntry upd = mk("base", 'E', "first full", true);
        upd.fault->rev = "abc1234";
        CHECK(s.replace(id1, upd));
        CHECK(!s.replace(9999, upd));
        auto lf = s.lastFault("base");
        CHECK(lf.has_value());
        CHECK_EQ(lf->id, id1);
        CHECK_EQ(lf->text, std::string("first full"));
        CHECK_EQ(lf->fault->rev, std::string("abc1234"));
        CHECK_EQ(s.query("base", 'D', 0, 10).size(), size_t(2));
    }
    DiagStore s(t.path);
    auto lf = s.lastFault("base");
    CHECK(lf.has_value());
    CHECK_EQ(lf->fault->rev, std::string("abc1234"));
    CHECK_EQ(s.query("base", 'D', 0, 10)[0].id, id2);
}

TEST(store_tail_cache_serves_repeated_queries_without_disk_reads) {
    TmpDir t;
    DiagStore s(t.path);
    for (int i = 0; i < 20; ++i) s.append(mk("base", 'I', "line " + std::to_string(i), i == 3));
    s.query("base", 'D', 0, 5);
    const size_t reads = s.diskReads();
    for (int i = 0; i < 10; ++i) {
        s.query("base", 'D', 0, 5);
        s.lastFault("base");
        s.unseenFaults("base");
        s.append(mk("base", 'I', "more"));
    }
    CHECK_EQ(s.diskReads(), reads);
    CHECK_EQ(s.query("base", 'D', 0, 3)[0].text, std::string("more"));
}

TEST(store_cache_window_smaller_than_history_pages_from_disk) {
    TmpDir t;
    DiagStore s(t.path, 1 << 20, 3, 5);  // cache keeps only 5 entries per node
    for (int i = 1; i <= 12; ++i) s.append(mk("base", 'I', "n" + std::to_string(i), i == 2));
    auto page = s.query("base", 'D', 0, 3);
    CHECK_EQ(page.size(), size_t(3));
    CHECK_EQ(page[0].text, std::string("n12"));
    // before id 4 is older than the cache window: must still be answered (from disk)
    auto old = s.query("base", 'D', 4, 10);
    CHECK_EQ(old.size(), size_t(3));
    CHECK_EQ(old[0].text, std::string("n3"));
    // the old fault is outside the window but still found and unseen
    CHECK(s.lastFault("base").has_value());
    CHECK_EQ(s.unseenFaults("base"), size_t(1));
}

TEST(store_creates_directory_recursively) {
    TmpDir t;
    std::string deep = t.path + "/a/b/c";
    DiagStore s(deep);
    s.append(mk("base", 'I', "x"));
    struct stat st;
    CHECK(::stat((deep + "/diag-base.jsonl").c_str(), &st) == 0);
}

TEST(store_write_failure_is_counted_not_fatal) {
    TmpDir t;
    DiagStore s(t.path);
    s.append(mk("base", 'I', "ok"));
    ::chmod(t.path.c_str(), 0555);
    if (::access((t.path + "/probe").c_str(), W_OK) == 0) return;  // running as root: cannot make it fail
    uint64_t id = s.append(mk("remote", 'I', "lost"));
    s.append(mk("remote", 'I', "lost again"));
    ::chmod(t.path.c_str(), 0755);
    CHECK(id > 0);
    CHECK_EQ(s.writeErrors(), size_t(2));
}
