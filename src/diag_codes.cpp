#include "midicontrol/diag_codes.hpp"

#include <cstdio>

#include "midicontrol/protocol/diag.hpp"

namespace mc::diag {

namespace pd = mc::protocol::diag;

namespace {
const char* faultKind(uint32_t k) {
    // The remote's FAULT arg uses the base's fault::Kind numbering; it emits kind 1 for both HardFault and NMI (told
    // apart by the ipsr detail, 3 or 2). A remote loop stall is its own record (LOOP_STALL), see decode().
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

std::string resetCause(uint32_t arg) {
    // The remote sends (RCC_CSR >> 24) & 0xFE (diagring::resetCauseFromCsr): bit1 OBL, 2 PIN, 3 PWR,
    // 4 SFT, 5 IWDG, 6 WWDG, 7 LPWR.
    static const char* names[] = {nullptr, "OBL", "PIN", "POWER", "SOFTWARE", "IWDG", "WWDG", "LOWPOWER"};
    std::string s;
    for (int b = 1; b <= 7; ++b)
        if (arg & (1u << b)) s += (s.empty() ? "" : "|") + std::string(names[b]);
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

std::string resetCauseText(uint32_t flags) { return resetCause(flags); }

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

std::optional<DiagEntry> DiagRespDecoder::flush() {
    auto r = std::move(pending_);
    pending_.reset();
    detail_ = 0;
    return r;
}

std::optional<DiagRespDecoded> DiagRespDecoder::decode(const uint8_t* p, size_t len, int64_t pcTimeMs) {
    if (!p || len < pd::RESP_HEADER_SIZE) return std::nullopt;
    size_t n = p[4];
    if (n > pd::MAX_RECORDS || len != pd::RESP_HEADER_SIZE + n * pd::RECORD_SIZE) return std::nullopt;
    DiagRespDecoded d;
    d.bootId = static_cast<uint16_t>(p[0] | (p[1] << 8));
    d.nextCursor = static_cast<uint16_t>(p[2] | (p[3] << 8));
    if (pending_ && pending_->boot != d.bootId)  // the node rebooted: the details will never come
        if (auto e = flush()) d.entries.push_back(std::move(*e));
    for (size_t i = 0; i < n; ++i) {
        pd::Record r = pd::decodeRecord(p + pd::RESP_HEADER_SIZE + i * pd::RECORD_SIZE);
        if (r.code == static_cast<uint8_t>(pd::Code::FAULT_DETAIL) && pending_ && detail_ < 4) {
            FaultInfo& f = *pending_->fault;
            if (detail_ == 0) f.pc = r.arg;
            else if (detail_ == 1) f.lr = r.arg;
            else if (detail_ == 2) f.xpsr = r.arg;  // 4th value (ipsr) is the handler's own, not kept
            if (++detail_ == 4) d.entries.push_back(*flush());
            continue;
        }
        if (auto e = flush()) d.entries.push_back(std::move(*e));
        DiagEntry e;
        e.pcTimeMs = pcTimeMs;
        e.node = "remote";
        e.boot = d.bootId;
        e.uptimeMs = r.uptimeMs;
        e.level = levelChar(r.level);
        e.tag = "diag";
        e.text = diagCodeText(r.code, r.arg);
        // FAULT and LOOP_STALL are both followed by four FAULT_DETAIL records (pc, lr, xpsr, ipsr; PROTOCOL.md)
        bool isFault = r.code == static_cast<uint8_t>(pd::Code::FAULT);
        bool isStall = r.code == static_cast<uint8_t>(pd::Code::LOOP_STALL);
        if (isFault || isStall) {
            FaultInfo f;
            f.kind = isStall ? "LOOP_STALL" : faultKind(r.arg);
            if (isStall) f.pc = r.arg;  // the stall's pc is the record's arg; the first detail repeats it
            f.uptimeMs = r.uptimeMs;
            e.fault = f;
            e.level = 'E';
            pending_ = std::move(e);
            detail_ = 0;
        } else {
            d.entries.push_back(std::move(e));
        }
    }
    return d;
}

std::optional<DiagRespDecoded> decodeDiagResp(const uint8_t* p, size_t len, int64_t pcTimeMs) {
    DiagRespDecoder dec;
    auto d = dec.decode(p, len, pcTimeMs);
    if (d)
        if (auto e = dec.flush()) d.value().entries.push_back(std::move(*e));
    return d;
}

}  // namespace mc::diag
