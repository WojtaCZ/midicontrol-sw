#include <cstdio>

#include "midicontrol/protocol/types.hpp"

namespace mc::protocol {

const char* msgTypeName(MsgType type) {
    switch (type) {
        case MsgType::ACK: return "ACK";
        case MsgType::NAK: return "NAK";
        case MsgType::PLAY_SONG: return "PLAY_SONG";
        case MsgType::STOP_SONG: return "STOP_SONG";
        case MsgType::RECORD_SONG: return "RECORD_SONG";
        case MsgType::NOW_PLAYING_INFO: return "NOW_PLAYING_INFO";
        case MsgType::REPLAY_ANCHOR: return "REPLAY_ANCHOR";
        case MsgType::GET_SONG_LIST: return "GET_SONG_LIST";
        case MsgType::SONG_LIST_RESP: return "SONG_LIST_RESP";
        case MsgType::SIGNAL_PLAYING: return "SIGNAL_PLAYING";
        case MsgType::SIGNAL_RECORDING: return "SIGNAL_RECORDING";
        case MsgType::SIGNAL_STOPPED: return "SIGNAL_STOPPED";
        case MsgType::SIGNAL_ANCHOR: return "SIGNAL_ANCHOR";
        case MsgType::SIGNAL_INFO: return "SIGNAL_INFO";
        case MsgType::SET_CURRENT: return "SET_CURRENT";
        case MsgType::GET_STATUS: return "GET_STATUS";
        case MsgType::STATUS_RESP: return "STATUS_RESP";
        case MsgType::GET_INFO: return "GET_INFO";
        case MsgType::INFO_RESP: return "INFO_RESP";
        case MsgType::KEEPALIVE: return "KEEPALIVE";
        case MsgType::KEEPALIVE_ACK: return "KEEPALIVE_ACK";
        case MsgType::FW_UPDATE_START: return "FW_UPDATE_START";
        case MsgType::FW_UPDATE_DATA: return "FW_UPDATE_DATA";
        case MsgType::FW_UPDATE_END: return "FW_UPDATE_END";
        case MsgType::FW_UPDATE_STATUS: return "FW_UPDATE_STATUS";
        case MsgType::ENTER_BOOTLOADER: return "ENTER_BOOTLOADER";
        case MsgType::DIAG_GET: return "DIAG_GET";
        case MsgType::DIAG_RESP: return "DIAG_RESP";
    }
    return "UNKNOWN";
}

const char* nakReasonName(uint8_t reason) {
    switch (static_cast<NakReason>(reason)) {
        case NakReason::UNKNOWN_MSG: return "UNKNOWN_MSG";
        case NakReason::INVALID_PAYLOAD: return "INVALID_PAYLOAD";
        case NakReason::CRC_ERROR: return "CRC_ERROR";
        case NakReason::BUSY: return "BUSY";
        case NakReason::NOT_PLAYING: return "NOT_PLAYING";
        case NakReason::UNREACHABLE: return "UNREACHABLE";
        case NakReason::NOT_FOUND: return "NOT_FOUND";
        case NakReason::NO_TRANSFER: return "NO_TRANSFER";
    }
    return "UNKNOWN";
}

std::string describePacket(const Packet& pkt) {
    std::string s = msgTypeName(pkt.msgType);
    s += " id=" + std::to_string(pkt.packetId);
    const auto& p = pkt.payload;
    if (pkt.msgType == MsgType::ACK && !p.empty()) {
        s += " for=" + std::to_string(p[0]);
    } else if (pkt.msgType == MsgType::NAK && p.size() >= 2) {
        s += " for=" + std::to_string(p[0]) + " " + nakReasonName(p[1]);
    } else if (!p.empty()) {
        constexpr size_t kMaxBytes = 16;
        s += " [";
        for (size_t i = 0; i < p.size() && i < kMaxBytes; ++i) {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X", p[i]);
            if (i) s += ' ';
            s += buf;
        }
        if (p.size() > kMaxBytes) s += " ...";
        s += "]";
    }
    return s;
}

}  // namespace mc::protocol
