#ifndef MIDICONTROL_SMF_HPP
#define MIDICONTROL_SMF_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Minimal Standard MIDI File (SMF) reader, just enough to drive the MIDIControl
// player. Supports format 0/1, running status, meta events, and SysEx.
//
// NOTE on SysEx framing: the song files produced for this project embed SysEx
// as a raw F0 ... F7 run *without* the standard SMF variable-length size
// prefix. This reader therefore reads SysEx by scanning to the terminating F7
// rather than trusting a length byte. Regular (length-prefixed) SysEx that also
// ends in F7 is handled by the same scan.
namespace mc::smf {

enum class EventKind : uint8_t {
    Channel,  // note on/off, CC, program change, etc. (raw `bytes` is the message)
    SysEx,    // `bytes` is the body WITHOUT the surrounding F0/F7
    Meta,     // `bytes` is the meta payload; `metaType` is the meta type byte
};

struct Event {
    uint32_t tick = 0;       // absolute tick from start of song
    double seconds = 0.0;    // absolute time in seconds (after tempo resolution)
    EventKind kind = EventKind::Channel;
    uint8_t metaType = 0;    // valid when kind == Meta
    std::vector<uint8_t> bytes;

    bool isNoteOn() const {
        return kind == EventKind::Channel && bytes.size() == 3 &&
               (bytes[0] & 0xF0) == 0x90 && bytes[2] != 0;
    }
};

struct MidiFile {
    uint16_t format = 0;
    uint16_t division = 480;  // ticks per quarter note (PPQ); SMPTE not supported
    // All tracks merged and sorted by absolute tick, with seconds resolved from
    // the tempo map.
    std::vector<Event> events;

    // First MIDIControl display SysEx (target full/song), or empty if none.
    std::vector<uint8_t> firstDisplaySysEx() const;
};

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

// Parse an SMF from a byte buffer. Throws ParseError on malformed input.
MidiFile parse(const std::vector<uint8_t>& data);

// Read a file from disk and parse it. Throws ParseError / std::runtime_error.
MidiFile load(const std::string& path);

}  // namespace mc::smf

#endif  // MIDICONTROL_SMF_HPP
