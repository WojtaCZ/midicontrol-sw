#ifndef MIDICONTROL_FIRMWARE_UPLOAD_HPP
#define MIDICONTROL_FIRMWARE_UPLOAD_HPP

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

// Starts the update in the background; returns "" when it started, else why
// not (busy, base not connected, playing), answered as 409.
using Start = std::function<std::string(Accepted)>;

// 202 {"target","version","size"} once started; 404 unknown target; 422 {"error"}
// for a file that is not a valid update file for that unit; 409 {"error"} from `start`.
mc::http::Response handlePost(const std::string& target, const std::string& body, const Start& start);

}  // namespace mc::fwupload

#endif  // MIDICONTROL_FIRMWARE_UPLOAD_HPP
