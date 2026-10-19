#include "midicontrol/protocol/parser.hpp"

#include "midicontrol/protocol/codec.hpp"
#include "midicontrol/protocol/constants.hpp"

namespace mc::protocol {

std::optional<Packet> Parser::feed(uint8_t byte) {
    if (_state == State::Idle) {
        if (byte == FRAME_START) {
            _state = State::Receiving;
            _buffer.clear();
        }
        return std::nullopt;
    }

    // State::Receiving
    if (byte == FRAME_END) {
        _state = State::Idle;
        auto pkt = decodeFrame(_buffer);
        _buffer.clear();
        if (!pkt) ++_badFrames;
        return pkt;  // nullopt on a malformed frame is fine — caller ignores it
    }

    if (byte == FRAME_START) {
        // A new frame started before the previous one finished: resync.
        _buffer.clear();
        return std::nullopt;
    }

    // Guard against runaway frames (line noise without a FRAME_END).
    if (_buffer.size() >= MAX_FRAME_SIZE) {
        ++_badFrames;
        _state = State::Idle;
        _buffer.clear();
        return std::nullopt;
    }

    _buffer.push_back(byte);
    return std::nullopt;
}

void Parser::reset() {
    _state = State::Idle;
    _buffer.clear();
}

}  // namespace mc::protocol
