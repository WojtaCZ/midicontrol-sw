"""
MidiPlayer — vlastní MIDI přehrávač s podporou SysEx zpráv MIDIControl.

Přehrává MIDI soubory přes python-rtmidi, zachytává MIDIControl SysEx
zprávy (header 0x4D 0x43) a předává display info přes callback.
Podpora opakování slok (verse repeat).
"""

import threading
import time
from dataclasses import dataclass, field
from typing import Optional, Callable, List

import mido
import rtmidi


# --- SysEx konstanty ---

SYSEX_HEADER = [0x4D, 0x43]  # 'M', 'C'

# Cílové adresy v SysEx (target byte)
SYSEX_TARGET_PC = 0x01
SYSEX_TARGET_DISPLAY = 0x02
SYSEX_TARGET_ALL = 0x00

# LED barvy
LED_RED = 1
LED_GREEN = 2
LED_BLUE = 3
LED_YELLOW = 4
LED_OFF = 0x0F


@dataclass
class DisplayInfo:
    """Informace o aktuálním stavu displeje z SysEx zprávy."""
    song: int = 0       # 0-1999
    verse: int = 0      # 0-99
    letter: str = ''    # 'A'-'D' nebo '' (vypnuto)
    led: int = LED_OFF  # 1=red, 2=green, 3=blue, 4=yellow, 0x0F=off


@dataclass
class VerseAnchor:
    """Kotva sloky v MIDI souboru."""
    verse: int           # Číslo sloky
    abs_time: float      # Absolutní čas v sekundách
    event_index: int     # Index události v merged seznamu


