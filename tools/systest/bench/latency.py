#!/usr/bin/env python3
"""Bench latency trials on the real rig (base + remote over SWD, PC app under the rig, MIDI tap).

usage: latency.py <rundir> <pcbin> <config> [--xdg-state DIR] [--play N] [--resets N] [--pcrestarts N] [--seed S]

Parts (each trial's raw numbers go to latency.jsonl, the summary to latency.md):
  play       remote keys 4,0,1,OK on playerScreen -> PC PLAY_SONG -> warm-up -> Playing -> first MIDI byte / first Note On
             -> base screen change -> remote frame change; then OK -> STOP_SONG -> Playback stopped -> base back to
             mainScreen; notes still sounding after stop (must be 0).
  reset      remote SYSRESETREQ over SWD (AIRCR, no halt) -> base `connected` 1->0->1 (SWD) -> PC "Remote state resynced".
  pcrestart  SIGTERM the PC app, start it again -> serial open -> resync ACKed -> /api/status answers.

Error bounds:
  - A key's time is the mailbox write (t_w). The firmware consumed it within (t_w, t_w + ack_ms], so key->X is reported
    as measured with the bound -ack_ms.
  - PC lines are timestamped when the rig reads them from the pipe. The app flushes every line (src/log.cpp), so the
    added delay is the pipe/thread wake-up only (sub-ms).
  - SWD screen/frame changes in fast mode are bracketed by the previous sample: (lo, mono]. bound_ms is reported.
  - MIDI tap times are the tap's receive time, +25 us median forward hop (miditap selftest).
"""
import argparse, json, os, statistics, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig as rigmod  # noqa: E402
import swd  # noqa: E402

AIRCR, SYSRESETREQ = 0xE000ED0C, 0x05FA0004


def first(r, pred, since, timeout):
    return r.wait_for(pred, timeout, since)


def ms(a, b):
    return None if a is None or b is None else round((b - a) * 1000, 1)


def pc(text):
    return lambda e: e["kind"] == "pc" and text in e["line"]


def screen_to(unit, name):
    return lambda e: (e["kind"] == "swd" and e.get("unit") == unit and "changed" in e
                      and e["changed"].get("screen", [None, None])[1] == name)


def frame_of(unit):
    return lambda e: e["kind"] == "frame" and e["unit"] == unit


