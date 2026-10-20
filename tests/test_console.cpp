#include <string>

#include "midicontrol/console.hpp"
#include "test_framework.hpp"

using namespace mc::console;
using Kind = Command::Kind;

TEST(console_parse) {
    auto c = parse("  PLAY 406a ");
    CHECK(c.kind == Kind::Play);
    CHECK_EQ(c.arg, "406a");
    CHECK(parse("stop").kind == Kind::Stop);
    CHECK(parse("verse").kind == Kind::Verse);
    CHECK(parse("repeat").kind == Kind::Verse);
    CHECK(parse("list").kind == Kind::List);
    CHECK(parse("record").kind == Kind::Record);
    CHECK(parse("on").kind == Kind::On);
    CHECK(parse("OFF").kind == Kind::Off);
    CHECK(parse("status").kind == Kind::Status);
    CHECK(parse("quit").kind == Kind::Quit);
    CHECK(parse("exit").kind == Kind::Quit);
    CHECK(parse("").kind == Kind::Empty);
    CHECK(parse(" \t ").kind == Kind::Empty);
    CHECK(parse("play").kind == Kind::Unknown);
    auto u = parse("bogus thing");
    CHECK(u.kind == Kind::Unknown);
    CHECK_EQ(u.arg, "bogus thing");
}

TEST(console_parse_update) {
    auto c = parse("update Remote fw-update.bin");
    CHECK(c.kind == Kind::Update);
    CHECK_EQ(c.target, "remote");
    CHECK_EQ(c.arg, "fw-update.bin");
    auto sp = parse("update base /tmp/my fw-update.bin");
    CHECK(sp.kind == Kind::Update);
    CHECK_EQ(sp.arg, "/tmp/my fw-update.bin");
    auto bad = parse("update base");
    CHECK(bad.kind == Kind::Usage);
    CHECK(bad.arg.rfind("Usage: update <base|remote>", 0) == 0);
    CHECK(parse("update organ x.bin").kind == Kind::Usage);
}

TEST(console_absolutize) {
    CHECK_EQ(absolutize("update base fw.bin", "/home/a"), "update base /home/a/fw.bin");
    CHECK_EQ(absolutize("  UPDATE remote sub/fw.bin", "/home/a"), "update remote /home/a/sub/fw.bin");
    CHECK_EQ(absolutize("update base /abs/fw.bin", "/home/a"), "update base /abs/fw.bin");
    CHECK_EQ(absolutize("play 401", "/home/a"), "play 401");
    CHECK_EQ(absolutize("update base", "/home/a"), "update base");
}

TEST(console_end_marker) {
    CHECK_EQ(endMarker(true), "\x1eOK\n");
    CHECK_EQ(endMarker(false), "\x1e" "FAIL\n");
    CHECK(std::string(kCommandsHelp).find("update <base|remote>") != std::string::npos);
}

TEST(console_parses_diag) {
    using mc::console::Command;
    CHECK(mc::console::parse("diag").kind == Command::Kind::Diag);
    CHECK(mc::console::parse("  DIAG ").kind == Command::Kind::Diag);
    CHECK(std::string(mc::console::kCommandsHelp).find("diag") != std::string::npos);
}
