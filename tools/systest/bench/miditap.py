"""ALSA sequencer relay between the PC software and the base's USB-MIDI port: the bench-tier MIDI tap.

Two ports: "pc" (subscribable; the PC writes to it, arecordmidi reads from it) and "base" (private, subscribed both ways to
the base's port). Direct unsubscribed delivery to a kernel rawmidi client fails with ENODEV (seq_midi opens the USB output
only while a subscription exists), so the base side must be a real subscription; routing by receiving port also keeps
base input from echoing back to the base. Each forward is sent to the subscribers of the other port.

Why a relay: Player opens RtMidiOut at play() and closes it at stop(), so subscribing a snooper to the PC's port races
every play; there is no usbmon in the container. Instead the PC runs with MIDICONTROL_MIDI_KEY=<relay name>, so it sends
to (and arecordmidi records from) this client, which forwards
  PC -> relay  => base port (direct, unqueued), stamped dir "out"
  base -> relay => all subscribers of the relay port (arecordmidi), stamped dir "in"
Each event is time-stamped with time.monotonic() when it is read (poll() wake-up, no sleep loop). The relay adds one
userspace hop (sub-millisecond; measured by `selftest`), stated as the tap error bound in the report. It ends where the
base's USB device receives the bytes: there is no MIDI-out counter in the base (midi::send busy-waits on USART3), so
"first byte on the DIN wire" is not observable here.

Active-note tracking (`notes_on`) lets a scenario check that stop leaves no hanging note (CC 123/120 clear a channel).
"""
import ctypes
import os
import select
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from alsaseq import _lib  # shared libasound handle (tools/systest/alsaseq.py)

SND_SEQ_OPEN_DUPLEX = 3
CAPS = 0x1 | 0x2 | 0x20 | 0x40  # READ WRITE SUBS_READ SUBS_WRITE (PC-facing port: RtMidi and arecordmidi subscribe)
CAPS_PRIVATE = 0x1 | 0x2  # READ WRITE, not subscribable by others: RtMidi does not list it, so MIDICONTROL_MIDI_KEY
#                          can only pick the PC-facing port
TYPE_MIDI_GENERIC, TYPE_APPLICATION = 1 << 1, 1 << 20
QUEUE_DIRECT, ADDR_SUBSCRIBERS, ADDR_UNKNOWN = 253, 254, 253
POLLIN = 1

# snd_seq_event_t: type u8, flags u8, tag u8, queue u8, time 8 B, source {client u8, port u8}, dest {client, port}, data
OFF_QUEUE, OFF_SRC, OFF_DST = 3, 12, 14

_lib.snd_seq_event_input.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
_lib.snd_seq_event_output_direct.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
_lib.snd_seq_poll_descriptors_count.argtypes = [ctypes.c_void_p, ctypes.c_short]
_lib.snd_seq_poll_descriptors.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_uint, ctypes.c_short]
_lib.snd_seq_connect_from.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.snd_seq_connect_to.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]
_lib.snd_midi_event_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_void_p]
_lib.snd_midi_event_decode.restype = ctypes.c_long


class _PollFd(ctypes.Structure):
    _fields_ = [("fd", ctypes.c_int), ("events", ctypes.c_short), ("revents", ctypes.c_short)]


def find_client(key, out=None):
    """ALSA client id whose name contains key (e.g. 'MIDIControl'), from `aconnect -l` (no /proc/asound/seq here)."""
    import re, subprocess
    if out is None:
        out = subprocess.run(["aconnect", "-l"], capture_output=True, text=True).stdout
    for m in re.finditer(r"^client (\d+): '([^']*)'", out, re.M):
        if key in m.group(2):
            return int(m.group(1))
    return None


