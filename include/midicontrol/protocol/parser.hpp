#ifndef MIDICONTROL_PROTOCOL_PARSER_HPP
#define MIDICONTROL_PROTOCOL_PARSER_HPP

#include <cstdint>
#include <optional>
#include <vector>

#include "midicontrol/protocol/types.hpp"

namespace mc::protocol {

// Byte-by-byte receive state machine, mirroring the firmware Parser. Feed bytes
// as they arrive from the serial port; feed() returns a Packet once a complete,
// CRC-valid frame has been received.
class Parser {
public:
    // Feed one received byte. Returns a decoded Packet when a full frame ends.
    std::optional<Packet> feed(uint8_t byte);

    // Reset to the idle state, discarding any partially-received frame.
    void reset();

    // Frames dropped so far: failed decode (CRC, length, escaping) or runaway
    // frames without FRAME_END. Not cleared by reset().
    uint64_t badFrames() const { return _badFrames; }

private:
    enum class State : uint8_t {
        Idle,       // waiting for FRAME_START
        Receiving,  // accumulating bytes until FRAME_END
    };

    State _state = State::Idle;
    std::vector<uint8_t> _buffer;
    uint64_t _badFrames = 0;
};

}  // namespace mc::protocol

#endif  // MIDICONTROL_PROTOCOL_PARSER_HPP
