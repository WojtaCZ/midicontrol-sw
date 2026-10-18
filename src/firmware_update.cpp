#include "midicontrol/firmware_update.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/serial_port.hpp"

namespace mc {

using mc::protocol::MsgType;

uint32_t crc32(const uint8_t* data, std::size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

namespace {

constexpr std::size_t kChunk = 120;  // <= MAX_PAYLOAD_SIZE - 4 (offset header)

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

}  // namespace

bool FirmwareUpdater::streamImage(uint8_t target, const std::vector<uint8_t>& image, Progress cb) {
    const uint32_t size = static_cast<uint32_t>(image.size());
    const uint32_t crc = crc32(image.data(), image.size());

    // FW_UPDATE_START: target(1) + size(4) + crc32(4)
    std::vector<uint8_t> start;
    start.push_back(target);
    putLe32(start, size);
    putLe32(start, crc);
    if (!_comm.sendReliable(target, MsgType::FW_UPDATE_START, start, /*timeoutMs=*/3000,
                            /*maxRetries=*/5)) {
        std::cerr << "firmware update: device did not accept FW_UPDATE_START\n";
        return false;
    }

    // FW_UPDATE_DATA chunks: offset(4) + bytes
    for (uint32_t off = 0; off < size; off += kChunk) {
        uint32_t n = std::min<uint32_t>(kChunk, size - off);
        std::vector<uint8_t> payload;
        putLe32(payload, off);
        payload.insert(payload.end(), image.begin() + off, image.begin() + off + n);
        if (!_comm.sendReliable(target, MsgType::FW_UPDATE_DATA, payload, /*timeoutMs=*/1500,
                                /*maxRetries=*/8)) {
            std::cerr << "\nfirmware update: chunk at offset " << off << " failed\n";
            return false;
        }
        if (cb) cb(off + n, size);
    }

    // FW_UPDATE_END: device verifies the CRC and applies/reboots.
    if (!_comm.sendReliable(target, MsgType::FW_UPDATE_END, {}, /*timeoutMs=*/5000,
                            /*maxRetries=*/3)) {
        std::cerr << "firmware update: device rejected the image at FW_UPDATE_END "
                     "(CRC mismatch?)\n";
        return false;
    }
    return true;
}

bool FirmwareUpdater::updateRemote(const std::string& binPath, Progress cb) {
    auto image = readFile(binPath);
    if (image.empty()) {
        std::cerr << "firmware update: cannot read " << binPath << "\n";
        return false;
    }
    std::cout << "Updating remote over BLE (" << image.size() << " bytes)...\n";
    bool ok = streamImage(protocol::ADDR_REMOTE, image, cb);
    if (ok) {
        std::cout << "\nRemote accepted the image; it will reboot and the bootloader "
                     "will apply it.\n";
    }
    return ok;
}

bool FirmwareUpdater::updateBase(const std::string& binPath, Progress cb) {
    auto image = readFile(binPath);
    if (image.empty()) {
        std::cerr << "firmware update: cannot read " << binPath << "\n";
        return false;
    }
    std::cout << "Updating base over USB (" << image.size() << " bytes)...\n";

    // Ask the running application to reboot into the USB bootloader.
    std::cout << "Requesting reboot into bootloader...\n";
    _comm.sendReliable(protocol::ADDR_BASE, MsgType::ENTER_BOOTLOADER, {}, /*timeoutMs=*/1000,
                       /*maxRetries=*/3);

    // Wait for the device to re-enumerate as the bootloader (same VID:PID).
    auto waitForPort = [](int settleMs) -> std::string {
        std::this_thread::sleep_for(std::chrono::milliseconds(settleMs));
        for (int i = 0; i < 100; ++i) {  // up to ~10 s
            if (auto p = findMidiControlPort()) return *p;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return {};
    };

    std::string blPort = waitForPort(1500);
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
    std::string appPort = waitForPort(1500);
    if (!appPort.empty()) {
        _comm.reconnect(appPort);
        std::cout << "Reconnected to the base on " << appPort << ".\n";
    } else {
        std::cerr << "Warning: base did not reappear; reconnect manually.\n";
    }
    return true;
}

}  // namespace mc