class MidiTap:
    def __init__(self, base_client, base_port=0, name=None, on_event=None):
        self.name = name or f"systest-tap-{os.getpid()}"
        self.base = (base_client, base_port)
        self.on_event = on_event  # callback(rec) on the relay thread; keep it short
        self._seq = ctypes.c_void_p()
        if _lib.snd_seq_open(ctypes.byref(self._seq), b"default", SND_SEQ_OPEN_DUPLEX, 0) < 0:
            raise RuntimeError("cannot open ALSA sequencer")
        _lib.snd_seq_set_client_name(self._seq, self.name.encode())
        self.port = _lib.snd_seq_create_simple_port(self._seq, b"pc", CAPS, TYPE_MIDI_GENERIC | TYPE_APPLICATION)
        self.bport = _lib.snd_seq_create_simple_port(self._seq, b"base", CAPS_PRIVATE, TYPE_MIDI_GENERIC | TYPE_APPLICATION)
        if self.port < 0 or self.bport < 0:
            raise RuntimeError("cannot create ALSA ports")
        self.client = _lib.snd_seq_client_id(self._seq)
        if _lib.snd_seq_connect_from(self._seq, self.bport, base_client, base_port) < 0:
            raise RuntimeError(f"cannot subscribe from base {base_client}:{base_port}")
        if _lib.snd_seq_connect_to(self._seq, self.bport, base_client, base_port) < 0:
            raise RuntimeError(f"cannot subscribe to base {base_client}:{base_port}")
        _lib.snd_seq_nonblock(self._seq, 1)
        n = _lib.snd_seq_poll_descriptors_count(self._seq, POLLIN)
        pfds = (_PollFd * n)()
        _lib.snd_seq_poll_descriptors(self._seq, pfds, n, POLLIN)
        self._poll = select.poll()
        for p in pfds:
            self._poll.register(p.fd, select.POLLIN)
        self._dev = ctypes.c_void_p()
        _lib.snd_midi_event_new(4096, ctypes.byref(self._dev))
        _lib.snd_midi_event_no_status(self._dev, 1)
        self.events = []  # dicts: mono, dir ("out" PC->base, "in" base->PC), bytes (hex), fwd_us
        self.counts = {"out": 0, "in": 0, "fwd_err": 0}
        self.notes_on = set()  # (channel, note) sent to the base and not released
        self._lock = threading.Lock()
        self._run = True
        self._thr = threading.Thread(target=self._loop, name="miditap", daemon=True)
        self._thr.start()

    @property
    def key(self):
        return self.name

    def subscribed(self):
        """(base client id now, our base port is connected both ways) from `aconnect -l`."""
        import re, subprocess
        out = subprocess.run(["aconnect", "-l"], capture_output=True, text=True).stdout
        cid = find_client("MIDIControl", out)
        if cid is None:
            return None, False
        blk = re.search(rf"^client {cid}:.*?(?=^client |\Z)", out, re.M | re.S).group(0)
        me = f"{self.client}:{self.bport}"
        to = re.search(r"Connecting To: ([^\n]*)", blk)
        frm = re.search(r"Connected From: ([^\n]*)", blk)
        ok = bool(to and me in [x.strip().split("[")[0] for x in to.group(1).split(",")]
                  and frm and me in [x.strip().split("[")[0] for x in frm.group(1).split(",")])
        return cid, ok

    def ensure(self):
        """Re-subscribe after the base re-enumerated (reset, firmware update): the kernel drops our subscriptions and
        sends to zero subscribers succeed, so fwd_err cannot see it. Returns None (fine), "gone" or "resubscribed"."""
        cid, ok = self.subscribed()
        if cid is None:
            return "gone"
        if ok and cid == self.base[0]:
            return None
        _lib.snd_seq_disconnect_from.argtypes = _lib.snd_seq_disconnect_to.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                                                                         ctypes.c_int, ctypes.c_int]
        _lib.snd_seq_disconnect_from(self._seq, self.bport, *self.base)
        _lib.snd_seq_disconnect_to(self._seq, self.bport, *self.base)
        self.base = (cid, self.base[1])
        a = _lib.snd_seq_connect_from(self._seq, self.bport, *self.base)
        b = _lib.snd_seq_connect_to(self._seq, self.bport, *self.base)
        self.counts["resub"] = self.counts.get("resub", 0) + 1
        return "resubscribed" if a >= 0 and b >= 0 else f"resubscribe failed ({a},{b})"

    def _track(self, b):
        if not b:
            return
        st, ch = b[0] & 0xF0, b[0] & 0x0F
        if st == 0x90 and len(b) >= 3:
            (self.notes_on.add if b[2] else self.notes_on.discard)((ch, b[1]))
        elif st == 0x80 and len(b) >= 2:
            self.notes_on.discard((ch, b[1]))
        elif st == 0xB0 and len(b) >= 2 and b[1] in (120, 123):
            self.notes_on = {n for n in self.notes_on if n[0] != ch}

    def _loop(self):
        buf = ctypes.create_string_buffer(4096)
        while self._run:
            if not self._poll.poll(100):
                continue
            while self._run:
                ev = ctypes.c_void_p()
                rc = _lib.snd_seq_event_input(self._seq, ctypes.byref(ev))
                if rc < 0 or not ev.value:
                    break
                t = time.monotonic()
                raw = (ctypes.c_ubyte * 28).from_address(ev.value)
                if 60 <= raw[0] < 100:  # system / announce events
                    continue
                from_base = raw[OFF_DST + 1] == self.bport
                raw[OFF_QUEUE] = QUEUE_DIRECT
                raw[OFF_SRC + 1] = self.port if from_base else self.bport
                raw[OFF_DST], raw[OFF_DST + 1] = ADDR_SUBSCRIBERS, ADDR_UNKNOWN
                ok = _lib.snd_seq_event_output_direct(self._seq, ev) >= 0
                t1 = time.monotonic()
                n = _lib.snd_midi_event_decode(self._dev, buf, 4096, ev)
                _lib.snd_midi_event_reset_decode(self._dev)
                b = bytes(buf.raw[:n]) if n > 0 else b""
                d = "in" if from_base else "out"
                rec = {"mono": round(t, 6), "dir": d, "bytes": b.hex(), "fwd_us": round((t1 - t) * 1e6)}
                with self._lock:
                    self.counts[d] += 1
                    if not ok:
                        self.counts["fwd_err"] += 1
                    if d == "out":
                        self._track(b)
                    self.events.append(rec)
                if self.on_event:
                    self.on_event(rec)

    def snapshot(self, since=0.0):
        with self._lock:
            return [e for e in self.events if e["mono"] >= since]

    def first_after(self, since, direction="out"):
        with self._lock:
            for e in self.events:
                if e["mono"] >= since and e["dir"] == direction:
                    return e
        return None

    def trim(self, keep_s=600):
        """Drop events older than keep_s (burn-in memory bound); counts stay cumulative."""
        lim = time.monotonic() - keep_s
        with self._lock:
            self.events = [e for e in self.events if e["mono"] >= lim]

    def close(self):
        self._run = False
        self._thr.join(timeout=1)
        _lib.snd_seq_close(self._seq)


