#include "midicontrol/fault_decode.hpp"

#include <cstdlib>
#include <cstring>

namespace mc::diag {

namespace {
struct BitName {
    uint8_t bit;
    const char* name;
};
const BitName kCfsr[] = {{0, "IACCVIOL"},   {1, "DACCVIOL"},    {3, "MUNSTKERR"}, {4, "MSTKERR"},  {5, "MLSPERR"},   {7, "MMARVALID"},
                         {8, "IBUSERR"},    {9, "PRECISERR"},   {10, "IMPRECISERR"}, {11, "UNSTKERR"}, {12, "STKERR"},  {13, "LSPERR"},
                         {15, "BFARVALID"}, {16, "UNDEFINSTR"}, {17, "INVSTATE"}, {18, "INVPC"},     {19, "NOCP"},    {20, "STKOF"},
                         {24, "UNALIGNED"}, {25, "DIVBYZERO"}};
const BitName kHfsr[] = {{1, "VECTTBL"}, {30, "FORCED"}, {31, "DEBUGEVT"}};

template <size_t N>
std::vector<std::string> names(uint32_t v, const BitName (&t)[N]) {
    std::vector<std::string> out;
    for (const auto& b : t)
        if (v & (1u << b.bit)) out.emplace_back(b.name);
    return out;
}

constexpr uint32_t kMmarValid = 1u << 7, kBfarValid = 1u << 15;

// Value of "key=0x...." or "key=<dec>" within s; false when the key is absent.
bool field(const std::string& s, const char* key, uint32_t& out) {
    std::string k = std::string(key) + "=";
    size_t p = 0;
    while ((p = s.find(k, p)) != std::string::npos) {
        if (p == 0 || s[p - 1] == ' ') break;
        p += k.size();
    }
    if (p == std::string::npos) return false;
    out = static_cast<uint32_t>(std::strtoul(s.c_str() + p + k.size(), nullptr, 0));
    return true;
}

bool isKind(const std::string& w) {
    static const char* k[] = {"HARDFAULT", "MEMMANAGE", "BUSFAULT", "USAGEFAULT", "ERROR", "LOOP_STALL", "UNKNOWN", "NMI"};
    for (const char* x : k)
        if (w == x) return true;
    return false;
}

// Splits "<KIND> #<n> rest" -> true.
bool header(const std::string& s, std::string& kind, uint32_t& count, std::string& rest) {
    size_t sp = s.find(' ');
    if (sp == std::string::npos) return false;
    kind = s.substr(0, sp);
    if (!isKind(kind) || sp + 1 >= s.size() || s[sp + 1] != '#') return false;
    char* end = nullptr;
    count = static_cast<uint32_t>(std::strtoul(s.c_str() + sp + 2, &end, 10));
    rest = end ? std::string(end) : std::string();
    return true;
}
}  // namespace

std::vector<std::string> decodeCfsr(uint32_t v) { return names(v, kCfsr); }
std::vector<std::string> decodeHfsr(uint32_t v) { return names(v, kHfsr); }

void BaseFaultParser::applyFars() {
    if (!cur_ || !haveFars_) return;
    cur_->mmfar = (cur_->hasCfsr && (cur_->cfsr & kMmarValid)) ? rawMmfar_ : 0;
    cur_->bfar = (cur_->hasCfsr && (cur_->cfsr & kBfarValid)) ? rawBfar_ : 0;
}

bool BaseFaultParser::feed(const LogLine& l) {
    if (l.tag != "fault" || l.level != 'E') return false;
    const std::string& s = l.text;
    std::string kind, rest;
    uint32_t count = 0;
    if (header(s, kind, count, rest)) {
        bool merge = cur_ && cur_->kind == kind && cur_->count == count && cur_->uptimeMs == 0 && cur_->rev.empty();
        if (!merge) {
            cur_ = FaultInfo{};
            cur_->kind = kind;
            cur_->count = count;
            haveFars_ = false;
        }
        uint32_t v;
        if (field(rest, "pc", v)) cur_->pc = v;
        if (field(rest, "lr", v)) cur_->lr = v;
        size_t c = rest.find(" code ");
        if (c != std::string::npos) cur_->code = static_cast<uint32_t>(std::strtoul(rest.c_str() + c + 6, nullptr, 10));
        size_t u = rest.find(" up ");
        if (u != std::string::npos) {
            char* end = nullptr;
            unsigned long sec = std::strtoul(rest.c_str() + u + 4, &end, 10);
            unsigned long ms = (end && *end == '.') ? std::strtoul(end + 1, &end, 10) : 0;
            cur_->uptimeMs = sec * 1000ull + ms;
        }
        size_t r = rest.find(" rev ");
        if (r != std::string::npos) cur_->rev = rest.substr(r + 5);
        return true;
    }
    if (!cur_) return false;
    uint32_t v;
    if (s.compare(0, 3, "pc=") == 0) {
        if (field(s, "pc", v)) cur_->pc = v;
        if (field(s, "lr", v)) cur_->lr = v;
        if (field(s, "xpsr", v)) cur_->xpsr = v;
        return true;
    }
    if (s.compare(0, 3, "r0=") == 0) return true;  // core registers: not kept
    if (s.compare(0, 5, "cfsr=") == 0) {
        field(s, "cfsr", v);
        cur_->cfsr = v;
        cur_->hasCfsr = true;
        cur_->cfsrBits = decodeCfsr(v);
        applyFars();
        return true;
    }
    if (s.compare(0, 5, "hfsr=") == 0) {
        field(s, "hfsr", v);
        cur_->hfsr = v;
        cur_->hfsrBits = decodeHfsr(v);
        return true;
    }
    if (s.compare(0, 6, "mmfar=") == 0) {
        if (field(s, "mmfar", v)) rawMmfar_ = v;
        if (field(s, "bfar", v)) rawBfar_ = v;
        haveFars_ = true;
        applyFars();
        return true;
    }
    return false;
}

std::optional<FaultInfo> BaseFaultParser::take() {
    auto r = std::move(cur_);
    cur_.reset();
    haveFars_ = false;
    return r;
}

}  // namespace mc::diag
