#include "midicontrol/playback.hpp"

#include "midicontrol/sysex.hpp"

namespace mc {

using mc::display::Field;
using mc::display::Update;

// ---------------------------------------------------------------------------
// Sections
// ---------------------------------------------------------------------------

std::vector<Section> scanSections(const mc::smf::MidiFile& mf) {
    std::vector<Section> out(1);  // prelude-like head until a verse anchor says otherwise
    bool noteSeen = false;
    bool anchorSeen = false;

    for (size_t i = 0; i < mf.events.size(); ++i) {
        const auto& ev = mf.events[i];
        if (ev.kind == mc::smf::EventKind::Channel) {
            noteSeen = true;
            continue;
        }
        if (ev.kind != mc::smf::EventKind::SysEx) continue;
        auto info = mc::sysex::parse(ev.bytes);
        if (!info || !info->hasVerse) continue;

        const bool blank = info->verseDigits[0] == mc::sysex::BLANK_DIGIT &&
                           info->verseDigits[1] == mc::sysex::BLANK_DIGIT;
        const int verse = blank ? 0 : info->verse;

        // Anchors before the first note, or on the tick of the current section,
        // define the current section instead of starting an empty one.
        Section& last = out.back();
        if ((!anchorSeen && !noteSeen) || ev.tick == last.tick) {
            last.verse = verse;
            anchorSeen = true;
            continue;
        }
        anchorSeen = true;

        // The section starts with the first event on the anchor's tick, so notes
        // merged in front of the SysEx on that tick belong to it.
        size_t first = i;
        while (first > 0 && mf.events[first - 1].tick == ev.tick) --first;
        Section s;
        s.firstEvent = first;
        s.tick = ev.tick;
        s.startSeconds = ev.seconds;
        s.verse = verse;
        out.push_back(s);
    }
    return out;
}

std::string sectionLabel(const Section& s, int pendingRepeats) {
    std::string l = s.verse == 0 ? "Predehra" : "Sloka " + std::to_string(s.verse);
    if (pendingRepeats > 0) l += " +" + std::to_string(pendingRepeats);
    return l;
}

// ---------------------------------------------------------------------------
// PlaybackEngine
// ---------------------------------------------------------------------------

PlaybackEngine::PlaybackEngine(mc::smf::MidiFile file, std::string code, PlaybackOptions opts,
                               Callbacks cb)
    : _file(std::move(file)),
      _code(std::move(code)),
      _opts(opts),
      _cb(std::move(cb)),
      _sections(scanSections(_file)) {
    // The leading anchor: every display SysEx before the first note. Merged in
    // order, so a later partial message overrides an earlier field.
    uint32_t firstNoteTick = UINT32_MAX;
    for (const auto& ev : _file.events) {
        if (ev.kind == mc::smf::EventKind::Channel) {
            firstNoteTick = ev.tick;
            break;
        }
    }
    _isHeadAnchor.assign(_file.events.size(), false);
    for (size_t i = 0; i < _file.events.size(); ++i) {
        const auto& ev = _file.events[i];
        if (ev.tick > firstNoteTick) break;
        if (ev.kind != mc::smf::EventKind::SysEx) continue;
        auto info = mc::sysex::parse(ev.bytes);
        if (!info) continue;
        _isHeadAnchor[i] = true;
        Update u = mc::display::fromSysEx(*info);
        if (u.song == Field::Set) {
            _base.song = Field::Set;
            _base.songDigits = u.songDigits;
        }
        if (u.verse == Field::Set) {
            _base.verse = Field::Set;
            _base.verseDigits = u.verseDigits;
        }
        if (u.letter == Field::Set) {
            _base.letter = Field::Set;
            _base.letterCode = u.letterCode;
        }
        if (u.led == Field::Set) {
            _base.led = Field::Set;
            _base.ledCode = u.ledCode;
        }
    }

    // Fallback to the song code (523.mid has an empty anchor).
    auto fromCode = mc::display::fromSongCode(_code);
    if (_base.song != Field::Set) {
        if (fromCode) {
            _base.song = Field::Set;
            _base.songDigits = fromCode->songDigits;
            if (_base.letter != Field::Set) {
                _base.letter = Field::Set;
                _base.letterCode = fromCode->letterCode;
            }
        } else {
            _base.song = Field::Blank;
        }
    }
    // A new song always defines every field so nothing of the previous song
    // stays on the indicator.
    if (_base.verse != Field::Set) _base.verse = Field::Blank;
    if (_base.letter != Field::Set) _base.letter = Field::Blank;
    if (_base.led != Field::Set) _base.led = Field::Blank;
}

bool PlaybackEngine::addVerse() {
    std::string label;
    {
        std::lock_guard<std::mutex> lk(_mutex);
        if (_stop.load() || _finished) return false;
        ++_pending;
        label = sectionLabel(_sections[_current], _pending);
    }
    if (_cb.anchor) _cb.anchor(label);  // show "+k" on the remote right away
    return true;
}

std::string PlaybackEngine::label() const {
    std::lock_guard<std::mutex> lk(_mutex);
    return sectionLabel(_sections[_current], _pending);
}

void PlaybackEngine::announce() {
    if (_cb.anchor) _cb.anchor(label());
}

void PlaybackEngine::allNotesOff() {
    if (!_cb.midi) return;
    for (int ch = 0; ch < 16; ++ch) {
        _cb.midi({static_cast<uint8_t>(0xB0 | ch), 123, 0});
    }
}

PlaybackEngine::Result PlaybackEngine::run(Timebase& tb) {
    _running.store(true);
    Result result = Result::Finished;
    const auto& events = _file.events;

    // The preamble (organ setup) runs once, before the start display, so a
    // stop during it ends the run without any song event having been sent.
    if (!_opts.preamble.empty()) {
        tb.startPass();
        double t = 0.0;
        for (const auto& step : _opts.preamble) {
            if (_stop.load()) {
                _running.store(false);
                return Result::Stopped;
            }
            if (_cb.midi) _cb.midi(step.bytes);
            t += step.delayAfterSeconds;
            if (!tb.waitUntil(t)) {
                _running.store(false);
                return Result::Stopped;
            }
        }
    }

    if (_cb.display) _cb.display(_base);
    announce();
    tb.startPass();

    size_t idx = 0;
    double offset = 0.0;  // file time at the last startPass()
    while (true) {
        size_t sec;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            sec = _current;
        }
        const bool lastSection = sec + 1 >= _sections.size();
        const size_t end = lastSection ? events.size() : _sections[sec + 1].firstEvent;
        const double endTime = lastSection ? (events.empty() ? 0.0 : events.back().seconds)
                                           : _sections[sec + 1].startSeconds;

        bool aborted = false;
        for (; idx < end; ++idx) {
            const auto& ev = events[idx];
            if (_stop.load() || !tb.waitUntil(ev.seconds - offset)) {
                aborted = true;
                break;
            }
            if (ev.kind == mc::smf::EventKind::Channel) {
                const bool programChange = !ev.bytes.empty() && (ev.bytes[0] & 0xF0) == 0xC0;
                if (_cb.midi && !(programChange && _opts.dropProgramChanges)) _cb.midi(ev.bytes);
            } else if (ev.kind == mc::smf::EventKind::SysEx && !_isHeadAnchor[idx]) {
                auto info = mc::sysex::parse(ev.bytes);
                if (info && _cb.display) _cb.display(mc::display::fromSysEx(*info));
            }
            // Meta events are not sent to the output.
        }
        // Keep a trailing rest of the section before repeating or moving on.
        if (!aborted && (_stop.load() || !tb.waitUntil(endTime - offset))) aborted = true;
        if (aborted) {
            result = Result::Stopped;
            break;
        }

        bool repeat;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            repeat = _pending > 0;
            if (repeat) {
                --_pending;
            } else if (lastSection) {
                _finished = true;
            } else {
                ++_current;
            }
        }
        if (!repeat && lastSection) break;
        if (!repeat) {
            announce();  // the section's own anchor SysEx updates the indicator
            continue;
        }

        // Replay the current section from its anchor; the indicator is left as
        // it is, so the verse number stays the same.
        allNotesOff();
        if (_opts.gapSeconds > 0.0) {
            tb.startPass();
            if (!tb.waitUntil(_opts.gapSeconds) || _stop.load()) {
                result = Result::Stopped;
                break;
            }
        }
        announce();
        idx = _sections[sec].firstEvent;
        offset = _sections[sec].startSeconds;
        tb.startPass();
    }

    allNotesOff();
    {
        std::lock_guard<std::mutex> lk(_mutex);
        _finished = true;
    }
    _running.store(false);
    return result;
}

}  // namespace mc
