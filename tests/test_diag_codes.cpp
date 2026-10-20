#include <string>
#include <vector>

#include "midicontrol/diag_codes.hpp"
#include "midicontrol/protocol/diag.hpp"
#include "test_framework.hpp"

using namespace mc::diag;
namespace pd = mc::protocol::diag;

namespace {
std::vector<uint8_t> resp(uint16_t boot, uint16_t next, const std::vector<pd::Record>& recs) {
    std::vector<uint8_t> v = {uint8_t(boot), uint8_t(boot >> 8), uint8_t(next), uint8_t(next >> 8), uint8_t(recs.size())};
    for (const auto& r : recs) {
        uint8_t b[pd::RECORD_SIZE];
        pd::encodeRecord(r, b);
        v.insert(v.end(), b, b + sizeof b);
    }
    return v;
}
}  // namespace

TEST(diagcodes_text) {
    CHECK_EQ(diagCodeText(13, 3412), std::string("low battery 3412 mV"));
    CHECK_EQ(diagCodeText(1, 0x14), std::string("boot, reset: PIN|SOFTWARE"));
    CHECK_EQ(diagCodeText(1, 0), std::string("boot"));
    CHECK_EQ(diagCodeText(0x7F, 0), std::string("code 0x7F"));
    CHECK_EQ(diagCodeText(16, 4), std::string("4 records lost"));
}

TEST(diagresp_three_records_with_unknown_code) {
    auto v = resp(7, 3, {{0, 6, 3, 100, 0}, {1, 0x7F, 2, 200, 0}, {2, 13, 2, 300, 3412}});
    auto d = decodeDiagResp(v.data(), v.size(), 1'000'000);
    CHECK(d.has_value());
    CHECK_EQ(d->bootId, uint16_t(7));
    CHECK_EQ(d->nextCursor, uint16_t(3));
    CHECK_EQ(d->entries.size(), size_t(3));
    CHECK_EQ(d->entries[0].node, std::string("remote"));
    CHECK_EQ(d->entries[0].level, 'I');
    CHECK_EQ(d->entries[0].uptimeMs, uint64_t(100));
    CHECK_EQ(d->entries[0].boot, uint32_t(7));
    CHECK_EQ(d->entries[0].pcTimeMs, int64_t(1'000'000));
    CHECK_EQ(d->entries[1].text, std::string("code 0x7F"));
    CHECK_EQ(d->entries[1].level, 'W');
    CHECK_EQ(d->entries[2].text, std::string("low battery 3412 mV"));
    CHECK_EQ(d->entries[2].tag, std::string("diag"));
}

TEST(diagresp_fault_detail_folded) {
    auto v = resp(2, 5, {{0, 2, 1, 900, 1},  // FAULT, kind 1 = HARDFAULT
                         {1, 3, 1, 900, 0x08001234}, {2, 3, 1, 900, 0x08005678}, {3, 3, 1, 900, 0x61000000}, {4, 3, 1, 900, 3}});
    auto d = decodeDiagResp(v.data(), v.size(), 5);
    CHECK(d.has_value());
    CHECK_EQ(d->entries.size(), size_t(1));
    const auto& e = d->entries[0];
    CHECK_EQ(e.level, 'E');
    CHECK(e.fault.has_value());
    CHECK_EQ(e.fault->kind, std::string("HARDFAULT"));
    CHECK_EQ(e.fault->pc, uint32_t(0x08001234));
    CHECK_EQ(e.fault->lr, uint32_t(0x08005678));
    CHECK_EQ(e.fault->xpsr, uint32_t(0x61000000));
}

TEST(diagresp_rejects_bad_length) {
    auto v = resp(1, 1, {{0, 6, 3, 1, 0}});
    CHECK(!decodeDiagResp(v.data(), v.size() - 1, 0).has_value());
    CHECK(!decodeDiagResp(v.data(), 3, 0).has_value());
    auto z = resp(1, 1, {});
    auto d = decodeDiagResp(z.data(), z.size(), 0);
    CHECK(d.has_value());
    CHECK(d->entries.empty());
}

TEST(diagresp_truncated_fault_detail_still_yields_entry) {
    auto v = resp(2, 2, {{0, 2, 1, 900, 2}, {1, 3, 1, 900, 0x1111}});
    auto d = decodeDiagResp(v.data(), v.size(), 5);
    CHECK(d.has_value());
    CHECK_EQ(d->entries.size(), size_t(1));
    CHECK_EQ(d->entries[0].fault->kind, std::string("MEMMANAGE"));
    CHECK_EQ(d->entries[0].fault->pc, uint32_t(0x1111));
}

TEST(diagcodes_boot_all_cause_bits) {
    // arg = (RCC_CSR >> 24) & 0xFE: bit1 OBL, 2 PIN, 3 POWER, 4 SOFTWARE, 5 IWDG, 6 WWDG, 7 LOWPOWER
    CHECK_EQ(diagCodeText(1, 0xFE), std::string("boot, reset: OBL|PIN|POWER|SOFTWARE|IWDG|WWDG|LOWPOWER"));
    CHECK_EQ(diagCodeText(1, 0x20), std::string("boot, reset: IWDG"));
}

TEST(diagresp_fault_split_across_responses) {
    DiagRespDecoder dec;
    // FAULT + 2 details in the first page (last slots), 2 details in the next
    auto a = resp(2, 3, {{0, 6, 3, 10, 0}, {1, 2, 1, 900, 1}, {2, 3, 1, 900, 0x08001234}});
    auto b = resp(2, 6, {{3, 3, 1, 900, 0x08005678}, {4, 3, 1, 900, 0x61000000}, {5, 3, 1, 900, 3}, {6, 6, 3, 950, 0}});
    auto d1 = dec.decode(a.data(), a.size(), 1);
    CHECK(d1.has_value());
    CHECK_EQ(d1->entries.size(), size_t(1));  // the incomplete FAULT is held back
    auto d2 = dec.decode(b.data(), b.size(), 2);
    CHECK(d2.has_value());
    CHECK_EQ(d2->entries.size(), size_t(2));
    CHECK(d2->entries[0].fault.has_value());
    CHECK_EQ(d2->entries[0].fault->pc, uint32_t(0x08001234));
    CHECK_EQ(d2->entries[0].fault->lr, uint32_t(0x08005678));
    CHECK_EQ(d2->entries[0].fault->xpsr, uint32_t(0x61000000));
    CHECK_EQ(d2->entries[1].text, std::string("ble connected"));
}

TEST(diagresp_boot_change_flushes_pending_fault) {
    DiagRespDecoder dec;
    auto a = resp(2, 2, {{0, 2, 1, 900, 1}, {1, 3, 1, 900, 0x1111}});
    auto b = resp(3, 1, {{0, 6, 3, 5, 0}});
    CHECK_EQ(dec.decode(a.data(), a.size(), 1)->entries.size(), size_t(0));
    auto d = dec.decode(b.data(), b.size(), 2);
    CHECK_EQ(d->entries.size(), size_t(2));
    CHECK(d->entries[0].fault.has_value());
    CHECK_EQ(d->entries[0].boot, uint32_t(2));
    CHECK_EQ(d->entries[0].fault->pc, uint32_t(0x1111));
    CHECK_EQ(d->entries[1].boot, uint32_t(3));
}
