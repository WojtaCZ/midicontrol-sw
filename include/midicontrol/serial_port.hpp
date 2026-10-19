#ifndef MIDICONTROL_SERIAL_PORT_HPP
#define MIDICONTROL_SERIAL_PORT_HPP

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mc {

// Thin POSIX (termios) serial-port wrapper for the MIDIControl USB CDC link.
class SerialPort {
public:
    SerialPort() = default;
    ~SerialPort();
    SerialPort(const SerialPort&) = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    // Open a device path (e.g. "/dev/ttyACM0") at the given baud rate (raw 8N1),
    // in exclusive mode (TIOCEXCL).
    bool open(const std::string& path, int baud = 115200);
    void close();
    bool isOpen() const { return _fd >= 0; }

    // False once a read/write failed with a hard error or the device node is
    // gone (USB unplugged / device rebooted). The port then needs reopening.
    bool healthy() const;
    const std::string& path() const { return _path; }

    // Write all bytes. Returns false on error.
    bool write(const uint8_t* data, size_t len);
    bool write(const std::vector<uint8_t>& data) { return write(data.data(), data.size()); }

    // Read up to `maxLen` bytes without blocking longer than the configured
    // VTIME (~100 ms). Returns the bytes read (possibly empty).
    std::vector<uint8_t> read(size_t maxLen = 256);

private:
    int _fd = -1;
    std::atomic<bool> _failed{false};  // set by read (reader thread) and write (any thread)
    std::string _path;
};

// Find the MIDIControl serial device by USB VID:PID (default abcd:1234) using
// sysfs. Returns the /dev path (e.g. "/dev/ttyACM0") or nullopt if not present.
std::optional<std::string> findMidiControlPort(const std::string& vid = "abcd",
                                               const std::string& pid = "1234");

// "ABCD:1234" for a USB serial device path ("/dev/ttyACM0"), empty if unknown.
std::string usbIdOf(const std::string& devicePath);

}  // namespace mc

#endif  // MIDICONTROL_SERIAL_PORT_HPP
