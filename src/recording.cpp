#include "midicontrol/recording.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>

namespace mc::recording {

std::string reservePath(const std::string& folder, std::time_t t) {
    char stamp[32];
    std::tm tm {};
    localtime_r(&t, &tm);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &tm);
    const std::string base = folder + "/" + stamp;
    for (int n = 1; n <= 99; ++n) {
        std::string path = base + (n == 1 ? std::string() : "-" + std::to_string(n)) + ".mid";
        int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
        if (fd >= 0) {
            ::close(fd);
            return path;
        }
        if (errno != EEXIST) return {};
    }
    return {};
}

}  // namespace mc::recording
