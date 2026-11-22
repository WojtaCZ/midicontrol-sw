#include "midicontrol/songs.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>

#include "midicontrol/protocol/constants.hpp"

namespace fs = std::filesystem;

namespace mc::songs {

namespace {

std::atomic<bool> g_normalise{true};

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool hasMidExtension(const fs::path& p) { return upper(p.extension().string()) == ".MID"; }

}  // namespace

bool validCode(const std::string& code) {
    size_t n = 0;
    int value = 0;
    while (n < code.size() && code[n] >= '0' && code[n] <= '9') {
        if (n == 4) return false;  // 5+ digits
        value = value * 10 + (code[n] - '0');
        ++n;
    }
    if (n == 0 || value > 1999) return false;
    if (n == code.size()) return true;
    if (code.size() != n + 1) return false;
    const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(code[n])));
    return c >= 'A' && c <= 'D';
}

std::string normalizeCode(const std::string& code, bool strip) {
    if (!strip || !validCode(code)) return code;
    size_t z = 0;
    while (z + 1 < code.size() && code[z] == '0' && std::isdigit(static_cast<unsigned char>(code[z + 1]))) ++z;
    return code.substr(z);
}

void setNormalise(bool on) { g_normalise.store(on); }
bool normalising() { return g_normalise.load(); }

std::optional<std::string> sanitizeCode(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && (s.back() == '\0' || std::isspace(static_cast<unsigned char>(s.back())))) {
        s.pop_back();
    }
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    s = upper(s.substr(a));

    if (s.size() > mc::protocol::MAX_SONG_CODE_LEN || !validCode(s)) return std::nullopt;
    return normalizeCode(s, normalising());
}

std::optional<std::string> sanitizeCode(const std::vector<uint8_t>& payload) {
    return sanitizeCode(std::string(payload.begin(), payload.end()));
}

std::optional<std::string> findSongFile(const std::string& folder, const std::string& code) {
    auto clean = sanitizeCode(code);
    if (!clean) return std::nullopt;

    // Fast path: exact spelling.
    std::error_code ec;
    fs::path exact = fs::path(folder) / (*clean + ".mid");
    if (fs::is_regular_file(exact, ec)) return exact.string();

    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        std::error_code ec2;
        if (!entry.is_regular_file(ec2)) continue;
        const fs::path& p = entry.path();
        if (!hasMidExtension(p)) continue;
        if (normalizeCode(upper(p.stem().string()), normalising()) == *clean) return p.string();
    }
    return std::nullopt;
}

bool naturalLess(const std::string& a, const std::string& b) {
    auto lead = [](const std::string& s, size_t& n) {
        n = 0;
        unsigned long v = 0;
        while (n < s.size() && std::isdigit(static_cast<unsigned char>(s[n]))) {
            v = v * 10 + static_cast<unsigned long>(s[n] - '0');
            ++n;
        }
        return v;
    };
    size_t na = 0, nb = 0;
    unsigned long va = lead(a, na), vb = lead(b, nb);
    if ((na > 0) != (nb > 0)) return na > 0;  // numbered codes first
    if (na > 0 && va != vb) return va < vb;
    return a < b;
}

std::vector<std::string> listSongs(const std::string& folder) {
    std::vector<std::string> codes;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        std::error_code ec2;
        if (!entry.is_regular_file(ec2)) continue;
        const fs::path& p = entry.path();
        if (!hasMidExtension(p)) continue;
        // Q1-2a: the stem itself must be the code (as for play and songcheck); " 12.mid" is not offered.
        const std::string stem = p.stem().string();
        auto code = sanitizeCode(stem);
        if (code && *code == normalizeCode(upper(stem), normalising())) codes.push_back(*code);
    }
    std::sort(codes.begin(), codes.end(), naturalLess);
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

std::vector<uint8_t> buildSongListPage(const std::vector<std::string>& codes, size_t offset,
                                       size_t& count, size_t maxPayload) {
    std::vector<uint8_t> out;
    out.push_back(0x00);  // hasMore
    count = 0;
    for (size_t i = offset; i < codes.size(); ++i) {
        const std::string& code = codes[i];
        size_t needed = code.size() + (count > 0 ? 1 : 0);
        if (out.size() + needed > maxPayload) {
            out[0] = 0x01;
            break;
        }
        if (count > 0) out.push_back(';');
        out.insert(out.end(), code.begin(), code.end());
        ++count;
    }
    return out;
}

}  // namespace mc::songs
