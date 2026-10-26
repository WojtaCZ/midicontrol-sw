#include "midicontrol/diag_store.hpp"

#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "midicontrol/status_json.hpp"

#include "midicontrol/log.hpp"

namespace mc::diag {

namespace {

// ---- minimal tolerant JSON reader (objects, arrays, strings, integers; others skipped) ----
struct Val {
    enum T { None, Num, Str, Arr, Obj } t = None;
    uint64_t num = 0;
    std::string str;
    std::vector<Val> arr;
    std::map<std::string, Val> obj;
};

struct Reader {
    const std::string& s;
    size_t i = 0;
    bool ok = true;
    explicit Reader(const std::string& str) : s(str) {}

    void ws() {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    }
    static void utf8(std::string& o, uint32_t c) {
        if (c < 0x80) o += char(c);
        else if (c < 0x800) { o += char(0xC0 | (c >> 6)); o += char(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { o += char(0xE0 | (c >> 12)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
        else { o += char(0xF0 | (c >> 18)); o += char(0x80 | ((c >> 12) & 0x3F)); o += char(0x80 | ((c >> 6) & 0x3F)); o += char(0x80 | (c & 0x3F)); }
    }
    std::string str_() {
        std::string o;
        ++i;  // opening quote
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c != '\\') { o += c; continue; }
            if (i >= s.size()) break;
            char e = s[i++];
            switch (e) {
                case 'n': o += '\n'; break;
                case 't': o += '\t'; break;
                case 'r': o += '\r'; break;
                case 'b': o += '\b'; break;
                case 'f': o += '\f'; break;
                case 'u': {
                    if (i + 4 > s.size()) { ok = false; return o; }
                    uint32_t cp = static_cast<uint32_t>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
                    i += 4;
                    if (cp >= 0xD800 && cp < 0xDC00 && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                        uint32_t lo = static_cast<uint32_t>(std::strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16));
                        if (lo >= 0xDC00 && lo < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                    }
                    utf8(o, cp);
                    break;
                }
                default: o += e;
            }
        }
        if (i >= s.size()) ok = false;
        else ++i;
        return o;
    }
    Val val(int depth = 0) {
        Val v;
        ws();
        if (i >= s.size() || depth > 8) { ok = false; return v; }
        char c = s[i];
        if (c == '"') { v.t = Val::Str; v.str = str_(); }
        else if (c == '{') {
            v.t = Val::Obj;
            ++i;
            ws();
            if (i < s.size() && s[i] == '}') { ++i; return v; }
            while (ok) {
                ws();
                if (i >= s.size() || s[i] != '"') { ok = false; break; }
                std::string k = str_();
                ws();
                if (i >= s.size() || s[i] != ':') { ok = false; break; }
                ++i;
                v.obj[k] = val(depth + 1);
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == '}') { ++i; break; }
                ok = false;
            }
        } else if (c == '[') {
            v.t = Val::Arr;
            ++i;
            ws();
            if (i < s.size() && s[i] == ']') { ++i; return v; }
            while (ok) {
                v.arr.push_back(val(depth + 1));
                ws();
                if (i < s.size() && s[i] == ',') { ++i; continue; }
                if (i < s.size() && s[i] == ']') { ++i; break; }
                ok = false;
            }
        } else if ((c >= '0' && c <= '9') || c == '-') {
            v.t = Val::Num;
            bool neg = c == '-';
            if (neg) ++i;
            uint64_t n = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9') n = n * 10 + static_cast<uint64_t>(s[i++] - '0');
            v.num = neg ? static_cast<uint64_t>(-static_cast<int64_t>(n)) : n;
        } else {  // true / false / null
            while (i < s.size() && s[i] >= 'a' && s[i] <= 'z') ++i;
        }
        return v;
    }
};

uint64_t getNum(const Val& o, const char* k, uint64_t def = 0) {
    auto it = o.obj.find(k);
    return it != o.obj.end() && it->second.t == Val::Num ? it->second.num : def;
}
std::string getStr(const Val& o, const char* k) {
    auto it = o.obj.find(k);
    return it != o.obj.end() && it->second.t == Val::Str ? it->second.str : std::string();
}
std::vector<std::string> getStrs(const Val& o, const char* k) {
    std::vector<std::string> r;
    auto it = o.obj.find(k);
    if (it != o.obj.end() && it->second.t == Val::Arr)
        for (const auto& v : it->second.arr)
            if (v.t == Val::Str) r.push_back(v.str);
    return r;
}

std::string strs(const std::vector<std::string>& v) {
    std::string o = "[";
    for (size_t i = 0; i < v.size(); ++i) o += (i ? "," : "") + status::jsonEscape(v[i]);
    return o + "]";
}

std::string toLine(const DiagEntry& e) {
    std::ostringstream o;
    o << "{\"id\":" << e.id << ",\"t\":" << e.pcTimeMs << ",\"node\":" << status::jsonEscape(e.node) << ",\"boot\":" << e.boot
      << ",\"up\":" << e.uptimeMs << ",\"lvl\":" << status::jsonEscape(std::string(1, e.level)) << ",\"tag\":" << status::jsonEscape(e.tag)
      << ",\"text\":" << status::jsonEscape(e.text);
    if (e.fault) {
        const FaultInfo& f = *e.fault;
        o << ",\"fault\":{\"kind\":" << status::jsonEscape(f.kind) << ",\"pc\":" << f.pc << ",\"lr\":" << f.lr << ",\"xpsr\":" << f.xpsr;
        if (f.hasCfsr) o << ",\"cfsr\":" << f.cfsr;
        o << ",\"cfsr_bits\":" << strs(f.cfsrBits) << ",\"hfsr\":" << f.hfsr << ",\"hfsr_bits\":" << strs(f.hfsrBits) << ",\"mmfar\":" << f.mmfar
          << ",\"bfar\":" << f.bfar << ",\"rev\":" << status::jsonEscape(f.rev) << ",\"up\":" << f.uptimeMs << ",\"count\":" << f.count
          << ",\"code\":" << f.code << "}";
    }
    o << "}\n";
    return o.str();
}

bool fromLine(const std::string& line, DiagEntry& e) {
    Reader r(line);
    Val v = r.val();
    if (!r.ok || v.t != Val::Obj || !v.obj.count("id")) return false;
    e = DiagEntry{};
    e.id = getNum(v, "id");
    e.pcTimeMs = static_cast<int64_t>(getNum(v, "t"));
    e.node = getStr(v, "node");
    e.boot = static_cast<uint32_t>(getNum(v, "boot"));
    e.uptimeMs = getNum(v, "up");
    std::string l = getStr(v, "lvl");
    e.level = l.empty() ? 'I' : l[0];
    e.tag = getStr(v, "tag");
    e.text = getStr(v, "text");
    auto it = v.obj.find("fault");
    if (it != v.obj.end() && it->second.t == Val::Obj) {
        const Val& f = it->second;
        FaultInfo fi;
        fi.kind = getStr(f, "kind");
        fi.pc = static_cast<uint32_t>(getNum(f, "pc"));
        fi.lr = static_cast<uint32_t>(getNum(f, "lr"));
        fi.xpsr = static_cast<uint32_t>(getNum(f, "xpsr"));
        fi.hasCfsr = f.obj.count("cfsr") > 0;
        fi.cfsr = static_cast<uint32_t>(getNum(f, "cfsr"));
        fi.hfsr = static_cast<uint32_t>(getNum(f, "hfsr"));
        fi.mmfar = static_cast<uint32_t>(getNum(f, "mmfar"));
        fi.bfar = static_cast<uint32_t>(getNum(f, "bfar"));
        fi.cfsrBits = getStrs(f, "cfsr_bits");
        fi.hfsrBits = getStrs(f, "hfsr_bits");
        fi.rev = getStr(f, "rev");
        fi.uptimeMs = getNum(f, "up");
        fi.count = static_cast<uint32_t>(getNum(f, "count"));
        fi.code = static_cast<uint32_t>(getNum(f, "code"));
        e.fault = std::move(fi);
    }
    return true;
}

int levelRank(char l) {
    switch (l) {
        case 'E': return 0;
        case 'W': return 1;
        case 'I': return 2;
        case 'D': return 3;
    }
    return 2;
}

std::string safeNode(const std::string& n) {
    std::string s;
    for (char c : n) s += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    return s.empty() ? "unknown" : s;
}

// mkdir -p (errors are ignored here: the first append reports an unwritable store).
void makeDirs(const std::string& path) {
    for (size_t pos = 1; pos != std::string::npos;) {
        pos = path.find('/', pos);
        ::mkdir(path.substr(0, pos).c_str(), 0755);
        if (pos != std::string::npos) ++pos;
    }
}

}  // namespace

std::string entryJson(const DiagEntry& e) {
    std::string l = toLine(e);
    l.pop_back();  // the newline
    return l;
}

DiagStore::DiagStore(std::string dir, size_t rotateBytes, int keepFiles, size_t cacheEntries)
    : dir_(std::move(dir)), rotateBytes_(rotateBytes), cacheEntries_(cacheEntries < 1 ? 1 : cacheEntries), keepFiles_(keepFiles < 1 ? 1 : keepFiles) {
    makeDirs(dir_);
    // Largest id of every file in the directory -> next id; sizes of the current files.
    uint64_t maxId = 0;
    if (DIR* d = ::opendir(dir_.c_str())) {
        while (dirent* de = ::readdir(d)) {
            std::string n = de->d_name;
            if (n.rfind("diag-", 0) != 0 || n.find(".jsonl") == std::string::npos) continue;
            std::ifstream f(dir_ + "/" + n);
            std::string line;
            while (std::getline(f, line)) {
                DiagEntry e;
                if (fromLine(line, e) && e.id > maxId) maxId = e.id;
            }
            if (n.size() > 6 && n.compare(n.size() - 6, 6, ".jsonl") == 0) {
                struct stat st;
                if (::stat((dir_ + "/" + n).c_str(), &st) == 0) sizes_[n.substr(5, n.size() - 11)] = static_cast<size_t>(st.st_size);
            }
        }
        ::closedir(d);
    }
    nextId_ = maxId + 1;
    std::ifstream a(dir_ + "/diag-ack.json");
    std::stringstream ss;
    ss << a.rdbuf();
    std::string txt = ss.str();
    Reader r(txt);
    Val v = r.val();
    if (r.ok && v.t == Val::Obj) {
        ackAll_ = getNum(v, "ack");
        if (ackAll_ >= nextId_) nextId_ = ackAll_ + 1;
        for (const char* n : {"base", "remote"}) {
            const uint64_t id = getNum(v, n);
            if (id) ackIds_[n] = id;
            if (id >= nextId_) nextId_ = id + 1;
        }
    }
}

std::string DiagStore::path(const std::string& node, int idx) const {
    std::string p = dir_ + "/diag-" + safeNode(node) + ".jsonl";
    return idx ? p + "." + std::to_string(idx) : p;
}

void DiagStore::rotate(const std::string& node) {
    std::remove(path(node, keepFiles_ - 1).c_str());
    for (int i = keepFiles_ - 2; i >= 1; --i) std::rename(path(node, i).c_str(), path(node, i + 1).c_str());
    if (keepFiles_ > 1) std::rename(path(node, 0).c_str(), path(node, 1).c_str());
    else std::remove(path(node, 0).c_str());
    sizes_[safeNode(node)] = 0;
    cache_.erase(safeNode(node));  // the oldest entries just left the files
}

uint64_t DiagStore::append(DiagEntry e) {
    std::lock_guard<std::mutex> lk(mu_);
    e.id = nextId_++;
    std::string line = toLine(e);
    size_t& sz = sizes_[safeNode(e.node)];
    if (checked_.insert(safeNode(e.node)).second && sz > 0) {
        // A crash mid-write can leave a last line without '\n'; terminate it so the new line parses.
        std::ifstream in(path(e.node, 0), std::ios::binary | std::ios::ate);
        if (in && in.tellg() > 0) {
            in.seekg(-1, std::ios::end);
            if (in.get() != '\n') {
                std::ofstream(path(e.node, 0), std::ios::app | std::ios::binary) << '\n';
                ++sz;
            }
        }
    }
    if (sz > 0 && sz + line.size() > rotateBytes_) rotate(e.node);
    {
        std::ofstream f(path(e.node, 0), std::ios::app | std::ios::binary);
        f << line;
        f.flush();
        if (f) {
            sizes_[safeNode(e.node)] += line.size();
            writeFailing_ = false;
        } else {
            ++writeErrors_;
            if (!writeFailing_) {
                writeFailing_ = true;  // one warning per failure streak
                mc::log::warn(mc::status::Node::Software, "diagnostics store: cannot write " + path(e.node, 0) + " (entries kept in memory only)");
            }
        }
    }
    auto it = cache_.find(safeNode(e.node));
    if (it != cache_.end()) {
        NodeCache& c = it->second;
        if (e.fault) {
            c.faultIds.push_back(e.id);
            c.lastFault = e;
        }
        if (e.level == 'E' || e.level == 'W') {
            c.sev.push_back(e);
            if (c.sev.size() > cacheEntries_) {
                c.sev.pop_front();
                c.sevComplete = false;
            }
        }
        c.tail.push_back(e);
        if (c.tail.size() > cacheEntries_) {
            c.tail.pop_front();
            c.complete = false;
        }
    }
    return e.id;
}

std::vector<DiagEntry> DiagStore::readAll(const std::string& node) const {
    std::vector<DiagEntry> out;
    for (int i = keepFiles_ - 1; i >= 0; --i) {
        std::ifstream f(path(node, i), std::ios::binary);
        std::string line;
        while (std::getline(f, line)) {
            DiagEntry e;
            if (fromLine(line, e)) out.push_back(std::move(e));
        }
    }
    return out;
}

DiagStore::NodeCache& DiagStore::cacheFor(const std::string& node) const {
    auto it = cache_.find(safeNode(node));
    if (it != cache_.end()) return it->second;
    ++diskReads_;
    NodeCache c;
    std::vector<DiagEntry> all = readAll(node);
    c.complete = all.size() <= cacheEntries_;
    c.sevComplete = true;
    for (const auto& e : all) {
        if (e.level == 'E' || e.level == 'W') {
            c.sev.push_back(e);
            if (c.sev.size() > cacheEntries_) {
                c.sev.pop_front();
                c.sevComplete = false;
            }
        }
        if (e.fault) {
            c.faultIds.push_back(e.id);
            c.lastFault = e;
        }
    }
    const size_t from = all.size() > cacheEntries_ ? all.size() - cacheEntries_ : 0;
    for (size_t i = from; i < all.size(); ++i) c.tail.push_back(std::move(all[i]));
    return cache_.emplace(safeNode(node), std::move(c)).first->second;
}

std::vector<DiagEntry> DiagStore::query(const std::string& node, char minLevel, uint64_t beforeId, size_t limit) const {
    std::lock_guard<std::mutex> lk(mu_);
    const int maxRank = levelRank(minLevel);
    auto scan = [&](const auto& entries) {
        std::vector<DiagEntry> out;
        for (size_t i = entries.size(); i-- > 0 && out.size() < limit;) {
            if (beforeId && entries[i].id >= beforeId) continue;
            if (levelRank(entries[i].level) > maxRank) continue;
            out.push_back(entries[i]);
        }
        return out;
    };
    const NodeCache& c = cacheFor(node);
    if (maxRank <= levelRank('W')) {  // E/W filter: the severity tail answers it without a disk scan
        std::vector<DiagEntry> sevOut = scan(c.sev);
        if (sevOut.size() >= limit || c.sevComplete) return sevOut;
    }
    std::vector<DiagEntry> out = scan(c.tail);
    if (out.size() >= limit || c.complete) return out;
    // The page reaches past the cached window: ask the files.
    ++diskReads_;
    return scan(readAll(node));
}

std::optional<DiagEntry> DiagStore::lastFault(const std::string& node) const {
    std::lock_guard<std::mutex> lk(mu_);
    return cacheFor(node).lastFault;
}

size_t DiagStore::unseenFaults(const std::string& node) const {
    std::lock_guard<std::mutex> lk(mu_);
    size_t n = 0;
    const uint64_t acked = ackFor(node);
    for (uint64_t id : cacheFor(node).faultIds)
        if (id > acked) ++n;
    return n;
}

bool DiagStore::replace(uint64_t id, DiagEntry e) {
    std::lock_guard<std::mutex> lk(mu_);
    e.id = id;
    for (int i = 0; i < keepFiles_; ++i) {
        const std::string p = path(e.node, i);
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        std::string out, line;
        bool found = false;
        while (std::getline(in, line)) {
            DiagEntry old;
            if (!found && fromLine(line, old) && old.id == id) {
                out += toLine(e);
                found = true;
            } else {
                out += line + "\n";
            }
        }
        in.close();
        if (!found) continue;
        const std::string tmp = p + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            f << out;
            if (!f) {
                ++writeErrors_;
                return false;
            }
        }
        std::rename(tmp.c_str(), p.c_str());
        if (i == 0) sizes_[safeNode(e.node)] = out.size();
        cache_.erase(safeNode(e.node));
        return true;
    }
    return false;
}

size_t DiagStore::diskReads() const {
    std::lock_guard<std::mutex> lk(mu_);
    return diskReads_;
}

size_t DiagStore::writeErrors() const {
    std::lock_guard<std::mutex> lk(mu_);
    return writeErrors_;
}

void DiagStore::ack(uint64_t id) {
    std::lock_guard<std::mutex> lk(mu_);
    if (id >= nextId_) id = nextId_ - 1;  // never beyond the newest id: a future id would silence later faults
    std::vector<std::string> nodes;
    for (const char* n : {"base", "remote"}) {
        for (uint64_t f : cacheFor(n).faultIds) {
            if (f == id) nodes.push_back(n);
        }
    }
    if (nodes.empty()) nodes = {"base", "remote"};  // not a fault id: up to there on every node
    bool changed = false;
    for (const auto& n : nodes) {
        if (id <= ackFor(n)) continue;
        ackIds_[n] = id;
        changed = true;
    }
    if (changed) saveAck();
}

uint64_t DiagStore::ackFor(const std::string& node) const {
    auto it = ackIds_.find(node);
    return std::max(ackAll_, it == ackIds_.end() ? 0 : it->second);
}

void DiagStore::saveAck() const {
    std::string tmp = dir_ + "/diag-ack.json.tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << "{\"ack\":" << ackAll_;
        for (const auto& [n, id] : ackIds_) f << ",\"" << n << "\":" << id;
        f << "}\n";
    }
    std::rename(tmp.c_str(), (dir_ + "/diag-ack.json").c_str());
}

std::optional<std::pair<uint16_t, uint16_t>> DiagStore::loadRemoteCursor() const {
    std::ifstream f(dir_ + "/diag-remote-cursor.txt");
    unsigned boot = 0, cursor = 0;
    if (!(f >> boot >> cursor) || boot > 0xFFFF || cursor > 0xFFFF) return std::nullopt;
    return std::make_pair(static_cast<uint16_t>(boot), static_cast<uint16_t>(cursor));
}

void DiagStore::saveRemoteCursor(uint16_t boot, uint16_t cursor) {
    const std::string tmp = dir_ + "/diag-remote-cursor.txt.tmp";
    {
        std::ofstream f(tmp, std::ios::trunc);
        f << boot << ' ' << cursor << '\n';
        if (!f) return;
    }
    std::rename(tmp.c_str(), (dir_ + "/diag-remote-cursor.txt").c_str());
}

}  // namespace mc::diag
