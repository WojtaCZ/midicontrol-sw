#include "midicontrol/firmware_update.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

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

// Read and validate an update file for `board`; empty on error (reported).
std::vector<uint8_t> loadImage(const std::string& path, uint8_t board) {
    auto image = readFile(path);
    if (image.empty()) {
        std::cerr << "firmware update: cannot read " << path << "\n";
        return {};
    }
    fwimage::Info info;
    auto c = fwimage::check(image, board, &info);
    if (c != fwimage::Check::Ok) {
        std::cerr << "firmware update: refusing " << path << ": " << fwimage::describe(c) << "\n";
        return {};
    }
    std::cout << "Image " << path << ": " << info.imageSize << " bytes, version "
              << (info.version >> 16) << "." << ((info.version >> 8) & 0xFF) << "."
              << (info.version & 0xFF) << "\n";
    return image;
}

}  // namespace

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
        std::cerr << "firmware update: device did not accept FW_UPDATE_START"
                  << (st ? std::string(" (") + statusText(*st) + ")" : std::string()) << "\n";
        return false;
    }
    // READY follows the ACK; tolerate its absence, fail on an error status.
    auto st = _comm.waitFwStatus(1000);
    if (isFailure(st)) {
        std::cerr << "firmware update: device reported " << statusText(*st) << " after START\n";
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
            std::cerr << "\nfirmware update: chunk at offset " << off << " failed"
                      << (s ? std::string(" (") + statusText(*s) + ")" : std::string()) << "\n";
            return false;
        }
        auto s = _comm.takeFwStatus();
        if (isFailure(s)) {
            std::cerr << "\nfirmware update: device reported " << statusText(*s) << " at offset "
                      << off << "\n";
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
        std::cerr << "\nfirmware update: device rejected the image at FW_UPDATE_END"
                  << (endStatus ? std::string(" (") + statusText(*endStatus) + ")"
                                : std::string())
                  << "\n";
        return false;
    }
    if (!endStatus) {
        std::cerr << "\nfirmware update: END was ACKed but no CRC_OK status arrived; "
                     "assuming success\n";
    }
    return true;
}

bool FirmwareUpdater::updateRemote(const std::string& binPath, Progress cb) {
    auto image = loadImage(binPath, protocol::ADDR_REMOTE);
    if (image.empty()) return false;
    std::cout << "Updating remote over BLE (" << image.size() << " bytes)...\n";
    bool ok = streamImage(protocol::ADDR_REMOTE, image, cb);
    if (ok) {
        std::cout << "\nRemote accepted the image; it will reboot and the bootloader "
                     "will apply it.\n";
    }
    return ok;
}

bool FirmwareUpdater::updateBase(const std::string& binPath, Progress cb) {
    auto image = loadImage(binPath, protocol::ADDR_BASE);
    if (image.empty()) return false;
    std::cout << "Updating base over USB (" << image.size() << " bytes)...\n";

    // Ask the running application to reboot into the USB bootloader.
    std::cout << "Requesting reboot into bootloader...\n";
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
        std::cerr << "firmware update: bootloader did not appear on USB\n";
        return false;
    }
    if (!_comm.reconnect(blPort)) {
        std::cerr << "firmware update: cannot open bootloader port " << blPort << "\n";
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    bool ok = streamImage(protocol::ADDR_BASE, image, cb);
    if (!ok) return false;

    std::cout << "\nImage programmed; base is rebooting into the new firmware...\n";

    // Reconnect to the application once it re-enumerates.
    std::string appPort = waitForPort(kAppPid, 1500);
    if (!appPort.empty()) {
        _comm.reconnect(appPort);
        std::cout << "Reconnected to the base on " << appPort << ".\n";
    } else {
        std::cerr << "Warning: base did not reappear; reconnect manually.\n";
    }
    return true;
}

}  // namespace mc
