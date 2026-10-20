#include "midicontrol/diag_api.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <map>

namespace mc::diag {

namespace {

constexpr size_t kDefaultLimit = 50;
constexpr size_t kMaxLimit = 200;

std::map<std::string, std::string> parseQuery(const std::string& q) {
    std::map<std::string, std::string> out;
    size_t pos = 0;
    while (pos <= q.size()) {
        size_t amp = q.find('&', pos);
        if (amp == std::string::npos) amp = q.size();
        const std::string part = q.substr(pos, amp - pos);
        if (!part.empty()) {
            size_t eq = part.find('=');
            out[part.substr(0, eq)] = eq == std::string::npos ? "" : part.substr(eq + 1);
        }
        pos = amp + 1;
    }
    return out;
}

bool parseUint(const std::string& s, uint64_t& out) {
    if (s.empty() || s.size() > 18) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    out = std::strtoull(s.c_str(), nullptr, 10);
    return true;
}

}  // namespace

std::optional<std::string> apiDiag(DiagStore& store, const std::string& query, const std::function<void()>& afterAck) {
    const auto q = parseQuery(query);
    std::vector<std::string> nodes = {"base", "remote"};
    char level = 'D';
    uint64_t before = 0, limit = kDefaultLimit, ack = 0;
    bool doAck = false;
    for (const auto& [k, v] : q) {
        if (k == "node") {
            if (v != "base" && v != "remote") return std::nullopt;
            nodes = {v};
        } else if (k == "level") {
            if (v.size() != 1 || std::string("EWID").find(v[0]) == std::string::npos) return std::nullopt;
            level = v[0];
        } else if (k == "before") {
            if (!parseUint(v, before)) return std::nullopt;
        } else if (k == "limit") {
            if (!parseUint(v, limit)) return std::nullopt;
        } else if (k == "ack") {
            if (!parseUint(v, ack)) return std::nullopt;
            doAck = true;
        }
    }
    limit = std::min<uint64_t>(std::max<uint64_t>(limit, 1), kMaxLimit);
    if (doAck) {
        store.ack(ack);
        if (afterAck) afterAck();
    }

    std::vector<DiagEntry> all;
    for (const auto& n : nodes) {
        auto part = store.query(n, level, before, static_cast<size_t>(limit));
        all.insert(all.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    }
    std::sort(all.begin(), all.end(), [](const DiagEntry& a, const DiagEntry& b) { return a.id > b.id; });
    if (all.size() > limit) all.resize(static_cast<size_t>(limit));

    std::string out = "{\"schema\":1,\"entries\":[";
    for (size_t i = 0; i < all.size(); ++i) out += (i ? "," : "") + entryJson(all[i]);
    out += "],\"next_before\":";
    out += all.size() >= limit ? std::to_string(all.back().id) : "null";
    out += "}";
    return out;
}

void publishFaultStatus(const DiagStore& store, mc::status::StatusRegistry& status) {
    using mc::status::Node;
    const std::pair<const char*, Node> map[] = {{"base", Node::Base}, {"remote", Node::Controller}};
    for (const auto& [name, node] : map) {
        auto f = store.lastFault(name);
        status.setFaults(node, store.unseenFaults(name), f ? entryJson(*f) : std::string());
    }
}

std::string formatDiag(const DiagStore& store) {
    std::string out;
    char buf[160];
    for (const char* node : {"base", "remote"}) {
        auto f = store.lastFault(node);
        if (!f || !f->fault) {
            out += std::string(node) + ": no fault recorded\n";
        } else {
            const FaultInfo& fi = *f->fault;
            std::snprintf(buf, sizeof(buf), "%s: last fault %s #%u pc=0x%08x lr=0x%08x xpsr=0x%08x%s\n", node, fi.kind.c_str(),
                          static_cast<unsigned>(fi.count), static_cast<unsigned>(fi.pc), static_cast<unsigned>(fi.lr),
                          static_cast<unsigned>(fi.xpsr), store.unseenFaults(node) > 0 ? " (unseen)" : "");
            out += buf;
            auto join = [](const std::vector<std::string>& v) {
                std::string r;
                for (const auto& b : v) r += (r.empty() ? "" : " ") + b;
                return r;
            };
            if (fi.hasCfsr) out += "  CFSR " + join(fi.cfsrBits) + "\n";
            if (!fi.hfsrBits.empty()) out += "  HFSR " + join(fi.hfsrBits) + "\n";
            if (!fi.rev.empty()) out += "  rev " + fi.rev + "\n";
        }
        auto lines = store.query(node, 'D', 0, 20);
        for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
            std::snprintf(buf, sizeof(buf), "  [%4llu.%03llu] %c ", static_cast<unsigned long long>(it->uptimeMs / 1000),
                          static_cast<unsigned long long>(it->uptimeMs % 1000), it->level);
            out += std::string(buf) + it->tag + ": " + it->text + "\n";
        }
    }
    return out;
}

}  // namespace mc::diag
