#ifndef MIDICONTROL_RECORDING_HPP
#define MIDICONTROL_RECORDING_HPP

#include <ctime>
#include <string>

namespace mc::recording {

// PCC-1: reserve a new recording file in `folder` for the local time `t`:
// "<YYYY-MM-DDTHH:MM:SS>.mid", or "...-2.mid", "-3.mid", ... when that name
// exists. The file is created empty with O_EXCL, so two recordings started in
// the same second never share a file. Returns "" when nothing could be created.
std::string reservePath(const std::string& folder, std::time_t t);

}  // namespace mc::recording

#endif  // MIDICONTROL_RECORDING_HPP
