#include "midicontrol/firmware_image.hpp"

namespace mc {

uint32_t crc32(const uint8_t* data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

}  // namespace mc

namespace mc::fwimage {

namespace {
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
}  // namespace

Check check(const std::vector<uint8_t>& file, uint8_t board, Info* info) {
    if (file.size() < kTrailerSize) return Check::NoTrailer;
    const uint8_t* t = file.data() + file.size() - kTrailerSize;
    if (le32(t + 20) != kMagic) return Check::NoTrailer;
    if (crc32(t, 16) != le32(t + 16) || t[13] != kFormat) return Check::BadTrailer;

    Info i;
    i.imageSize = le32(t + 0);
    i.imageCrc = le32(t + 4);
    i.version = le32(t + 8);
    i.boardId = t[12];
    if (i.boardId != board) return Check::WrongBoard;
    if (i.imageSize == 0 || i.imageSize != file.size() - kTrailerSize) return Check::BadSize;
    if (crc32(file.data(), i.imageSize) != i.imageCrc) return Check::BadCrc;
    if (info) *info = i;
    return Check::Ok;
}

std::string describe(Check c) {
    switch (c) {
        case Check::Ok: return "ok";
        case Check::NoTrailer:
            return "no image trailer - this looks like a plain .bin; use the <name>-update.bin "
                   "from the firmware build";
        case Check::BadTrailer: return "image trailer is corrupt or of an unknown format";
        case Check::WrongBoard: return "image is built for the other unit (board ID mismatch)";
        case Check::BadSize: return "image size in the trailer does not match the file";
        case Check::BadCrc: return "image CRC does not match the trailer";
    }
    return "unknown";
}

}  // namespace mc::fwimage
