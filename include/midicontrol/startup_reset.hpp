#ifndef MIDICONTROL_STARTUP_RESET_HPP
#define MIDICONTROL_STARTUP_RESET_HPP

#include <cstdint>
#include <vector>

// Gap 2 / SYS-PWR-07 / SYS-NOTE-04: reset the organ when the service starts.
//
// After a start or restart the service cannot know whether the organ was left
// powered or a note was left sounding (a crash mid-song). Once the base link
// is up (first keepalive ACK) it sends All Notes Off + All Sound Off on all 16
// channels to the organ MIDI output and then SET_CURRENT 0 (organ power off).
// The indicator bus (D14) is not touched: no display clear is sent.
//
// StartupReset is the pure decision part; CommHandler's control thread asks
// due() and performs the actions (notes first, then power off).
namespace mc::startup {

// CC123 (All Notes Off) then CC120 (All Sound Off) for each channel 0..15.
std::vector<std::vector<uint8_t>> allNotesOffMessages();

struct Actions {
    bool allNotesOff = false;  // send allNotesOffMessages() to the organ MIDI out
    bool organOff = false;     // then SET_CURRENT 0
};

class StartupReset {
public:
    // How long to wait for the base's MIDI port after the link is up before
    // powering off without the notes-off (nothing to send them through).
    explicit StartupReset(int64_t portGraceMs = 5000) : _graceMs(portGraceMs) {}

    // msSinceBaseUp < 0: the base link is not up yet.
    Actions due(int64_t msSinceBaseUp, bool midiPortFound) const;
    void done(const Actions& performed);
    // A song was started first: the operator owns the organ; never power off.
    void cancel() { _finished = true; }
    bool finished() const { return _finished; }

private:
    int64_t _graceMs;
    bool _finished = false;
};

}  // namespace mc::startup

#endif  // MIDICONTROL_STARTUP_RESET_HPP
