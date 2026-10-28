#include <string>

#include "midicontrol/http.hpp"
#include "test_framework.hpp"

using namespace mc::http;

namespace {
Response get(const std::string& path, const std::string& method = "GET") {
    return route(Request{method, path, "", "", ""}, "<html>", [] { return std::string("{\"a\":1}"); });
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

TEST(http_parse_body_split_and_limits) {
    Request req;
    const std::string head = "PUT /api/stops/401 HTTP/1.1\r\nContent-Length: 5\r\n\r\n";
    CHECK(parseRequest(head + "ab", req) == ParseResult::Incomplete);
    CHECK(parseRequest(head + "abcde", req) == ParseResult::Ok);
    CHECK_EQ(req.body, "abcde");
    CHECK(parseRequest("PUT /x HTTP/1.1\r\ncontent-type: Application/JSON\r\n"
                       "content-length: 1\r\n\r\n{", req) == ParseResult::Ok);
    CHECK_EQ(req.contentType, "application/json");
    CHECK(parseRequest("PUT /x HTTP/1.1\r\nContent-Length: 5000\r\n\r\n", req) ==
          ParseResult::TooLarge);
    CHECK(parseRequest("PUT /x HTTP/1.1\r\nContent-Length: x\r\n\r\n", req) == ParseResult::Bad);
}

TEST(http_route_stops) {
    std::string gotCode, gotBody;
    StopsApi api;
    api.get = [] { return std::string("{\"stops\":[]}"); };
    api.put = [&](const std::string& code, const std::string& body) {
        gotCode = code;
        gotBody = body;
        return Response{200, "application/json", "{}"};
    };
    auto call = [&](const Request& r, const StopsApi* s) {
        return route(r, "<html>", [] { return std::string("{}"); }, nullptr, s);
    };
    auto r = call(Request{"GET", "/api/stops", "", "", ""}, &api);
    CHECK_EQ(r.status, 200);
    CHECK_EQ(r.contentType, "application/json");
    CHECK_EQ(r.body, "{\"stops\":[]}");
    r = call(Request{"PUT", "/api/stops/401", "", "application/json", "{\"a\":1}"}, &api);
    CHECK_EQ(r.status, 200);
    CHECK_EQ(gotCode, "401");
    CHECK_EQ(gotBody, "{\"a\":1}");
    gotCode.clear();
    CHECK_EQ(call(Request{"PUT", "/api/stops/401", "", "text/plain", "x"}, &api).status, 415);
    CHECK_EQ(gotCode, "");
    CHECK_EQ(call(Request{"PUT", "/api/status", "", "application/json", "{}"}, &api).status, 405);
    CHECK_EQ(call(Request{"PUT", "/api/stops/401", "", "application/json", "{}"}, nullptr).status, 404);
    CHECK_EQ(call(Request{"POST", "/", "", "", ""}, &api).status, 405);
}

TEST(http_parse_firmware_upload_limit) {
    // Only a firmware upload may carry a large body; everything else keeps the 4 KiB limit.
    Request req;
    const std::string body(100000, 'x');
    auto head = [](const std::string& line, size_t n) {
        return line + " HTTP/1.1\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
               std::to_string(n) + "\r\n\r\n";
    };
    CHECK(parseRequest(head("POST /api/firmware/base", body.size()) + body, req) == ParseResult::Ok);
    CHECK_EQ(req.body.size(), body.size());
    CHECK_EQ(req.contentType, "application/octet-stream");
    CHECK(parseRequest(head("POST /api/firmware/base", body.size()) + "abc", req) == ParseResult::Incomplete);
    CHECK(parseRequest(head("POST /api/firmware/base", kMaxFirmwareBody + 1), req) == ParseResult::TooLarge);
    CHECK(parseRequest(head("PUT /api/firmware/base", body.size()), req) == ParseResult::TooLarge);
    CHECK(parseRequest(head("POST /api/stops/401", body.size()), req) == ParseResult::TooLarge);
}

TEST(http_route_firmware) {
    std::string gotTarget, gotBody;
    FirmwareApi api;
    api.post = [&](const std::string& target, const std::string& body) {
        gotTarget = target;
        gotBody = body;
        return Response{202, "application/json", "{}"};
    };
    auto call = [&](const Request& r, const FirmwareApi* fw) {
        return route(r, "<html>", [] { return std::string("{}"); }, nullptr, nullptr, fw);
    };
    auto r = call(Request{"POST", "/api/firmware/base", "", "application/octet-stream", "img"}, &api);
    CHECK_EQ(r.status, 202);
    CHECK_EQ(gotTarget, "base");
    CHECK_EQ(gotBody, "img");
    gotTarget.clear();
    // A cross-site form can only send form/text types: refused before the handler runs.
    CHECK_EQ(call(Request{"POST", "/api/firmware/base", "", "multipart/form-data; boundary=x", "img"}, &api).status, 415);
    CHECK_EQ(call(Request{"POST", "/api/firmware/base", "", "text/plain", "img"}, &api).status, 415);
    CHECK_EQ(gotTarget, "");
    CHECK_EQ(call(Request{"POST", "/api/firmware/base", "", "application/octet-stream", "img"}, nullptr).status, 404);
    CHECK_EQ(call(Request{"GET", "/api/firmware/base", "", "", ""}, &api).status, 404);
    CHECK_EQ(call(Request{"POST", "/api/status", "", "application/octet-stream", ""}, &api).status, 405);
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
    CHECK(serialize(errorResponse(415)).rfind("HTTP/1.1 415 Unsupported Media Type\r\n", 0) == 0);
    CHECK(serialize(errorResponse(413)).rfind("HTTP/1.1 413 Payload Too Large\r\n", 0) == 0);
}
