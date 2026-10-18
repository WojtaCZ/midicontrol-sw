#include <vector>

#include "midicontrol/firmware_image.hpp"
#include "test_framework.hpp"

using namespace mc::fwimage;
using Bytes = std::vector<uint8_t>;

namespace {

void put32(Bytes& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

// Same layout as midicontrol-bootloader tools/fw_trailer.py.
Bytes makeUpdateFile(uint8_t board, size_t appSize = 100) {
    Bytes f;
    for (size_t i = 0; i < appSize; ++i) f.push_back(static_cast<uint8_t>(i * 7));
    while (f.size() % 8) f.push_back(0xFF);
    const uint32_t imageSize = static_cast<uint32_t>(f.size());
    Bytes t;
    put32(t, imageSize);
    put32(t, mc::crc32(f.data(), f.size()));
    put32(t, (1u << 16) | (2u << 8) | 3u);
    t.push_back(board);
    t.push_back(kFormat);
    t.push_back(0);
    t.push_back(0);
    put32(t, mc::crc32(t.data(), 16));
    put32(t, kMagic);
    f.insert(f.end(), t.begin(), t.end());
    return f;
}

}  // namespace

TEST(crc32_check_value) {
    const char* s = "123456789";
    CHECK_EQ(mc::crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
}

TEST(fwimage_accepts_valid_update_file) {
    Info info;
    auto f = makeUpdateFile(0x02);
    CHECK(check(f, 0x02, &info) == Check::Ok);
    CHECK_EQ(info.imageSize, 104u);
    CHECK_EQ(info.version, 0x010203u);
    CHECK_EQ(static_cast<int>(info.boardId), 2);
    CHECK(check(makeUpdateFile(0x03), 0x03) == Check::Ok);
}

TEST(fwimage_rejects_plain_bin_and_wrong_board) {
    Bytes plain(200, 0x11);
    CHECK(check(plain, 0x02) == Check::NoTrailer);
    CHECK(check(Bytes{1, 2, 3}, 0x02) == Check::NoTrailer);
    CHECK(check(makeUpdateFile(0x03), 0x02) == Check::WrongBoard);
    CHECK(check(makeUpdateFile(0x02), 0x03) == Check::WrongBoard);
}

TEST(fwimage_rejects_corruption) {
    auto f = makeUpdateFile(0x02);
    auto badImage = f;
    badImage[5] ^= 0x01;
    CHECK(check(badImage, 0x02) == Check::BadCrc);

    auto badTrailer = f;
    badTrailer[badTrailer.size() - 24 + 8] ^= 0x01;  // version byte, covered by trailer CRC
    CHECK(check(badTrailer, 0x02) == Check::BadTrailer);

    auto extra = f;
    extra.insert(extra.begin(), 0x00);  // size no longer matches
    CHECK(check(extra, 0x02) == Check::BadSize);
}
