#ifndef MIDICONTROL_SONGCHECK_HPP
#define MIDICONTROL_SONGCHECK_HPP

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Health check of the song files in the play folder, for the status page.
//
// Problems reported per file (exact texts):
//   "cannot parse: <reason>"              the SMF reader rejected it
//   "empty display anchor"                 an empty SysEx (F0 F7), or one with the
//                                          MIDIControl header that doesn't decode,
//                                          before the first note (e.g. 523.mid)
//   "no display anchors"                   no display SysEx with a verse field at
//                                          all (not repeated after an empty anchor)
//   "file name is not a valid song code"   the name without .mid isn't [0-9A-Z]{1,8}
//   kWarnSongShifted / kWarnVerseShifted   a display SysEx whose song or verse
//                                          digits are not right-aligned (a blank
//                                          after a digit, e.g. 513.mid "513 ",
//                                          512.mid verse "1 "). An authoring
//                                          mistake (D24): warned, never fixed.
namespace mc::songcheck {

inline constexpr const char* kWarnSongShifted =
    "warning: song number digits are shifted (not right-aligned) in a display SysEx";
inline constexpr const char* kWarnVerseShifted =
    "warning: verse digits are shifted (not right-aligned) in a display SysEx";

struct FileCheck {
    std::string file;  // base name, e.g. "523.mid"
    std::string code;  // upper-cased stem
    std::vector<std::string> problems;
};

FileCheck checkFile(const std::string& path);

// Rescans a folder, parsing only files that are new or whose mtime or size
// changed. Each (file, problem) pair is reported once, when first found.
class Scanner {
public:
    struct Result {
        std::vector<FileCheck> all;  // every .mid file, in song order
        std::vector<std::pair<std::string, std::string>> newProblems;  // (file, problem)
    };

    Result rescan(const std::string& folder);

private:
    struct Entry {
        int64_t mtimeNs = 0;
        uint64_t size = 0;
        FileCheck check;
    };
    std::map<std::string, Entry> _files;  // by file name
    std::set<std::pair<std::string, std::string>> _reported;
};

}  // namespace mc::songcheck

#endif  // MIDICONTROL_SONGCHECK_HPP
