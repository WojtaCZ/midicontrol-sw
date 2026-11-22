#include <string>

#include "midicontrol/web_page.hpp"
#include "test_framework.hpp"

namespace {
bool has(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }
}  // namespace

TEST(web_page_self_contained) {
    const std::string& html = mc::web::statusPageHtml();
    CHECK(html.rfind("<!doctype html>", 0) == 0);
    CHECK(has(html, "/api/status"));
    CHECK(has(html, "Overview"));
    CHECK(has(html, "Software"));
    CHECK(has(html, "Base"));
    CHECK(has(html, "Controller"));
    CHECK(has(html, "prefers-color-scheme"));
    // No internet: nothing loaded from anywhere else.
    CHECK(!has(html, "http://"));
    CHECK(!has(html, "https://"));
    CHECK(!has(html, "<script src"));
    CHECK(!has(html, "<link"));
    CHECK(!has(html, "@import"));
    // Data is inserted as text, never parsed as markup.
    CHECK(!has(html, "innerHTML"));
}

TEST(web_page_fetch_times_out) {
    // A frozen service must turn the page to "no connection", not just age it.
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "AbortController"));
    CHECK(has(html, "bez spojení"));
    CHECK(has(html, "stale"));  // the last known state is greyed out
}

TEST(web_page_has_diagnostics_tab) {
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "Diagnostics"));
    CHECK(has(html, "/api/diag"));
    CHECK(has(html, "/api/diag/ack")); // "mark seen" is a POST (B7)
    CHECK(!has(html, "&ack="));
    CHECK(has(html, "last_fault"));    // per-node last fault comes from /api/status
    CHECK(has(html, "0x1FF"));         // interrupted exception = xpsr & 0x1FF
    CHECK(has(html, "BFARVALID"));     // BFAR/MMFAR only when valid
    CHECK(has(html, "MMARVALID"));
    CHECK(!has(html, "innerHTML"));
    CHECK(!has(html, "http://"));
    CHECK(!has(html, "https://"));
}

TEST(web_page_home_and_advanced) {
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "Přehled"));     // home: health, now playing, stops
    CHECK(has(html, "Pokročilé"));   // Advanced: the debug pages
    CHECK(has(html, "#adv/"));
    CHECK(has(html, "Právě hraje"));
    CHECK(has(html, "Vše v pořádku"));
    CHECK(has(html, "unseen_faults"));
}

TEST(web_page_has_stops_editor) {
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "Rejstříky písní"));
    CHECK(has(html, "/api/stops"));
    CHECK(has(html, "application/json"));  // the PUT is refused without it
    CHECK(has(html, "PUT"));
    CHECK(has(html, "nenastaveno"));       // no entry: the organ is left as it is
    CHECK(has(html, "vše vypnuto"));       // saved with zero stops
    CHECK(has(html, "neuložené změny"));
    CHECK(has(html, "Zrušit nastavení"));  // the button that forgets a song's stops
    CHECK(has(html, "beforeunload"));
    CHECK(!has(html, "innerHTML"));
}

TEST(web_page_song_list_scales) {
    // 100+ songs: search, filter, sort, a count, and the list keeps its scroll position on redraw.
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "Hledat píseň"));
    CHECK(has(html, "Nenastavené"));
    CHECK(has(html, "nejvíc rejstříků"));
    CHECK(has(html, "Zobrazeno "));
    CHECK(has(html, "E.list.scrollTop=top"));
}

TEST(web_page_has_firmware_upload) {
    // Advanced -> Firmware: one card per unit; the browser reads the trailer before anything is sent.
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "\"firmware\",\"Firmware\""));
    CHECK(has(html, "/api/firmware/"));
    CHECK(has(html, "application/octet-stream"));
    CHECK(has(html, "-update.bin"));
    CHECK(has(html, "0x4D43544C"));     // trailer magic
    CHECK(has(html, "0xEDB88320"));     // CRC-32, the same as fwimage::check
    CHECK(has(html, "fw_update_state")); // progress comes from /api/status
    CHECK(has(html, "dragover"));
    CHECK(!has(html, "innerHTML"));
    CHECK(!has(html, "http://"));
}

TEST(web_page_home_last_end_is_czech) {
    // US-9: comm.cpp writes "ended normally" / "stopped (base disconnected)" / "failed: <why>";
    // the home page maps them by prefix so no English reaches the organist.
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "function endText"));
    CHECK(has(html, "startsWith(\"ended\")"));
    CHECK(has(html, "startsWith(\"failed\")"));
    CHECK(has(html, "startsWith(\"stopped (base\")"));
    CHECK(has(html, "dohrála"));
    CHECK(has(html, "nepodařilo se ji přehrát"));
    CHECK(has(html, "zastavena, základna se odpojila"));
    // The old exact-key map, which missed "ended normally", is gone.
    CHECK(!has(html, "{ended:\"dohrála\""));
    // Stops editor: HTTP errors are Czech, the server's English body is not shown.
    CHECK(has(html, "spojení s přehrávačem selhalo"));
    CHECK(!has(html, "(await r.text()).trim()"));
}

// B3: the page shows the organ power fault and an armed idle-off timer.
TEST(web_page_shows_organ_power_fault_and_idle_off) {
    const std::string& html = mc::web::statusPageHtml();
    CHECK(has(html, "organ_power_fault"));
    CHECK(has(html, "organ_idle_off_armed"));
    CHECK(has(html, "Idle-off armed"));
    CHECK(has(html, "Organ power fault"));
    CHECK(has(html, "časovač vypnutí"));
}

// Owner rule 2026-11-22: the page has no play/stop/organ controls and the daemon no play/record route.
TEST(web_page_has_no_play_controls) {
    const std::string& html = mc::web::statusPageHtml();
    CHECK(!has(html, "/api/play"));
    CHECK(!has(html, "/api/record"));
    CHECK(!has(html, "/api/stop\""));
    CHECK(!has(html, "/api/organ"));
}
