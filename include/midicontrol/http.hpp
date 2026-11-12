#ifndef MIDICONTROL_HTTP_HPP
#define MIDICONTROL_HTTP_HPP

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

// Minimal HTTP/1.1 for the status page: request parsing, routing and response
// building as pure functions (the socket loop lives in the app layer). GET
// serves the page and APIs; the writes are PUT /api/stops/<code> with a JSON
// body and POST /api/firmware/<base|remote> with the update file as
// application/octet-stream. Every response closes the connection.
namespace mc::http {

inline constexpr size_t kMaxRequest = 8192;  // head + body
inline constexpr size_t kMaxBody = 4096;
// POST /api/firmware/<target> only: a whole `<name>-update.bin` (the app regions are 112-128 KiB).
inline constexpr size_t kMaxFirmwareBody = 512 * 1024;
inline constexpr const char* kFirmwarePrefix = "/api/firmware/";

struct Request {
    std::string method;
    std::string path;   // without the query string
    std::string query;  // after '?', undecoded; empty if none
    std::string contentType;  // Content-Type header value, lower-cased; empty if none
    std::string body;         // exactly Content-Length bytes
};

enum class ParseResult { Incomplete, Ok, Bad, TooLarge, LengthRequired };

// Parse the request in `buffer` (all bytes received so far). Complete once the
// blank line ("\r\n\r\n") and then Content-Length body bytes have arrived;
// TooLarge when head + body exceed `maxSize` or Content-Length exceeds
// kMaxBody (for POST /api/firmware/...: the head exceeds `maxSize` or the body
// kMaxFirmwareBody); Bad when the request line is not "METHOD SP /target SP HTTP/1.x"
// or Content-Length is not a number; LengthRequired for a Transfer-Encoding other than
// identity (chunked bodies are not read; answered 411). Header names are case-insensitive.
ParseResult parseRequest(const std::string& buffer, Request& out, size_t maxSize = kMaxRequest);

struct Response {
    int status = 200;
    std::string contentType;
    std::string body;
    std::string allow = {};  // methods for the Allow header (set on every 405); empty = no header
};

// Status line, Content-Type, Content-Length, Allow (when set), Cache-Control:
// no-store, Connection: close, then the body.
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

// Handler for POST /api/firmware/<target>; `post` validates target and image.
struct FirmwareApi {
    std::function<Response(const std::string& target, const std::string& body)> post;
};

// GET / -> the page, GET /api/status -> statusJson(), GET /api/diag -> diagJson(query) (404 without a
// handler), GET /api/stops -> stops->get(), PUT /api/stops/<code> -> stops->put(code, body) (415 unless
// Content-Type is application/json, so a plain cross-site form cannot post; 404 without `stops`),
// POST /api/firmware/<target> -> firmware->post(target, body) (415 unless Content-Type is
// application/octet-stream, which a cross-site form cannot send; 404 without `firmware`; other
// methods on /api/firmware/<target> 405 with Allow: POST; POST /api/firmware without a target 404,
// other methods there 405), other paths 404, other methods 405. Every 405 carries Allow.
Response route(const Request& req, const std::string& html,
               const std::function<std::string()>& statusJson, const DiagHandler& diagJson = nullptr,
               const StopsApi* stops = nullptr, const FirmwareApi* firmware = nullptr);

}  // namespace mc::http

#endif  // MIDICONTROL_HTTP_HPP
