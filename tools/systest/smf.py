"""Tiny SMF writer for virtual-tier songs: display SysEx anchors + notes (format 0, 480 tpq, 120 bpm)."""
import struct


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append((n & 0x7F) | 0x80)
        n >>= 7
    return bytes(reversed(out))


def display_sysex(song, verse=None, letter=None):
    """F0 4D 43 01 00 [S3..S0] [V1 V0] [LETTER] [LED] F7; verse None = blank (prelude)."""
    s = [int(c) for c in f"{song:4d}".replace(" ", "\x0f")] if False else [(song // 10 ** i) % 10 for i in (3, 2, 1, 0)]
    v = [0x0F, 0x0F] if verse is None else [verse // 10, verse % 10]
    let = 0x0F if letter is None else 0x0A + "ABCD".index(letter)
    body = bytes([0x4D, 0x43, 0x01, 0x00] + s + v + [let, 0x0F])
    return b"\xF0" + vlq(len(body) + 1) + body + b"\xF7"


def build_song(code=401, verses=2, beats_per_section=1, notes=(60, 62, 64, 65, 67), tpq=480):
    """Prelude + `verses` verses, one note per beat in each section. Returns bytes of the .mid."""
    ev = []   # (abs tick, bytes)
    t = 0
    ev.append((0, b"\xFF\x51\x03" + (500000).to_bytes(3, "big")))
    for sec in range(verses + 1):
        ev.append((t, display_sysex(code, None if sec == 0 else sec)))
        n = notes[sec % len(notes)]
        ev.append((t, bytes([0x90, n, 100])))
        t += beats_per_section * tpq
        ev.append((t, bytes([0x80, n, 0])))
    ev.append((t, b"\xFF\x2F\x00"))
    out = bytearray()
    last = 0
    for at, data in ev:
        out += vlq(at - last) + data
        last = at
    trk = bytes(out)
    return b"MThd" + struct.pack(">IHHH", 6, 0, 1, tpq) + b"MTrk" + struct.pack(">I", len(trk)) + trk
