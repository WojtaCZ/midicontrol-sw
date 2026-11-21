#ifndef MIDICONTROL_SONGS_HPP
#define MIDICONTROL_SONGS_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Song library: a folder of "<code>.mid" files, addressed by song code.
//
// A song code is 1-4 digits (value <= 1999) with an optional A-D suffix ("406A"), the same string that
// PLAY_SONG / SIGNAL_PLAYING carry and the file name without extension.
namespace mc::songs {

// PCW-1 / Q1-2 a (owner 2026-11-14): THE song-code rule, shared by every site
// (protocol, CLI, song list, stops editor, songcheck, display): 1-4 digits,
// value <= 1999, optional single A-D suffix (either case). No trimming here.
bool validCode(const std::string& code);

// Normalise a raw code (from the wire or the CLI): strips trailing NULs and
// surrounding whitespace, upper-cases, then accepts only validCode().
// nullopt for anything else, so a code can never name a path outside the
// song folder. (The protocol limit MAX_SONG_CODE_LEN = 8 stays the frame bound.)
std::optional<std::string> sanitizeCode(const std::string& raw);
std::optional<std::string> sanitizeCode(const std::vector<uint8_t>& payload);

// Find "<code>.mid" in `folder`, matching the file name case-insensitively
// (406a.MID matches code 406A). Returns the full path or nullopt.
std::optional<std::string> findSongFile(const std::string& folder, const std::string& code);

// All song codes in `folder`: regular files with a .mid extension (any case)
// whose stem is a valid code, upper-cased, de-duplicated and sorted
// naturally (by leading number, then the rest: 401 < 406A < 406B < 1000).
std::vector<std::string> listSongs(const std::string& folder);

// Natural order used by listSongs (exposed for tests).
bool naturalLess(const std::string& a, const std::string& b);

// Build a SONG_LIST_RESP payload `[hasMore, "a;b;c"]` starting at `offset`,
// filling up to `maxPayload` bytes. `count` receives the number of entries
// included (the requester's next offset is offset + count).
std::vector<uint8_t> buildSongListPage(const std::vector<std::string>& codes, size_t offset,
                                       size_t& count, size_t maxPayload = 128);

}  // namespace mc::songs

#endif  // MIDICONTROL_SONGS_HPP
