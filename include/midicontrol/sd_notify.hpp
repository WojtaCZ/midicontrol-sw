#ifndef MIDICONTROL_SD_NOTIFY_HPP
#define MIDICONTROL_SD_NOTIFY_HPP

#include <string>

namespace mc {

// systemd notification without libsystemd: one datagram ("READY=1",
// "WATCHDOG=1", "STOPPING=1") to $NOTIFY_SOCKET. A no-op returning false when
// the variable is unset, e.g. when started by hand.
bool sdNotify(const std::string& state);

}  // namespace mc

#endif  // MIDICONTROL_SD_NOTIFY_HPP
