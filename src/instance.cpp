#include "midicontrol/instance.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>

namespace mc {

std::string runtimeDir() {
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg) return xdg;
    std::string dir = "/tmp/midicontrol-" + std::to_string(getuid());
    mkdir(dir.c_str(), 0700);
    return dir;
}

InstanceLock::~InstanceLock() {
    if (_fd >= 0) ::close(_fd);
}

bool InstanceLock::acquire(const std::string& path) {
    if (_fd >= 0) return true;
    int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd);
        return false;
    }
    _fd = fd;
    return true;
}

}  // namespace mc
