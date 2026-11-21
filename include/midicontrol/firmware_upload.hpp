#ifndef MIDICONTROL_FIRMWARE_UPLOAD_HPP
#define MIDICONTROL_FIRMWARE_UPLOAD_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/firmware_image.hpp"
#include "midicontrol/http.hpp"

// POST /api/firmware/<base|remote>: the status page's firmware upload. The body
// is the `<name>-update.bin` from the firmware build. It is checked here with
// fwimage::check against the unit named in the URL (board ID, sizes, CRCs), so
// a controller image dropped on the base card (or back) never reaches a device;
// the device checks the target and the trailer once more (FW_UPDATE_START/END).
namespace mc::fwupload {

// URL target -> protocol address: "base" 0x02, "remote" 0x03 (as in `update <base|remote>`).
std::optional<uint8_t> boardFor(const std::string& target);

struct Accepted {
    uint8_t board = 0;
    fwimage::Info info;
    std::vector<uint8_t> image;
};

// Why a firmware update (web upload or CLI `update`) must not start now, or "" when it may.
// Busy comes first: a running base update takes the base off USB, which would read as
// "not connected". Playing is refused because both units are reached through the base and a
// base reboot (ENTER_BOOTLOADER) would cut the song off.
std::string updateRefusal(bool updating, bool playing, bool baseAlive);

// Starts the update in the background; returns "" when it started, else why
// not (busy, base not connected, playing), answered as 409.
using Start = std::function<std::string(Accepted)>;

// 202 {"target","version","size"} once started; 404 unknown target; 422 {"error"}
// for a file that is not a valid update file for that unit; 409 {"error"} from `start`.
mc::http::Response handlePost(const std::string& target, const std::string& body, const Start& start);

// Progress of the image transfer for the status fields fw_update_pct / fw_update_msg
// (FAULTS 2.6: the message used to stay at "Requesting reboot into bootloader..."). update()
// returns a step only when the percentage changes (the first call always does), so the status
// is not rewritten for every block.
class TransferProgress {
public:
    struct Step {
        int pct = 0;
        std::string msg = {};
    };
    std::optional<Step> update(std::size_t done, std::size_t total);

private:
    int _lastPct = -1;
};

}  // namespace mc::fwupload

#endif  // MIDICONTROL_FIRMWARE_UPLOAD_HPP
