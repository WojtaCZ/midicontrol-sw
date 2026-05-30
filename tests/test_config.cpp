#include <stdexcept>

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
