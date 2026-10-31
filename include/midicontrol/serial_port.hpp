#ifndef MIDICONTROL_SERIAL_PORT_HPP
#define MIDICONTROL_SERIAL_PORT_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
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
    // in exclusive mode (TIOCEXCL + flock; false when another process holds the port).
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

    // Test seam: take over an already open fd (a pipe/pty end) without the tty setup.
    void adoptFdForTest(int fd, const std::string& path) { _fd = fd; _path = path; _failed = false; }

private:
    int _fd = -1;
    std::atomic<bool> _failed{false};  // set by read (reader thread) and write (any thread)
    std::string _path;
};

// Find the MIDIControl *protocol* serial device (USB interface 00 of the VID:PID, default abcd:1234)
// using sysfs under `sysRoot` (a parameter for tests). The base also exposes a second CDC with the
// text log; that one is never returned here. nullopt if not present.
std::optional<std::string> findMidiControlPort(const std::string& vid = "abcd", const std::string& pid = "1234",
                                               const std::string& sysRoot = "/sys");

// The base's "MIDIControl log" CDC: the interface whose string is "MIDIControl log", else interface 04.
std::optional<std::string> findMidiControlLogPort(const std::string& vid = "abcd", const std::string& pid = "1234",
                                                  const std::string& sysRoot = "/sys");

// Call `tryOpen` whenever `path` is readable and writable, until it returns true or `timeout` passes.
// A freshly enumerated CDC shows in sysfs before its /dev node exists, and the node is root-only for a
// moment (bench: ~185 ms + ~10 ms), so a single open right after re-enumeration fails.
bool openWhenReady(const std::string& path, const std::function<bool()>& tryOpen,
                   std::chrono::milliseconds timeout);

// "ABCD:1234" for a USB serial device path ("/dev/ttyACM0"), empty if unknown.
std::string usbIdOf(const std::string& devicePath);

}  // namespace mc

#endif  // MIDICONTROL_SERIAL_PORT_HPP
