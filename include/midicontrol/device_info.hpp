#ifndef MIDICONTROL_DEVICE_INFO_HPP
#define MIDICONTROL_DEVICE_INFO_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "midicontrol/status.hpp"

// INFO_RESP (answer to GET_INFO) from the base or the remote: TLV fields [tag u8, len u8, value[len]], values
// little-endian. Tags as in midicontrol-protocol inc/protocol/info.hpp (vendored here as constants). Unknown tags
// are skipped, a field with an unexpected length is ignored.
namespace mc::devinfo {

namespace tag {
constexpr uint8_t REVISION = 0x01, BRANCH = 0x02, BUILT = 0x03, DIRTY = 0x04, UPTIME_MS = 0x05, RESET_CAUSE = 0x06,
                  FAULT_COUNT = 0x07;
constexpr uint8_t BLE_FLAGS = 0x10, BLE_PEER = 0x11, BLE_NAME = 0x12, BLE_PAIRING = 0x13;
constexpr uint8_t DISPLAY = 0x20, SSR = 0x21;
constexpr uint8_t BATTERY_MV = 0x30, BATTERY_LEVEL = 0x31, CHARGER = 0x32, VBUS_MV = 0x33;
}  // namespace tag

// Status-registry fields of one INFO_RESP, e.g. {"fw_revision","c880b14"}, {"ble_secured",true},
// {"battery_mv",3987}. Every key starts with one of kPrefixes, so a stale set can be cleared (see fieldKeys()).
// An empty result means a malformed payload (truncated TLV).
std::vector<std::pair<std::string, mc::status::Value>> decode(const uint8_t* payload, size_t len);

// Every field name decode() can produce (to clear them when the node goes away).
const std::vector<std::string>& fieldKeys();

// "D88039FE5996" -> "D8:80:39:FE:59:96" (anything else unchanged).
std::string formatMac(const std::string& hex12);

// Base pairing state name (Bluetooth::PairingState).
const char* pairingName(uint8_t state);

// When to send GET_INFO to the base and to the remote (pure: time and sending injected). Per node: one GET at once
// when the node comes up, then every kPeriodMs; NAK UNREACHABLE (remote link not secured yet) retries after
// kRetryMs; NAK UNKNOWN_MSG (firmware without GET_INFO) stops until the node comes up again. Going down clears
// the node's fields (hook). Thread-safe: tick() runs on the keepalive thread, onNak/onResp on the reader.
class InfoPoller {
public:
    static constexpr int64_t kPeriodMs = 15000;
    static constexpr int64_t kRetryMs = 5000;
    enum Target { Base = 0, Remote = 1 };

    struct Hooks {
        std::function<uint8_t(Target)> sendGet;  // returns the packet id
        std::function<void(Target)> clear;       // node went away: forget its fields
        std::function<void(Target)> unsupported; // NAK UNKNOWN_MSG: the firmware has no GET_INFO
    };

    explicit InfoPoller(Hooks hooks) : _hooks(std::move(hooks)) {}

    // up = the node is reachable now (base: serial connected and not updating; remote: base reports the BLE link
    // and no remote update runs).
    void tick(int64_t nowMs, bool baseUp, bool remoteUp);
    void onNak(uint8_t pid, uint8_t reason, int64_t nowMs);
    void onResp(Target from, int64_t nowMs);
    // One of our GET_INFO ids (routine traffic: counters only, no events).
    bool owns(uint8_t pid) const;

private:
    struct State {
        bool up = false;
        bool stopped = false;  // UNKNOWN_MSG until the next up edge
        int64_t dueMs = 0;
        std::vector<uint8_t> pids;  // recent GET ids (wrap; the last few matter)
    };
    void tickOne(Target t, int64_t nowMs, bool up);

    Hooks _hooks;
    mutable std::mutex _mutex;
    State _st[2];
};

}  // namespace mc::devinfo

#endif  // MIDICONTROL_DEVICE_INFO_HPP
