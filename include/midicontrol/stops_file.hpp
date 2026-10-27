#ifndef MIDICONTROL_STOPS_FILE_HPP
#define MIDICONTROL_STOPS_FILE_HPP

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/http.hpp"
#include "midicontrol/organ.hpp"
#include "midicontrol/playback.hpp"

// stops.conf: the organ stops saved per song, written only by the web page.
// One section [SONGS], one line per song: CODE=numbers separated by spaces.
// A song that is present is "set" (an empty list = all stops off); a song that
// is absent is "not set" and leaves the organ as it is.
namespace mc::stops {

inline constexpr const char* kFileName = "stops.conf";

std::string filePath(const std::string& playFolder);  // playFolder + "/stops.conf"

using SongStops = std::map<std::string, std::vector<int>>;  // present = set; empty vector = all off

// Lenient: a bad line or number is skipped and reported in `warnings` (may be null).
SongStops parse(const std::string& text, int stopCount, std::vector<std::string>* warnings);
std::string serialize(const SongStops& s);  // header comment, [SONGS], natural order
SongStops load(const std::string& path, int stopCount, std::vector<std::string>* warnings);  // missing file = {}

// Temp file in the same directory, fsync, rename, fsync the directory.
// Throws std::runtime_error (the old file stays untouched).
void save(const std::string& path, const SongStops& s);

// Body of PUT: {"stops":[...]} -> set, {"stops":null} -> nullopt inner; nullopt outer = invalid.
// Numbers must be integers in 1..stopCount; the result is sorted and unique.
std::optional<std::optional<std::vector<int>>> parseBody(const std::string& body, int stopCount);

// GET /api/stops body (schema 1); songs come in the order of `songCodes`.
std::string toJson(const mc::organ::OrganConfig& organ, const std::vector<std::string>& songCodes,
                   const SongStops& s);
// {"code":"401","stops":[..]|null}
std::string entryJson(const std::string& code, const std::optional<std::vector<int>>& stops);

struct SongPlan {
    std::vector<MidiStep> preamble;  // empty unless the song is set and stops are enabled
    bool dropProgramChanges = false;
    std::string text;                // "3 4 5" | "all off" | "not set" | "disabled"
};
SongPlan planFor(const mc::organ::OrganConfig& organ, const SongStops& s, const std::string& code);

// The web thread's handle; saves are serialised by a mutex.
class Store {
public:
    Store(std::string path, int stopCount);
    SongStops get() const;
    // Replace (set) or remove (nullopt) one song; load-modify-save.
    // Throws on write failure, file unchanged.
    void put(const std::string& code, const std::optional<std::vector<int>>& stops);

private:
    std::string path_;
    int stopCount_;
    mutable std::mutex mu_;
};

// PUT /api/stops/<rawCode>: the code is sanitised and must be one of `songCodes`, the body parsed
// with parseBody. 200 with entryJson, 400 for any invalid input, 500 (text/plain, the error
// text) when the store cannot read or write the file.
mc::http::Response handlePut(Store& store, const mc::organ::OrganConfig& organ,
                             const std::vector<std::string>& songCodes, const std::string& rawCode,
                             const std::string& body);

}  // namespace mc::stops

#endif  // MIDICONTROL_STOPS_FILE_HPP
