#ifndef MIDICONTROL_STARTUP_RESET_HPP
#define MIDICONTROL_STARTUP_RESET_HPP

#include <cstdint>
#include <vector>

// Gap 2 / SYS-PWR-07 / SYS-NOTE-04: reset the organ when the service starts.
//
// After a start or restart the service cannot know whether a note was left
// sounding (a crash or restart mid-song). Once the base link is up (first
// keepalive ACK) it ALWAYS sends All Notes Off + All Sound Off on all 16
// channels to the organ MIDI output (notes part). Then the power part, decided
// by the SSR state the base reports (STATUS_RESP):
//   - reported OFF: SET_CURRENT 0 (harmless, as before);
//   - reported ON: Q1-1 b (owner 2026-11-14) - a manual organ-on survives a PC
//     start: no power off, the idle-off timer is armed instead;
//   - not reported within statusGraceMs: SET_CURRENT 0 (safety first).
// The power part is never decided before the notes part.
// The indicator bus (D14) is not touched: no display clear is sent.
// It runs again after the base link was lost and is back (PCC-3).
//
// StartupReset is the pure decision part; CommHandler's control thread asks
// due() and performs the actions (notes first, then the power part).
namespace mc::startup {

// CC123 (All Notes Off) then CC120 (All Sound Off) for each channel 0..15.
std::vector<std::vector<uint8_t>> allNotesOffMessages();

// The SSR state from the base's last authoritative STATUS_RESP.
enum class Ssr { Unknown, Off, On };

struct Actions {
    bool allNotesOff = false;   // send allNotesOffMessages() to the organ MIDI out
    bool notesSkipped = false;  // no MIDI port within the grace: notes part given up
    bool organOff = false;      // then SET_CURRENT 0
    bool armIdleOff = false;    // SSR found on: leave it, start the idle-off timer
};

class StartupReset {
public:
    // How long to wait for the base's MIDI port after the link is up before
    // powering off without the notes-off (nothing to send them through).
    // statusGraceMs: how long to wait for the SSR report (GET_STATUS is polled
    // every 5 s) before powering off without it.
    explicit StartupReset(int64_t portGraceMs = 5000, int64_t statusGraceMs = 12000)
        : _graceMs(portGraceMs), _statusGraceMs(statusGraceMs) {}

    // msSinceBaseUp < 0: the base link is not up yet.
    Actions due(int64_t msSinceBaseUp, bool midiPortFound, Ssr ssr) const;
    void done(const Actions& performed);
    // A song was started first: the operator owns the organ; never power off.
    void cancel() { _finished = true; }
    bool finished() const { return _finished; }

private:
    int64_t _graceMs;
    int64_t _statusGraceMs;
    bool _notesDone = false;
    bool _powerDone = false;
    bool _finished = false;
};

}  // namespace mc::startup

#endif  // MIDICONTROL_STARTUP_RESET_HPP
