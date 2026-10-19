#ifndef MIDICONTROL_PROCESS_HPP
#define MIDICONTROL_PROCESS_HPP

#include <string>
#include <vector>

// Child processes (arecordmidi). The service blocks SIGINT/SIGTERM in all of
// its threads (they are taken with sigwait), and a spawned child would inherit
// that mask and never react to SIGTERM; spawn() resets it.
namespace mc::proc {

// Start argv[0] (looked up in PATH). Returns the pid, or a negative error.
int spawn(const std::vector<std::string>& argv);

enum class Ended { Exited, Killed, NotRunning };

// SIGTERM, wait up to `graceMs` for the child to exit, then SIGKILL. Always
// reaps the child; never blocks longer than about graceMs.
Ended terminate(int pid, int graceMs = 2000);

}  // namespace mc::proc

#endif  // MIDICONTROL_PROCESS_HPP
