#ifndef MIDICONTROL_DISPATCH_HPP
#define MIDICONTROL_DISPATCH_HPP

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "midicontrol/protocol/types.hpp"

// Handling of packets received by the PC, per PROTOCOL.md v2 "Reliability":
//
//  - every message addressed to the PC is ACKed or NAKed, except ACK, NAK,
//    KEEPALIVE, KEEPALIVE_ACK, NOW_PLAYING_INFO and broadcasts;
//  - a frame with the same (Source, PacketId) as the previous one from that
//    source is answered again with the same ACK/NAK but not executed twice;
//  - unknown or unsupported types are NAKed UNKNOWN_MSG, a bad payload
//    INVALID_PAYLOAD;
//  - responses (SONG_LIST_RESP) go to whoever asked, after the ACK.
//
// No I/O and no threads here: the app layer plugs in the output functions.
namespace mc {

// The PC's command handlers. Called on the serial reader thread, so they must
// not block (queue the actual work).
class CommandSink {
public:
    virtual ~CommandSink() = default;
    // Return a NAK reason to refuse (e.g. NOT_FOUND), nullopt to accept.
    virtual std::optional<mc::protocol::NakReason> playSong(const std::string& code,
                                                            uint8_t requester) = 0;
    virtual void stopSong(uint8_t requester) = 0;
    // A7: RECORD_SONG [1] / empty (start) and [0] (stop), from the base. A sink without recording answers UNKNOWN_MSG.
    virtual std::optional<mc::protocol::NakReason> startRecording(uint8_t requester) {
        (void)requester;
        return mc::protocol::NakReason::UNKNOWN_MSG;
    }
    virtual void stopRecording(uint8_t requester) { (void)requester; }
    // Emergency abort (EMERGENCY_OFF 0x15 from the base, after the remote's MF hold): stop playback and recording,
    // clear queued state, organ state OFF. Idempotent. Default: nothing to abort.
    virtual void emergencyOff(uint8_t requester) { (void)requester; }
    // Add-verse; NOT_PLAYING when idle.
    virtual std::optional<mc::protocol::NakReason> addVerse() = 0;
    // SONG_LIST_RESP payload for the given offset.
    virtual std::vector<uint8_t> songListPage(size_t offset) = 0;
};

class Dispatcher {
public:
    struct Output {
        // Unreliable send (ACK, NAK, KEEPALIVE_ACK).
        std::function<void(uint8_t dest, mc::protocol::MsgType, const std::vector<uint8_t>&)> send;
        // Reliable send, retried until ACKed (responses such as SONG_LIST_RESP).
        std::function<void(uint8_t dest, mc::protocol::MsgType, const std::vector<uint8_t>&)>
            sendWithAck;
        std::function<void(uint8_t ackedPid)> onAck;
        std::function<void(uint8_t nakedPid, uint8_t reason)> onNak;
        std::function<void()> onKeepaliveAck;
        // Optional: a retransmission that was answered again but not executed.
        std::function<void(const mc::protocol::Packet&)> onDuplicate;
        // DIAG_RESP from the remote (already ACKed; a retransmission is not delivered twice).
        std::function<void(const mc::protocol::Packet&)> onDiagResp;
        // INFO_RESP from the base or the remote (already ACKed).
        std::function<void(const mc::protocol::Packet&)> onInfoResp;
    };

    Dispatcher(CommandSink& sink, Output out) : _sink(sink), _out(std::move(out)) {}

    void handle(const mc::protocol::Packet& pkt);

    // Forget duplicate-detection state (after a reconnect).
    void reset() { _last.clear(); }

    // True for message types whose receipt must be ACKed / NAKed.
    static bool needsAck(mc::protocol::MsgType type);

private:
    struct Reply {
        uint8_t packetId;
        bool nak;
        uint8_t reason;
    };

    void reply(const mc::protocol::Packet& pkt, std::optional<mc::protocol::NakReason> nak);

    CommandSink& _sink;
    Output _out;
    // Per source: the last kRemembered reliable frames and our answers. More than one: a node can have several
    // reliable frames in flight (DIAG_RESP + INFO_RESP), and the retry of an earlier one arrives after a later one.
    static constexpr size_t kRemembered = 8;
    std::map<uint8_t, std::deque<Reply>> _last;
};

}  // namespace mc

#endif  // MIDICONTROL_DISPATCH_HPP
