#include "midicontrol/status_json.hpp"

#include <cstdio>
#include <sstream>

namespace mc::status {

namespace {

// Length of the valid UTF-8 sequence at s[i], or 0 if it is not valid
// (bad lead byte, truncated, bad continuation, overlong, surrogate, > U+10FFFF).
size_t utf8Length(const std::string& s, size_t i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char c = b(i);
    size_t len;
    uint32_t cp;
    if (c >= 0xC2 && c <= 0xDF) {
        len = 2;
        cp = c & 0x1F;
    } else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        cp = c & 0x0F;
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        cp = c & 0x07;
    } else {
        return 0;
    }
    if (i + len > s.size()) return 0;
    for (size_t k = 1; k < len; ++k) {
        if ((b(i + k) & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (b(i + k) & 0x3F);
    }
    if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return 0;  // overlong
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    if (cp > 0x10FFFF) return 0;
    return len;
}

void writeValue(std::ostringstream& os, const Value& v) {
    if (std::holds_alternative<bool>(v)) {
        os << (std::get<bool>(v) ? "true" : "false");
    } else if (std::holds_alternative<int64_t>(v)) {
        os << std::get<int64_t>(v);
    } else if (std::holds_alternative<std::string>(v)) {
        os << jsonEscape(std::get<std::string>(v));
    } else {
        os << "null";
    }
}

void writeEvent(std::ostringstream& os, const Event& ev, bool withNode) {
    os << "{\"t\":" << ev.timeMs << ",\"level\":\"" << levelName(ev.level) << "\"";
    if (withNode) os << ",\"node\":\"" << nodeKey(ev.node) << "\"";
    os << ",\"text\":" << jsonEscape(ev.text) << "}";
}

void writeEvents(std::ostringstream& os, const std::vector<Event>& events, bool withNode) {
    os << "[";
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (it != events.rbegin()) os << ",";
        writeEvent(os, *it, withNode);
    }
    os << "]";
}

}  // namespace

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20 || c == 0x7F) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
            ++i;
            continue;
        }
        const size_t len = utf8Length(s, i);
        if (len == 0) {
            out += "\\ufffd";
            ++i;
        } else {
            out.append(s, i, len);
            i += len;
        }
    }
    out += '"';
    return out;
}

std::string toJson(const Snapshot& s) {
    std::ostringstream os;
    os << "{\"schema\":1,\"now_ms\":" << s.nowMs << ",\"nodes\":{";
    for (size_t i = 0; i < s.nodes.size(); ++i) {
        const Node node = static_cast<Node>(i);
        const NodeSnapshot& n = s.nodes[i];
        if (i) os << ",";
        os << "\"" << nodeKey(node) << "\":{\"dot\":\"" << dotName(evaluateDot(node, s))
           << "\",\"fields\":{";
        bool first = true;
        for (const auto& [key, value] : n.fields) {
            if (!first) os << ",";
            first = false;
            os << jsonEscape(key) << ":";
            writeValue(os, value);
        }
        os << "},\"counters\":{";
        first = true;
        for (const auto& [key, value] : n.counters) {
            if (!first) os << ",";
            first = false;
            os << jsonEscape(key) << ":" << value;
        }
        os << "},\"events\":";
        writeEvents(os, n.events, false);
        os << "}";
    }
    os << "},\"problems\":";
    writeEvents(os, s.problems, true);
    os << ",\"songs\":[";
    for (size_t i = 0; i < s.songs.size(); ++i) {
        if (i) os << ",";
        os << "{\"code\":" << jsonEscape(s.songs[i].code)
           << ",\"problem\":" << jsonEscape(s.songs[i].problem) << "}";
    }
    os << "]}";
    return os.str();
}

}  // namespace mc::status
