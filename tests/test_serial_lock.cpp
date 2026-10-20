#include <fcntl.h>
#include <pty.h>
#include <sys/file.h>
#include <unistd.h>

#include <string>

#include "midicontrol/serial_port.hpp"
#include "test_framework.hpp"

// A terminal program that opened the port first (and holds flock(), like picocom) makes our open fail
// quietly; once it lets go, the open succeeds. Uses a pty slave as the "port".
TEST(serial_open_fails_while_another_process_holds_the_flock) {
    int master = -1, slave = -1;
    char name[128] = {};
    CHECK(openpty(&master, &slave, name, nullptr, nullptr) == 0);
    const std::string path = name;
    int other = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    CHECK(other >= 0);
    CHECK(flock(other, LOCK_EX | LOCK_NB) == 0);  // "picocom" is running
    mc::SerialPort port;
    CHECK(!port.open(path, 115200));
    CHECK(!port.isOpen());
    flock(other, LOCK_UN);
    CHECK(port.open(path, 115200));
    // and now we are the one holding it
    CHECK(flock(other, LOCK_EX | LOCK_NB) != 0);
    port.close();
    ::close(other);
    ::close(slave);
    ::close(master);
}

// A hung-up tty (the USB device went away but its /dev node stays, e.g. in a container without udev) reads
// as EOF forever and the path still exists: the reader must notice it from the hangup, not from the path.
TEST(serial_read_detects_a_hung_up_port_even_though_the_path_exists) {
    int master = -1, slave = -1;
    char name[128] = {};
    CHECK(openpty(&master, &slave, name, nullptr, nullptr) == 0);
    mc::SerialPort port;
    CHECK(port.open(name, 115200));
    port.read(16);
    CHECK(port.healthy());
    ::close(master);  // the device vanishes
    port.read(16);
    CHECK(!port.healthy());
    port.close();
    ::close(slave);
}
