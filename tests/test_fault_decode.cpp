#include <string>
#include <vector>

#include "midicontrol/fault_decode.hpp"
#include "midicontrol/log_line.hpp"
#include "test_framework.hpp"

using namespace mc::diag;
using Names = std::vector<std::string>;

TEST(fault_decode_cfsr) {
    CHECK(decodeCfsr(0x00008200) == (Names{"PRECISERR", "BFARVALID"}));
    CHECK(decodeCfsr(0).empty());
    CHECK(decodeCfsr(0x02000000) == (Names{"DIVBYZERO"}));
}

TEST(fault_decode_hfsr) {
    CHECK(decodeHfsr(0x40000000) == (Names{"FORCED"}));
    CHECK(decodeHfsr(0x80000002) == (Names{"VECTTBL", "DEBUGEVT"}));
}

namespace {
LogLine fl(const std::string& text) {
    LogLine l;
    l.uptimeMs = 1;
    l.level = 'E';
    l.tag = "fault";
    l.text = text;
    l.truncated = false;
    return l;
}

void feedAll(BaseFaultParser& p, const std::vector<std::string>& lines) {
    for (const auto& s : lines) CHECK(p.feed(fl(s)));
}
}  // namespace

TEST(fault_parser_banner_with_bfar) {
    BaseFaultParser p;
    feedAll(p, {"BUSFAULT #3 code 0 up 42.500 s rev abc1234",
                "pc=0x0800a1b2 lr=0x08009c01 xpsr=0x61000003 exc_return=0xffffffe9 ipsr=5",
                "r0=0x00000001 r1=0x00000002 r2=0x00000003 r3=0x00000004 r12=0x0000000c",
                "cfsr=0x00008200 PRECISERR BFARVALID",
                "hfsr=0x40000000 FORCED",
                "mmfar=0x11111111 bfar=0x40021000 afsr=0x00000000 msp=0x20001000 psp=0x00000000"});
    auto f = p.take();
    CHECK(f.has_value());
    CHECK_EQ(f->kind, std::string("BUSFAULT"));
    CHECK_EQ(f->count, uint32_t(3));
    CHECK_EQ(f->rev, std::string("abc1234"));
    CHECK_EQ(f->uptimeMs, uint64_t(42500));
    CHECK_EQ(f->pc, uint32_t(0x0800a1b2));
    CHECK_EQ(f->lr, uint32_t(0x08009c01));
    CHECK_EQ(f->xpsr, uint32_t(0x61000003));
    CHECK(f->hasCfsr);
    CHECK_EQ(f->cfsr, uint32_t(0x8200));
    CHECK((f->cfsrBits == Names{"PRECISERR", "BFARVALID"}));
    CHECK((f->hfsrBits == Names{"FORCED"}));
    CHECK_EQ(f->bfar, uint32_t(0x40021000));
    CHECK_EQ(f->mmfar, uint32_t(0));  // MMARVALID not set -> not exposed
    CHECK(!p.take().has_value());
}

TEST(fault_parser_bfar_hidden_without_valid_bit) {
    BaseFaultParser p;
    feedAll(p, {"HARDFAULT #1 code 0 up 1.000 s rev abc1234",
                "cfsr=0x00000400 IMPRECISERR",
                "mmfar=0x11111111 bfar=0x22222222 afsr=0x00000000 msp=0x0 psp=0x0"});
    auto f = p.take();
    CHECK(f.has_value());
    CHECK_EQ(f->bfar, uint32_t(0));
    CHECK_EQ(f->mmfar, uint32_t(0));
}

TEST(fault_parser_record_time_line_then_take) {
    BaseFaultParser p;
    CHECK(p.feed(fl("HARDFAULT #2 pc=0x08001234 lr=0x08005678 code 0")));
    auto f = p.take();
    CHECK(f.has_value());
    CHECK_EQ(f->kind, std::string("HARDFAULT"));
    CHECK_EQ(f->count, uint32_t(2));
    CHECK_EQ(f->pc, uint32_t(0x08001234));
    CHECK_EQ(f->lr, uint32_t(0x08005678));
    CHECK(!f->hasCfsr);
}

TEST(fault_parser_record_time_merges_with_banner) {
    BaseFaultParser p;
    CHECK(p.feed(fl("ERROR #1 pc=0x00000000 lr=0x08000101 code 32769")));
    CHECK(p.feed(fl("ERROR #1 code 32769 up 5.000 s rev abc1234")));
    auto f = p.take();
    CHECK(f.has_value());
    CHECK_EQ(f->code, uint32_t(32769));
    CHECK_EQ(f->lr, uint32_t(0x08000101));
    CHECK_EQ(f->uptimeMs, uint64_t(5000));
}

TEST(fault_parser_ignores_unrelated_lines) {
    BaseFaultParser p;
    LogLine l = fl("last HARDFAULT #1 kept (dump)");
    l.level = 'I';
    CHECK(!p.feed(l));
    l = fl("cmd failed");
    l.tag = "ble";
    CHECK(!p.feed(l));
    CHECK(!p.take().has_value());
}
