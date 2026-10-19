#ifndef MIDICONTROL_STATUS_JSON_HPP
#define MIDICONTROL_STATUS_JSON_HPP

#include <string>

#include "midicontrol/status.hpp"

// JSON for GET /api/status (schema 1):
//
//   {"schema":1,"now_ms":N,
//    "nodes":{"software":{"dot":"green","fields":{...},"counters":{...},
//                         "events":[{"t":N,"level":"INFO","text":"..."}]},
//             "base":{...},"controller":{...}},
//    "problems":[{"t":N,"level":"WARN","node":"base","text":"..."}],
//    "songs":[{"code":"523","problem":"empty display anchor"}]}
//
// Events and problems are newest first. Firmware-reported data will be more
// fields and counters on the same node objects, so the shape stays the same.
namespace mc::status {

// `s` as a quoted JSON string. Control characters become \uXXXX and bytes that
// are not valid UTF-8 become �, so any text (file names, wire data) is safe.
std::string jsonEscape(const std::string& s);

std::string toJson(const Snapshot& s);

}  // namespace mc::status

#endif  // MIDICONTROL_STATUS_JSON_HPP
