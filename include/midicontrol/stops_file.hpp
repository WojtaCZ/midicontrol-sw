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

// Per-song stop files: the organ stops of song CODE are saved in <PlayFolder>/<CODE>.conf,
// next to <CODE>.mid, written only by the web page:
//   [STOPS]
//   Stops = 3 4 5
// A song with a file is "set" (`Stops =` with no numbers = all stops off); a song without one
// is "not set" and leaves the organ as it is.
namespace mc::stops {

inline constexpr const char* kExtension = ".conf";
inline constexpr const char* kLegacyFileName = "stops.conf";  // old single file, no longer read

using Stops = std::optional<std::vector<int>>;              // nullopt = not set; empty = all off
using SongStops = std::map<std::string, std::vector<int>>;  // the set songs only

// The song's existing stop file (any case of the name, like the .mid), or nullopt.
std::optional<std::string> findFile(const std::string& playFolder, const std::string& code);
// Where a save writes: the existing file, else the .mid's name with .conf, else <CODE>.conf.
std::string filePath(const std::string& playFolder, const std::string& code);

// Lenient: a bad line or number is skipped and reported in `warnings` (may be null);
// `name` prefixes the warnings. No usable Stops line = not set.
Stops parse(const std::string& text, int stopCount, const std::string& name,
            std::vector<std::string>* warnings);
std::string serialize(const std::string& code, const std::vector<int>& stops);
// Missing file = not set; any other read error throws std::runtime_error.
Stops load(const std::string& playFolder, const std::string& code, int stopCount,
           std::vector<std::string>* warnings);

// Temp file in the same directory, fsync, rename, fsync the directory.
// Throws std::runtime_error (the old file stays untouched).
void save(const std::string& playFolder, const std::string& code, const std::vector<int>& stops);
// Delete the song's file (nothing to do if there is none). Throws on failure.
void remove(const std::string& playFolder, const std::string& code);

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
SongPlan planFor(const mc::organ::OrganConfig& organ, const Stops& stops);

// The web thread's handle; saves are serialised by a mutex.
class Store {
public:
    Store(std::string playFolder, int stopCount);
    // The set songs among `codes`. Throws when a song's file is unreadable.
    SongStops get(const std::vector<std::string>& codes) const;
    // Save (set) or delete (nullopt) one song's file. Throws on failure, file unchanged.
    void put(const std::string& code, const Stops& stops);

private:
    std::string folder_;
    int stopCount_;
    mutable std::mutex mu_;
};

// PUT /api/stops/<rawCode>: the code is sanitised and must be one of `songCodes`, the body parsed
// with parseBody. 200 with entryJson, 400 for any invalid input, 500 (text/plain, the error
// text) when the song's file cannot be written or deleted.
mc::http::Response handlePut(Store& store, const mc::organ::OrganConfig& organ,
                             const std::vector<std::string>& songCodes, const std::string& rawCode,
                             const std::string& body);

}  // namespace mc::stops

#endif  // MIDICONTROL_STOPS_FILE_HPP
