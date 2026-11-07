#include "midicontrol/device_info.hpp"

#include "midicontrol/diag_codes.hpp"
#include "midicontrol/protocol/types.hpp"

namespace mc::devinfo {

namespace {

uint32_t le(const uint8_t* p, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
    return v;
}

std::string ascii(const uint8_t* p, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) s += (p[i] >= 0x20 && p[i] < 0x7F) ? static_cast<char>(p[i]) : '?';
    return s;
}

}  // namespace

const char* pairingName(uint8_t state) {
    switch (state) {
        case 0: return "idle";
        case 1: return "scanning";
        case 2: return "connecting";
        case 3: return "bonding";
        case 4: return "done";
        case 5: return "failed";
    }
    return "unknown";
}

static std::string phyName(uint8_t v) {
	switch (v) {
		case 1: return "1M";
		case 2: return "2M";
		case 3: return "Coded";
	}
	return "?";
}

std::string formatMac(const std::string& hex12) {
    if (hex12.size() != 12) return hex12;
    std::string s;
    for (size_t i = 0; i < 12; i += 2) s += (i ? ":" : "") + hex12.substr(i, 2);
    return s;
}

const std::vector<std::string>& fieldKeys() {
    static const std::vector<std::string> keys = {
        "fw_revision", "fw_branch", "fw_built", "fw_dirty", "uptime_ms", "reset_cause", "fault_count",
        "ble_connected", "ble_stream_open", "ble_secured", "ble_bonded", "ble_module_up", "ble_peer_alive",
        "ble_pc_alive", "ble_peer", "ble_name", "ble_pairing", "display", "ssr", "battery_mv", "battery_level", "battery_percent",
		"charging", "charge_done", "vbus", "battery_critical", "vbus_mv",
		"link_rssi", "link_peer_rssi", "link_interval_ms", "link_latency", "link_timeout_ms", "link_phy",
		"link_losses", "link_err_conn", "link_module_reboots", "link_secure_retries", "link_reconnect_ms"};
    return keys;
}

std::vector<std::pair<std::string, mc::status::Value>> decode(const uint8_t* payload, size_t len) {
    using V = mc::status::Value;
    std::vector<std::pair<std::string, V>> out;
    auto num = [&](const char* key, int64_t v) { out.emplace_back(key, V{v}); };
    auto flag = [&](const char* key, bool v) { out.emplace_back(key, V{v}); };
    auto str = [&](const char* key, std::string v) { out.emplace_back(key, V{std::move(v)}); };

    size_t i = 0;
    while (i < len) {
        if (i + 2 > len) return {};
        const uint8_t t = payload[i], n = payload[i + 1];
        const uint8_t* v = payload + i + 2;
        if (i + 2 + n > len) return {};
        i += 2 + n;
        switch (t) {
            case tag::REVISION: str("fw_revision", ascii(v, n)); break;
            case tag::BRANCH: str("fw_branch", ascii(v, n)); break;
            case tag::BUILT: str("fw_built", ascii(v, n)); break;
            case tag::DIRTY: if (n == 1) flag("fw_dirty", v[0] != 0); break;
            case tag::UPTIME_MS: if (n == 4) num("uptime_ms", le(v, 4)); break;
            case tag::RESET_CAUSE: {
                if (n != 1) break;
                std::string r = mc::diag::resetCauseText(v[0]);
                str("reset_cause", r.empty() ? "none" : r);
                break;
            }
            case tag::FAULT_COUNT: if (n == 1) num("fault_count", v[0]); break;
            case tag::BLE_FLAGS:
                if (n != 1) break;
                flag("ble_connected", v[0] & 0x01);
                flag("ble_stream_open", v[0] & 0x02);
                flag("ble_secured", v[0] & 0x04);
                flag("ble_bonded", v[0] & 0x08);
                flag("ble_module_up", v[0] & 0x10);
                flag("ble_peer_alive", v[0] & 0x20);
                flag("ble_pc_alive", v[0] & 0x40);
                break;
            case tag::BLE_PEER: str("ble_peer", formatMac(ascii(v, n))); break;
            case tag::BLE_NAME: str("ble_name", ascii(v, n)); break;
            case tag::BLE_PAIRING: if (n == 1) str("ble_pairing", pairingName(v[0])); break;
			case tag::LINK_RSSI:
			case tag::LINK_PEER_RSSI:
				// i8 dBm; -128 = not measured (no link, or no reading in the last ~25 s): the field is left out
				if (n == 1 && static_cast<int8_t>(v[0]) != -128) num(t == tag::LINK_RSSI ? "link_rssi" : "link_peer_rssi", static_cast<int8_t>(v[0]));
				break;
			case tag::LINK_PARAMS:
				if (n == 6 && le(v, 2) != 0) {
					num("link_interval_ms", le(v, 2) * 125 / 100);  // unit 1.25 ms, whole ms
					num("link_latency", le(v + 2, 2));
					num("link_timeout_ms", le(v + 4, 2) * 10);
				}
				break;
			case tag::LINK_PHY:
				if (n == 2 && v[0] != 0) str("link_phy", phyName(v[0]) + (v[0] == v[1] ? "" : " / " + phyName(v[1])));
				break;
			case tag::LINK_COUNTERS:
				if (n == 10) {
					num("link_losses", le(v, 2));
					num("link_err_conn", le(v + 2, 2));
					num("link_module_reboots", le(v + 4, 2));
					num("link_secure_retries", le(v + 6, 2));
					num("link_reconnect_ms", le(v + 8, 2));
				}
				break;
			case tag::DISPLAY: if (n == 1) flag("display", v[0] != 0); break;
            case tag::SSR: if (n == 1) flag("ssr", v[0] != 0); break;
            case tag::BATTERY_MV: if (n == 2) num("battery_mv", le(v, 2)); break;
            case tag::BATTERY_LEVEL: if (n == 1) num("battery_level", v[0]); break;
            case tag::BATTERY_PERCENT: if (n == 1) num("battery_percent", v[0]); break;
            case tag::CHARGER:
                if (n != 1) break;
                flag("charging", v[0] & 0x01);
                flag("charge_done", v[0] & 0x02);
                flag("vbus", v[0] & 0x04);
                flag("battery_critical", v[0] & 0x08);
                break;
            case tag::VBUS_MV: if (n == 2) num("vbus_mv", le(v, 2)); break;
            default: break;  // newer firmware: skip
        }
    }
    return out;
}

