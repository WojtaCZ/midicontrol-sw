#include "midicontrol/http.hpp"

#include <cctype>

namespace mc::http {

namespace {

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 415: return "Unsupported Media Type";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        default: return "Error";
    }
}

bool isToken(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < 'A' || c > 'Z') return false;
    }
    return true;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    const size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return std::string();
    return s.substr(b, s.find_last_not_of(" \t") - b + 1);
}

}  // namespace

ParseResult parseRequest(const std::string& buffer, Request& out, size_t maxSize) {
    const size_t end = buffer.find("\r\n\r\n");
    if (end == std::string::npos) {
        return buffer.size() > maxSize ? ParseResult::TooLarge : ParseResult::Incomplete;
    }
    if (end + 4 > maxSize) return ParseResult::TooLarge;

    const std::string line = buffer.substr(0, buffer.find("\r\n"));
    const size_t sp1 = line.find(' ');
    if (sp1 == std::string::npos) return ParseResult::Bad;
    const size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string::npos || line.find(' ', sp2 + 1) != std::string::npos) {
        return ParseResult::Bad;
    }
    const std::string method = line.substr(0, sp1);
    const std::string target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string version = line.substr(sp2 + 1);
    if (!isToken(method) || target.empty() || target[0] != '/' ||
        version.rfind("HTTP/1.", 0) != 0) {
        return ParseResult::Bad;
    }

    // Header lines between the request line and the blank line.
    std::string contentType;
    size_t contentLength = 0;
    size_t pos = line.size() + 2;
    while (pos < end + 2) {
        const size_t eol = buffer.find("\r\n", pos);
        const std::string h = buffer.substr(pos, eol - pos);
        pos = eol + 2;
        const size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(h.substr(0, colon));
        const std::string value = trim(h.substr(colon + 1));
        if (name == "content-type") {
            contentType = lower(value);
        } else if (name == "content-length") {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                return ParseResult::Bad;
            }
            if (value.size() > 9) return ParseResult::TooLarge;  // would overflow; far above kMaxBody
            contentLength = std::stoul(value);
        }
    }
    if (contentLength > kMaxBody || end + 4 + contentLength > maxSize) return ParseResult::TooLarge;
    if (buffer.size() < end + 4 + contentLength) return ParseResult::Incomplete;

    out.method = method;
    out.contentType = contentType;
    out.body = buffer.substr(end + 4, contentLength);
    const size_t q = target.find('?');
    out.path = target.substr(0, q);
    out.query = q == std::string::npos ? std::string() : target.substr(q + 1);
    return ParseResult::Ok;
}

std::string serialize(const Response& r) {
    std::string s = "HTTP/1.1 " + std::to_string(r.status) + " " + reasonPhrase(r.status) + "\r\n";
    s += "Content-Type: " + r.contentType + "\r\n";
    s += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
    s += "Cache-Control: no-store\r\n";
    s += "Connection: close\r\n\r\n";
    s += r.body;
    return s;
}

Response errorResponse(int status) {
    return Response{status, "text/plain; charset=utf-8",
                    std::to_string(status) + " " + reasonPhrase(status) + "\n"};
}

Response route(const Request& req, const std::string& html,
               const std::function<std::string()>& statusJson, const DiagHandler& diagJson,
               const StopsApi* stops) {
    constexpr const char* kStopsPrefix = "/api/stops/";
    if (req.method == "PUT" && req.path.rfind(kStopsPrefix, 0) == 0) {
        if (!stops || !stops->put) return errorResponse(404);
        if (req.contentType.rfind("application/json", 0) != 0) return errorResponse(415);
        return stops->put(req.path.substr(std::string(kStopsPrefix).size()), req.body);
    }
    if (req.method != "GET") return errorResponse(405);
    if (req.path == "/api/stops" && stops && stops->get) {
        return Response{200, "application/json", stops->get()};
    }
    if (req.path == "/") return Response{200, "text/html; charset=utf-8", html};
    if (req.path == "/api/status") return Response{200, "application/json", statusJson()};
    if (req.path == "/api/diag" && diagJson) {
        auto body = diagJson(req.query);
        if (!body) return errorResponse(400);
        return Response{200, "application/json", *body};
    }
    return errorResponse(404);
}

}  // namespace mc::http
