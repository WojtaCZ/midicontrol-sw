#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "midicontrol/songs.hpp"
#include "test_framework.hpp"

#ifndef MC_SONGS_DIR
#define MC_SONGS_DIR "songs"
#endif

using namespace mc::songs;
namespace fs = std::filesystem;

namespace {
bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
}  // namespace

TEST(song_code_sanitize) {
    CHECK(sanitizeCode("406a") == std::optional<std::string>("406A"));
    CHECK(sanitizeCode(std::string("406A\0", 5)) == std::optional<std::string>("406A"));
    CHECK(sanitizeCode(" 401 ") == std::optional<std::string>("401"));
    CHECK(sanitizeCode("1234") == std::optional<std::string>("1234"));
    CHECK_FALSE(sanitizeCode("12345678").has_value());   // PCW-1: 1-4 digits only
    CHECK_FALSE(sanitizeCode("123456789").has_value());  // > 8 chars (frame bound)
    CHECK_FALSE(sanitizeCode("").has_value());
    CHECK_FALSE(sanitizeCode("../401").has_value());
    CHECK_FALSE(sanitizeCode("40 1").has_value());
    CHECK_FALSE(sanitizeCode("401.mid").has_value());
    CHECK_FALSE(sanitizeCode(std::vector<uint8_t>{0x34, 0x00, 0x31}).has_value());  // embedded NUL
    CHECK(sanitizeCode(std::vector<uint8_t>{'5', '2', '3'}) == std::optional<std::string>("523"));
}

TEST(song_lookup_by_code_case_insensitive) {
    auto p = findSongFile(MC_SONGS_DIR, "406a");
    CHECK(p.has_value());
    CHECK(endsWith(*p, "/406A.mid"));
    CHECK(findSongFile(MC_SONGS_DIR, "523").has_value());
    CHECK_FALSE(findSongFile(MC_SONGS_DIR, "999").has_value());
    CHECK_FALSE(findSongFile(MC_SONGS_DIR, "../songs/401").has_value());
}

TEST(song_lookup_matches_upper_case_extension) {
    fs::path dir = fs::temp_directory_path() / "mc_songs_test_case";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "407b.MID") << "x";
    std::ofstream(dir / "notes.txt") << "x";
    std::ofstream(dir / "072 - Lide muj.mid") << "x";  // not a valid code

    auto p = findSongFile(dir.string(), "407B");
    CHECK(p.has_value());
    CHECK(endsWith(*p, "407b.MID"));

    auto list = listSongs(dir.string());
    CHECK_EQ(list.size(), 1u);
    CHECK(list[0] == "407B");
    fs::remove_all(dir);
}

TEST(song_list_from_folder_sorted_naturally) {
    auto list = listSongs(MC_SONGS_DIR);
    std::vector<std::string> expected{"401", "403", "406A", "406B", "407", "512",
                                      "513", "516", "517", "519", "523"};
    CHECK(list == expected);
    CHECK(naturalLess("523", "1000"));
    CHECK(naturalLess("406A", "406B"));
    CHECK(naturalLess("406", "406A"));
    CHECK_FALSE(naturalLess("1000", "523"));
}

TEST(song_list_paging_fits_payload_and_covers_all) {
    std::vector<std::string> codes;
    for (int i = 0; i < 60; ++i) codes.push_back(std::to_string(1000 + i));

    std::vector<std::string> seen;
    size_t offset = 0;
    int pages = 0;
    while (true) {
        size_t count = 0;
        auto page = buildSongListPage(codes, offset, count);
        CHECK(page.size() <= 128);
        CHECK(count > 0);
        std::string body(page.begin() + 1, page.end());
        size_t start = 0;
        while (start <= body.size()) {
            size_t semi = body.find(';', start);
            if (semi == std::string::npos) semi = body.size();
            seen.push_back(body.substr(start, semi - start));
            start = semi + 1;
        }
        ++pages;
        offset += count;
        if (page[0] == 0) break;
        CHECK(pages < 10);
    }
    CHECK(seen == codes);
    CHECK_EQ(pages, 3);  // 1 + 25 * 5 - 1 = 125 bytes -> 25 per page

    size_t count = 0;
    auto first = buildSongListPage(codes, 0, count);
    CHECK_EQ(count, 25u);
    CHECK_EQ(static_cast<int>(first[0]), 1);
}

TEST(song_list_paging_past_end_and_empty) {
    std::vector<std::string> codes{"401", "403"};
    size_t count = 7;
    auto page = buildSongListPage(codes, 5, count);
    CHECK_EQ(count, 0u);
    CHECK(page == std::vector<uint8_t>{0x00});

    page = buildSongListPage(codes, 0, count);
    CHECK_EQ(count, 2u);
    CHECK(page == (std::vector<uint8_t>{0x00, '4', '0', '1', ';', '4', '0', '3'}));
}

TEST(song_list_offers_only_codes_play_can_find) {
    // Q1-2a: the song list (remote SONG_LIST paging, web /api/stops) must apply the same rule as play and
    // songcheck. A file name with spaces around a valid code is flagged by songcheck and not found by play,
    // so it must not be offered either.
    fs::path dir = fs::temp_directory_path() / "mc_songs_test_list_rule";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / " 12.mid") << "x";
    std::ofstream(dir / "13 .mid") << "x";
    std::ofstream(dir / "14a.mid") << "x";

    auto list = listSongs(dir.string());
    for (const auto& code : list) CHECK(findSongFile(dir.string(), code).has_value());
    CHECK_EQ(list.size(), 1u);
    CHECK(list.size() == 1u && list[0] == "14A");
    fs::remove_all(dir);
}
