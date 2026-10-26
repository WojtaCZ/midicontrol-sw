#include <sys/stat.h>

#include <cstdlib>
#include <string>

#include "midicontrol/diag_api.hpp"
#include "midicontrol/http.hpp"
#include "midicontrol/status.hpp"
#include "midicontrol/status_json.hpp"
#include "test_framework.hpp"

using namespace mc::diag;
using mc::status::Node;

namespace {

struct TmpDir {
    std::string path;
    TmpDir() {
        char t[] = "/tmp/mcapiXXXXXX";
        path = mkdtemp(t);
    }
    ~TmpDir() {
        std::string cmd = "rm -rf '" + path + "'";
        (void)!system(cmd.c_str());
    }
};

bool has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

size_t count(const std::string& s, const std::string& part) {
    size_t n = 0;
    for (size_t p = s.find(part); p != std::string::npos; p = s.find(part, p + 1)) ++n;
    return n;
}

DiagEntry mk(const std::string& node, char lvl, const std::string& text, bool fault = false) {
    DiagEntry e;
    e.pcTimeMs = 1'700'000'000'000;
    e.node = node;
    e.uptimeMs = 42;
    e.level = lvl;
    e.tag = "t";
    e.text = text;
    if (fault) {
        FaultInfo f;
        f.kind = "HARDFAULT";
        f.pc = 0x08001234;
        f.lr = 0x08005678;
        f.xpsr = 0x61000003;
        f.cfsr = 0x8200;
        f.hasCfsr = true;
        f.cfsrBits = {"PRECISERR", "BFARVALID"};
        f.bfar = 0x40021000;
        f.rev = "abc1234";
        f.count = 3;
        e.fault = f;
    }
    return e;
}

}  // namespace

TEST(api_diag_shape_and_node_filter) {
    TmpDir t;
    DiagStore s(t.path);
    s.append(mk("base", 'I', "hello"));
    s.append(mk("remote", 'W', "low battery"));
    auto r = apiDiag(s, "node=base");
    CHECK(r.has_value());
    CHECK(r->rfind("{\"schema\":1,\"entries\":[", 0) == 0);
    CHECK(has(*r, "\"text\":\"hello\""));
    CHECK(!has(*r, "low battery"));
    CHECK(has(*r, "\"next_before\":null}"));
    CHECK(has(*r, "\"node\":\"base\""));
    CHECK(has(*r, "\"lvl\":\"I\""));
    // no node = both, newest first
    auto both = apiDiag(s, "");
    CHECK(both.has_value());
    CHECK(both->find("low battery") < both->find("hello"));
}

TEST(api_diag_level_filter_and_fault_object) {
    TmpDir t;
    DiagStore s(t.path);
    s.append(mk("base", 'D', "dbg"));
    s.append(mk("base", 'W', "warn"));
    s.append(mk("base", 'E', "boom", true));
    auto r = apiDiag(s, "node=base&level=W");
    CHECK(r.has_value());
    CHECK(!has(*r, "dbg"));
    CHECK(has(*r, "warn"));
    CHECK(has(*r, "\"fault\":{\"kind\":\"HARDFAULT\""));
    CHECK(has(*r, "\"cfsr_bits\":[\"PRECISERR\",\"BFARVALID\"]"));
    auto e = apiDiag(s, "node=base&level=E");
    CHECK_EQ(count(*e, "\"id\":"), size_t(1));
}

TEST(api_diag_paging_and_limit_clamp) {
    TmpDir t;
    DiagStore s(t.path);
    for (int i = 1; i <= 10; ++i) s.append(mk("base", 'I', "n" + std::to_string(i)));
    auto p1 = apiDiag(s, "node=base&limit=4");
    CHECK_EQ(count(*p1, "\"id\":"), size_t(4));
    CHECK(has(*p1, "\"next_before\":7}"));  // ids 10..7, the next page is before 7
    auto p2 = apiDiag(s, "node=base&limit=4&before=7");
    CHECK(has(*p2, "\"text\":\"n6\""));
    CHECK(!has(*p2, "\"text\":\"n7\""));
    CHECK(has(*p2, "\"next_before\":3}"));
    auto p3 = apiDiag(s, "node=base&limit=4&before=3");
    CHECK_EQ(count(*p3, "\"id\":"), size_t(2));
    CHECK(has(*p3, "\"next_before\":null}"));
    // limit above 200 is clamped, not an error
    for (int i = 0; i < 250; ++i) s.append(mk("remote", 'I', "r"));
    auto big = apiDiag(s, "node=remote&limit=9999");
    CHECK(big.has_value());
    CHECK_EQ(count(*big, "\"id\":"), size_t(200));
}

TEST(api_diag_rejects_bad_parameters) {
    TmpDir t;
    DiagStore s(t.path);
    CHECK(!apiDiag(s, "node=controller").has_value());
    CHECK(!apiDiag(s, "level=X").has_value());
    CHECK(!apiDiag(s, "before=abc").has_value());
    CHECK(!apiDiag(s, "limit=-1").has_value());
    CHECK(!apiDiag(s, "ack=").has_value());
}

