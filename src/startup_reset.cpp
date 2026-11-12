#include "midicontrol/startup_reset.hpp"

namespace mc::startup {

std::vector<std::vector<uint8_t>> allNotesOffMessages() {
    std::vector<std::vector<uint8_t>> out;
    out.reserve(32);
    for (int ch = 0; ch < 16; ++ch) {
        const auto status = static_cast<uint8_t>(0xB0 | ch);
        out.push_back({status, 123, 0});  // All Notes Off
        out.push_back({status, 120, 0});  // All Sound Off
    }
    return out;
}

Actions StartupReset::due(int64_t msSinceBaseUp, bool midiPortFound) const {
    Actions a;
    if (_finished || msSinceBaseUp < 0) return a;
    if (midiPortFound) {
        a.allNotesOff = true;
        a.organOff = true;
    } else if (msSinceBaseUp >= _graceMs) {
        a.organOff = true;  // no port to send the notes-off through; still power off
    }
    return a;
}

void StartupReset::done(const Actions& performed) {
    if (performed.organOff) _finished = true;
}

}  // namespace mc::startup