def play_trial(r, i, song=("4", "0", "1"), play_s=2.0):
    if r.screen("remote") != "playerScreen" and not r.to_player():
        return {"i": i, "err": "nav failed", "screen": r.screen("remote")}
    time.sleep(0.5)
    r.fast("remote", 20); r.fast("base", 20)
    time.sleep(0.3)
    r.keys(*song)
    time.sleep(r.rng.uniform(0.25, 0.6))
    t_ok = time.monotonic()
    if not r.key("OK"):
        return {"i": i, "err": "OK key not acked"}
    k = r.events(t_ok, "key")[-1]
    t_w = k["mono"]
    rec = {"i": i, "ok_ack_ms": k.get("ack_ms")}
    e_play = first(r, pc("PLAY_SONG"), t_w, 5)
    e_playing = first(r, pc("Playing "), t_w, 10)
    e_byte = first(r, lambda e: e["kind"] == "midi" and e["dir"] == "out" and e.get("onset"), t_w, 10)
    e_note = first(r, lambda e: e["kind"] == "midi" and e["dir"] == "out" and e.get("first_note"), t_w, 12)
    rec["key_to_play_song_ms"] = ms(t_w, e_play and e_play["mono"])
    rec["warmup_ms"] = ms(e_play and e_play["mono"], e_playing and e_playing["mono"])
    rec["key_to_first_byte_ms"] = ms(t_w, e_byte and e_byte["mono"])
    rec["first_byte"] = e_byte and e_byte["bytes"][:16]
    rec["playing_to_first_byte_ms"] = ms(e_playing and e_playing["mono"], e_byte and e_byte["mono"])
    rec["playing_to_first_note_ms"] = ms(e_playing and e_playing["mono"], e_note and e_note["mono"])
    if e_play:
        # frames that change while warming up (remote shows the song as soon as PLAY_SONG is ACKed?)
        f_r0 = first(r, lambda e: frame_of("remote")(e) and (not e_playing or e["mono"] < e_playing["mono"]),
                     e_play["mono"], 0.01)
        rec["remote_frame_during_warmup"] = bool(f_r0)
    if e_playing:
        s_b = first(r, screen_to("base", "nowPlayingScreen"), e_playing["mono"] - 0.5, 3)
        f_r = first(r, frame_of("remote"), e_playing["mono"], 3)
        rec["playing_to_base_screen_ms"] = ms(e_playing["mono"], s_b and s_b["mono"])
        rec["base_screen_bound_ms"] = s_b and round((s_b["mono"] - s_b["lo"]) * 1000, 1) if s_b and s_b.get("lo") else None
        rec["playing_to_remote_frame_ms"] = ms(e_playing["mono"], f_r and f_r["mono"])
        rec["remote_frame_bound_ms"] = f_r and f_r["bound_ms"]
    time.sleep(play_s)
    t_s = time.monotonic()
    r.fast("remote", 8); r.fast("base", 8)
    if not r.key("OK"):
        rec["err"] = "stop OK not acked"
        r.pc_cmd("stop")
        return rec
    ks = r.events(t_s, "key")[-1]
    e_stop = first(r, pc("STOP_SONG"), ks["mono"], 5)
    e_stopped = first(r, pc("Playback stopped"), ks["mono"], 6)
    s_main = first(r, screen_to("base", "mainScreen"), ks["mono"], 6)
    rec["stop_ack_ms"] = ks.get("ack_ms")
    rec["key_to_stop_song_ms"] = ms(ks["mono"], e_stop and e_stop["mono"])
    rec["key_to_stopped_ms"] = ms(ks["mono"], e_stopped and e_stopped["mono"])
    rec["stopped_to_base_main_ms"] = ms(e_stopped and e_stopped["mono"], s_main and s_main["mono"])
    time.sleep(0.8)
    rec["notes_on_after_stop"] = len(r.tap.notes_on)
    rec["fwd_err"] = r.tap.counts.get("fwd_err", 0)
    rec["remote_screen_after"] = r.screen("remote")
    return rec


def reset_trial(r, i):
    rem = r.units["remote"]
    r.fast("base", 25)
    time.sleep(0.3)
    t0 = time.monotonic()
    try:
        rem.write32(AIRCR, SYSRESETREQ)
    except Exception as e:  # the AP write may report an error as the core resets under it
        r.ev("note", text=f"reset write: {e!r}"[:200])
    r.ev("remote_reset", mono=t0, i=i)
    down = first(r, lambda e: e["kind"] == "swd" and e.get("unit") == "base" and "changed" in e
                 and e["changed"].get("connected", [None, None])[1] == 0, t0, 10)
    up = first(r, lambda e: e["kind"] == "swd" and e.get("unit") == "base" and "changed" in e
               and e["changed"].get("connected", [None, None])[1] == 1, (down or {"mono": t0})["mono"], 30)
    r_up = first(r, lambda e: e["kind"] == "swd" and e.get("unit") == "remote" and "changed" in e
                 and e["changed"].get("connected", [None, None])[1] == 1, t0 + 0.5, 30)
    r_main = first(r, screen_to("remote", "mainScreen"), t0, 30)
    # PC side: /api/status controller.ble_connected None -> True (1 Hz rig poll on top of the PC's 5 s GET_STATUS)
    pc_up = first(r, lambda e: e["kind"] == "api" and e.get("changed", {}).get("controller.ble_connected", [0, 0])[1] is True,
                  t0, 30)
    rs = r.events(t0, "pc")
    rec = {"i": i, "reset_to_base_down_ms": ms(t0, down and down["mono"]), "reset_to_base_up_ms": ms(t0, up and up["mono"]),
           "reset_to_remote_connected_ms": ms(t0, r_up and r_up["mono"]),
           "reset_to_remote_main_ms": ms(t0, r_main and r_main["mono"]),
           "reset_to_pc_api_controller_ms": ms(t0, pc_up and pc_up["mono"]),
           "pc_resync_seen": any("Remote state resynced" in e["line"] for e in rs),
           "base_bound_ms": up and up.get("lo") and round((up["mono"] - up["lo"]) * 1000, 1)}
    time.sleep(2)
    rec["nav_ok"] = r.to_player()
    return rec


