#include <map>
#include <set>
#include <stdexcept>
#include <string>

#include "midicontrol/config.hpp"
#include "test_framework.hpp"

using namespace mc;

TEST(parse_config_basic) {
    std::string ini =
        "[FILES]\n"
        "PlayFolder = /tmp/play\n"
        "RecordFolder = /tmp/rec\n"
        "\n"
        "[BINDINGS]\n"
        "Bind0 = 072 - Lide muj.mid\n"
        "# Bind1 = commented out\n";
    Config cfg = parseConfig(ini);
    CHECK(cfg.playFolder == "/tmp/play");
    CHECK(cfg.recFolder == "/tmp/rec");
    auto b0 = cfg.binding("Bind0");
    CHECK(b0.has_value());
    CHECK(*b0 == "072 - Lide muj.mid");
    CHECK_FALSE(cfg.binding("Bind1").has_value());
}

TEST(parse_config_missing_play_folder_throws) {
    std::string ini =
        "[FILES]\n"
        "RecordFolder = /tmp/rec\n";
    bool threw = false;
    try {
        parseConfig(ini);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(parse_config_missing_files_section_throws) {
    bool threw = false;
    try {
        parseConfig("[BINDINGS]\nBind0 = x\n");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(parse_ini_handles_comments_and_semicolons) {
    auto sections = parseIni("; a comment\n[S]\nk = v\n# another\n");
    CHECK(sections.count("S") == 1);
    CHECK(sections["S"]["k"] == "v");
}

TEST(parse_config_playback) {
    std::string ini =
        "[FILES]\nPlayFolder = p\nRecordFolder = r\n"
        "[PLAYBACK]\nWarmupMs = 4500\nKeepPowerOn = yes\nVerseGapMs = 800\n";
    Config cfg = parseConfig(ini);
    CHECK_EQ(cfg.warmupMs, 4500);
    CHECK(cfg.keepPowerOn);
    CHECK_EQ(cfg.verseGapMs, 800);
}

TEST(parse_config_defaults_and_invalid_values) {
    Config cfg = parseConfig("[FILES]\nPlayFolder = p\nRecordFolder = r\n");
    CHECK_EQ(cfg.warmupMs, 3000);
    CHECK_FALSE(cfg.keepPowerOn);
    CHECK_EQ(cfg.verseGapMs, 0);

    const char* bad[] = {"[PLAYBACK]\nWarmupMs = abc\n", "[PLAYBACK]\nWarmupMs = -1\n",
                         "[PLAYBACK]\nVerseGapMs = -5\n", "[PLAYBACK]\nKeepPowerOn = maybe\n"};
    for (auto* b : bad) {
        bool threw = false;
        try {
            parseConfig(std::string("[FILES]\nPlayFolder = p\nRecordFolder = r\n") + b);
        } catch (const std::exception&) {
            threw = true;
        }
        if (!threw) FAIL_AT(std::string("accepted: ") + b);
    }
}

TEST(resolve_config_path_order) {
    std::map<std::string, std::string> env;
    std::set<std::string> files;
    auto getenv = [&](const char* n) -> const char* {
        auto it = env.find(n);
        return it == env.end() ? nullptr : it->second.c_str();
    };
    auto exists = [&](const std::string& p) { return files.count(p) > 0; };

    env["HOME"] = "/home/u";
    CHECK(resolveConfigPath("", getenv, exists) == "config.conf");  // nothing else exists
    files.insert("/home/u/.config/midicontrol/config.conf");
    CHECK(resolveConfigPath("", getenv, exists) == "/home/u/.config/midicontrol/config.conf");
    env["XDG_CONFIG_HOME"] = "/xdg";
    CHECK(resolveConfigPath("", getenv, exists) == "config.conf");  // /xdg/... missing
    files.insert("/xdg/midicontrol/config.conf");
    CHECK(resolveConfigPath("", getenv, exists) == "/xdg/midicontrol/config.conf");
    env["MIDICONTROL_CONFIG"] = "/etc/mc.conf";
    CHECK(resolveConfigPath("", getenv, exists) == "/etc/mc.conf");
    CHECK(resolveConfigPath("/cli.conf", getenv, exists) == "/cli.conf");
}

TEST(resolve_folder_paths) {
    CHECK(resolveFolder("~/music", "/etc/mc", "/home/u") == "/home/u/music");
    CHECK(resolveFolder("/abs/play", "/etc/mc", "/home/u") == "/abs/play");
    CHECK(resolveFolder("songs", "/etc/mc", "/home/u") == "/etc/mc/songs");
}
