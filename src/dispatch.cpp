#include "midicontrol/dispatch.hpp"

#include "midicontrol/protocol/constants.hpp"
#include "midicontrol/songs.hpp"

namespace mc {

using namespace mc::protocol;

bool Dispatcher::needsAck(MsgType type) {
    switch (type) {
        case MsgType::ACK:
        case MsgType::NAK:
        case MsgType::KEEPALIVE:
        case MsgType::KEEPALIVE_ACK:
        case MsgType::NOW_PLAYING_INFO:
            return false;
        default:
            return true;
    }
}

void Dispatcher::reply(const Packet& pkt, std::optional<NakReason> nak) {
    auto& seen = _last[pkt.source];
    seen.push_back(Reply{pkt.packetId, nak.has_value(), nak ? static_cast<uint8_t>(*nak) : uint8_t{0}});
    if (seen.size() > kRemembered) seen.pop_front();
    if (!_out.send) return;
    if (nak) {
        _out.send(pkt.source, MsgType::NAK, {pkt.packetId, static_cast<uint8_t>(*nak)});
    } else {
        _out.send(pkt.source, MsgType::ACK, {pkt.packetId});
    }
}

void Dispatcher::handle(const Packet& pkt) {
    const bool broadcast = pkt.dest == ADDR_BROADCAST;
    if (pkt.dest != ADDR_PC && !broadcast) return;  // not for us

    switch (pkt.msgType) {
        case MsgType::ACK:
            if (!pkt.payload.empty() && _out.onAck) _out.onAck(pkt.payload[0]);
            return;
        case MsgType::NAK:
            if (pkt.payload.size() >= 2 && _out.onNak) _out.onNak(pkt.payload[0], pkt.payload[1]);
            return;
        case MsgType::KEEPALIVE_ACK:
            if (_out.onKeepaliveAck) _out.onKeepaliveAck();
            return;
        case MsgType::KEEPALIVE:
            if (!broadcast && _out.send) _out.send(pkt.source, MsgType::KEEPALIVE_ACK, {});
            return;
        case MsgType::NOW_PLAYING_INFO:
            return;  // PC -> base only; nothing to do, never ACKed
        default:
            break;
    }

    // Retransmission of the frame we just answered: answer again, don't execute.
    if (!broadcast) {
        const Reply* dup = nullptr;
        if (auto it = _last.find(pkt.source); it != _last.end()) {
            for (const auto& r : it->second) {
                if (r.packetId == pkt.packetId) dup = &r;
            }
        }
        if (dup) {
            if (_out.onDuplicate) _out.onDuplicate(pkt);
            if (_out.send) {
                if (dup->nak) {
                    _out.send(pkt.source, MsgType::NAK, {pkt.packetId, dup->reason});
                } else {
                    _out.send(pkt.source, MsgType::ACK, {pkt.packetId});
                }
            }
            return;
        }
    }

    // Broadcasts are executed but never answered.
    auto answer = [&](std::optional<NakReason> nak) {
        if (!broadcast) reply(pkt, nak);
    };

    switch (pkt.msgType) {
        case MsgType::PLAY_SONG: {
            auto code = mc::songs::sanitizeCode(pkt.payload);
            if (!code) {
                answer(NakReason::INVALID_PAYLOAD);
                return;
            }
            answer(_sink.playSong(*code, pkt.source));
            return;
        }
        case MsgType::STOP_SONG:
            if (pkt.payload.size() > 1 || (pkt.payload.size() == 1 && pkt.payload[0] > 0x01)) {
                answer(NakReason::INVALID_PAYLOAD);
                return;
            }
            answer(std::nullopt);
            _sink.stopSong(pkt.source);  // pause (0x01) is treated as stop
            return;
        case MsgType::REPLAY_ANCHOR:
            answer(_sink.addVerse());
            return;
        case MsgType::GET_SONG_LIST: {
            if (pkt.payload.size() > 1) {
                answer(NakReason::INVALID_PAYLOAD);
                return;
            }
            answer(std::nullopt);
            size_t offset = pkt.payload.empty() ? 0 : pkt.payload[0];
            auto page = _sink.songListPage(offset);
            if (!broadcast && _out.sendWithAck) {
                _out.sendWithAck(pkt.source, MsgType::SONG_LIST_RESP, page);
            }
            return;
        }
        case MsgType::DIAG_RESP:
            answer(std::nullopt);  // ACKed like STATUS_RESP
            if (!broadcast && _out.onDiagResp) _out.onDiagResp(pkt);
            return;
        case MsgType::INFO_RESP:
            answer(std::nullopt);  // ACKed like STATUS_RESP
            if (!broadcast && _out.onInfoResp) _out.onInfoResp(pkt);
            return;
        // Informational messages the PC may receive: accept.
        case MsgType::SONG_LIST_RESP:
        case MsgType::STATUS_RESP:
        case MsgType::SIGNAL_PLAYING:
        case MsgType::SIGNAL_RECORDING:
        case MsgType::SIGNAL_STOPPED:
        case MsgType::SIGNAL_ANCHOR:
        case MsgType::FW_UPDATE_STATUS:
            answer(std::nullopt);
            return;
        default:
            // Unknown, or a known type the PC does not implement.
            answer(NakReason::UNKNOWN_MSG);
            return;
    }
}

}  // namespace mc
