#ifndef MIDICONTROL_HTTP_HPP
#define MIDICONTROL_HTTP_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

// Minimal HTTP/1.1 for the status page: request parsing, routing and response
// building as pure functions (the socket loop lives in the app layer). GET
// serves the page and APIs; the only write is PUT /api/stops/<code> with a JSON
// body. Every response closes the connection.
namespace mc::http {

inline constexpr size_t kMaxRequest = 8192;  // head + body
inline constexpr size_t kMaxBody = 4096;

struct Request {
    std::string method;
    std::string path;   // without the query string
    std::string query;  // after '?', undecoded; empty if none
    std::string contentType;  // Content-Type header value, lower-cased; empty if none
    std::string body;         // exactly Content-Length bytes
};

enum class ParseResult { Incomplete, Ok, Bad, TooLarge };

// Parse the request in `buffer` (all bytes received so far). Complete once the
// blank line ("\r\n\r\n") and then Content-Length body bytes have arrived;
// TooLarge when head + body exceed `maxSize` or Content-Length exceeds
// kMaxBody; Bad when the request line is not "METHOD SP /target SP HTTP/1.x"
// or Content-Length is not a number. Header names are case-insensitive.
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

// Handlers for the organ-stops API; `put` validates the raw path segment.
struct StopsApi {
    std::function<std::string()> get;                                               // GET /api/stops
    std::function<Response(const std::string& code, const std::string& body)> put;  // PUT /api/stops/<code>
};

// GET / -> the page, GET /api/status -> statusJson(), GET /api/diag -> diagJson(query) (404 without a
// handler), GET /api/stops -> stops->get(), PUT /api/stops/<code> -> stops->put(code, body) (415 unless
// Content-Type is application/json, so a plain cross-site form cannot post; 404 without `stops`),
// other paths 404, other methods 405.
Response route(const Request& req, const std::string& html,
               const std::function<std::string()>& statusJson, const DiagHandler& diagJson = nullptr,
               const StopsApi* stops = nullptr);

}  // namespace mc::http

#endif  // MIDICONTROL_HTTP_HPP
