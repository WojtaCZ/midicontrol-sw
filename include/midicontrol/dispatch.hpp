#ifndef MIDICONTROL_DISPATCH_HPP
#define MIDICONTROL_DISPATCH_HPP

#include <cstddef>
#include <cstdint>
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
    std::map<uint8_t, Reply> _last;  // per source: last reliable frame and our answer
};

}  // namespace mc

#endif  // MIDICONTROL_DISPATCH_HPP
