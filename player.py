"""
MidiPlayer — custom MIDI player with MIDIControl SysEx message support.

Plays MIDI files via python-rtmidi, captures MIDIControl SysEx
messages (header 0x4D 0x43) and forwards display info via callback.
Supports verse repeat.
"""

import threading
import time
from dataclasses import dataclass, field
from typing import Optional, Callable, List

import mido
import rtmidi


# --- SysEx constants ---

SYSEX_HEADER = [0x4D, 0x43]  # 'M', 'C'

# Target addresses in SysEx (target byte)
SYSEX_TARGET_PC = 0x01
SYSEX_TARGET_DISPLAY = 0x02
SYSEX_TARGET_ALL = 0x00

# LED colors
LED_RED = 1
LED_GREEN = 2
LED_BLUE = 3
LED_YELLOW = 4
LED_OFF = 0x0F


@dataclass
class DisplayInfo:
    """Current display state from a SysEx message."""
    song: int = 0       # 0-1999
    verse: int = 0      # 0-99
    letter: str = ''    # 'A'-'D' or '' (off)
    led: int = LED_OFF  # 1=red, 2=green, 3=blue, 4=yellow, 0x0F=off


@dataclass
class VerseAnchor:
    """Verse anchor point in a MIDI file."""
    verse: int           # Verse number
    abs_time: float      # Absolute time in seconds
    event_index: int     # Event index in the merged list


