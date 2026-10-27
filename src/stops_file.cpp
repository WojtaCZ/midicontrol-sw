#include "midicontrol/stops_file.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>

#include "midicontrol/log.hpp"
#include "midicontrol/songs.hpp"
#include "midicontrol/status_json.hpp"

namespace mc::stops {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

void warn(std::vector<std::string>* w, const std::string& msg) {
    if (w) w->push_back(msg);
}

std::string numbersText(const std::vector<int>& v) {
    std::string out;
    for (int n : v) {
        if (!out.empty()) out += ' ';
        out += std::to_string(n);
    }
    return out;
}

std::string jsonNumbers(const std::vector<int>& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        out += std::to_string(v[i]);
    }
    return out + "]";
}

[[noreturn]] void failSave(const std::string& path, const std::string& what) {
    throw std::runtime_error("stops: cannot save " + path + ": " + what + ": " + std::strerror(errno));
}

// Minimal scanner for the two PUT body shapes.
struct Scanner {
    const std::string& s;
    size_t i = 0;
    void ws() {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    }
    bool eat(char c) {
        ws();
        if (i < s.size() && s[i] == c) { ++i; return true; }
        return false;
    }
    bool eatWord(const char* w) {
        ws();
        size_t n = std::strlen(w);
        if (s.compare(i, n, w) == 0) { i += n; return true; }
        return false;
    }
    bool atEnd() { ws(); return i == s.size(); }
};

}  // namespace

std::string filePath(const std::string& playFolder) {
    return playFolder + "/" + kFileName;
}

SongStops parse(const std::string& text, int stopCount, std::vector<std::string>* warnings) {
    SongStops out;
    std::istringstream in(text);
    std::string raw;
    bool inSongs = true;  // key=value lines before any header count as [SONGS]
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        std::string where = std::string(kFileName) + ":" + std::to_string(lineNo) + ": ";
        if (line[0] == '[') {
            // Like config.conf: case-insensitive name, optional trailing comment.
            size_t close = line.find(']');
            std::string name = close == std::string::npos ? line.substr(1) : line.substr(1, close - 1);
            std::string rest = close == std::string::npos ? "" : trim(line.substr(close + 1));
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            name = trim(name);
            inSongs = (name == "SONGS");
            if (inSongs && !rest.empty() && rest[0] != '#' && rest[0] != ';')
                warn(warnings, where + "ignored text after the section name: " + rest);
            continue;
        }
        if (!inSongs) {
            if (line.find('=') != std::string::npos) warn(warnings, where + "ignored line outside [SONGS]: " + line);
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn(warnings, where + "ignored line without '=': " + line);
            continue;
        }
        auto code = mc::songs::sanitizeCode(trim(line.substr(0, eq)));
        if (!code) {
            warn(warnings, where + "ignored invalid song code: " + trim(line.substr(0, eq)));
            continue;
        }
        std::set<int> nums;
        size_t tokens = 0;
        std::istringstream vals(line.substr(eq + 1));
        std::string tok;
        while (vals >> tok) {
            ++tokens;
            bool digits = tok.size() <= 4;
            for (unsigned char c : tok) digits = digits && std::isdigit(c);
            int n = digits ? std::stoi(tok) : 0;
            if (n < 1 || n > stopCount) {
                warn(warnings, where + "ignored stop '" + tok + "' for " + *code);
                continue;
            }
            nums.insert(n);
        }
        if (tokens > 0 && nums.empty()) {
            // Nothing usable: fail safe to "not set", never to "all off".
            warn(warnings, where + "ignored line with no valid stop for " + *code);
            continue;
        }
        out[*code] = std::vector<int>(nums.begin(), nums.end());
    }
    return out;
}

std::string serialize(const SongStops& s) {
    std::vector<const SongStops::value_type*> items;
    for (const auto& kv : s) items.push_back(&kv);
    std::sort(items.begin(), items.end(), [](auto* a, auto* b) {
        return mc::songs::naturalLess(a->first, b->first);
    });
    std::string out =
        "# Organ stops per song. This file is rewritten by the web page on every save:\n"
        "# comments and unreadable lines are not kept. Edit by hand only while the player is stopped.\n"
        "# CODE=stop numbers (empty = all stops off). A song without a line is left as it is.\n"
        "[SONGS]\n";
    for (auto* kv : items) out += kv->first + "=" + numbersText(kv->second) + "\n";
    return out;
}

SongStops load(const std::string& path, int stopCount, std::vector<std::string>* warnings) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // Only a missing file means "nothing set"; anything else must not be mistaken for it.
        int e = errno;
        if (e == ENOENT) return {};
        throw std::runtime_error("stops: cannot read " + path + ": " + std::strerror(e));
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), stopCount, warnings);
}

void save(const std::string& path, const SongStops& s) {
    const std::string text = serialize(s);
    const std::string tmp = path + ".tmp." + std::to_string(getpid());
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) failSave(tmp, "open");
    auto fail = [&](const char* what) {
        int e = errno;
        ::close(fd);
        ::unlink(tmp.c_str());
        errno = e;
        failSave(tmp, what);
    };
    size_t off = 0;
    while (off < text.size()) {
        ssize_t n = ::write(fd, text.data() + off, text.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            fail("write");
        }
        off += static_cast<size_t>(n);
    }
    if (::fsync(fd) != 0) fail("fsync");
    if (::close(fd) != 0) {
        int e = errno;
        ::unlink(tmp.c_str());
        errno = e;
        failSave(tmp, "close");
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        int e = errno;
        ::unlink(tmp.c_str());
        errno = e;
        failSave(path, "rename");
    }
    // Make the rename itself durable; the file is already in place, so a failure here is not fatal.
    size_t slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        (void)::fsync(dfd);
        ::close(dfd);
    }
}

