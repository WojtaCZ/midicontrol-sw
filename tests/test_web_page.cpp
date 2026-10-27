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
    CHECK(has(html, "ack="));          // "mark seen"
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
