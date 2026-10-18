#include "midicontrol/serial_port.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

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

std::optional<std::string> findMidiControlPort(const std::string& vid, const std::string& pid) {
    // Scan tty devices and resolve each to its USB device's idVendor/idProduct.
    const char* kSysTty = "/sys/class/tty";
    DIR* dir = opendir(kSysTty);
    if (!dir) return std::nullopt;

    std::optional<std::string> result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.rfind("ttyACM", 0) != 0 && name.rfind("ttyUSB", 0) != 0) continue;

        // Walk up the device symlink chain looking for idVendor/idProduct.
        std::string base = std::string(kSysTty) + "/" + name + "/device";
        for (int depth = 0; depth < 6; ++depth) {
            std::string vidStr = readSysfsLine(base + "/idVendor");
            std::string pidStr = readSysfsLine(base + "/idProduct");
            if (!vidStr.empty() && !pidStr.empty()) {
                if (vidStr == vid && pidStr == pid) {
                    result = "/dev/" + name;
                }
                break;
            }
            base += "/..";
        }
        if (result) break;
    }
    closedir(dir);
    return result;
}

}  // namespace mc
