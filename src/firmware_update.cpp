#include "midicontrol/firmware_update.hpp"

#include <chrono>
#include <fstream>
#include <thread>

#include "midicontrol/log.hpp"
#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/serial_port.hpp"

namespace mc {

using mc::protocol::FwStatus;
using mc::protocol::MsgType;

namespace {

// 120 B per chunk: fits MAX_PAYLOAD_SIZE with the 4-byte offset and is a
// multiple of 8, as the base's flash programming requires (all but the last).
constexpr std::size_t kChunk = 120;
static_assert(kChunk % 8 == 0, "base chunks must be multiples of 8 bytes");

// The base bootloader enumerates with the application PID + 1.
constexpr const char* kVid = "abcd";
constexpr const char* kAppPid = "1234";
constexpr const char* kBootloaderPid = "1235";

std::vector<uint8_t> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                                std::istreambuf_iterator<char>());
}

void putLe32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}

const char* statusText(uint8_t s) {
    switch (static_cast<FwStatus>(s)) {
        case FwStatus::READY: return "READY";
        case FwStatus::IN_PROGRESS: return "IN_PROGRESS";
        case FwStatus::CRC_OK: return "CRC_OK";
        case FwStatus::CRC_FAIL: return "CRC_FAIL";
        case FwStatus::WRITE_ERROR: return "WRITE_ERROR";
        case FwStatus::BAD_SIZE: return "BAD_SIZE";
    }
    return "unknown";
}

bool isFailure(std::optional<uint8_t> s) {
    if (!s) return false;
    auto st = static_cast<FwStatus>(*s);
    return st == FwStatus::CRC_FAIL || st == FwStatus::WRITE_ERROR || st == FwStatus::BAD_SIZE;
}

std::string versionText(uint32_t v) {
    return std::to_string(v >> 16) + "." + std::to_string((v >> 8) & 0xFF) + "." +
           std::to_string(v & 0xFF);
}

std::string withStatus(std::optional<uint8_t> s) {
    return s ? std::string(" (") + statusText(*s) + ")" : std::string();
}

}  // namespace

void FirmwareUpdater::info(const std::string& text) {
    mc::log::info(mc::status::Node::Software, text);
    if (_say) _say(text);
}

void FirmwareUpdater::error(const std::string& text) {
    mc::log::error(mc::status::Node::Software, text);
    if (_say) _say(text);
}

// Read an update file; empty on error (reported).
std::vector<uint8_t> FirmwareUpdater::loadImage(const std::string& path) {
    auto image = readFile(path);
    if (image.empty()) error("firmware update: cannot read " + path);
    return image;
}

// Validate an update file for `board`; false on error (reported).
bool FirmwareUpdater::checkImage(const std::vector<uint8_t>& image, const std::string& name, uint8_t board) {
    fwimage::Info info;
    auto c = fwimage::check(image, board, &info);
    if (c != fwimage::Check::Ok) {
        error("firmware update: refusing " + name + ": " + fwimage::describe(c, fwimage::boardIdOf(image).value_or(0)));
        return false;
    }
    this->info("Image " + name + ": " + std::to_string(info.imageSize) + " bytes, version " +
               versionText(info.version));
    return true;
}

bool FirmwareUpdater::streamImage(uint8_t target, const std::vector<uint8_t>& image, Progress cb) {
    const uint32_t size = static_cast<uint32_t>(image.size());
    const uint32_t crc = crc32(image.data(), image.size());

    // FW_UPDATE_START: target(1) + size(4) + crc32(4). The remote erases its
    // staging flash before the ACK (up to ~8 s), so wait up to 15 s there; a
    // retransmitted START is re-ACKed without erasing again.
    std::vector<uint8_t> start;
    start.push_back(target);
    putLe32(start, size);
    putLe32(start, crc);
    const int startTimeout = target == protocol::ADDR_REMOTE ? 15000 : 3000;
    _comm.clearFwStatus();
    if (!_comm.sendReliable(target, MsgType::FW_UPDATE_START, start, startTimeout,
                            /*maxRetries=*/3)) {
        auto st = _comm.waitFwStatus(500);
        error("firmware update: device did not accept FW_UPDATE_START" + withStatus(st));
        return false;
    }
    // READY follows the ACK; tolerate its absence, fail on an error status.
    auto st = _comm.waitFwStatus(1000);
    if (isFailure(st)) {
        error(std::string("firmware update: device reported ") + statusText(*st) + " after START");
        return false;
    }

    // FW_UPDATE_DATA chunks: offset(4) + bytes. A retransmitted chunk is
    // re-ACKed by the device without rewriting (same PacketId in sendReliable).
    for (uint32_t off = 0; off < size; off += kChunk) {
        uint32_t n = std::min<uint32_t>(kChunk, size - off);
        std::vector<uint8_t> payload;
        putLe32(payload, off);
        payload.insert(payload.end(), image.begin() + off, image.begin() + off + n);
        if (!_comm.sendReliable(target, MsgType::FW_UPDATE_DATA, payload, /*timeoutMs=*/1500,
                                /*maxRetries=*/8)) {
            auto s = _comm.waitFwStatus(300);
            error("firmware update: chunk at offset " + std::to_string(off) + " failed" +
                  withStatus(s));
            return false;
        }
        auto s = _comm.takeFwStatus();
        if (isFailure(s)) {
            error(std::string("firmware update: device reported ") + statusText(*s) +
                  " at offset " + std::to_string(off));
            return false;
        }
        if (cb) cb(off + n, size);
    }

    // FW_UPDATE_END: device verifies the file CRC and the trailer, then
    // answers CRC_OK (applies / reboots) or CRC_FAIL / WRITE_ERROR / BAD_SIZE.
    _comm.clearFwStatus();
    bool acked = _comm.sendReliable(target, MsgType::FW_UPDATE_END, {}, /*timeoutMs=*/5000,
                                    /*maxRetries=*/3);
    auto endStatus = _comm.waitFwStatus(acked ? 3000 : 500);
    if (!acked || isFailure(endStatus)) {
        error("firmware update: device rejected the image at FW_UPDATE_END" +
              withStatus(endStatus));
        return false;
    }
    if (!endStatus) {
        info("firmware update: END was ACKed but no CRC_OK status arrived; assuming success");
    }
    return true;
}