TEST(api_diag_ack_clears_unseen_faults_and_calls_back) {
    TmpDir t;
    DiagStore s(t.path);
    s.append(mk("base", 'E', "f1", true));
    uint64_t id = s.append(mk("remote", 'E', "f2", true));
    CHECK_EQ(s.unseenFaults("base"), size_t(1));
    int called = 0;
    auto r = apiDiag(s, "ack=" + std::to_string(id), [&] { ++called; });
    CHECK(r.has_value());
    CHECK_EQ(called, 1);
    CHECK_EQ(s.unseenFaults("base"), size_t(1));  // acknowledged per node: the base fault stays unseen
    CHECK_EQ(s.unseenFaults("remote"), size_t(0));
}

TEST(status_json_has_last_fault_unseen_and_amber_dot_until_ack) {
    TmpDir t;
    DiagStore s(t.path);
    int64_t now = 1'700'000'000'000;
    mc::status::StatusRegistry reg([&] { return now; });
    reg.set(Node::Base, "connected", true);
    reg.set(Node::Base, "alive", true);
    auto j0 = mc::status::toJson(reg.snapshot());
    CHECK(has(j0, "{\"schema\":2,"));
    CHECK(has(j0, "\"dot\":\"green\""));
    CHECK(has(j0, "\"last_fault\":null,\"unseen_faults\":0"));

    uint64_t id = s.append(mk("base", 'E', "boom", true));
    publishFaultStatus(s, reg);
    auto j1 = mc::status::toJson(reg.snapshot());
    CHECK(has(j1, "\"base\":{\"dot\":\"yellow\""));
    CHECK(has(j1, "\"unseen_faults\":1"));
    CHECK(has(j1, "\"last_fault\":{\"id\":" + std::to_string(id)));
    CHECK(has(j1, "\"kind\":\"HARDFAULT\""));

    apiDiag(s, "ack=" + std::to_string(id), [&] { publishFaultStatus(s, reg); });
    auto j2 = mc::status::toJson(reg.snapshot());
    CHECK(has(j2, "\"base\":{\"dot\":\"green\""));
    CHECK(has(j2, "\"unseen_faults\":0"));
    CHECK(has(j2, "\"last_fault\":{\"id\":"));  // the fault stays shown, just acknowledged
}

TEST(http_route_diag) {
    using namespace mc::http;
    auto diag = [](const std::string& q) -> std::optional<std::string> {
        if (q == "bad") return std::nullopt;
        return "{\"q\":\"" + q + "\"}";
    };
    auto status = [] { return std::string("{}"); };
    Request req;
    CHECK(parseRequest("GET /api/diag?node=base&limit=5 HTTP/1.1\r\n\r\n", req) == ParseResult::Ok);
    CHECK_EQ(req.path, std::string("/api/diag"));
    CHECK_EQ(req.query, std::string("node=base&limit=5"));
    auto r = route(req, "<html>", status, diag);
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.contentType, std::string("application/json"));
    CHECK_EQ(r.body, std::string("{\"q\":\"node=base&limit=5\"}"));
    req.query = "bad";
    CHECK_EQ(route(req, "<html>", status, diag).status, 400);
    CHECK_EQ(route(Request{"GET", "/api/diag", ""}, "<html>", status).status, 404);  // no diag handler
    CHECK_EQ(route(Request{"POST", "/api/diag", ""}, "<html>", status, diag).status, 405);
}

TEST(console_diag_text_last_fault_per_node_and_last_20_lines) {
    TmpDir t;
    DiagStore s(t.path);
    CHECK(has(formatDiag(s), "no fault"));
    s.append(mk("base", 'E', "boom", true));
    for (int i = 1; i <= 30; ++i) s.append(mk("base", 'I', "line" + std::to_string(i)));
    s.append(mk("remote", 'W', "remote warn"));
    auto text = formatDiag(s);
    CHECK(has(text, "base: last fault HARDFAULT #3"));
    CHECK(has(text, "pc=0x08001234"));
    CHECK(has(text, "PRECISERR"));
    CHECK(has(text, "remote: no fault recorded"));
    CHECK(has(text, "line30"));
    CHECK(has(text, "line11"));
    CHECK(!has(text, "line10\n"));  // only the last 20 lines of the base
    CHECK(!has(text, "line9\n"));
    CHECK(has(text, "remote warn"));
    CHECK(has(text, "(unseen)"));
}

TEST(api_diag_kind_reset_finds_resets_older_than_one_page) {
    TmpDir t;
    DiagStore s(t.path);
    s.append(mk("base", 'I', "reset: PIN (csr 0x04000000)"));
    s.append(mk("remote", 'I', "boot, reset: PIN|POWER"));
    for (int i = 0; i < 450; ++i) s.append(mk("base", 'I', "ble: chatter"));
    auto r = apiDiag(s, "kind=reset&limit=10");
    CHECK(r.has_value());
    CHECK(r->find("reset: PIN (csr") != std::string::npos);
    CHECK(r->find("boot, reset: PIN|POWER") != std::string::npos);
    CHECK(r->find("chatter") == std::string::npos);
    CHECK(r->find("\"next_before\":null") != std::string::npos);
    CHECK(!apiDiag(s, "kind=other").has_value());
}
