#include "midicontrol/serial_port.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>

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
    buf.resize(maxLen);
    ssize_t n = ::read(_fd, buf.data(), maxLen);
    if (n < 0 && errno != EINTR && errno != EAGAIN) _failed = true;
    if (n <= 0) {
        buf.clear();
    } else {
        buf.resize(static_cast<size_t>(n));
    }
    return buf;
}

namespace {

constexpr const char* kSysTty = "/sys/class/tty";

// idVendor/idProduct of the USB device behind tty `name` ("ttyACM0"), found
// by walking up the device symlink chain. False if it isn't a USB device.
bool usbIds(const std::string& name, std::string& vid, std::string& pid) {
    std::string base = std::string(kSysTty) + "/" + name + "/device";
    for (int depth = 0; depth < 6; ++depth) {
        vid = readSysfsLine(base + "/idVendor");
        pid = readSysfsLine(base + "/idProduct");
        if (!vid.empty() && !pid.empty()) return true;
        base += "/..";
    }
    return false;
}

}  // namespace

std::optional<std::string> findMidiControlPort(const std::string& vid, const std::string& pid) {
    // Scan tty devices and resolve each to its USB device's idVendor/idProduct.
    DIR* dir = opendir(kSysTty);
    if (!dir) return std::nullopt;

    std::optional<std::string> result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.rfind("ttyACM", 0) != 0 && name.rfind("ttyUSB", 0) != 0) continue;
        std::string v, p;
        if (usbIds(name, v, p) && v == vid && p == pid) {
            result = "/dev/" + name;
            break;
        }
    }
    closedir(dir);
    return result;
}

std::string usbIdOf(const std::string& devicePath) {
    auto slash = devicePath.find_last_of('/');
    std::string vid, pid;
    if (!usbIds(devicePath.substr(slash == std::string::npos ? 0 : slash + 1), vid, pid)) return {};
    std::string id = vid + ":" + pid;
    for (char& ch : id) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return id;
}

}  // namespace mc