def pcrestart_trial(r, i):
    t0 = time.monotonic()
    r.stop_pc()
    t1 = time.monotonic()
    r.start_pc()
    op = first(r, pc("Opened serial port"), t1, 15)
    rs = first(r, pc("Remote state resynced"), t1, 30)
    t_api = None
    end = time.monotonic() + 15
    while time.monotonic() < end:
        try:
            if r.api_get("/api/status", timeout=1).get("schema") == 2:
                t_api = time.monotonic()
                break
        except Exception:
            pass
        time.sleep(0.05)
    rec = {"i": i, "stop_ms": ms(t0, t1), "start_to_serial_ms": ms(t1, op and op["mono"]),
           "start_to_resync_ms": ms(t1, rs and rs["mono"]), "start_to_api_ms": ms(t1, t_api)}
    time.sleep(3)
    return rec


def stats(vals):
    v = sorted(x for x in vals if isinstance(x, (int, float)))
    if not v:
        return None
    p95 = v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))]
    return {"n": len(v), "min": v[0], "median": round(statistics.median(v), 1), "p95": p95, "max": v[-1]}


def summarize(trials, out):
    lines = []
    for part, recs in trials.items():
        if not recs:
            continue
        lines.append(f"\n## {part} ({len(recs)} trials, {sum(1 for x in recs if 'err' in x)} errors)\n")
        lines.append("| metric | n | min | median | p95 | max |\n|---|---|---|---|---|---|")
        keys = [k for k in recs[0] if k.endswith("_ms")] + sorted({k for x in recs for k in x if k.endswith("_ms")}
                                                                   - set(recs[0]))
        for k in keys:
            s = stats([x.get(k) for x in recs])
            if s:
                lines.append(f"| {k} | {s['n']} | {s['min']} | {s['median']} | {s['p95']} | {s['max']} |")
        for k in ("notes_on_after_stop", "fwd_err"):
            if k in recs[0]:
                lines.append(f"\n{k}: max {max(x.get(k, 0) for x in recs)}")
        errs = [x for x in recs if "err" in x]
        for x in errs:
            lines.append(f"- trial {x['i']}: {x['err']}")
    with open(out, "w") as f:
        f.write("# Latency trials\n\nAll times ms. Bounds: see latency.py docstring.\n" + "\n".join(lines) + "\n")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rundir"); ap.add_argument("pcbin"); ap.add_argument("config")
    ap.add_argument("--xdg-state"); ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--play", type=int, default=30); ap.add_argument("--resets", type=int, default=10)
    ap.add_argument("--pcrestarts", type=int, default=10)
    a = ap.parse_args()
    r = rigmod.Rig(a.rundir, a.pcbin, a.config, xdg_state=a.xdg_state, seed=a.seed)
    r.start()
    trials = {"play": [], "reset": [], "pcrestart": []}
    out = open(os.path.join(a.rundir, "latency.jsonl"), "a")

    def log(part, rec):
        rec["part"] = part
        trials[part].append(rec)
        out.write(json.dumps(rec) + "\n"); out.flush()
        print(part, json.dumps(rec), flush=True)
    try:
        if not r.pc_line("Remote state resynced", 40, 0):
            print("WARN: no resync seen at start", flush=True)
        r.to_player()
        for i in range(a.play):
            log("play", play_trial(r, i))
            time.sleep(r.rng.uniform(1.0, 2.0))
        for i in range(a.resets):
            log("reset", reset_trial(r, i))
        for i in range(a.pcrestarts):
            log("pcrestart", pcrestart_trial(r, i))
    finally:
        r.stop()
        out.close()
        print(summarize(trials, os.path.join(a.rundir, "latency.md")))


if __name__ == "__main__":
    main()