bool FirmwareUpdater::updateRemote(const std::string& binPath, Progress cb) {
    auto image = loadImage(binPath);
    return !image.empty() && updateRemote(image, binPath, cb);
}

bool FirmwareUpdater::updateBase(const std::string& binPath, Progress cb) {
    auto image = loadImage(binPath);
    return !image.empty() && updateBase(image, binPath, cb);
}

bool FirmwareUpdater::updateRemote(const std::vector<uint8_t>& image, const std::string& name, Progress cb) {
    if (!checkImage(image, name, protocol::ADDR_REMOTE)) return false;
    info("Updating remote over BLE (" + std::to_string(image.size()) + " bytes)...");

    // No DIAG_GET while the image streams (spec: never during an OTA); base keepalive/status go on (not setUpdating).
    struct DiagPauseGuard {
        CommHandler& comm;
        explicit DiagPauseGuard(CommHandler& c) : comm(c) { comm.pauseDiag(true); }
        ~DiagPauseGuard() { comm.pauseDiag(false); }
    } diagPaused(_comm);

    bool ok = streamImage(protocol::ADDR_REMOTE, image, cb);
    if (ok) {
        info("Remote accepted the image; it will reboot and the bootloader will apply it.");
    }
    return ok;
}

bool FirmwareUpdater::updateBase(const std::vector<uint8_t>& image, const std::string& name, Progress cb) {
    if (!checkImage(image, name, protocol::ADDR_BASE)) return false;
    info("Updating base over USB (" + std::to_string(image.size()) + " bytes)...");

    struct UpdatingGuard {
        CommHandler& comm;
        explicit UpdatingGuard(CommHandler& c) : comm(c) { comm.setUpdating(true); }
        ~UpdatingGuard() { comm.setUpdating(false); }
    } updating(_comm);

    // Ask the running application to reboot into the USB bootloader.
    info("Requesting reboot into bootloader...");
    _comm.sendReliable(protocol::ADDR_BASE, MsgType::ENTER_BOOTLOADER, {}, /*timeoutMs=*/1000,
                       /*maxRetries=*/3);

    // Wait for the device to re-enumerate: the bootloader as ABCD:1235, the
    // application again as ABCD:1234.
    auto waitForPort = [](const char* pid, int settleMs) -> std::string {
        std::this_thread::sleep_for(std::chrono::milliseconds(settleMs));
        for (int i = 0; i < 100; ++i) {  // up to ~10 s
            if (auto p = findMidiControlPort(kVid, pid)) return *p;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return {};
    };

    std::string blPort = waitForPort(kBootloaderPid, 500);
    if (blPort.empty()) {
        error("firmware update: bootloader did not appear on USB");
        return false;
    }
    // The port is found through sysfs before its /dev node is usable: retry the open for a while.
    auto openPort = [this](const std::string& port) {
        return openWhenReady(port, [&] { return _comm.reconnect(port); }, std::chrono::seconds(5));
    };
    if (!openPort(blPort)) {
        error("firmware update: cannot open bootloader port " + blPort);
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    info("Sending the image to the base bootloader...");
    bool ok = streamImage(protocol::ADDR_BASE, image, cb);
    if (!ok) return false;

    info("Image programmed; base is rebooting into the new firmware...");

    // Reconnect to the application once it re-enumerates.
    std::string appPort = waitForPort(kAppPid, 1500);
    if (!appPort.empty() && openPort(appPort)) {
        info("Reconnected to the base on " + appPort + ".");
    } else {
        // The image is programmed; the reader keeps looking for the base.
        error("firmware update: base did not reappear yet; it is reconnected automatically "
              "when it comes back.");
    }
    return true;
}

}  // namespace mc