def selftest(n=200):
    """Relay hop cost without hardware: a SeqClient stands in for the base port, another for the PC sender."""
    from alsaseq import SeqClient
    import subprocess
    fake_base = SeqClient(f"systest-fbase-{os.getpid()}")
    tap = MidiTap(fake_base.client, fake_base.port)
    sender = SeqClient(f"systest-fpc-{os.getpid()}")
    subprocess.run(["aconnect", f"{sender.client}:{sender.port}", f"{tap.client}:{tap.port}"], check=True)
    t_send = []
    for i in range(n):
        t_send.append(time.monotonic())
        sender_send(sender, bytes([0x90, 60 + i % 12, 64]))
        time.sleep(0.005)
    time.sleep(0.2)
    got = fake_base.snapshot()
    lat = sorted((g[0] - s) * 1000 for s, g in zip(t_send, got))
    hop = sorted(e["fwd_us"] for e in tap.snapshot())
    tap.close(); sender.close(); fake_base.close()
    return {"sent": n, "received": len(got), "end_to_end_ms_median": round(lat[len(lat) // 2], 3) if lat else None,
            "end_to_end_ms_max": round(lat[-1], 3) if lat else None, "fwd_us_median": hop[len(hop) // 2] if hop else None,
            "fwd_us_max": hop[-1] if hop else None}


def sender_send(client, data):
    """Send raw MIDI bytes from a SeqClient to its subscribers (selftest helper)."""
    ev = (ctypes.c_ubyte * 28)()
    enc = ctypes.c_void_p()
    _lib.snd_midi_event_new(64, ctypes.byref(enc))
    _lib.snd_midi_event_encode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_void_p]
    _lib.snd_midi_event_encode(enc, data, len(data), ev)
    _lib.snd_midi_event_free(enc)
    ev[OFF_QUEUE] = QUEUE_DIRECT
    ev[OFF_SRC + 1] = client.port
    ev[OFF_DST], ev[OFF_DST + 1] = ADDR_SUBSCRIBERS, ADDR_UNKNOWN
    _lib.snd_seq_event_output_direct(client._seq, ev)


if __name__ == "__main__":
    import json
    print(json.dumps(selftest()))
