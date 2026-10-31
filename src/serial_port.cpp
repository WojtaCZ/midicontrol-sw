#include "midicontrol/serial_port.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/file.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <thread>

namespace mc {

namespace {

speed_t baudToSpeed(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

std::string readSysfsLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (f) std::getline(f, line);
    // strip trailing whitespace / newline
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r' || line.back() == ' ')) {
        line.pop_back();
    }
    return line;
}

}  // namespace

SerialPort::~SerialPort() { close(); }

bool SerialPort::open(const std::string& path, int baud) {
    close();
    _fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (_fd < 0) return false;
    // Exclusive: a second process can never open (and steal) the base link.
    if (ioctl(_fd, TIOCEXCL) != 0) {
        close();
        return false;
    }
    // Advisory lock: TIOCEXCL only keeps out processes that open AFTER us; picocom/minicom opened before us
    // would otherwise share the port and split the lines. They honour flock(); busy = open fails (callers retry).
    if (flock(_fd, LOCK_EX | LOCK_NB) != 0) {
        close();
        return false;
    }
    _failed = false;
    _path = path;

    termios tty {};
    if (tcgetattr(_fd, &tty) != 0) {
        close();
        return false;
    }

    cfmakeraw(&tty);
    speed_t spd = baudToSpeed(baud);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CRTSCTS;        // no hardware flow control
    tty.c_cflag &= ~CSTOPB;         // 1 stop bit
    tty.c_cflag &= ~PARENB;         // no parity
    tty.c_cc[VMIN] = 0;             // non-blocking-ish read
    tty.c_cc[VTIME] = 1;            // 0.1s read timeout

    if (tcsetattr(_fd, TCSANOW, &tty) != 0) {
        close();
        return false;
    }
    // Switch back to blocking mode (VMIN/VTIME govern timing now).
    int flags = fcntl(_fd, F_GETFL, 0);
    fcntl(_fd, F_SETFL, flags & ~O_NONBLOCK);

    tcflush(_fd, TCIOFLUSH);
    return true;
}

void SerialPort::close() {
    if (_fd >= 0) {
        ioctl(_fd, TIOCNXCL);  // the flag would outlive us while others hold the tty
        ::close(_fd);
        _fd = -1;
    }
}

bool SerialPort::healthy() const {
    if (_fd < 0 || _failed) return false;
    return ::access(_path.c_str(), F_OK) == 0;
}

bool SerialPort::write(const uint8_t* data, size_t len) {
    if (_fd < 0) return false;
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(_fd, data + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN) _failed = true;
            return false;
        }
        written += static_cast<size_t>(n);
    }
    return true;
}

std::vector<uint8_t> SerialPort::read(size_t maxLen) {
    std::vector<uint8_t> buf;
    if (_fd < 0) return buf;
    // A hung-up tty (USB device gone, /dev node still there) reads as EOF without errno; only POLLHUP says so.
    // Data still pending is delivered first; the port fails when a read then returns nothing.
    pollfd pfd{_fd, POLLIN, 0};
    const bool hup = ::poll(&pfd, 1, 0) > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL));
    buf.resize(maxLen);
    ssize_t n = ::read(_fd, buf.data(), maxLen);
    if (n < 0 && errno != EINTR && errno != EAGAIN) _failed = true;
    if (n == 0 && hup) _failed = true;
    if (n <= 0) {
        buf.clear();
    } else {
        buf.resize(static_cast<size_t>(n));
    }
    return buf;
}

namespace {

// One USB tty as sysfs describes it.
struct UsbTty {
    std::string name;       // "ttyACM0"
    std::string vid, pid;   // lower-case hex as sysfs prints it
    int ifNum = -1;         // bInterfaceNumber, -1 if unknown
    std::string ifString;   // the interface's string descriptor ("MIDIControl log"), may be empty
};

// idVendor/idProduct of the USB device behind tty `name` ("ttyACM0"), found
// by walking up the device symlink chain; also the interface directory's own
// attributes (the first directory of the chain). False if it isn't a USB device.
bool usbInfo(const std::string& sysRoot, const std::string& name, UsbTty& out) {
    const std::string first = sysRoot + "/class/tty/" + name + "/device";
    std::string base = first;
    for (int depth = 0; depth < 6; ++depth) {
        out.vid = readSysfsLine(base + "/idVendor");
        out.pid = readSysfsLine(base + "/idProduct");
        if (!out.vid.empty() && !out.pid.empty()) {
            out.name = name;
            const std::string num = readSysfsLine(first + "/bInterfaceNumber");
            out.ifNum = num.empty() ? -1 : static_cast<int>(std::strtol(num.c_str(), nullptr, 16));
            out.ifString = readSysfsLine(first + "/interface");
            return true;
        }
        base += "/..";
    }
    return false;
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

constexpr const char* kLogInterfaceName = "MIDIControl log";
constexpr int kProtocolInterface = 0;
constexpr int kLogInterface = 4;

// All ttyACM/ttyUSB devices of the given VID:PID, sorted by name for a stable order.
std::vector<UsbTty> candidates(const std::string& vid, const std::string& pid, const std::string& sysRoot) {
    std::vector<UsbTty> out;
    DIR* dir = opendir((sysRoot + "/class/tty").c_str());
    if (!dir) return out;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.rfind("ttyACM", 0) != 0 && name.rfind("ttyUSB", 0) != 0) continue;
        UsbTty t;
        if (usbInfo(sysRoot, name, t) && lower(t.vid) == lower(vid) && lower(t.pid) == lower(pid)) out.push_back(t);
    }
    closedir(dir);
    std::sort(out.begin(), out.end(), [](const UsbTty& a, const UsbTty& b) { return a.name < b.name; });
    return out;
}

}  // namespace

std::optional<std::string> findMidiControlPort(const std::string& vid, const std::string& pid, const std::string& sysRoot) {
    // The protocol CDC is USB interface 00. The log CDC (a second ttyACM of the same device) must never
    // be taken for it, whatever order the kernel lists them in.
    const auto all = candidates(vid, pid, sysRoot);
    for (const auto& t : all) {
        if (t.ifNum == kProtocolInterface) return "/dev/" + t.name;
    }
    // Old firmware / no interface info: the first one that is not the log CDC.
    for (const auto& t : all) {
        if (t.ifNum < 0 && t.ifString != kLogInterfaceName) return "/dev/" + t.name;
    }
    return std::nullopt;
}

std::optional<std::string> findMidiControlLogPort(const std::string& vid, const std::string& pid, const std::string& sysRoot) {
    const auto all = candidates(vid, pid, sysRoot);
    for (const auto& t : all) {
        if (t.ifString == kLogInterfaceName) return "/dev/" + t.name;
    }
    for (const auto& t : all) {
        if (t.ifNum == kLogInterface) return "/dev/" + t.name;
    }
    return std::nullopt;
}

std::string usbIdOf(const std::string& devicePath) {
    auto slash = devicePath.find_last_of('/');
    UsbTty t;
    if (!usbInfo("/sys", devicePath.substr(slash == std::string::npos ? 0 : slash + 1), t)) return {};
    std::string id = t.vid + ":" + t.pid;
    for (char& ch : id) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return id;
}

bool openWhenReady(const std::string& path, const std::function<bool()>& tryOpen,
                   std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (::access(path.c_str(), R_OK | W_OK) == 0 && tryOpen()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

}  // namespace mc
