#include "midicontrol/songcheck.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <map>
#include <vector>
#include <stdexcept>

#include "midicontrol/smf.hpp"
#include "midicontrol/songs.hpp"
#include "midicontrol/sysex.hpp"

namespace fs = std::filesystem;

namespace mc::songcheck {

namespace {

constexpr const char* kWarnLeadingZero = "file name has leading zeros (played as the code without them)";
constexpr const char* kWarnDuplicateCode = "another file names the same song code";

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bool hasMidExtension(const fs::path& p) { return upper(p.extension().string()) == ".MID"; }

// Right-aligned: blanks only in front of the number, never after a digit.
template <size_t N>
bool rightAligned(const std::array<uint8_t, N>& d) {
    bool digitSeen = false;
    for (uint8_t b : d) {
        if (b == mc::sysex::BLANK_DIGIT) {
            if (digitSeen) return false;
        } else {
            digitSeen = true;
        }
    }
    return true;
}

void checkAnchors(const mc::smf::MidiFile& mf, std::vector<std::string>& problems) {
    uint32_t firstNoteTick = UINT32_MAX;
    for (const auto& ev : mf.events) {
        if (ev.kind == mc::smf::EventKind::Channel) {
            firstNoteTick = ev.tick;
            break;
        }
    }
    bool emptyAnchor = false;
    bool verseAnchor = false;
    bool songShifted = false;
    bool verseShifted = false;
    for (const auto& ev : mf.events) {
        if (ev.kind != mc::smf::EventKind::SysEx) continue;
        auto info = mc::sysex::parse(ev.bytes);
        if (info && info->hasVerse) verseAnchor = true;
        if (info && info->hasSong && !rightAligned(info->songDigits)) songShifted = true;
        if (info && info->hasVerse && !rightAligned(info->verseDigits)) verseShifted = true;
        // Other vendors' SysEx (e.g. a GM reset) is not an anchor at all.
        const bool ours = ev.bytes.size() >= 2 && ev.bytes[0] == mc::sysex::HEADER0 &&
                          ev.bytes[1] == mc::sysex::HEADER1;
        if (ev.tick <= firstNoteTick && !info && (ev.bytes.empty() || ours)) emptyAnchor = true;
    }
    if (emptyAnchor) {
        problems.push_back("empty display anchor");
    } else if (!verseAnchor) {
        problems.push_back("no display anchors");
    }
    // D24: warn only; the file is played as authored.
    if (songShifted) problems.push_back(kWarnSongShifted);
    if (verseShifted) problems.push_back(kWarnVerseShifted);
}

}  // namespace

FileCheck checkFile(const std::string& path) {
    const fs::path p(path);
    FileCheck c;
    c.file = p.filename().string();
    const std::string stem = p.stem().string();
    const std::string up = upper(stem);
    if (!mc::songs::validCode(up)) {
        c.code = up;
        c.problems.push_back("file name is not a valid song code");
    } else {
        // B4 (Q2-15): "0406.mid" is song 406; warn, since the name is not canonical.
        c.code = mc::songs::normalizeCode(up, true);
        if (c.code != up) c.problems.push_back(kWarnLeadingZero);
    }
    try {
        checkAnchors(mc::smf::load(path), c.problems);
    } catch (const std::exception& e) {
        c.problems.push_back(std::string("cannot parse: ") + e.what());
    }
    return c;
}

Scanner::Result Scanner::rescan(const std::string& folder) {
    Result r;
    std::set<std::string> seen;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        std::error_code ec2;
        if (!entry.is_regular_file(ec2) || !hasMidExtension(entry.path())) continue;
        const std::string name = entry.path().filename().string();
        struct stat st {};
        if (stat(entry.path().c_str(), &st) != 0) continue;
        const int64_t mtimeNs = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000000000 +
                                st.st_mtim.tv_nsec;
        const uint64_t size = static_cast<uint64_t>(st.st_size);
        seen.insert(name);

        auto it = _files.find(name);
        if (it == _files.end() || it->second.mtimeNs != mtimeNs || it->second.size != size) {
            Entry e{mtimeNs, size, checkFile(entry.path().string())};
            for (const auto& problem : e.check.problems) {
                if (_reported.insert({name, problem}).second) r.newProblems.push_back({name, problem});
            }
            _files[name] = std::move(e);
        }
    }
    // Forget deleted files, so a file put back is reported again.
    for (auto it = _files.begin(); it != _files.end();) {
        if (seen.count(it->first)) {
            ++it;
            continue;
        }
        for (auto rp = _reported.begin(); rp != _reported.end();) {
            rp = rp->first == it->first ? _reported.erase(rp) : std::next(rp);
        }
        it = _files.erase(it);
    }
    for (const auto& [name, e] : _files) r.all.push_back(e.check);
    // B4: two files that name the same song once leading zeros are ignored (406.mid and 0406.mid).
    std::map<std::string, std::vector<std::string>> byCode;
    for (const auto& c : r.all) {
        if (mc::songs::validCode(c.code)) byCode[c.code].push_back(c.file);
    }
    for (auto& c : r.all) {
        auto g = byCode.find(c.code);
        if (g == byCode.end() || g->second.size() < 2) continue;
        const std::string problem = std::string(kWarnDuplicateCode) + " (" + c.code + ")";
        c.problems.push_back(problem);
        if (_reported.insert({c.file, problem}).second) r.newProblems.push_back({c.file, problem});
    }
    std::sort(r.all.begin(), r.all.end(), [](const FileCheck& a, const FileCheck& b) {
        if (a.code != b.code) return mc::songs::naturalLess(a.code, b.code);
        return a.file < b.file;
    });
    return r;
}

}  // namespace mc::songcheck
