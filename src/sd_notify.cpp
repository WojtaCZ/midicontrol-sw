#include "midicontrol/sd_notify.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

namespace mc {

bool sdNotify(const std::string& state) {
    const char* path = std::getenv("NOTIFY_SOCKET");
    if (!path || !*path) return false;
    const size_t len = std::strlen(path);
    sockaddr_un addr{};
    if (len >= sizeof(addr.sun_path)) return false;
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path, len);
    if (addr.sun_path[0] == '@') addr.sun_path[0] = '\0';  // abstract namespace
    int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    const auto addrLen = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + len);
    ssize_t n = ::sendto(fd, state.data(), state.size(), MSG_NOSIGNAL,
                         reinterpret_cast<sockaddr*>(&addr), addrLen);
    ::close(fd);
    return n == static_cast<ssize_t>(state.size());
}

}  // namespace mc
