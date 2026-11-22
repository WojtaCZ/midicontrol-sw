"""Virtual tier: WHEN notes come out of the real `midicontrol` (not just which).

A click-track song (16th notes, 4-note chords, tempo changes, three sections) is played through the real binary;
every captured note-on time (alsaseq.SeqClient, time.monotonic() at poll() wake-up) is compared with its SMF time
relative to the first note. Section 1 is repeated once (REPLAY_ANCHOR) to measure the seam.

Numbers are printed (run with `-s`) and, when SYSTEST_TIMING_OUT=<path> is set, appended there as JSON lines.
The idle run has hard limits; the CPU-loaded run is a measurement and only fails at max error > 10 ms."""
import json
import multiprocessing
import os
import struct
import time
from fractions import Fraction

import pytest

import smf
from fakebase import MT
from helpers import labels, note_ons, wait_connected, wait_until

pytestmark = pytest.mark.virtual

TPQ = 480
CHORD = (48, 52, 55, 60)
SECTION_LEN_TICKS = 1920

# limits (ms) for the idle run
P99_MS, MAX_MS, DRIFT_MS, CHORD_MS, SEAM_MS = 1.0, 3.0, 0.1, 1.0, 2.0
LOAD_MAX_MS = 10.0


def _tempo(us):
    return b"\xFF\x51\x03" + us.to_bytes(3, "big")


def build_click_song():
    """Returns (mid bytes, tempo map [(tick, us/quarter)], note-on list [(tick, note, section index)], section start ticks)."""
    ev = []          # (tick, order, bytes)
    notes = []       # (tick, note)

    def add(tick, data, order=1):
        ev.append((tick, order, data))

    def note(tick, key, length=60):
        add(tick, bytes([0x90, key, 100]))
        add(tick + length, bytes([0x80, key, 0]), 2)
        notes.append((tick, key))

    tempos = [(0, 500000), (2880, 666667), (3840, 428571)]
    for tk, us in tempos:
        add(tk, _tempo(us), 0)
    sec_starts = [0, 1920, 3840]
    add(0, smf.display_sysex(401, None), 0)
    add(1920, smf.display_sysex(401, 1), 0)
    add(3840, smf.display_sysex(401, 2), 0)
    for i in range(16):                          # prelude: 16th notes at 120 bpm
        note(i * 120, 60 + i % 12)
    for tk in (1920, 2400):                      # verse 1: chords, then 16ths at 90 bpm
        for k in CHORD:
            note(tk, k, 200)
    for i in range(8):
        note(2880 + i * 120, 64 + i)
    for k in CHORD:                              # verse 2 at 140 bpm: chord, 16ths, chord
        note(3840, k, 200)
    for i in range(8):
        note(4320 + i * 120, 55 + i)
    for k in CHORD:
        note(5280, k, 200)
    end = 5760
    ev.append((end, 3, b"\xFF\x2F\x00"))
    ev.sort(key=lambda e: (e[0], e[1]))
    out, last = bytearray(), 0
    for tk, _, data in ev:
        out += smf.vlq(tk - last) + data
        last = tk
    trk = bytes(out)
    mid = b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPQ) + b"MTrk" + struct.pack(">I", len(trk)) + trk
    notes.sort(key=lambda n: n[0])               # stable: keeps the order the file has within a tick
    return mid, tempos, notes, sec_starts


def smf_seconds(tick, tempos):
    """Exact (Fraction) seconds of a tick under the tempo map."""
    sec, prev_tick, us = Fraction(0), 0, tempos[0][1]
    for tk, t_us in tempos:
        if tk >= tick:
            break
        sec += Fraction((tk - prev_tick) * us, TPQ * 10 ** 6)
        prev_tick, us = tk, t_us
    return float(sec + Fraction((tick - prev_tick) * us, TPQ * 10 ** 6))


def pct(vals, p):
    s = sorted(vals)
    if not s:
        return 0.0
    return s[min(len(s) - 1, int(round(p / 100.0 * (len(s) - 1))))]


def slope_ms_per_s(xs, ys):
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs)
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den if den else 0.0


def _burn(stop):
    x = 0
    while not stop.is_set():
        x = (x * 1103515245 + 12345) & 0xFFFFFFFF


