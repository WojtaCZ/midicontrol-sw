#include <string>

#include "midicontrol/log_line.hpp"
#include "test_framework.hpp"

using namespace mc::diag;

TEST(logline_parses_example) {
    auto l = parseLogLine("[    12.345] W ble: cmd failed: C\r\n");
    CHECK(l.has_value());
    CHECK_EQ(l->uptimeMs, uint64_t(12345));
    CHECK_EQ(l->level, 'W');
    CHECK_EQ(l->tag, std::string("ble"));
    CHECK_EQ(l->text, std::string("cmd failed: C"));
    CHECK(!l->truncated);
}

TEST(logline_rejects_malformed) {
    CHECK(!parseLogLine("").has_value());
    CHECK(!parseLogLine("[12.3] X").has_value());
    CHECK(!parseLogLine("garbage").has_value());
    CHECK(!parseLogLine("[    12.345] Q ble: x").has_value());
}

TEST(logline_long_line_is_kept) {
    std::string line = "[     1.000] I sys: " + std::string(300, 'a');
    auto l = parseLogLine(line);
    CHECK(l.has_value());
    CHECK_EQ(l->text.size(), size_t(300));
}

TEST(logline_trailing_tilde_means_truncated) {
    auto l = parseLogLine("[     0.010] I sys: long text~");
    CHECK(l.has_value());
    CHECK(l->truncated);
    CHECK_EQ(l->text, std::string("long text"));
}

TEST(logline_empty_text_and_big_uptime) {
    auto l = parseLogLine("[123456.789] E fault: x");
    CHECK(l.has_value());
    CHECK_EQ(l->uptimeMs, uint64_t(123456789));
}
