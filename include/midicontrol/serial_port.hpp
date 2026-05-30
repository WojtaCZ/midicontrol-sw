#ifndef MIDICONTROL_SERIAL_PORT_HPP
#define MIDICONTROL_SERIAL_PORT_HPP

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

    // Open a device path (e.g. "/dev/ttyACM0") at the given baud rate (raw 8N1).
    bool open(const std::string& path, int baud = 115200);
    void close();
    bool isOpen() const { return _fd >= 0; }

    // Write all bytes. Returns false on error.
    bool write(const uint8_t* data, size_t len);
    bool write(const std::vector<uint8_t>& data) { return write(data.data(), data.size()); }

    // Read up to `maxLen` bytes without blocking longer than the configured
    // VTIME (~100 ms). Returns the bytes read (possibly empty).
    std::vector<uint8_t> read(size_t maxLen = 256);

private:
    int _fd = -1;
};

// Find the MIDIControl serial device by USB VID:PID (default abcd:1234) using
// sysfs. Returns the /dev path (e.g. "/dev/ttyACM0") or nullopt if not present.
std::optional<std::string> findMidiControlPort(const std::string& vid = "abcd",
                                               const std::string& pid = "1234");

}  // namespace mc

#endif  // MIDICONTROL_SERIAL_PORT_HPP
