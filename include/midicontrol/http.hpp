#ifndef MIDICONTROL_HTTP_HPP
#define MIDICONTROL_HTTP_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

// Minimal HTTP/1.1 for the read-only status page: request parsing, routing
// and response building as pure functions (the socket loop lives in the app
// layer). Only GET is served; every response closes the connection.
namespace mc::http {

inline constexpr size_t kMaxRequest = 8192;

struct Request {
    std::string method;
    std::string path;   // without the query string
    std::string query;  // after '?', undecoded; empty if none
};

enum class ParseResult { Incomplete, Ok, Bad, TooLarge };

// Parse the request head in `buffer` (all bytes received so far). Complete
// once the blank line ("\r\n\r\n") has arrived; TooLarge when more than
// `maxSize` bytes came without it; Bad when the request line is not
// "METHOD SP /target SP HTTP/1.x".
ParseResult parseRequest(const std::string& buffer, Request& out, size_t maxSize = kMaxRequest);

struct Response {
    int status = 200;
    std::string contentType;
    std::string body;
};

// Status line, Content-Type, Content-Length, Cache-Control: no-store,
// Connection: close, then the body.
std::string serialize(const Response& r);

// text/plain "<code> <reason>\n".
Response errorResponse(int status);

// Body of GET /api/diag for a query string; nullopt = invalid parameters (400).
using DiagHandler = std::function<std::optional<std::string>(const std::string& query)>;

// GET / -> the page, GET /api/status -> statusJson(), GET /api/diag -> diagJson(query) (404 without a
// handler), other paths 404, other methods 405.
Response route(const Request& req, const std::string& html,
               const std::function<std::string()>& statusJson, const DiagHandler& diagJson = nullptr);

}  // namespace mc::http

#endif  // MIDICONTROL_HTTP_HPP
