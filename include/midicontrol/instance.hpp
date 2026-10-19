#ifndef MIDICONTROL_INSTANCE_HPP
#define MIDICONTROL_INSTANCE_HPP

#include <string>

namespace mc {

// Per-user runtime directory: $XDG_RUNTIME_DIR, else /tmp/midicontrol-<uid>
// (created with mode 0700). Holds midicontrol.lock and midicontrol.sock.
std::string runtimeDir();

// Single instance: an exclusive flock on the lock file, held for the life of
// the process (released by the kernel when it exits, however it exits).
class InstanceLock {
public:
    InstanceLock() = default;
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

    // False when another process holds the lock (or the file can't be opened).
    bool acquire(const std::string& path);

private:
    int _fd = -1;
};

}  // namespace mc

#endif  // MIDICONTROL_INSTANCE_HPP
