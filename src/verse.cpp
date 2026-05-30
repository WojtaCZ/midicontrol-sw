#include "midicontrol/verse.hpp"

#include <set>

#include "midicontrol/sysex.hpp"

namespace mc {

std::vector<VerseAnchor> scanVerseAnchors(const mc::smf::MidiFile& mf) {
    std::vector<VerseAnchor> anchors;
    std::set<int> seen;

    for (size_t i = 0; i < mf.events.size(); ++i) {
        const auto& ev = mf.events[i];
        if (ev.kind != mc::smf::EventKind::SysEx) continue;
        auto info = mc::sysex::parse(ev.bytes);
        if (!info.has_value() || !info->hasVerse) continue;
        if (seen.insert(info->verse).second) {
            anchors.push_back(VerseAnchor{info->verse, ev.seconds, i});
        }
    }
    return anchors;
}

}  // namespace mc