void InfoPoller::tick(int64_t nowMs, bool baseUp, bool remoteUp) {
    tickOne(Base, nowMs, baseUp);
    tickOne(Remote, nowMs, remoteUp);
}

void InfoPoller::tickOne(Target t, int64_t nowMs, bool up) {
    bool send = false, clear = false;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        State& s = _st[t];
        if (up != s.up) {
            s.up = up;
            s.stopped = false;
            s.dueMs = nowMs;  // at once on the up edge
            clear = !up;
        }
        if (up && !s.stopped && nowMs >= s.dueMs) {
            s.dueMs = nowMs + kPeriodMs;
            send = true;
        }
    }
    if (clear && _hooks.clear) _hooks.clear(t);
    if (!send || !_hooks.sendGet) return;
    const uint8_t pid = _hooks.sendGet(t);
    std::lock_guard<std::mutex> lk(_mutex);
    auto& p = _st[t].pids;
    if (p.size() >= 2) p.erase(p.begin());  // ids wrap within minutes (keepalives): only the last GETs count
    p.push_back(pid);
}

void InfoPoller::onNak(uint8_t pid, uint8_t reason, int64_t nowMs) {
    using mc::protocol::NakReason;
    Target hit = Base;
    bool found = false, unsupported = false;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        for (Target t : {Base, Remote}) {
            for (uint8_t x : _st[t].pids) {
                if (x == pid) {
                    hit = t;
                    found = true;
                }
            }
        }
        if (!found) return;
        State& s = _st[hit];
        if (reason == static_cast<uint8_t>(NakReason::UNKNOWN_MSG)) {
            unsupported = !s.stopped;
            s.stopped = true;
        } else if (reason == static_cast<uint8_t>(NakReason::UNREACHABLE)) {
            s.dueMs = nowMs + kRetryMs;  // link up but not secured yet: ask again soon
        }
    }
    if (unsupported && _hooks.unsupported) _hooks.unsupported(hit);
}

void InfoPoller::onResp(Target from, int64_t nowMs) {
    std::lock_guard<std::mutex> lk(_mutex);
    State& s = _st[from];
    if (s.dueMs > nowMs + kPeriodMs) s.dueMs = nowMs + kPeriodMs;
}

bool InfoPoller::owns(uint8_t pid) const {
    std::lock_guard<std::mutex> lk(_mutex);
    for (const auto& s : _st)
        for (uint8_t x : s.pids)
            if (x == pid) return true;
    return false;
}

}  // namespace mc::devinfo
