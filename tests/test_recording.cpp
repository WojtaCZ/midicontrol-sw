// PCC-1: recording file names are reserved atomically and unique within one second.
#include <ctime>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "midicontrol/recording.hpp"
#include "test_framework.hpp"

namespace fs = std::filesystem;

namespace {
fs::path freshDir(const char* tag) {
    auto d = fs::temp_directory_path() / (std::string("mc_recpath_") + tag + "_" + std::to_string(getpid()));
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
}  // namespace

TEST(recording_paths_in_the_same_second_are_unique) {
    auto d = freshDir("same");
    const std::time_t t = 1763200000;  // fixed second
    auto a = mc::recording::reservePath(d.string(), t);
    auto b = mc::recording::reservePath(d.string(), t);
    auto c = mc::recording::reservePath(d.string(), t);
    CHECK(!a.empty() && !b.empty() && !c.empty());
    CHECK(a != b && b != c && a != c);
    CHECK(fs::exists(a) && fs::exists(b) && fs::exists(c));  // reserved on disk
    CHECK(a.size() > 4 && a.compare(a.size() - 4, 4, ".mid") == 0);
    CHECK(b.find("-2.mid") != std::string::npos);
    fs::remove_all(d);
}

TEST(recording_path_never_reuses_an_existing_file) {
    auto d = freshDir("exist");
    const std::time_t t = 1763200000;
    auto a = mc::recording::reservePath(d.string(), t);
    CHECK(!a.empty());
    fs::resize_file(a, 0);
    auto b = mc::recording::reservePath(d.string(), t);
    CHECK(b != a);
    fs::remove_all(d);
}

TEST(recording_path_missing_folder_gives_empty) {
    auto d = freshDir("missing") / "nope";
    CHECK(mc::recording::reservePath(d.string(), 1763200000).empty());
}
