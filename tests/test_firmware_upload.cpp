#include <string>
#include <vector>

#include "midicontrol/firmware_upload.hpp"
#include "test_framework.hpp"

using namespace mc::fwupload;

namespace {

bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

void put32(std::string& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<char>(x >> (8 * i)));
}

// Same layout as midicontrol-bootloader tools/fw_trailer.py.
std::string makeUpdateFile(uint8_t board, size_t appSize = 100) {
    std::string f;
    for (size_t i = 0; i < appSize; ++i) f.push_back(static_cast<char>(i * 7));
    while (f.size() % 8) f.push_back(static_cast<char>(0xFF));
    const auto* p = reinterpret_cast<const uint8_t*>(f.data());
    std::string t;
    put32(t, static_cast<uint32_t>(f.size()));
    put32(t, mc::crc32(p, f.size()));
    put32(t, (1u << 16) | (2u << 8) | 3u);
    t.push_back(static_cast<char>(board));
    t.push_back(static_cast<char>(mc::fwimage::kFormat));
    t.push_back(0);
    t.push_back(0);
    put32(t, mc::crc32(reinterpret_cast<const uint8_t*>(t.data()), 16));
    put32(t, mc::fwimage::kMagic);
    return f + t;
}

struct Recorder {
    int calls = 0;
    Accepted got;
    std::string refuse;  // what start() answers
    Start fn() {
        return [this](Accepted a) {
            ++calls;
            got = std::move(a);
            return refuse;
        };
    }
};

}  // namespace

TEST(fwupload_targets) {
    CHECK(boardFor("base") == std::optional<uint8_t>(0x02));
    CHECK(boardFor("remote") == std::optional<uint8_t>(0x03));
    CHECK(!boardFor("controller").has_value());
    CHECK(!boardFor("").has_value());
    CHECK(!boardFor("BASE").has_value());
}

TEST(fwupload_accepts_matching_image) {
    Recorder rec;
    auto r = handlePost("base", makeUpdateFile(0x02), rec.fn());
    CHECK_EQ(r.status, 202);
    CHECK_EQ(r.contentType, "application/json");
    CHECK(has(r.body, "\"version\":\"1.2.3\""));
    CHECK(has(r.body, "\"target\":\"base\""));
    CHECK_EQ(rec.calls, 1);
    CHECK_EQ(static_cast<int>(rec.got.board), 0x02);
    CHECK_EQ(rec.got.image.size(), 128u);
    CHECK_EQ(rec.got.info.version, 0x010203u);

    Recorder rem;
    CHECK_EQ(handlePost("remote", makeUpdateFile(0x03), rem.fn()).status, 202);
    CHECK_EQ(static_cast<int>(rem.got.board), 0x03);
}

TEST(fwupload_refuses_the_other_units_image) {
    // The point of the feature: a controller image dropped on the base card (and back) never starts.
    Recorder rec;
    auto r = handlePost("base", makeUpdateFile(0x03), rec.fn());
    CHECK_EQ(r.status, 422);
    CHECK_EQ(r.contentType, "application/json");
    CHECK_EQ(r.body, "{\"error\":\"this is the controller firmware, not the base firmware; nothing was sent\"}");
    CHECK_EQ(rec.calls, 0);
    r = handlePost("remote", makeUpdateFile(0x02), rec.fn());
    CHECK_EQ(r.status, 422);
    CHECK(has(r.body, "base firmware"));
    CHECK_EQ(rec.calls, 0);
}

TEST(fwupload_names_an_unknown_board_id_as_the_page_does) {
    // Neither base nor controller: the page says "unknown unit", so the service must not say "the other unit".
    Recorder rec;
    auto r = handlePost("base", makeUpdateFile(0x07), rec.fn());
    CHECK_EQ(r.status, 422);
    CHECK_EQ(r.body, "{\"error\":\"this is built for an unknown unit (board ID 7); nothing was sent\"}");
    CHECK_EQ(handlePost("remote", makeUpdateFile(0xFF), rec.fn()).body,
             "{\"error\":\"this is built for an unknown unit (board ID 255); nothing was sent\"}");
    CHECK_EQ(rec.calls, 0);
}

TEST(fwupload_refuses_other_file_kinds) {
    Recorder rec;
    auto r = handlePost("base", std::string("\x7f" "ELF\x01\x01\x01", 7) + std::string(200, '\0'), rec.fn());
    CHECK_EQ(r.status, 422);
    CHECK(has(r.body, "ELF"));
    CHECK(has(r.body, "-update.bin"));
    r = handlePost("base", ":020000040800F2\n:00000001FF\n", rec.fn());
    CHECK_EQ(r.status, 422);
    CHECK(has(r.body, "hex"));
    r = handlePost("base", std::string(300, 'x'), rec.fn());  // plain .bin: no trailer
    CHECK_EQ(r.status, 422);
    CHECK_EQ(handlePost("base", "", rec.fn()).status, 422);
    auto bad = makeUpdateFile(0x02);
    bad[5] ^= 1;  // payload no longer matches the image CRC
    r = handlePost("base", bad, rec.fn());
    CHECK_EQ(r.status, 422);
    CHECK(has(r.body, "CRC"));
    CHECK_EQ(rec.calls, 0);
}

TEST(fwupload_unknown_target_and_busy) {
    Recorder rec;
    CHECK_EQ(handlePost("display", makeUpdateFile(0x02), rec.fn()).status, 404);
    CHECK_EQ(rec.calls, 0);
    rec.refuse = "a firmware update is already running";
    auto r = handlePost("base", makeUpdateFile(0x02), rec.fn());
    CHECK_EQ(r.status, 409);
    CHECK(has(r.body, "already running"));
    CHECK_EQ(rec.calls, 1);
}
