#include "midicontrol/midi_ports.hpp"

#include <RtMidi.h>

#include "midicontrol/serial_port.hpp"

namespace mc {

std::optional<MidiPorts> findMidiControlPorts(const std::string& keyArg) {
    const std::string key = keyArg.empty() ? midiKey() : keyArg;
    MidiPorts ports;
    try {
        RtMidiOut out;
        unsigned int nOut = out.getPortCount();
        for (unsigned int i = 0; i < nOut; ++i) {
            std::string name = out.getPortName(i);
            if (name.find(key) != std::string::npos) {
                ports.outIndex = static_cast<int>(i);
                ports.outName = name;
            }
        }

        RtMidiIn in;
        unsigned int nIn = in.getPortCount();
        for (unsigned int i = 0; i < nIn; ++i) {
            std::string name = in.getPortName(i);
            if (name.find(key) != std::string::npos) {
                ports.inIndex = static_cast<int>(i);
                ports.inName = name;
            }
        }
    } catch (RtMidiError&) {
        return std::nullopt;
    }

    if (ports.outIndex < 0 || ports.inIndex < 0) return std::nullopt;
    return ports;
}

}  // namespace mc
