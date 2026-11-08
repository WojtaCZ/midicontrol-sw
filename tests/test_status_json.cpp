#include <string>

#include "midicontrol/status.hpp"
#include "midicontrol/status_json.hpp"
#include "test_framework.hpp"

using namespace mc::status;

namespace {
bool contains(const std::string& s, const std::string& part) {
    return s.find(part) != std::string::npos;
}
}  // namespace

TEST(json_escape_specials) {
    CHECK_EQ(jsonEscape("a\"b\\c\n\x01"), "\"a\\\"b\\\\c\\n\\u0001\"");
    CHECK_EQ(jsonEscape("tab\there"), "\"tab\\there\"");
}

TEST(json_escape_utf8) {
    CHECK_EQ(jsonEscape("Lid\xC3\xA9"), "\"Lid\xC3\xA9\"");
    CHECK_EQ(jsonEscape("\xE2\x9A\xA0"), "\"\xE2\x9A\xA0\"");  // 3-byte sequence kept
    CHECK_EQ(jsonEscape("\xFF"), "\"\\ufffd\"");
    CHECK_EQ(jsonEscape("\xC3"), "\"\\ufffd\"");          // truncated
    CHECK_EQ(jsonEscape("\xC3" "A"), "\"\\ufffdA\"");     // bad continuation
    CHECK_EQ(jsonEscape("\xC0\xAF"), "\"\\ufffd\\ufffd\"");  // overlong
}

TEST(json_schema_keys) {
    int64_t t = 5000;
    StatusRegistry r([&] { return t; });
    r.set(Node::Base, "device", std::string("/dev/ttyACM0"));
    r.set(Node::Base, "connected", false);
    r.set(Node::Base, "reconnects", int64_t(2));
    r.set(Node::Base, "unset", std::monostate{});
    r.count(Node::Base, "tx", 3);
    r.event(Node::Base, Level::Warn, "lost");
    auto j = toJson(r.snapshot());
    CHECK(contains(j, "{\"schema\":2,\"now_ms\":5000,"));
    CHECK(contains(j, "\"nodes\":{\"software\":{\"dot\":\"green\""));
    CHECK(contains(j, "\"base\":{\"dot\":\"yellow\""));
    CHECK(contains(j, "\"controller\":{\"dot\":\"grey\""));
    CHECK(contains(j, "\"device\":\"/dev/ttyACM0\""));
    CHECK(contains(j, "\"connected\":false"));
    CHECK(contains(j, "\"reconnects\":2"));
    CHECK(contains(j, "\"unset\":null"));
    CHECK(contains(j, "\"counters\":{\"tx\":3,\"warnings\":1}"));
    CHECK(contains(j, "\"events\":[{\"t\":5000,\"level\":\"WARN\",\"text\":\"lost\"}]"));
    CHECK(contains(j, "\"problems\":[{\"t\":5000,\"level\":\"WARN\",\"node\":\"base\",\"text\":\"lost\"}]"));
    CHECK(contains(j, "\"songs\":[]"));
    CHECK(j.back() == '}');
}

TEST(status_json_unset_organ_power_renders_null) {
    StatusRegistry r([] { return int64_t(1); });
    r.set(Node::Base, "organ_power", std::monostate{});
    CHECK(contains(toJson(r.snapshot()), "\"organ_power\":null"));
    r.set(Node::Base, "organ_power", true);
    CHECK(contains(toJson(r.snapshot()), "\"organ_power\":true"));
}

TEST(json_events_newest_first) {
    int64_t t = 1;
    StatusRegistry r([&] { return t; });
    r.event(Node::Software, Level::Info, "first");
    t = 2;
    r.event(Node::Software, Level::Info, "second");
    r.setSongs({{"523", "empty display anchor"}});
    auto j = toJson(r.snapshot());
    CHECK(j.find("second") < j.find("first"));
    CHECK(contains(j, "\"songs\":[{\"code\":\"523\",\"problem\":\"empty display anchor\"}]"));
}

TEST(status_json_renders_recording_bool) {
    StatusRegistry r([] { return int64_t(1); });
    r.set(Node::Software, "recording", true);
    CHECK(contains(toJson(r.snapshot()), "\"recording\":true"));
    r.set(Node::Software, "recording", false);
    CHECK(contains(toJson(r.snapshot()), "\"recording\":false"));
}
