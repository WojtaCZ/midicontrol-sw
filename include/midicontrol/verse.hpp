#ifndef MIDICONTROL_VERSE_HPP
#define MIDICONTROL_VERSE_HPP

#include <cstdint>
#include <vector>

#include "midicontrol/smf.hpp"

namespace mc {

// A point in the merged event stream where a new verse begins (marked by a
// MIDIControl display SysEx carrying a not-yet-seen verse number).
struct VerseAnchor {
    int verse = 0;          // verse number from the SysEx
    double seconds = 0.0;   // absolute time of the anchor
    size_t eventIndex = 0;  // index into MidiFile::events
};

// Scan a parsed file for verse anchors, in event order. The first occurrence of
// each distinct verse number becomes an anchor (mirrors the Python player).
std::vector<VerseAnchor> scanVerseAnchors(const mc::smf::MidiFile& mf);

}  // namespace mc

#endif  // MIDICONTROL_VERSE_HPP
