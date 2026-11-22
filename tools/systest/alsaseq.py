"""Minimal ALSA sequencer client via ctypes (libasound): the MIDI capture/inject endpoint of the virtual tier.

The client is named `systest-<pid>` (never contains "MIDIControl"). It has ONE port with read+write+subscribe
capabilities, so RtMidi (the PC) finds it both as an output and an input port when MIDICONTROL_MIDI_KEY is
`systest-<pid>`. Events are time-stamped with time.monotonic() when the reader thread receives them."""
import ctypes
import ctypes.util
import os
import select
import threading
import time

_lib = ctypes.CDLL(ctypes.util.find_library("asound") or "libasound.so.2")

SND_SEQ_OPEN_DUPLEX = 3
CAP_ALL = 0x1 | 0x2 | 0x20 | 0x40 | 0x4  # READ WRITE SUBS_READ SUBS_WRITE ... (0x4 = DUPLEX)
TYPE_MIDI_GENERIC = 1 << 1
TYPE_APPLICATION = 1 << 20

SND_SEQ_EVENT_NOTEON, SND_SEQ_EVENT_NOTEOFF = 6, 7
SND_SEQ_EVENT_CONTROLLER, SND_SEQ_EVENT_PGMCHANGE = 10, 11
SND_SEQ_EVENT_SYSEX = 130
SND_SEQ_EVENT_CLIENT_START, SND_SEQ_EVENT_PORT_SUBSCRIBED = 60, 66

_lib.snd_seq_event_input.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
_lib.snd_midi_event_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_void_p]
_lib.snd_midi_event_decode.restype = ctypes.c_long
_lib.snd_seq_event_input_pending.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.snd_seq_poll_descriptors_count.argtypes = [ctypes.c_void_p, ctypes.c_short]
_lib.snd_seq_poll_descriptors.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_short]


class _PollFd(ctypes.Structure):
    _fields_ = [("fd", ctypes.c_int), ("events", ctypes.c_short), ("revents", ctypes.c_short)]


class SeqClient:
    def __init__(self, name=None):
        self.name = name or f"systest-{os.getpid()}"
        self._seq = ctypes.c_void_p()
        if _lib.snd_seq_open(ctypes.byref(self._seq), b"default", SND_SEQ_OPEN_DUPLEX, 0) < 0:
            raise RuntimeError("cannot open ALSA sequencer")
        _lib.snd_seq_set_client_name(self._seq, self.name.encode())
        self.port = _lib.snd_seq_create_simple_port(
            self._seq, b"port", 0x1 | 0x2 | 0x20 | 0x40, TYPE_MIDI_GENERIC | TYPE_APPLICATION)
        if self.port < 0:
            raise RuntimeError("cannot create ALSA port")
        _lib.snd_seq_nonblock(self._seq, 1)
        self.client = _lib.snd_seq_client_id(self._seq)
        self._dev = ctypes.c_void_p()
        _lib.snd_midi_event_new(4096, ctypes.byref(self._dev))
        _lib.snd_midi_event_init(self._dev)
        _lib.snd_midi_event_no_status(self._dev, 1)
        # Wake the reader through poll() instead of a sleep loop: a sleep would add up to its period of
        # jitter to every timestamp, which the timing tests (test_virtual_timing.py) measure in tens of us.
        n = _lib.snd_seq_poll_descriptors_count(self._seq, select.POLLIN)
        pfds = (_PollFd * n)()
        _lib.snd_seq_poll_descriptors(self._seq, pfds, n, select.POLLIN)
        self._poll = select.poll()
        for p in pfds:
            self._poll.register(p.fd, select.POLLIN)
        self.events = []  # (monotonic time, bytes)
        self._lock = threading.Lock()
        self._run = True
        self._thr = threading.Thread(target=self._loop, daemon=True)
        self._thr.start()

    def _loop(self):
        buf = ctypes.create_string_buffer(4096)
        while self._run:
            ev = ctypes.c_void_p()
            rc = _lib.snd_seq_event_input(self._seq, ctypes.byref(ev))
            if rc < 0 or not ev.value:
                if not self._run:
                    return
                try:
                    self._poll.poll(50)
                except (OSError, ValueError):
                    time.sleep(0.005)
                continue
            t = time.monotonic()
            etype = ctypes.c_ubyte.from_address(ev.value).value
            if 60 <= etype < 100:  # system/announce events
                continue
            n = _lib.snd_midi_event_decode(self._dev, buf, 4096, ev)
            if n > 0:
                with self._lock:
                    self.events.append((t, bytes(buf.raw[:n])))
            _lib.snd_midi_event_reset_decode(self._dev)

    @property
    def key(self):
        return self.name

    def snapshot(self):
        with self._lock:
            return list(self.events)

    def clear(self):
        with self._lock:
            self.events.clear()

    def wait_for(self, pred, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            ev = self.snapshot()
            r = pred(ev)
            if r:
                return r
            time.sleep(0.02)
        return None

    def close(self):
        self._run = False
        _lib.snd_seq_close(self._seq)  # unblocks the reader (it ends on error)
