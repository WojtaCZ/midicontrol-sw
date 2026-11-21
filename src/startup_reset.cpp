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

Actions StartupReset::due(int64_t msSinceBaseUp, bool midiPortFound, Ssr ssr) const {
    Actions a;
    if (_finished || msSinceBaseUp < 0) return a;
    if (!_notesDone) {
        if (midiPortFound) {
            a.allNotesOff = true;
        } else if (msSinceBaseUp >= _graceMs) {
            a.notesSkipped = true;  // no port to send the notes-off through
        } else {
            return a;  // notes first: wait for the port
        }
    }
    if (!_powerDone) {
        if (ssr == Ssr::On) {
            a.armIdleOff = true;
        } else if (ssr == Ssr::Off || msSinceBaseUp >= _statusGraceMs) {
            a.organOff = true;
        }
    }
    return a;
}

void StartupReset::done(const Actions& performed) {
    if (performed.allNotesOff || performed.notesSkipped) _notesDone = true;
    if (performed.organOff || performed.armIdleOff) _powerDone = true;
    if (_notesDone && _powerDone) _finished = true;
}

}  // namespace mc::startup