class MidiPlayer:
    """MIDI file player with MIDIControl SysEx message support."""

    def __init__(self, outport_index: int):
        self._outport_index = outport_index
        self._thread: Optional[threading.Thread] = None
        self._playing = False
        self._stop_event = threading.Event()
        self._jump_event = threading.Event()

        # Callbacks
        self.on_display_info: Optional[Callable[[DisplayInfo], None]] = None
        self.on_finished: Optional[Callable[[], None]] = None

        # Verse repeat
        self._verse_anchors: List[VerseAnchor] = []
        self._current_verse_idx = 0  # Index into _verse_anchors
        self._repeat_mode = False
        self._jump_target_idx: Optional[int] = None

        # Current display info
        self.current_info: Optional[DisplayInfo] = None

        # Merged events for playback
        self._events: list = []

    @property
    def playing(self) -> bool:
        return self._playing

    @property
    def repeat_mode(self) -> bool:
        return self._repeat_mode

    def play(self, filepath: str):
        """Start playing a MIDI file in a daemon thread."""
        if self._playing:
            self.stop()

        mid = mido.MidiFile(filepath)
        self._events = self._merge_tracks(mid)
        self._verse_anchors = self._scan_verse_anchors(self._events)
        self._current_verse_idx = 0
        self._repeat_mode = False
        self._stop_event.clear()
        self._jump_event.clear()
        self._jump_target_idx = None
        self._playing = True

        self._thread = threading.Thread(target=self._playback_loop, daemon=True)
        self._thread.start()

    def stop(self):
        """Stop playback."""
        if not self._playing:
            return
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        self._playing = False

    def repeat_verse(self):
        """Jump to the start of the current verse."""
        if not self._playing or not self._verse_anchors:
            return
        self._jump_target_idx = self._verse_anchors[self._current_verse_idx].event_index
        self._jump_event.set()

    def toggle_repeat(self):
        """Toggle automatic verse repeat mode."""
        self._repeat_mode = not self._repeat_mode
        return self._repeat_mode

    # --- Internal methods ---

    @staticmethod
    def _merge_tracks(mid: mido.MidiFile) -> list:
        """Merge all tracks into a list of (abs_time, msg) sorted by time."""
        events = []
        for msg in mid:
            # mido iterates merged with delta times; we need absolute
            pass

        # Use mido merge, but compute absolute times ourselves
        abs_time = 0.0
        for msg in mid:
            abs_time += msg.time
            events.append((abs_time, msg))
        return events

    @staticmethod
    def _parse_sysex(data: list) -> Optional[DisplayInfo]:
        """Parse MIDIControl SysEx payload (full display, target=0x00).

        Format: [0x4D, 0x43, 0x01, 0x00, S3, S2, S1, S0, V1, V0, LETTER, LED]
        - S3..S0: individual song digits (0-9, 0xF=blank), 1 byte each
        - V1, V0: verse digits (0-9, 0xF=blank), 1 byte each
        - LETTER: 0xA-0xD = A-D, 0xF = off
        - LED: 1-4 = color, 0xF = off
        """
        # Check header and minimum length
        if len(data) < 12:
            return None
        if data[0] != 0x4D or data[1] != 0x43:
            return None

        # Decode song (4 individual digits)
        song = 0
        for d in data[4:8]:
            if d <= 9:
                song = song * 10 + d
            # 0xF = blank, skip

        # Decode verse (2 individual digits)
        verse = 0
        v1 = data[8]
        v0 = data[9]
        if v1 <= 9:
            verse += v1 * 10
        if v0 <= 9:
            verse += v0

        # Decode letter
        letter_byte = data[10]
        if 0x0A <= letter_byte <= 0x0D:
            letter = chr(ord('A') + (letter_byte - 0x0A))
        else:
            letter = ''

        # Decode LED
        led_byte = data[11]
        if led_byte in (1, 2, 3, 4):
            led = led_byte
        else:
            led = LED_OFF

        return DisplayInfo(song=song, verse=verse, letter=letter, led=led)

    @staticmethod
    def _scan_verse_anchors(events: list) -> List[VerseAnchor]:
        """Find verse anchors (SysEx messages with verse info) in merged events."""
        anchors = []
        seen_verses = set()

        for idx, (abs_time, msg) in enumerate(events):
            if msg.type != 'sysex':
                continue
            info = MidiPlayer._parse_sysex(list(msg.data))
            if info is None:
                continue
            # Each new verse = anchor
            if info.verse not in seen_verses:
                seen_verses.add(info.verse)
                anchors.append(VerseAnchor(
                    verse=info.verse,
                    abs_time=abs_time,
                    event_index=idx,
                ))

        return anchors

    def _notes_off(self, port: rtmidi.MidiOut):
        """Turn off all notes on all channels."""
        for ch in range(16):
            port.send_message([0xB0 | ch, 123, 0])

    def _playback_loop(self):
        """Main playback loop — runs in a daemon thread."""
        port = rtmidi.MidiOut()
        port.open_port(self._outport_index)

        try:
            self._play_from(0, 0.0, port)
        finally:
            self._notes_off(port)
            port.close_port()
            self._playing = False
            if self.on_finished and not self._stop_event.is_set():
                self.on_finished()

    def _play_from(self, start_index: int, start_time: float, port: rtmidi.MidiOut):
        """Play events from the given index/time."""
        playback_start = time.monotonic()
        time_offset = start_time  # Absolute MIDI time where we start

        idx = start_index
        while idx < len(self._events):
            if self._stop_event.is_set():
                return

            # Check for jump
            if self._jump_event.is_set():
                self._jump_event.clear()
                target_idx = self._jump_target_idx
                if target_idx is not None:
                    self._notes_off(port)
                    target_time = self._events[target_idx][0]
                    self._jump_target_idx = None
                    # Recursive call for the new starting point
                    self._play_from(target_idx, target_time, port)
                    return

            abs_time, msg = self._events[idx]

            # Wait for the correct time
            target_wall = playback_start + (abs_time - time_offset)
            while True:
                now = time.monotonic()
                remaining = target_wall - now
                if remaining <= 0:
                    break
                # Sleep in small increments for responsive stop/jump
                sleep_time = min(remaining, 0.01)
                time.sleep(sleep_time)

                if self._stop_event.is_set():
                    return
                if self._jump_event.is_set():
                    self._jump_event.clear()
                    target_idx = self._jump_target_idx
                    if target_idx is not None:
                        self._notes_off(port)
                        target_time = self._events[target_idx][0]
                        self._jump_target_idx = None
                        self._play_from(target_idx, target_time, port)
                        return

            # Process event
            if msg.type == 'sysex':
                info = self._parse_sysex(list(msg.data))
                if info is not None:
                    self.current_info = info
                    # Update verse index
                    for i, anchor in enumerate(self._verse_anchors):
                        if anchor.event_index == idx:
                            self._current_verse_idx = i
                            break
                    # Callback
                    if self.on_display_info:
                        self.on_display_info(info)
                # SysEx is NOT sent to MIDI output
            elif not msg.is_meta:
                # Send MIDI message to output
                port.send_message(msg.bytes())

            idx += 1

            # Auto-repeat at verse end
            if self._repeat_mode and self._verse_anchors:
                # If we're at the last event before the next verse
                next_verse_idx = self._current_verse_idx + 1
                if next_verse_idx < len(self._verse_anchors):
                    next_anchor = self._verse_anchors[next_verse_idx]
                    if idx >= next_anchor.event_index:
                        # Jump back to the start of the current verse
                        self._notes_off(port)
                        cur_anchor = self._verse_anchors[self._current_verse_idx]
                        self._play_from(cur_anchor.event_index, cur_anchor.abs_time, port)
                        return
