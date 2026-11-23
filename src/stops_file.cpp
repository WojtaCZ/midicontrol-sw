#include "midicontrol/stops_file.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
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

// Make a rename or unlink durable; the change is already made, so a failure here is not fatal.
void syncDir(const std::string& dir) {
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd >= 0) {
        (void)::fsync(dfd);
        ::close(dfd);
    }
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

std::optional<std::string> findFile(const std::string& playFolder, const std::string& code) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path exact = fs::path(playFolder) / (code + kExtension);
    if (fs::is_regular_file(exact, ec)) return exact.string();
    for (const auto& entry : fs::directory_iterator(playFolder, ec)) {
        std::error_code ec2;
        if (!entry.is_regular_file(ec2)) continue;
        const fs::path& p = entry.path();
        if (upper(p.extension().string()) != upper(kExtension)) continue;
        if (mc::songs::normalizeCode(upper(p.stem().string()), mc::songs::normalising()) == code) return p.string();
    }
    return std::nullopt;
}

std::string filePath(const std::string& playFolder, const std::string& code) {
    if (auto f = findFile(playFolder, code)) return *f;
    if (auto mid = mc::songs::findSongFile(playFolder, code))
        return std::filesystem::path(*mid).replace_extension(kExtension).string();
    return playFolder + "/" + code + kExtension;
}

Stops parse(const std::string& text, int stopCount, const std::string& name,
            std::vector<std::string>* warnings) {
    Stops out;
    std::istringstream in(text);
    std::string raw;
    bool inStops = true;  // key=value lines before any header count as [STOPS]
    int lineNo = 0;
    while (std::getline(in, raw)) {
        ++lineNo;
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        std::string where = name + ":" + std::to_string(lineNo) + ": ";
        if (line[0] == '[') {
            // Like config.conf: case-insensitive name, optional trailing comment.
            size_t close = line.find(']');
            std::string sec = close == std::string::npos ? line.substr(1) : line.substr(1, close - 1);
            std::string rest = close == std::string::npos ? "" : trim(line.substr(close + 1));
            inStops = (upper(trim(sec)) == "STOPS");
            if (inStops && !rest.empty() && rest[0] != '#' && rest[0] != ';')
                warn(warnings, where + "ignored text after the section name: " + rest);
            continue;
        }
        size_t eq = line.find('=');
        if (!inStops || eq == std::string::npos || upper(trim(line.substr(0, eq))) != "STOPS") {
            warn(warnings, where + "ignored line: " + line);
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
                warn(warnings, where + "ignored stop '" + tok + "'");
                continue;
            }
            nums.insert(n);
        }
        if (tokens > 0 && nums.empty()) {
            // Nothing usable: fail safe to "not set", never to "all off".
            warn(warnings, where + "ignored line with no valid stop");
            continue;
        }
        if (out) warn(warnings, where + "a second Stops line replaces the first");
        out = std::vector<int>(nums.begin(), nums.end());
    }
    return out;
}

std::string serialize(const std::string& code, const std::vector<int>& stops) {
    return "# Organ stops for song " + code + ". This file is rewritten by the web page on every save:\n"
           "# comments are not kept. Edit by hand only while the player is stopped.\n"
           "# Stops = stop numbers (empty = all stops off). Delete the file to leave the organ as it is.\n"
           "[STOPS]\n"
           "Stops = " + numbersText(stops) + "\n";
}

