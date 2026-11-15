#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
#include <string>

#include "midicontrol/serial_port.hpp"
#include "test_framework.hpp"

namespace {

struct FakeSys {
    std::string root;
    FakeSys() {
        char t[] = "/tmp/mcsysXXXXXX";
        root = mkdtemp(t);
    }
    ~FakeSys() {
        std::string cmd = "rm -rf '" + root + "'";
        (void)!system(cmd.c_str());
    }
    void mk(const std::string& rel) {
        std::string p = root;
        size_t pos = 0;
        while ((pos = rel.find('/', pos)) != std::string::npos) {
            ::mkdir((root + "/" + rel.substr(0, pos)).c_str(), 0755);
            ++pos;
        }
        ::mkdir((root + "/" + rel).c_str(), 0755);
    }
    void file(const std::string& rel, const std::string& text) { std::ofstream(root + "/" + rel) << text << "\n"; }
    // One USB device with the given VID:PID and a tty on interface `ifnum` (string `iface`).
    void usbTty(const std::string& dev, const std::string& tty, const std::string& vid, const std::string& pid, const std::string& ifnum,
                const std::string& iface) {
        const std::string devDir = "devices/usb1/" + dev;
        mk(devDir);
        file(devDir + "/idVendor", vid);
        file(devDir + "/idProduct", pid);
        const std::string ifDir = devDir + "/" + dev + ":1." + ifnum;
        mk(ifDir);
        file(ifDir + "/bInterfaceNumber", ifnum);
        if (!iface.empty()) file(ifDir + "/interface", iface);
        mk("class/tty");
        mk("class/tty/" + tty);
        (void)!symlink((root + "/" + ifDir).c_str(), (root + "/class/tty/" + tty + "/device").c_str());
    }
};

}  // namespace

TEST(port_select_protocol_is_interface_00_log_is_named_interface) {
    FakeSys s;
    // Names deliberately reversed against the interface numbers: the log CDC is ttyACM0.
    s.usbTty("1-1", "ttyACM0", "abcd", "1234", "04", "MIDIControl log");
    // (second tty of the same USB device: needs its own interface directory)
    s.usbTty("1-1", "ttyACM1", "abcd", "1234", "00", "");
    s.usbTty("1-2", "ttyACM2", "0483", "5740", "00", "");  // somebody else's CDC
    auto proto = mc::findMidiControlPort("abcd", "1234", s.root);
    CHECK(proto.has_value());
    CHECK_EQ(*proto, std::string("/dev/ttyACM1"));
    auto log = mc::findMidiControlLogPort("abcd", "1234", s.root);
    CHECK(log.has_value());
    CHECK_EQ(*log, std::string("/dev/ttyACM0"));
}

TEST(port_select_log_falls_back_to_interface_04) {
    FakeSys s;
    s.usbTty("1-1", "ttyACM0", "abcd", "1234", "00", "");
    s.usbTty("1-1", "ttyACM1", "abcd", "1234", "04", "");  // no interface string
    auto log = mc::findMidiControlLogPort("abcd", "1234", s.root);
    CHECK(log.has_value());
    CHECK_EQ(*log, std::string("/dev/ttyACM1"));
}

TEST(port_select_protocol_never_the_log_cdc) {
    FakeSys s;
    s.usbTty("1-1", "ttyACM0", "abcd", "1234", "04", "MIDIControl log");  // only the log CDC present
    CHECK(!mc::findMidiControlPort("abcd", "1234", s.root).has_value());
    CHECK(!mc::findMidiControlLogPort("abcd", "9999", s.root).has_value());
}

TEST(port_select_no_sysfs) {
    CHECK(!mc::findMidiControlPort("abcd", "1234", "/nonexistent-sysfs").has_value());
}

// P1-13: the bootloader (abcd:1235) is seen; the running application (abcd:1234) and other CDCs are not.
TEST(port_select_base_in_bootloader_is_detected) {
    FakeSys app;
    app.usbTty("1-1", "ttyACM0", "abcd", "1234", "00", "");
    app.usbTty("1-2", "ttyACM1", "0483", "5740", "00", "");
    CHECK(!mc::baseInBootloader(app.root));
    FakeSys bl;
    bl.usbTty("1-1", "ttyACM0", "abcd", "1235", "00", "");
    CHECK(mc::baseInBootloader(bl.root));
    FakeSys none;
    CHECK(!mc::baseInBootloader(none.root));
}

// Test-only env seam (system-test harness): every sysfs lookup follows MIDICONTROL_SYSFS_ROOT, the returned
// /dev paths follow MIDICONTROL_DEV_ROOT, the MIDI name key follows MIDICONTROL_MIDI_KEY.
namespace {
struct EnvGuard {
    EnvGuard(const char* k, const std::string& v) : _k(k) { ::setenv(k, v.c_str(), 1); }
    ~EnvGuard() { ::unsetenv(_k); }
    const char* _k;
};
}  // namespace

TEST(port_select_env_sysfs_and_dev_root) {
    FakeSys s;
    s.usbTty("1-1", "ttyACM7", "abcd", "1234", "00", "");
    s.usbTty("1-1", "ttyACM8", "abcd", "1234", "04", "MIDIControl log");
    EnvGuard a("MIDICONTROL_SYSFS_ROOT", s.root);
    CHECK_EQ(mc::sysfsRoot(), s.root);
    auto p = mc::findMidiControlPort();
    CHECK(p.has_value());
    CHECK_EQ(*p, std::string("/dev/ttyACM7"));
    EnvGuard b("MIDICONTROL_DEV_ROOT", "/tmp/fakedev");
    CHECK_EQ(*mc::findMidiControlPort(), std::string("/tmp/fakedev/ttyACM7"));
    CHECK_EQ(*mc::findMidiControlLogPort(), std::string("/tmp/fakedev/ttyACM8"));
    CHECK_EQ(mc::usbIdOf("/tmp/fakedev/ttyACM7"), std::string("ABCD:1234"));
    CHECK(!mc::baseInBootloader());
}

TEST(port_select_env_bootloader_probe) {
    FakeSys bl;
    bl.usbTty("1-1", "ttyACM0", "abcd", "1235", "00", "");
    CHECK(!mc::baseInBootloader());
    EnvGuard a("MIDICONTROL_SYSFS_ROOT", bl.root);
    CHECK(mc::baseInBootloader());
}

TEST(port_select_env_defaults_and_midi_key) {
    CHECK_EQ(mc::sysfsRoot(), std::string("/sys"));
    CHECK_EQ(mc::devRoot(), std::string("/dev"));
    CHECK_EQ(mc::midiKey(), std::string("MIDIControl"));
    EnvGuard a("MIDICONTROL_MIDI_KEY", "systest-42");
    CHECK_EQ(mc::midiKey(), std::string("systest-42"));
}
