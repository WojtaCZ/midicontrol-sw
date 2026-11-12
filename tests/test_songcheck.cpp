#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "midicontrol/songcheck.hpp"
#include "test_framework.hpp"

#ifndef MC_SONGS_DIR
#define MC_SONGS_DIR "songs"
#endif

using namespace mc::songcheck;

namespace {
bool hasProblem(const FileCheck& c, const std::string& p) {
    return std::find(c.problems.begin(), c.problems.end(), p) != c.problems.end();
}

std::string tempDir() {
    char tmpl[] = "/tmp/mc_songcheck_XXXXXX";
    const char* d = mkdtemp(tmpl);
    if (!d) FAIL_AT("mkdtemp failed");
    return d;
}

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream f(path, std::ios::binary);
    f << bytes;
}
}  // namespace

TEST(songcheck_real_files) {
    DIR* dir = opendir(MC_SONGS_DIR);
    CHECK(dir != nullptr);
    int files = 0;
    while (dirent* e = readdir(dir)) {
        std::string name = e->d_name;
        if (name.size() < 4 || name.substr(name.size() - 4) != ".mid") continue;
        ++files;
        auto c = checkFile(std::string(MC_SONGS_DIR) + "/" + name);
        CHECK_EQ(c.file, name);
        if (name == "523.mid") {
            if (!hasProblem(c, "empty display anchor")) FAIL_AT("523.mid: no empty-anchor problem");
            CHECK_EQ(c.code, "523");
        } else if (name == "512.mid" || name == "513.mid") {
            // Shifted display digits (D24), checked in songcheck_warns_real_shifted_files.
            if (c.problems.size() != 1) FAIL_AT(name + ": expected exactly the shifted-digits warning");
        } else if (!c.problems.empty()) {
            closedir(dir);
            FAIL_AT(name + ": unexpected problem: " + c.problems[0]);
        }
    }
    closedir(dir);
    CHECK(files >= 11);
}

TEST(songcheck_bad_name_and_garbage) {
    auto dir = tempDir();
    auto path = dir + "/072 - Lide muj.mid";
    writeFile(path, "garbage");
    auto c = checkFile(path);
    CHECK(hasProblem(c, "file name is not a valid song code"));
    bool parse = false;
    for (const auto& p : c.problems) parse = parse || p.rfind("cannot parse: ", 0) == 0;
    CHECK(parse);
    unlink(path.c_str());
    rmdir(dir.c_str());
}

TEST(songcheck_scanner_reports_once) {
    auto dir = tempDir();
    auto path = dir + "/1.mid";
    writeFile(path, "garbage");
    writeFile(dir + "/notes.txt", "ignored");
    Scanner scanner;
    auto r1 = scanner.rescan(dir);
    CHECK_EQ(r1.all.size(), size_t(1));
    CHECK_EQ(r1.newProblems.size(), size_t(1));
    CHECK_EQ(r1.newProblems[0].first, "1.mid");
    auto r2 = scanner.rescan(dir);
    CHECK_EQ(r2.all.size(), size_t(1));
    CHECK_EQ(r2.newProblems.size(), size_t(0));

    // A changed file is parsed again; a problem it still has is not repeated.
    writeFile(path, "more garbage!");
    auto r3 = scanner.rescan(dir);
    CHECK_EQ(r3.newProblems.size(), size_t(0));

    unlink(path.c_str());
    auto r4 = scanner.rescan(dir);
    CHECK(r4.all.empty());
    unlink((dir + "/notes.txt").c_str());
    rmdir(dir.c_str());
}

TEST(songcheck_scanner_missing_folder) {
    Scanner scanner;
    auto r = scanner.rescan("/nonexistent/mc/folder");
    CHECK(r.all.empty());
    CHECK(r.newProblems.empty());
}

// ---------------------------------------------------------------------------
// FAULTS 2.8 / gap 31 / D24: shifted display digits are authoring mistakes;
// songcheck warns about them (it does not fix the file).
// ---------------------------------------------------------------------------
namespace {
// A one-track SMF: a full-display SysEx at tick 0, then one note.
std::string smfWithDisplay(const std::vector<uint8_t>& digits8) {
    std::string trk;
    std::string body = std::string("\x4D\x43\x01\x00", 4);
    for (uint8_t b : digits8) body += static_cast<char>(b);
    body += '\xF7';
    trk += '\x00';
    trk += '\xF0';
    trk += static_cast<char>(body.size());
    trk += body;
    trk += std::string("\x00\x90\x3C\x40", 4);
    trk += std::string("\x60\x80\x3C\x00", 4);
    trk += std::string("\x00\xFF\x2F\x00", 4);
    std::string f = std::string("MThd\x00\x00\x00\x06\x00\x00\x00\x01\x00\x60", 14);
    f += "MTrk";
    const uint32_t n = static_cast<uint32_t>(trk.size());
    f += static_cast<char>(n >> 24);
    f += static_cast<char>(n >> 16);
    f += static_cast<char>(n >> 8);
    f += static_cast<char>(n);
    return f + trk;
}

FileCheck checkSynthetic(const std::string& name, const std::vector<uint8_t>& digits8) {
    auto dir = tempDir();
    auto path = dir + "/" + name;
    writeFile(path, smfWithDisplay(digits8));
    auto c = checkFile(path);
    unlink(path.c_str());
    rmdir(dir.c_str());
    return c;
}
}  // namespace

TEST(songcheck_aligned_digits_no_warning) {
    // " 401" verse " 1": right-aligned, as authored in 401.mid.
    auto c = checkSynthetic("401.mid", {0x0F, 0x04, 0x00, 0x01, 0x0F, 0x01, 0x0F, 0x0F});
    CHECK(c.problems.empty());
    // A two-digit verse and a four-digit song are aligned too.
    auto d = checkSynthetic("1234.mid", {0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x0F, 0x0F});
    CHECK(d.problems.empty());
}

TEST(songcheck_warns_shifted_verse) {
    // Verse "1 " (1 in the tens position), as in 512.mid.
    auto c = checkSynthetic("512.mid", {0x0F, 0x05, 0x01, 0x02, 0x01, 0x0F, 0x0F, 0x0F});
    CHECK(hasProblem(c, kWarnVerseShifted));
    CHECK(!hasProblem(c, kWarnSongShifted));
}

TEST(songcheck_warns_shifted_song) {
    // Song "513 " (shifted left), as in 513.mid.
    auto c = checkSynthetic("513.mid", {0x05, 0x01, 0x03, 0x0F, 0x0F, 0x01, 0x0F, 0x0F});
    CHECK(hasProblem(c, kWarnSongShifted));
    CHECK(!hasProblem(c, kWarnVerseShifted));
    // A gap inside the number ("5 13") is shifted as well.
    auto d = checkSynthetic("513.mid", {0x0F, 0x05, 0x0F, 0x03, 0x0F, 0x01, 0x0F, 0x0F});
    CHECK(hasProblem(d, kWarnSongShifted));
}

TEST(songcheck_warns_real_shifted_files) {
    // The real authoring mistakes from the owner's folder, when present.
    const std::string d = MC_SONGS_DIR;
    if (access((d + "/512.mid").c_str(), R_OK) == 0) {
        auto c = checkFile(d + "/512.mid");
        CHECK(hasProblem(c, kWarnVerseShifted));
    }
    if (access((d + "/513.mid").c_str(), R_OK) == 0) {
        auto c = checkFile(d + "/513.mid");
        CHECK(hasProblem(c, kWarnSongShifted));
    }
}
