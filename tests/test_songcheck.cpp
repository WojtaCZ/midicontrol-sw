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
        } else if (name == "rejstriky.mid") {
            // Organ stop-list reference file, not a song: its name is not a song code.
            if (!hasProblem(c, "file name is not a valid song code"))
                FAIL_AT("rejstriky.mid: no bad-name problem");
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