Stops load(const std::string& playFolder, const std::string& code, int stopCount,
           std::vector<std::string>* warnings) {
    auto path = findFile(playFolder, code);
    if (!path) return std::nullopt;
    std::ifstream f(*path, std::ios::binary);
    if (!f) {
        // Only a missing file means "not set"; anything else must not be mistaken for it.
        int e = errno;
        if (e == ENOENT) return std::nullopt;
        throw std::runtime_error("stops: cannot read " + *path + ": " + std::strerror(e));
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return parse(ss.str(), stopCount, std::filesystem::path(*path).filename().string(), warnings);
}

namespace {
void writeAtomic(const std::string& playFolder, const std::string& path, const std::string& text) {
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
    syncDir(playFolder);
}
}  // namespace

void save(const std::string& playFolder, const std::string& code, const std::vector<int>& stops) {
    writeAtomic(playFolder, filePath(playFolder, code), serialize(code, stops));
}

void remove(const std::string& playFolder, const std::string& code) {
    auto path = findFile(playFolder, code);
    if (!path) return;
    if (::unlink(path->c_str()) != 0 && errno != ENOENT) failSave(*path, "delete");
    syncDir(playFolder);
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
    out += "],\"default\":" + jsonNumbers(organ.defaultStops) + ",\"names\":{";
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

namespace {
std::string defaultPath(const std::string& playFolder) { return playFolder + "/" + kDefaultFileName; }
}  // namespace

std::vector<int> loadDefault(const std::string& playFolder, int stopCount, const std::vector<int>& configured,
                             std::vector<std::string>* warnings) {
    const std::string path = defaultPath(playFolder);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return configured;
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        warn(warnings, path + ": cannot read, using the configured default");
        return configured;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    Stops s = parse(ss.str(), stopCount, path, warnings);
    return s ? *s : configured;  // no usable Stops line = as if absent
}

void saveDefault(const std::string& playFolder, const std::vector<int>& stops) {
    writeAtomic(playFolder, defaultPath(playFolder), serialize("(default for songs without a file)", stops));
}

void removeDefault(const std::string& playFolder) {
    const std::string path = defaultPath(playFolder);
    if (::unlink(path.c_str()) != 0 && errno != ENOENT) failSave(path, "delete");
    syncDir(playFolder);
}

SongPlan planFor(const mc::organ::OrganConfig& organ, const Stops& stops) {
    SongPlan plan;
    if (!organ.stopsEnabled) {
        plan.text = "disabled";
        return plan;
    }
    if (!stops) {
        if (organ.defaultStops.empty()) {
            plan.text = "not set";
            return plan;
        }
        plan.preamble = mc::organ::buildPreamble(organ, organ.defaultStops);
        plan.dropProgramChanges = organ.dropSongProgramChanges;
        plan.text = "default " + numbersText(organ.defaultStops);
        return plan;
    }
    plan.preamble = mc::organ::buildPreamble(organ, *stops);
    plan.dropProgramChanges = organ.dropSongProgramChanges;
    plan.text = stops->empty() ? "all off" : numbersText(*stops);
    return plan;
}

Store::Store(std::string playFolder, int stopCount) : folder_(std::move(playFolder)), stopCount_(stopCount) {}

SongStops Store::get(const std::vector<std::string>& codes) const {
    std::lock_guard<std::mutex> lk(mu_);
    SongStops out;
    for (const auto& code : codes) {
        std::vector<std::string> warnings;
        Stops s = load(folder_, code, stopCount_, &warnings);
        for (const auto& w : warnings) mc::log::warn(mc::status::Node::Software, w);
        if (s) out[code] = std::move(*s);
    }
    return out;
}

void Store::put(const std::string& code, const Stops& stops) {
    std::lock_guard<std::mutex> lk(mu_);
    if (stops) save(folder_, code, *stops);
    else remove(folder_, code);
}

std::vector<int> Store::defaultStops(const std::vector<int>& configured) const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> warnings;
    auto d = loadDefault(folder_, stopCount_, configured, &warnings);
    for (const auto& w : warnings) mc::log::warn(mc::status::Node::Software, w);
    return d;
}

void Store::putDefault(const Stops& stops) {
    std::lock_guard<std::mutex> lk(mu_);
    if (stops) saveDefault(folder_, *stops);
    else removeDefault(folder_);
}

mc::http::Response handlePut(Store& store, const mc::organ::OrganConfig& organ,
                             const std::vector<std::string>& songCodes, const std::string& rawCode,
                             const std::string& body) {
    if (rawCode == kDefaultCode) {  // the default registration, same body as a song
        auto stops = parseBody(body, organ.stopCount());
        if (!stops) return mc::http::errorResponse(400);
        try {
            store.putDefault(*stops);
        } catch (const std::exception& e) {
            return mc::http::Response{500, "text/plain; charset=utf-8", std::string(e.what()) + "\n"};
        }
        return mc::http::Response{200, "application/json", entryJson(kDefaultCode, *stops)};
    }
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
