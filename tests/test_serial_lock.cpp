#include <fcntl.h>
#include <pty.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

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

// A hung-up tty (USB device gone, /dev node still there) reads as EOF without errno and the path still
// exists: only POLLHUP tells. A pipe whose write end is closed behaves the same way.
TEST(serial_pending_data_is_delivered_then_eof_with_hangup_fails_the_port) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    CHECK(write(fds[1], "abc", 3) == 3);
    ::close(fds[1]);  // the device vanishes after its last bytes
    mc::SerialPort port;
    port.adoptFdForTest(fds[0], "/dev/null");  // path exists, like a stale /dev node
    auto first = port.read(16);
    CHECK(first.size() == 3);
    CHECK(port.healthy());
    CHECK(port.read(16).empty());
    CHECK(!port.healthy());
    port.close();
}

TEST(serial_hangup_before_any_data_fails_the_port) {
    int fds[2];
    CHECK(pipe(fds) == 0);
    ::close(fds[1]);
    mc::SerialPort port;
    port.adoptFdForTest(fds[0], "/dev/null");
    CHECK(port.read(16).empty());
    CHECK(!port.healthy());
    port.close();
}

// A freshly enumerated CDC: the sysfs entry exists before the /dev node, which is root-only for a moment
// (bench 2026-10-24 and 2026-10-28). The open is tried only once the node is accessible, and again until it works.
static std::string tempNodePath() {
    char tmpl[] = "/tmp/mc-node-XXXXXX";
    int fd = mkstemp(tmpl);
    ::close(fd);
    ::unlink(tmpl);
    return tmpl;
}

TEST(open_when_ready_waits_for_the_node_to_appear) {
    const std::string path = tempNodePath();
    std::thread udev([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        int fd = ::open(path.c_str(), O_CREAT | O_WRONLY, 0600);
        ::close(fd);
    });
    int calls = 0;
    CHECK(mc::openWhenReady(path, [&] { ++calls; return true; }, std::chrono::milliseconds(2000)));
    CHECK(calls == 1);  // never called while the node was missing
    udev.join();
    ::unlink(path.c_str());
}

TEST(open_when_ready_skips_a_root_only_node_and_retries_a_failed_open) {
    const std::string path = tempNodePath();
    int fd = ::open(path.c_str(), O_CREAT | O_WRONLY, 0000);
    ::close(fd);
    std::thread udev([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        chmod(path.c_str(), 0660);
    });
    int calls = 0;
    CHECK(mc::openWhenReady(path, [&] { return ++calls >= 3; }, std::chrono::milliseconds(2000)));
    CHECK(calls == 3);
    udev.join();
    ::unlink(path.c_str());
}

TEST(open_when_ready_gives_up_after_the_timeout) {
    const std::string path = tempNodePath();  // never created
    int calls = 0;
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(!mc::openWhenReady(path, [&] { ++calls; return true; }, std::chrono::milliseconds(300)));
    const auto took = std::chrono::steady_clock::now() - t0;
    CHECK(calls == 0);
    CHECK(took >= std::chrono::milliseconds(300) && took < std::chrono::milliseconds(1000));
}