std::optional<std::optional<std::vector<int>>> parseBody(const std::string& body, int stopCount) {
    using Result = std::optional<std::optional<std::vector<int>>>;
    Scanner sc{body};
    if (!sc.eat('{') || !sc.eat('"') || !sc.eatWord("stops") || sc.s[sc.i] != '"') return Result{};
    ++sc.i;
    if (!sc.eat(':')) return Result{};
    if (sc.eatWord("null")) {
        if (!sc.eat('}') || !sc.atEnd()) return Result{};
        return Result{std::optional<std::vector<int>>{}};
    }
    if (!sc.eat('[')) return Result{};
    std::set<int> nums;
    if (!sc.eat(']')) {
        for (;;) {
            sc.ws();
            size_t start = sc.i;
            while (sc.i < sc.s.size() && std::isdigit(static_cast<unsigned char>(sc.s[sc.i]))) ++sc.i;
            if (sc.i == start || sc.i - start > 4) return Result{};
            int n = std::stoi(sc.s.substr(start, sc.i - start));
            if (n < 1 || n > stopCount) return Result{};
            nums.insert(n);
            if (sc.eat(',')) continue;
            if (sc.eat(']')) break;
            return Result{};
        }
    }
    if (!sc.eat('}') || !sc.atEnd()) return Result{};
    return Result{std::optional<std::vector<int>>{std::vector<int>(nums.begin(), nums.end())}};
}

std::string entryJson(const std::string& code, const std::optional<std::vector<int>>& stops) {
    return "{\"code\":" + mc::status::jsonEscape(code) + ",\"stops\":" +
           (stops ? jsonNumbers(*stops) : std::string("null")) + "}";
}

std::string toJson(const mc::organ::OrganConfig& organ, const std::vector<std::string>& songCodes,
                   const SongStops& s) {
    std::string out = "{\"schema\":1,\"groups\":[";
    for (size_t i = 0; i < organ.groups.size(); ++i) {
        const auto& g = organ.groups[i];
        if (i) out += ',';
        out += "{\"label\":" + mc::status::jsonEscape(g.label) + ",\"first\":" + std::to_string(g.first) +
               ",\"last\":" + std::to_string(g.last) + "}";
    }
    out += "],\"names\":{";
    for (size_t i = 0; i < organ.stops.size(); ++i) {
        if (i) out += ',';
        out += "\"" + std::to_string(organ.stops[i].number) + "\":" +
               mc::status::jsonEscape(organ.stops[i].name);
    }
    out += "},\"songs\":[";
    for (size_t i = 0; i < songCodes.size(); ++i) {
        if (i) out += ',';
        auto it = s.find(songCodes[i]);
        out += entryJson(songCodes[i], it == s.end() ? std::nullopt : std::optional<std::vector<int>>(it->second));
    }
    return out + "]}";
}

SongPlan planFor(const mc::organ::OrganConfig& organ, const SongStops& s, const std::string& code) {
    SongPlan plan;
    auto it = s.find(code);
    if (!organ.stopsEnabled) {
        plan.text = "disabled";
        return plan;
    }
    if (it == s.end()) {
        plan.text = "not set";
        return plan;
    }
    plan.preamble = mc::organ::buildPreamble(organ, it->second);
    plan.dropProgramChanges = organ.dropSongProgramChanges;
    plan.text = it->second.empty() ? "all off" : numbersText(it->second);
    return plan;
}

Store::Store(std::string path, int stopCount) : path_(std::move(path)), stopCount_(stopCount) {}

namespace {
SongStops loadLogged(const std::string& path, int stopCount) {
    std::vector<std::string> warnings;
    SongStops s = load(path, stopCount, &warnings);
    for (const auto& w : warnings) mc::log::warn(mc::status::Node::Software, w);
    return s;
}
}  // namespace

SongStops Store::get() const {
    std::lock_guard<std::mutex> lk(mu_);
    return loadLogged(path_, stopCount_);
}

void Store::put(const std::string& code, const std::optional<std::vector<int>>& stops) {
    std::lock_guard<std::mutex> lk(mu_);
    SongStops s = loadLogged(path_, stopCount_);
    if (stops) s[code] = *stops;
    else s.erase(code);
    save(path_, s);
}

mc::http::Response handlePut(Store& store, const mc::organ::OrganConfig& organ,
                             const std::vector<std::string>& songCodes, const std::string& rawCode,
                             const std::string& body) {
    auto code = mc::songs::sanitizeCode(rawCode);
    if (!code || std::find(songCodes.begin(), songCodes.end(), *code) == songCodes.end())
        return mc::http::errorResponse(400);
    auto stops = parseBody(body, organ.stopCount());
    if (!stops) return mc::http::errorResponse(400);
    try {
        store.put(*code, *stops);
    } catch (const std::exception& e) {
        return mc::http::Response{500, "text/plain; charset=utf-8", std::string(e.what()) + "\n"};
    }
    return mc::http::Response{200, "application/json", entryJson(*code, *stops)};
}

}  // namespace mc::stops
