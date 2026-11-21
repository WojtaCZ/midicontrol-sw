#ifndef MIDICONTROL_PROTOCOL_CRC8_HPP
#define MIDICONTROL_PROTOCOL_CRC8_HPP

#include <cstddef>
#include <cstdint>

namespace mc::protocol {

// CRC-8 polynomial 0x31, init 0x00, no reflection, no final XOR, check("123456789") = 0xA2. Not CRC-8/MAXIM (that is reflected, poly 0x8C).
// Identical algorithm and lookup table to the firmware.
uint8_t crc8(const uint8_t* data, size_t len);

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_CRC8_HPP