class MidiPlayer:
    """Přehrávač MIDI souborů s podporou MIDIControl SysEx zpráv."""

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
        self._current_verse_idx = 0  # Index do _verse_anchors
        self._repeat_mode = False
        self._jump_target_idx: Optional[int] = None

        # Aktuální display info
        self.current_info: Optional[DisplayInfo] = None

        # Merged události pro přehrávání
        self._events: list = []

    @property
    def playing(self) -> bool:
        return self._playing

    @property
    def repeat_mode(self) -> bool:
        return self._repeat_mode

    def play(self, filepath: str):
        """Spustí přehrávání MIDI souboru v daemon vlákně."""
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
        """Zastaví přehrávání."""
        if not self._playing:
            return
        self._stop_event.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2)
        self._playing = False

    def repeat_verse(self):
        """Skočí na začátek aktuální sloky."""
        if not self._playing or not self._verse_anchors:
            return
        self._jump_target_idx = self._verse_anchors[self._current_verse_idx].event_index
        self._jump_event.set()

    def toggle_repeat(self):
        """Přepne režim automatického opakování sloky."""
        self._repeat_mode = not self._repeat_mode
        return self._repeat_mode

    # --- Interní metody ---

    @staticmethod
    def _merge_tracks(mid: mido.MidiFile) -> list:
        """Sloučí všechny stopy do seznamu (abs_time, msg) seřazeného podle času."""
        events = []
        for msg in mid:
            # mido iteruje merged s delta časy; potřebujeme absolutní
            pass

        # Použijeme mido merge, ale počítáme absolutní časy sami
        abs_time = 0.0
        for msg in mid:
            abs_time += msg.time
            events.append((abs_time, msg))
        return events

    @staticmethod
    def _parse_sysex(data: list) -> Optional[DisplayInfo]:
        """Parsuje MIDIControl SysEx payload.

        Formát: [0x4D, 0x43, target, song_h, song_l, verse, letter_led]
        - song_h/song_l: po 2 nibblech, každý nibble = cifra (0-9) nebo F=mezera
        - verse: 2 nibbly
        - letter_led: horní nibble = písmeno (A=0xA..D=0xD, F=off),
                      dolní nibble = LED (1-4, F=off)
        """
        # Kontrola headeru
        if len(data) < 7:
            return None
        if data[0] != 0x4D or data[1] != 0x43:
            return None

        target = data[2]
        song_h = data[3]
        song_l = data[4]
        verse_byte = data[5]
        letter_led = data[6]

        # Dekódování song (4 cifry z BCD nibblů)
        d3 = (song_h >> 4) & 0x0F  # tisíce
        d2 = song_h & 0x0F         # stovky
        d1 = (song_l >> 4) & 0x0F  # desítky
        d0 = song_l & 0x0F         # jednotky

        song = 0
        for d in [d3, d2, d1, d0]:
            if d <= 9:
                song = song * 10 + d
            # F = mezera, přeskakujeme (song zůstává jako je)

        # Dekódování verse (2 BCD nibbly)
        v1 = (verse_byte >> 4) & 0x0F
        v0 = verse_byte & 0x0F
        verse = 0
        if v1 <= 9:
            verse += v1 * 10
        if v0 <= 9:
            verse += v0

        # Dekódování letter
        letter_nibble = (letter_led >> 4) & 0x0F
        if 0x0A <= letter_nibble <= 0x0D:
            letter = chr(ord('A') + (letter_nibble - 0x0A))
        else:
            letter = ''

        # Dekódování LED
        led_nibble = letter_led & 0x0F
        if led_nibble in (1, 2, 3, 4):
            led = led_nibble
        else:
            led = LED_OFF

        return DisplayInfo(song=song, verse=verse, letter=letter, led=led)

    @staticmethod
    def _scan_verse_anchors(events: list) -> List[VerseAnchor]:
        """Najde kotvy slok (SysEx zprávy s verse info) v merged událostech."""
        anchors = []
        seen_verses = set()

        for idx, (abs_time, msg) in enumerate(events):
            if msg.type != 'sysex':
                continue
            info = MidiPlayer._parse_sysex(list(msg.data))
            if info is None:
                continue
            # Každá nová sloka = kotva
            if info.verse not in seen_verses:
                seen_verses.add(info.verse)
                anchors.append(VerseAnchor(
                    verse=info.verse,
                    abs_time=abs_time,
                    event_index=idx,
                ))

        return anchors

    def _notes_off(self, port: rtmidi.MidiOut):
        """Vypne všechny noty na všech kanálech."""
        for ch in range(16):
            port.send_message([0xB0 | ch, 123, 0])

    def _playback_loop(self):
        """Hlavní smyčka přehrávání — běží v daemon vlákně."""
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
        """Přehraje události od daného indexu/času."""
        playback_start = time.monotonic()
        time_offset = start_time  # Absolutní čas v MIDI, kde začínáme

        idx = start_index
        while idx < len(self._events):
            if self._stop_event.is_set():
                return

            # Kontrola jump
            if self._jump_event.is_set():
                self._jump_event.clear()
                target_idx = self._jump_target_idx
                if target_idx is not None:
                    self._notes_off(port)
                    target_time = self._events[target_idx][0]
                    self._jump_target_idx = None
                    # Rekurzivní volání pro nový startovní bod
                    self._play_from(target_idx, target_time, port)
                    return

            abs_time, msg = self._events[idx]

            # Čekání na správný čas
            target_wall = playback_start + (abs_time - time_offset)
            while True:
                now = time.monotonic()
                remaining = target_wall - now
                if remaining <= 0:
                    break
                # Spíme po malých krocích pro responsivní stop/jump
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

            # Zpracování události
            if msg.type == 'sysex':
                info = self._parse_sysex(list(msg.data))
                if info is not None:
                    self.current_info = info
                    # Aktualizace verse indexu
                    for i, anchor in enumerate(self._verse_anchors):
                        if anchor.event_index == idx:
                            self._current_verse_idx = i
                            break
                    # Callback
                    if self.on_display_info:
                        self.on_display_info(info)
                # SysEx se NEposílá na MIDI výstup
            elif not msg.is_meta:
                # Odeslání MIDI zprávy na výstup
                port.send_message(msg.bytes())

            idx += 1

            # Auto-repeat na konci sloky
            if self._repeat_mode and self._verse_anchors:
                # Pokud jsme na poslední události před další slokou
                next_verse_idx = self._current_verse_idx + 1
                if next_verse_idx < len(self._verse_anchors):
                    next_anchor = self._verse_anchors[next_verse_idx]
                    if idx >= next_anchor.event_index:
                        # Skok zpět na začátek aktuální sloky
                        self._notes_off(port)
                        cur_anchor = self._verse_anchors[self._current_verse_idx]
                        self._play_from(cur_anchor.event_index, cur_anchor.abs_time, port)
                        return