def play_and_measure(make_rig, gap_ms=0, replay=True):
    mid, tempos, notes, sec_starts = build_click_song()
    rig = make_rig(songs=[(mid, "401.mid")], extra_conf=f"[PLAYBACK]\nVerseGapMs = {gap_ms}\n" if False else "")
    assert wait_connected(rig), rig.pc.log()
    rig.base.play("401")
    if replay:
        assert wait_until(lambda: "Sloka 1" in labels(rig.base), 8), rig.pc.log()
        rig.base.remote_cmd(MT.REPLAY_ANCHOR)
    assert wait_until(lambda: rig.base.received(MT.SIGNAL_STOPPED), 30), rig.pc.log()
    got = note_ons(rig.seq.snapshot())

    # expected play order: prelude, verse 1, [verse 1 again], verse 2 (+ file times of everything after the repeat shifted)
    s1, s2 = sec_starts[1], sec_starts[2]
    sec_dur = smf_seconds(s2, tempos) - smf_seconds(s1, tempos) + gap_ms / 1000.0
    exp = []   # (seconds, note, chord key)
    for tk, key in notes:
        if tk < s2:
            exp.append((smf_seconds(tk, tempos), key, tk))
            continue
    first_pass_v1 = [(smf_seconds(tk, tempos), key, tk) for tk, key in notes if s1 <= tk < s2]
    rest = [(smf_seconds(tk, tempos), key, tk) for tk, key in notes if tk >= s2]
    shift = sec_dur if replay else 0.0
    seq = [e for e in exp]
    seam_idx = None
    if replay:
        seam_idx = len(seq)
        seq += [(s + sec_dur, k, tk) for s, k, tk in first_pass_v1]
    seq += [(s + shift, k, tk) for s, k, tk in rest]
    assert [n for _, n in got] == [k for _, k, _ in seq], f"note sequence differs: {[n for _, n in got]}"

    t0, e0 = got[0][0], seq[0][0]
    xs = [e[0] - e0 for e in seq]
    errs = [((t - t0) - x) * 1000.0 for (t, _), x in zip(got, xs)]   # ms, + = late
    # chord spread: notes sharing a file tick and play pass
    spreads, i = [], 0
    while i < len(seq):
        j = i
        while j + 1 < len(seq) and seq[j + 1][2] == seq[i][2] and abs(seq[j + 1][0] - seq[i][0]) < 1e-9:
            j += 1
        if j > i:
            ts = [got[m][0] for m in range(i, j + 1)]
            spreads.append((max(ts) - min(ts)) * 1000.0)
        i = j + 1
    res = {
        "notes": len(got),
        "err_ms_p50": pct([abs(e) for e in errs], 50), "err_ms_p99": pct([abs(e) for e in errs], 99),
        "err_ms_max": max(abs(e) for e in errs), "err_ms_mean_signed": sum(errs) / len(errs),
        "drift_ms_over_song": slope_ms_per_s(xs, errs) * xs[-1],
        "drift_ms_per_s": slope_ms_per_s(xs, errs),
        "chord_spread_ms_max": max(spreads), "chords": len(spreads),
    }
    if replay:
        k = seam_idx
        res["drift_ms_over_song_before_seam"] = slope_ms_per_s(xs[:k], errs[:k]) * xs[k - 1]
        res["err_ms_step_at_seam"] = sum(errs[k:k + 4]) / 4 - sum(errs[k - 4:k]) / 4
        # seam = (first note of the repeated section - first note of its first pass) - (section duration)
        first1 = next(m for m, (_, _, tk) in enumerate(seq[:seam_idx]) if tk == s1)
        seam = ((got[seam_idx][0] - got[first1][0]) - sec_dur) * 1000.0
        res["seam_ms"] = seam
    return res


def report(tag, res):
    line = json.dumps({"run": tag, **{k: round(v, 4) if isinstance(v, float) else v for k, v in res.items()}})
    print("\nTIMING", line)
    out = os.environ.get("SYSTEST_TIMING_OUT")
    if out:
        with open(out, "a") as f:
            f.write(line + "\n")


@pytest.mark.req("SYS-NOTE-01")
def test_click_track_timing_idle(make_rig):
    res = play_and_measure(make_rig)
    report("idle", res)
    bad = []
    if res["err_ms_p99"] > P99_MS: bad.append(f"p99 {res['err_ms_p99']:.3f} ms > {P99_MS}")
    if res["err_ms_max"] > MAX_MS: bad.append(f"max {res['err_ms_max']:.3f} ms > {MAX_MS}")
    if abs(res["drift_ms_over_song"]) > DRIFT_MS: bad.append(f"drift {res['drift_ms_over_song']:.3f} ms > {DRIFT_MS}")
    if res["chord_spread_ms_max"] > CHORD_MS: bad.append(f"chord spread {res['chord_spread_ms_max']:.3f} ms > {CHORD_MS}")
    if abs(res["seam_ms"]) > SEAM_MS: bad.append(f"seam {res['seam_ms']:.3f} ms > {SEAM_MS}")
    assert not bad, f"timing limits failed: {bad}; all: {res}"


@pytest.mark.req("SYS-NOTE-01")
def test_click_track_timing_under_cpu_load(make_rig):
    """Measurement run: two busy processes per core. Only a gross failure (max error > 10 ms) fails."""
    stop = multiprocessing.Event()
    procs = [multiprocessing.Process(target=_burn, args=(stop,), daemon=True) for _ in range(2 * (os.cpu_count() or 4))]
    for p in procs:
        p.start()
    try:
        time.sleep(0.5)
        res = play_and_measure(make_rig)
    finally:
        stop.set()
        for p in procs:
            p.join(2)
            if p.is_alive():
                p.terminate()
    report("cpu-load", res)
    assert res["err_ms_max"] <= LOAD_MAX_MS, f"under load max error {res['err_ms_max']:.3f} ms > {LOAD_MAX_MS}; all: {res}"
