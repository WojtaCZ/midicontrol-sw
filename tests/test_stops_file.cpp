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
    SongStops s{{"401", {3, 4, 5}}, {"406B", {}}};
    std::string text = serialize(s);
    CHECK(has(text, "[SONGS]"));
    CHECK(has(text, "401=3 4 5"));
    CHECK(has(text, "406B="));
    std::vector<std::string> w;
    CHECK(parse(text, 42, &w) == s);
    CHECK(w.empty());
}
TEST(stops_file_parse_is_lenient) {
    std::vector<std::string> w;
    SongStops s = parse("# c\n[SONGS]\n406a = 7 5 5 99 x\nbad line\n401=1\n../x=2\n", 42, &w);
    CHECK_EQ(s.size(), 2u);
    CHECK(s.at("406A") == (std::vector<int>{5, 7}));
    CHECK(s.at("401") == (std::vector<int>{1}));
    CHECK(w.size() >= 3u);
}
TEST(stops_file_keeps_unknown_songs) {
    std::string dir = makeTempDir();
    std::string path = filePath(dir);
    writeFile(path, "[SONGS]\n999=2\n");
    Store st(path, 42);
    st.put("401", std::vector<int>{1});
    SongStops s = st.get();
    CHECK_EQ(s.size(), 2u);
    CHECK(s.count("999") == 1u);
    CHECK(s.at("401") == (std::vector<int>{1}));
}
TEST(stops_store_put_null_removes) {
    std::string dir = makeTempDir();
    Store st(filePath(dir), 42);
    st.put("401", std::vector<int>{1});
    st.put("401", std::nullopt);
    CHECK(st.get().count("401") == 0u);
}
TEST(stops_store_save_failure_keeps_file) {
    if (geteuid() == 0) return;
    std::string dir = makeTempDir();
    std::string path = filePath(dir);
    writeFile(path, "[SONGS]\n999=2\n");
    chmod(dir.c_str(), 0555);
    Store st(path, 42);
    bool threw = false;
    try { st.put("401", std::vector<int>{1}); } catch (const std::exception&) { threw = true; }
    chmod(dir.c_str(), 0755);
    CHECK(threw);
    CHECK_EQ(readFile(path), std::string("[SONGS]\n999=2\n"));
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
    CHECK(has(j, "{\"label\":\"PEDAL\",\"first\":8,\"last\":16}"));
    CHECK(has(j, "\"8\":\"Double Bass 16'\""));
    CHECK(has(j, "{\"code\":\"401\",\"stops\":[3]}"));
    CHECK(has(j, "{\"code\":\"403\",\"stops\":null}"));
}
TEST(stops_plan_for_song) {
    auto o = mc::organ::defaultOrgan();
    auto p = planFor(o, {{"401", {8}}}, "401");
    CHECK_EQ(p.preamble.size(), 2u);
    CHECK(p.dropProgramChanges);
    CHECK_EQ(p.text, std::string("8"));
    p = planFor(o, {{"401", {8}}}, "403");
    CHECK(p.preamble.empty());
    CHECK_FALSE(p.dropProgramChanges);
    CHECK_EQ(p.text, std::string("not set"));
    p = planFor(o, {{"401", {}}}, "401");
    CHECK_EQ(p.preamble.size(), 1u);
    CHECK_EQ(p.text, std::string("all off"));
    o.stopsEnabled = false;
    p = planFor(o, {{"401", {8}}}, "401");
    CHECK(p.preamble.empty());
    CHECK_EQ(p.text, std::string("disabled"));
}
TEST(stops_unreadable_file_is_not_treated_as_missing) {
    if (geteuid() == 0) return;
    std::string dir = makeTempDir();
    std::string path = filePath(dir);
    writeFile(path, "[SONGS]\n999=2\n");
    chmod(path.c_str(), 0000);
    Store st(path, 42);
    bool threw = false;
    try { st.put("401", std::vector<int>{1}); } catch (const std::exception&) { threw = true; }
    chmod(path.c_str(), 0644);
    CHECK(threw);
    CHECK_EQ(readFile(path), std::string("[SONGS]\n999=2\n"));
}
TEST(stops_file_line_without_valid_stop_is_not_set) {
    std::vector<std::string> w;
    SongStops s = parse("[SONGS]\n401=x\n402=99\n403=\n404=0 5\n", 42, &w);
    CHECK(s.count("401") == 0u);
    CHECK(s.count("402") == 0u);
    CHECK(s.count("403") == 1u);
    CHECK(s.at("403").empty());
    CHECK(s.at("404") == (std::vector<int>{5}));
    CHECK(w.size() >= 4u);
}
