#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "midicontrol/organ.hpp"
#include "midicontrol/stops_file.hpp"
#include "test_framework.hpp"

using namespace mc::stops;

namespace {
std::string makeTempDir() {
    char tmpl[] = "/tmp/mc-stops-XXXXXX";
    char* d = mkdtemp(tmpl);
    if (!d) FAIL_AT("mkdtemp failed");
    return d;
}
void writeFile(const std::string& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }
}  // namespace

TEST(stops_file_roundtrip) {
    std::string text = serialize("401", {3, 4, 5});
    CHECK(has(text, "[STOPS]"));
    CHECK(has(text, "Stops = 3 4 5"));
    std::vector<std::string> w;
    CHECK(parse(text, 42, "401.conf", &w) == Stops(std::vector<int>{3, 4, 5}));
    CHECK(parse(serialize("406B", {}), 42, "406B.conf", &w) == Stops(std::vector<int>{}));
    CHECK(w.empty());
}
TEST(stops_file_parse_is_lenient) {
    std::vector<std::string> w;
    Stops s = parse("# c\n[STOPS]\nbad line\nOther = 1\nstops = 7 5 5 99 x\n", 42, "401.conf", &w);
    CHECK(s == Stops(std::vector<int>{5, 7}));
    CHECK(w.size() >= 4u);
    CHECK(has(w[0], "401.conf:3: "));
}
TEST(stops_file_line_without_valid_stop_is_not_set) {
    std::vector<std::string> w;
    CHECK_FALSE(parse("[STOPS]\nStops = x\n", 42, "a", &w).has_value());
    CHECK_FALSE(parse("[STOPS]\nStops = 99\n", 42, "a", &w).has_value());
    CHECK_FALSE(parse("[STOPS]\n", 42, "a", &w).has_value());
    CHECK_FALSE(parse("", 42, "a", &w).has_value());
    CHECK(parse("[STOPS]\nStops =\n", 42, "a", &w) == Stops(std::vector<int>{}));
    CHECK(parse("[STOPS]\nStops = 0 5\n", 42, "a", &w) == Stops(std::vector<int>{5}));
}
TEST(stops_file_section_header_is_forgiving) {
    std::vector<std::string> w;
    CHECK(parse("[stops]\nStops=1 2\n", 42, "a", &w) == Stops(std::vector<int>{1, 2}));
    CHECK(parse(" [Stops] # mine\nSTOPS = 4\n", 42, "a", &w) == Stops(std::vector<int>{4}));
    CHECK(parse("# hand written\nStops = 3\n", 42, "a", &w) == Stops(std::vector<int>{3}));
    CHECK(w.empty());
    CHECK_FALSE(parse("[OTHER]\nStops = 3\n", 42, "a", &w).has_value());
    CHECK_EQ(w.size(), 1u);
}
TEST(stops_file_one_file_per_song) {
    std::string dir = makeTempDir();
    writeFile(dir + "/401.mid", "");
    writeFile(dir + "/406a.mid", "");
    save(dir, "401", {1, 8});
    save(dir, "406A", {});
    CHECK_EQ(readFile(dir + "/401.conf"), serialize("401", {1, 8}));
    CHECK(has(readFile(dir + "/406a.conf"), "Stops = \n"));  // named like the song's .mid
    CHECK(load(dir, "401", 42, nullptr) == Stops(std::vector<int>{1, 8}));
    CHECK(load(dir, "406A", 42, nullptr) == Stops(std::vector<int>{}));
    CHECK_FALSE(load(dir, "403", 42, nullptr).has_value());
    writeFile(dir + "/403.CONF", "[STOPS]\nStops = 2\n");  // hand-made, any case
    CHECK(load(dir, "403", 42, nullptr) == Stops(std::vector<int>{2}));
    save(dir, "403", {3});
    CHECK(has(readFile(dir + "/403.CONF"), "Stops = 3"));
    remove(dir, "403");
    CHECK_FALSE(findFile(dir, "403").has_value());
    remove(dir, "403");  // nothing left: no error
}
TEST(stops_store_get_and_put) {
    std::string dir = makeTempDir();
    Store st(dir, 42);
    st.put("401", std::vector<int>{1});
    st.put("402", std::vector<int>{2});
    SongStops s = st.get({"401", "402", "403"});
    CHECK_EQ(s.size(), 2u);
    CHECK(s.at("401") == (std::vector<int>{1}));
    st.put("401", std::nullopt);
    CHECK_FALSE(findFile(dir, "401").has_value());
    CHECK(st.get({"401", "402"}).count("401") == 0u);
    CHECK(findFile(dir, "402").has_value());  // other songs untouched
}
TEST(stops_store_save_failure_keeps_file) {
    if (geteuid() == 0) return;
    std::string dir = makeTempDir();
    writeFile(dir + "/401.conf", "[STOPS]\nStops = 2\n");
    chmod(dir.c_str(), 0555);
    Store st(dir, 42);
    bool threw = false, threwRemove = false;
    try { st.put("401", std::vector<int>{1}); } catch (const std::exception&) { threw = true; }
    try { st.put("401", std::nullopt); } catch (const std::exception&) { threwRemove = true; }
    chmod(dir.c_str(), 0755);
    CHECK(threw);
    CHECK(threwRemove);
    CHECK_EQ(readFile(dir + "/401.conf"), std::string("[STOPS]\nStops = 2\n"));
}
TEST(stops_unreadable_file_is_not_treated_as_missing) {
    if (geteuid() == 0) return;
    std::string dir = makeTempDir();
    writeFile(dir + "/401.conf", "[STOPS]\nStops = 2\n");
    chmod((dir + "/401.conf").c_str(), 0000);
    bool threw = false;
    try { load(dir, "401", 42, nullptr); } catch (const std::exception&) { threw = true; }
    chmod((dir + "/401.conf").c_str(), 0644);
    CHECK(threw);
}
TEST(stops_parse_body) {
    auto r = parseBody("{\"stops\":[3,1,3]}", 42);
    CHECK(r.has_value() && r->has_value());
    CHECK(**r == (std::vector<int>{1, 3}));
    r = parseBody("{\"stops\":null}", 42);
    CHECK(r.has_value());
    CHECK_FALSE(r->has_value());
    r = parseBody(" { \"stops\" : [ ] } ", 42);
    CHECK(r.has_value() && r->has_value());
    CHECK((*r)->empty());
    CHECK_FALSE(parseBody("{\"stops\":[0]}", 42).has_value());
    CHECK_FALSE(parseBody("{\"stops\":[43]}", 42).has_value());
    CHECK_FALSE(parseBody("{\"stops\":[\"3\"]}", 42).has_value());
    CHECK_FALSE(parseBody("{}", 42).has_value());
    CHECK_FALSE(parseBody("garbage", 42).has_value());
}
TEST(stops_json_shape) {
    std::string j = toJson(mc::organ::defaultOrgan(), {"401", "403"}, {{"401", {3}}});
    CHECK(has(j, "\"schema\":1"));
    CHECK(has(j, "{\"label\":\"Pedál\",\"first\":8,\"last\":16}"));
    CHECK(has(j, "\"8\":\"Kontrabas 16'\""));
    CHECK(has(j, "{\"code\":\"401\",\"stops\":[3]}"));
    CHECK(has(j, "{\"code\":\"403\",\"stops\":null}"));
}
TEST(stops_plan_for_song) {
    auto o = mc::organ::defaultOrgan();
    auto p = planFor(o, std::vector<int>{8});
    CHECK_EQ(p.preamble.size(), 2u);
    CHECK(p.dropProgramChanges);
    CHECK_EQ(p.text, std::string("8"));
    p = planFor(o, std::nullopt);
    CHECK(p.preamble.empty());
    CHECK_FALSE(p.dropProgramChanges);
    CHECK_EQ(p.text, std::string("not set"));
    p = planFor(o, std::vector<int>{});
    CHECK_EQ(p.preamble.size(), 1u);
    CHECK_EQ(p.text, std::string("all off"));
    o.stopsEnabled = false;
    p = planFor(o, std::vector<int>{8});
    CHECK(p.preamble.empty());
    CHECK_EQ(p.text, std::string("disabled"));
}
TEST(stops_put_handler_validates) {
    mc::organ::OrganConfig o = mc::organ::defaultOrgan();
    std::string dir = makeTempDir();
    Store st(dir, o.stopCount());
    std::vector<std::string> songs = {"401", "406A"};

    CHECK_EQ(handlePut(st, o, songs, "999", "{\"stops\":[1]}").status, 400);   // not in the folder
    CHECK_EQ(handlePut(st, o, songs, "../x", "{\"stops\":[1]}").status, 400);  // not a song code
    CHECK_EQ(handlePut(st, o, songs, "401", "{\"stops\":[0]}").status, 400);   // bad body
    CHECK_EQ(handlePut(st, o, songs, "401", "nonsense").status, 400);

    auto r = handlePut(st, o, songs, "401", "{\"stops\":[2,1]}");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, std::string("{\"code\":\"401\",\"stops\":[1,2]}"));
    CHECK(st.get(songs).at("401") == (std::vector<int>{1, 2}));

    r = handlePut(st, o, songs, "406a", "{\"stops\":[3]}");  // normalised like song codes
    CHECK_EQ(r.status, 200);
    CHECK(st.get(songs).count("406A") == 1u);

    r = handlePut(st, o, songs, "401", "{\"stops\":null}");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.body, std::string("{\"code\":\"401\",\"stops\":null}"));
    CHECK(st.get(songs).count("401") == 0u);

    if (geteuid() != 0) {
        chmod(dir.c_str(), 0500);  // the save cannot create its temp file
        r = handlePut(st, o, songs, "401", "{\"stops\":[1]}");
        chmod(dir.c_str(), 0700);
        CHECK_EQ(r.status, 500);
        CHECK(!r.body.empty());
    }
}
