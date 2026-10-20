#include "midicontrol/diag_codes.hpp"

#include <cstdio>

#include "midicontrol/protocol/diag.hpp"

namespace mc::diag {

namespace pd = mc::protocol::diag;

namespace {
const char* faultKind(uint32_t k) {
    // Same numbering as the base's fault::Kind.
    switch (k) {
        case 1: return "HARDFAULT";
        case 2: return "MEMMANAGE";
        case 3: return "BUSFAULT";
        case 4: return "USAGEFAULT";
        case 5: return "ERROR";
        case 6: return "LOOP_STALL";
    }
    return "UNKNOWN";
}

std::string resetCause(uint32_t csr) {
    // STM32G0 RCC_CSR reset flags.
    static const struct {
        int bit;
        const char* name;
    } f[] = {{25, "OBL"}, {26, "PIN"}, {27, "POWER"}, {28, "SOFTWARE"}, {29, "IWDG"}, {30, "WWDG"}, {31, "LOWPOWER"}};
    std::string s;
    for (const auto& x : f)
        if (csr & (1u << x.bit)) s += (s.empty() ? "" : "|") + std::string(x.name);
    return s;
}

char levelChar(uint8_t l) {
    switch (l) {
        case 1: return 'E';
        case 2: return 'W';
        case 4: return 'D';
    }
    return 'I';
}

std::string hex(uint32_t v) {
    char b[16];
    snprintf(b, sizeof b, "0x%08X", v);
    return b;
}
}  // namespace

std::string diagCodeText(uint8_t code, uint32_t arg) {
    using C = pd::Code;
    switch (static_cast<C>(code)) {
        case C::BOOT: {
            std::string r = resetCause(arg);
            return r.empty() ? "boot" : "boot, reset: " + r;
        }
        case C::FAULT: return std::string("fault ") + faultKind(arg);
        case C::FAULT_DETAIL: return "fault detail " + hex(arg);
        case C::LOOP_STALL: return "loop stall, pc " + hex(arg);
        case C::IWDG_RESET: return "watchdog reset";
        case C::BLE_CONNECT: return "ble connected";
        case C::BLE_DISCONNECT: return "ble disconnected";
        case C::BLE_SECURED: return "ble secured";
        case C::LINK_LOST: return "link lost";
        case C::RETRIES_EXHAUSTED: return "retries exhausted, msg type " + hex(arg);
        case C::CRC_ERRORS: return std::to_string(arg) + " crc errors";
        case C::CHARGER_FAULT: return "charger fault, REG09 " + hex(arg);
        case C::LOW_BATTERY: return "low battery " + std::to_string(arg) + " mV";
        case C::OTA_RESULT: return "ota result " + std::to_string(arg);
        case C::SHUTDOWN: return "shutdown, reason " + std::to_string(arg);
        case C::RECORDS_LOST: return std::to_string(arg) + " records lost";
    }
    char b[16];
    snprintf(b, sizeof b, "code 0x%02X", code);
    return b;
}

std::optional<DiagRespDecoded> decodeDiagResp(const uint8_t* p, size_t len, int64_t pcTimeMs) {
    if (!p || len < pd::RESP_HEADER_SIZE) return std::nullopt;
    size_t n = p[4];
    if (n > pd::MAX_RECORDS || len != pd::RESP_HEADER_SIZE + n * pd::RECORD_SIZE) return std::nullopt;
    DiagRespDecoded d;
    d.bootId = static_cast<uint16_t>(p[0] | (p[1] << 8));
    d.nextCursor = static_cast<uint16_t>(p[2] | (p[3] << 8));
    DiagEntry* fault = nullptr;  // FAULT entry still collecting details
    int detail = 0;
    for (size_t i = 0; i < n; ++i) {
        pd::Record r = pd::decodeRecord(p + pd::RESP_HEADER_SIZE + i * pd::RECORD_SIZE);
        if (r.code == static_cast<uint8_t>(pd::Code::FAULT_DETAIL) && fault && detail < 4) {
            uint32_t* dst[] = {&fault->fault->pc, &fault->fault->lr, &fault->fault->xpsr, nullptr};
            if (dst[detail]) *dst[detail] = r.arg;  // 4th value (ipsr) is the handler's own, not kept
            ++detail;
            continue;
        }
        fault = nullptr;
        DiagEntry e;
        e.pcTimeMs = pcTimeMs;
        e.node = "remote";
        e.boot = d.bootId;
        e.uptimeMs = r.uptimeMs;
        e.level = levelChar(r.level);
        e.tag = "diag";
        e.text = diagCodeText(r.code, r.arg);
        if (r.code == static_cast<uint8_t>(pd::Code::FAULT)) {
            FaultInfo f;
            f.kind = faultKind(r.arg);
            f.uptimeMs = r.uptimeMs;
            e.fault = f;
            e.level = 'E';
        }
        d.entries.push_back(std::move(e));
        if (d.entries.back().fault) {
            fault = &d.entries.back();
            detail = 0;
        }
    }
    return d;
}

}  // namespace mc::diag
