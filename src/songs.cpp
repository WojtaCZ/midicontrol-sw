#include "midicontrol/songs.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "midicontrol/protocol/constants.hpp"

namespace fs = std::filesystem;

namespace mc::songs {

namespace {

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool hasMidExtension(const fs::path& p) { return upper(p.extension().string()) == ".MID"; }

}  // namespace

std::optional<std::string> sanitizeCode(const std::string& raw) {
    std::string s = raw;
    while (!s.empty() && (s.back() == '\0' || std::isspace(static_cast<unsigned char>(s.back())))) {
        s.pop_back();
    }
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    s = upper(s.substr(a));

    if (s.empty() || s.size() > mc::protocol::MAX_SONG_CODE_LEN) return std::nullopt;
    for (char c : s) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z');
        if (!ok) return std::nullopt;
    }
    return s;
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
        if (upper(p.stem().string()) == *clean) return p.string();
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
        auto code = sanitizeCode(p.stem().string());
        if (code) codes.push_back(*code);
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
