#ifndef MIDICONTROL_FIRMWARE_UPDATE_HPP
#define MIDICONTROL_FIRMWARE_UPDATE_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "midicontrol/comm.hpp"
#include "midicontrol/firmware_image.hpp"

namespace mc {

// Pushes a firmware image to a device using the FW_UPDATE_* protocol messages.
//   * Remote: relayed over BLE by the base (comm must be connected to the base app).
//   * Base:   reboots the base into its USB bootloader, reopens the CDC port,
//             streams the image, then waits for it to reboot into the new app.
class FirmwareUpdater {
public:
    explicit FirmwareUpdater(CommHandler& comm) : _comm(comm) {}

    // done/total are byte counts, for a progress bar.
    using Progress = std::function<void(std::size_t done, std::size_t total)>;

    bool updateRemote(const std::string& binPath, Progress cb = {});
    bool updateBase(const std::string& binPath, Progress cb = {});

private:
    bool streamImage(uint8_t target, const std::vector<uint8_t>& image, Progress cb);

    CommHandler& _comm;
};

}  // namespace mc

#endif  // MIDICONTROL_FIRMWARE_UPDATE_HPP
