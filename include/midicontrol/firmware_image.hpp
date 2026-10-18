#ifndef MIDICONTROL_FIRMWARE_IMAGE_HPP
#define MIDICONTROL_FIRMWARE_IMAGE_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Firmware update file (`<name>-update.bin`) as produced by the firmware builds
// (midicontrol-bootloader tools/fw_trailer.py):
//
//     <app.bin> <0xFF padding to a multiple of 8> <trailer, 24 B>
//
//   off  size  field
//    0    4    imageSize   bytes before the trailer
//    4    4    imageCrc    CRC-32 over those bytes
//    8    4    version     (major << 16) | (minor << 8) | patch
//   12    1    boardId     0x02 base, 0x03 remote (= protocol node address)
//   13    1    format      1
//   14    2    reserved
//   16    4    trailerCrc  CRC-32 over trailer bytes 0..15
//   20    4    magic       0x4D43544C
//
// All little-endian. The bootloaders reject a plain .bin at FW_UPDATE_END, so
// the PC checks the file before sending anything.
namespace mc::fwimage {

inline constexpr uint32_t kMagic = 0x4D43544Cu;
inline constexpr size_t kTrailerSize = 24;
inline constexpr uint8_t kFormat = 1;

enum class Check {
    Ok,
    NoTrailer,   // plain .bin (use the -update.bin)
    BadTrailer,  // trailer CRC / format wrong
    WrongBoard,  // built for the other unit
    BadSize,
    BadCrc,
};

struct Info {
    uint32_t imageSize = 0;
    uint32_t imageCrc = 0;
    uint32_t version = 0;
    uint8_t boardId = 0;
};

// Validate an update file for `board` (protocol address of the target).
Check check(const std::vector<uint8_t>& file, uint8_t board, Info* info = nullptr);

std::string describe(Check c);

}  // namespace mc::fwimage

namespace mc {
// IEEE-802.3 CRC-32 (matches zlib and the firmware-side boot::crc32).
uint32_t crc32(const uint8_t* data, std::size_t len);
}  // namespace mc

#endif  // MIDICONTROL_FIRMWARE_IMAGE_HPP
