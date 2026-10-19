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
