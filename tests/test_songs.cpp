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

static size_t countCodes(const std::vector<uint8_t>& page) {
    if (page.size() <= 1) return 0;
    size_t n = 1;
    for (size_t i = 1; i < page.size(); ++i) n += page[i] == ';';
    return n;
}

TEST(song_list_paging_600_codes_u16_offsets) {
    // A9: no cap; offsets above 255 page correctly and the response count is the offset advance
    std::vector<std::string> codes;
    for (int i = 0; i < 600; ++i) codes.push_back(std::to_string(1000 + i));

    size_t count = 0;
    auto page = buildSongListPage(codes, 300, count);
    CHECK_EQ(count, 25u);
    CHECK_EQ(countCodes(page), count);
    CHECK_EQ(static_cast<int>(page[0]), 1);
    CHECK(std::string(page.begin() + 1, page.begin() + 5) == "1300");

    // walk the whole list advancing only by the codes received
    size_t offset = 0, total = 0;
    int pages = 0;
    while (true) {
        page = buildSongListPage(codes, offset, count);
        CHECK_EQ(countCodes(page), count);
        CHECK(page[0] == 0 || page[0] == 1);  // reserved bits stay 0
        total += count;
        offset += count;
        ++pages;
        if (page[0] == 0) break;
        CHECK(pages < 100);
    }
    CHECK_EQ(total, 600u);
    CHECK_EQ(offset, 600u);

    // last page: hasMore 0; an offset at or past the end: [0x00] and an empty list
    page = buildSongListPage(codes, 599, count);
    CHECK_EQ(count, 1u);
    CHECK_EQ(static_cast<int>(page[0]), 0);
    page = buildSongListPage(codes, 600, count);
    CHECK_EQ(count, 0u);
    CHECK(page == std::vector<uint8_t>{0x00});
    page = buildSongListPage(codes, 60000, count);
    CHECK_EQ(count, 0u);
    CHECK(page == std::vector<uint8_t>{0x00});
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

TEST(b4_leading_zero_file_found_by_plain_code) {
    fs::path dir = fs::temp_directory_path() / "mc_songs_test_b4";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream(dir / "0406.mid") << "x";
    auto p = findSongFile(dir.string(), "406");
    CHECK(p.has_value());
    CHECK(endsWith(*p, "0406.mid"));
    CHECK(findSongFile(dir.string(), "0406").has_value());
    auto list = listSongs(dir.string());
    CHECK_EQ(list.size(), 1u);
    CHECK(list[0] == "406");
    mc::songs::setNormalise(false);
    CHECK_FALSE(findSongFile(dir.string(), "406").has_value());
    mc::songs::setNormalise(true);
    fs::remove_all(dir);
}
