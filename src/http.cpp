#include "midicontrol/http.hpp"

#include <cctype>

namespace mc::http {

namespace {

const char* reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 415: return "Unsupported Media Type";
        case 413: return "Payload Too Large";
        case 422: return "Unprocessable Content";
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
    bool chunked = false;
    size_t pos = line.size() + 2;
    while (pos < end + 2) {
        const size_t eol = buffer.find("\r\n", pos);
        const std::string h = buffer.substr(pos, eol - pos);
        pos = eol + 2;
        const size_t colon = h.find(':');
        if (colon == std::string::npos) continue;
        const std::string name = lower(h.substr(0, colon));
        const std::string value = trim(h.substr(colon + 1));
        if (name == "host") {
            out.host = lower(value);
        } else if (name == "origin") {
            out.origin = lower(value);
        } else if (name == "content-type") {
            contentType = lower(value);
        } else if (name == "content-length") {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                return ParseResult::Bad;
            }
            if (value.size() > 9) return ParseResult::TooLarge;  // would overflow; far above kMaxBody
            contentLength = std::stoul(value);
        } else if (name == "transfer-encoding") {
            chunked = lower(value) != "identity";
        }
    }
    // Only Content-Length bodies are read; a chunked one would otherwise arrive as an empty body.
    if (chunked) return ParseResult::LengthRequired;
    // "/api/firmware" without a target too, so a whole image gets the route's 404 rather than 413.
    const std::string targetPath = target.substr(0, target.find('?'));
    const bool firmware = method == "POST" && (targetPath.rfind(kFirmwarePrefix, 0) == 0 ||
                                               targetPath + "/" == kFirmwarePrefix);
    if (firmware ? contentLength > kMaxFirmwareBody
                 : contentLength > kMaxBody || end + 4 + contentLength > maxSize) {
        return ParseResult::TooLarge;
    }
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
    if (!r.allow.empty()) s += "Allow: " + r.allow + "\r\n";
    s += "Cache-Control: no-store\r\n";
    s += "Connection: close\r\n\r\n";
    s += r.body;
    return s;
}

Response errorResponse(int status) {
    return Response{status, "text/plain; charset=utf-8",
                    std::to_string(status) + " " + reasonPhrase(status) + "\n"};
}

namespace {
Response notAllowed(const char* allow) {
    Response r = errorResponse(405);
    r.allow = allow;
    return r;
}
}  // namespace

namespace {
// "[::1]:8080" / "host:80" / "host" -> host part without port, brackets kept for IPv6.
std::string hostOnly(const std::string& hostPort) {
    if (!hostPort.empty() && hostPort[0] == '[') {
        const size_t e = hostPort.find(']');
        return e == std::string::npos ? hostPort : hostPort.substr(0, e + 1);
    }
    return hostPort.substr(0, hostPort.find(':'));
}

bool isIpLiteral(const std::string& h) {
    if (h.size() >= 2 && h.front() == '[' && h.back() == ']') return true;  // IPv6 literal
    if (h.empty()) return false;
    int dots = 0;
    for (char ch : h) {
        if (ch == '.') ++dots;
        else if (ch < '0' || ch > '9') return false;
    }
    return dots == 3 && h.front() != '.' && h.back() != '.' && h.find("..") == std::string::npos;
}
}  // namespace

bool writeAllowed(const Request& req, const std::vector<std::string>& allowedHosts) {
    if (req.method != "PUT" && req.method != "POST") return true;
    if (req.host.empty()) return false;
    const std::string h = hostOnly(req.host);
    bool ok = isIpLiteral(h) || h == "localhost";
    for (const auto& a : allowedHosts) ok = ok || h == a;
    if (!ok) return false;
    if (!req.origin.empty()) {
        // "null" (sandboxed/opaque origins) and anything not http(s)://<Host> is refused.
        const std::string http = "http://", https = "https://";
        std::string rest;
        if (req.origin.rfind(http, 0) == 0) rest = req.origin.substr(http.size());
        else if (req.origin.rfind(https, 0) == 0) rest = req.origin.substr(https.size());
        else return false;
        if (rest != req.host) return false;
    }
    return true;
}

Response route(const Request& req, const std::string& html,
               const std::function<std::string()>& statusJson, const DiagHandler& diagJson,
               const StopsApi* stops, const FirmwareApi* firmware, const Extras* extras) {
    constexpr const char* kStopsPrefix = "/api/stops/";
    constexpr const char* kDiagAck = "/api/diag/ack";
    if (!writeAllowed(req, extras ? extras->allowedHosts : std::vector<std::string>{})) return errorResponse(403);
    if (req.path == kDiagAck) {
        if (req.method != "POST") return notAllowed("POST");
        if (!extras || !extras->diagAck) return errorResponse(404);
        // A cross-site form can send urlencoded bodies; the Origin check above is what stops it.
        return extras->diagAck(req.body) ? Response{200, "application/json", "{}"} : errorResponse(400);
    }
    if (req.path.rfind(kFirmwarePrefix, 0) == 0) {
        if (req.method != "POST") return notAllowed("POST");
        if (!firmware || !firmware->post) return errorResponse(404);
        if (req.contentType.rfind("application/octet-stream", 0) != 0) return errorResponse(415);
        return firmware->post(req.path.substr(std::string(kFirmwarePrefix).size()), req.body);
    }
    if (req.path + "/" == kFirmwarePrefix) {
        // The upload resource is POST-only; a POST without a target names no device.
        return req.method == "POST" ? errorResponse(404) : notAllowed("POST");
    }
    if (req.method == "PUT" && req.path.rfind(kStopsPrefix, 0) == 0) {
        if (!stops || !stops->put) return errorResponse(404);
        if (req.contentType.rfind("application/json", 0) != 0) return errorResponse(415);
        return stops->put(req.path.substr(std::string(kStopsPrefix).size()), req.body);
    }
    if (req.method != "GET") {
        return notAllowed(req.path.rfind(kStopsPrefix, 0) == 0 ? "PUT" : "GET");
    }
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
