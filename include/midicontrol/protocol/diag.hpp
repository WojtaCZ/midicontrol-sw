#ifndef MIDICONTROL_PROTOCOL_DIAG_HPP
#define MIDICONTROL_PROTOCOL_DIAG_HPP

// Vendored copy of midicontrol-protocol inc/protocol/diag.hpp (namespace mc::protocol::diag).
// Keep in sync with the firmware repo.
//
// One record is 12 bytes, little-endian: seq u16, code u8, level u8, uptimeMs u32, arg u32.
// DIAG_GET  payload: [cursor u16 LE]
// DIAG_RESP payload: [boot_id u16 LE, next_cursor u16 LE, n u8, n x record], n <= MAX_RECORDS

#include <cstddef>
#include <cstdint>

namespace mc::protocol::diag {

constexpr size_t RECORD_SIZE = 12;
constexpr size_t MAX_RECORDS = 8;
constexpr size_t RESP_HEADER_SIZE = 5;
constexpr size_t MAX_RESP_PAYLOAD = RESP_HEADER_SIZE + MAX_RECORDS * RECORD_SIZE;  // 101

struct Record {
    uint16_t seq;
    uint8_t code;
    uint8_t level;
    uint32_t uptimeMs;
    uint32_t arg;
};

enum class Code : uint8_t {
    BOOT = 1,
    FAULT = 2,
    FAULT_DETAIL = 3,
    LOOP_STALL = 4,
    IWDG_RESET = 5,
    BLE_CONNECT = 6,
    BLE_DISCONNECT = 7,
    BLE_SECURED = 8,
    LINK_LOST = 9,
    RETRIES_EXHAUSTED = 10,
    CRC_ERRORS = 11,
    CHARGER_FAULT = 12,
    LOW_BATTERY = 13,
    OTA_RESULT = 14,
    SHUTDOWN = 15,
    RECORDS_LOST = 16,
    BLE_MODULE_REBOOT = 17,  // module reset on its own; arg = ms since the last one (0 = first)
    BLE_ERR_CONN = 18,       // link lost by supervision timeout; arg = last RSSI dBm (i8 low byte, 0x80 = unknown)
    STAGE_CLEAR_FAILED = 19, // remote bootloader applied a staged image but could not clear the W25Q pending flag; arg = 1
};

enum class Level : uint8_t { ERROR = 1, WARNING = 2, INFO = 3, DEBUG = 4 };

inline void encodeRecord(const Record& r, uint8_t* out12) {
    out12[0] = static_cast<uint8_t>(r.seq);
    out12[1] = static_cast<uint8_t>(r.seq >> 8);
    out12[2] = r.code;
    out12[3] = r.level;
    for (int i = 0; i < 4; ++i) {
        out12[4 + i] = static_cast<uint8_t>(r.uptimeMs >> (8 * i));
        out12[8 + i] = static_cast<uint8_t>(r.arg >> (8 * i));
    }
}

inline Record decodeRecord(const uint8_t* in12) {
    Record r{};
    r.seq = static_cast<uint16_t>(in12[0] | (in12[1] << 8));
    r.code = in12[2];
    r.level = in12[3];
    for (int i = 0; i < 4; ++i) {
        r.uptimeMs |= static_cast<uint32_t>(in12[4 + i]) << (8 * i);
        r.arg |= static_cast<uint32_t>(in12[8 + i]) << (8 * i);
    }
    return r;
}

}  // namespace mc::protocol::diag

#endif
