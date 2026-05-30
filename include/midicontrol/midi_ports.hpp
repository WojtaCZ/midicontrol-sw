#ifndef MIDICONTROL_MIDI_PORTS_HPP
#define MIDICONTROL_MIDI_PORTS_HPP

#include <optional>
#include <string>

namespace mc {

// Discovered MIDIControl ALSA MIDI ports (via RtMidi).
struct MidiPorts {
    int outIndex = -1;
    int inIndex = -1;
    std::string outName;
    std::string inName;
};

// Find the input/output ports whose name contains "MIDIControl". Returns
// nullopt if either is missing.
std::optional<MidiPorts> findMidiControlPorts(const std::string& key = "MIDIControl");

}  // namespace mc

#endif  // MIDICONTROL_MIDI_PORTS_HPP
