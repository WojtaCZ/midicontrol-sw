#include <string>

#include "midicontrol/http.hpp"
#include "test_framework.hpp"

using namespace mc::http;

namespace {
Response get(const std::string& path, const std::string& method = "GET") {
    return route(Request{method, path}, "<html>", [] { return std::string("{\"a\":1}"); });
}
}  // namespace

TEST(http_parse_good) {
    Request req;
    CHECK(parseRequest("GET /api/status?x=1 HTTP/1.1\r\nHost: a\r\n\r\n", req) == ParseResult::Ok);
    CHECK_EQ(req.method, "GET");
    CHECK_EQ(req.path, "/api/status");
    CHECK(parseRequest("GET / HTTP/1.0\r\n\r\n", req) == ParseResult::Ok);
    CHECK_EQ(req.path, "/");
}

TEST(http_parse_incomplete) {
    Request req;
    CHECK(parseRequest("", req) == ParseResult::Incomplete);
    CHECK(parseRequest("GET / HTTP/1.1\r\nHost", req) == ParseResult::Incomplete);
}

TEST(http_parse_too_large) {
    Request req;
    CHECK(parseRequest(std::string(9000, 'a'), req) == ParseResult::TooLarge);
    std::string big = "GET / HTTP/1.1\r\nX: " + std::string(9000, 'a') + "\r\n\r\n";
    CHECK(parseRequest(big, req) == ParseResult::TooLarge);
}

TEST(http_parse_bad) {
    Request req;
    CHECK(parseRequest("HELLO\r\n\r\n", req) == ParseResult::Bad);
    CHECK(parseRequest("GET http://x/ HTTP/1.1\r\n\r\n", req) == ParseResult::Bad);
    CHECK(parseRequest("GET / FTP/1.1\r\n\r\n", req) == ParseResult::Bad);
    CHECK(parseRequest("GET  / HTTP/1.1\r\n\r\n", req) == ParseResult::Bad);
}

TEST(http_route) {
    auto r = get("/");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.contentType, "text/html; charset=utf-8");
    CHECK_EQ(r.body, "<html>");
    r = get("/api/status");
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.contentType, "application/json");
    CHECK_EQ(r.body, "{\"a\":1}");
    CHECK_EQ(get("/nope").status, 404);
    CHECK_EQ(get("/../etc/passwd").status, 404);
    CHECK_EQ(get("/", "POST").status, 405);
    CHECK_EQ(get("/", "HEAD").status, 405);
}

TEST(http_serialize) {
    auto s = serialize(Response{200, "text/plain", "hi"});
    CHECK(s.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(s.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(s.find("Connection: close\r\n") != std::string::npos);
    CHECK(s.find("Cache-Control: no-store\r\n") != std::string::npos);
    CHECK(s.size() >= 6 && s.substr(s.size() - 6) == "\r\n\r\nhi");
    auto e = errorResponse(404);
    CHECK_EQ(e.status, 404);
    CHECK_EQ(e.body, "404 Not Found\n");
    CHECK(serialize(errorResponse(413)).rfind("HTTP/1.1 413 Payload Too Large\r\n", 0) == 0);
}
