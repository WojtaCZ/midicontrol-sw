#!/usr/bin/env python3
"""Step-3 user-like scenarios on the real rig (plan sec. 3.4), each run N times with seeded human pacing.

usage: scenarios.py <rundir> <pcbin> <src-run-with-play-folder> [--runs 3] [--only sunday,mistakes,...] [--seed S]

Every step is an action from actions.py with its expected observations; records go to actions.jsonl (+ scenario/run),
the summary to scenarios.md. Parts that cannot run on this bench are listed as BLOCKED with the reason (never silently
skipped).
"""
import argparse, json, os, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig as rigmod  # noqa: E402
import actions as A  # noqa: E402

BLOCKED = {
    "sunday": [
        "system power-up / remote power-on: the rig starts with both units powered (USB); covered by remote reset and "
        "PC restart actions instead",
        "prelude + two verses: the bench songs have ONE section each (no blank-verse prelude); add verse replays it",
        "organ idle-off after the last song: the PC switches the organ off at every song end/stop (KeepPowerOn=false, "
        "gap 3) and the 30 min idle timer (not a config key, gap 4) is armed only when the organ was switched on "
        "manually; checked instead: organ off after the song end",
    ],
    "recording": [
        "recording from the base: not implemented (gaps 15-17/30)",
        "notes into the base and file content: organ -> base DIN input dead on the bench (DIN fault, owner probe)",
        "play the recording back with anchors: the recorded file is empty without DIN input",
    ],
    "maintenance": [
        "firmware update of remote and base from the web page: done once in step 3.2 (c100 run, remote OTA via base, "
        "n=1); repeated at most once after the burn-in with the same image (flash wear, frozen firmware)",
        "base USB unplug/replug: needs a person at the bench (no switchable hub); owner item",
    ],
    "mistakes": [
        "remote 'out of range' held mid-verse: approximated by an SWD SYSRESETREQ of the remote (link drop ~3 s), "
        "not a held RF loss",
    ],
}


def sc_sunday(ctx):
    r = ctx.r
    return [
        ("play", A.play, ["406A"]), ("idle", A.idle, [r.rng.uniform(6, 10)]),
        ("add_verse", A.add_verse, []), ("idle", A.idle, [r.rng.uniform(8, 14)]),
        ("stop", A.stop, []), ("idle", A.idle, [r.rng.uniform(4, 8)]),
        ("play", A.play, ["406B"]), ("idle", A.idle, [r.rng.uniform(5, 9)]),
        ("stop", A.stop, []), ("idle", A.idle, [r.rng.uniform(3, 6)]),
        ("play", A.play, ["1999"], {"expect": "notfound"}), ("idle", A.idle, [r.rng.uniform(2, 4)]),
        ("play", A.play, ["407"]), ("wait_end", A.wait_end, []),
    ]


def sc_mistakes(ctx):
    r = ctx.r
    return [
        ("wrong_code_corrected", A.wrong_code_corrected, ["409", "401"]), ("idle", A.idle, [r.rng.uniform(4, 7)]),
        ("stop_twice", A.stop, [], {"twice": True}), ("idle", A.idle, [r.rng.uniform(3, 5)]),
        ("play", A.play, ["512"]), ("idle", A.idle, [r.rng.uniform(4, 7)]),
        ("play_while_playing", A.play, ["513"]), ("idle", A.idle, [r.rng.uniform(4, 6)]),
        ("add_verse_rapid", A.add_verse, [], {"presses": 3, "rapid": True}), ("idle", A.idle, [r.rng.uniform(2, 4)]),
        ("song_list_during_play", A.song_list_and_back, []), ("idle", A.idle, [r.rng.uniform(2, 4)]),
        ("remote_reset_mid_song", A.remote_reset, []), ("idle", A.idle, [r.rng.uniform(2, 4)]),
        ("stop", A.stop, []), ("idle", A.idle, [r.rng.uniform(2, 4)]),
        ("refused_2000", A.play, ["2000"], {"expect": "refused"}),
    ]


def sc_recording(ctx):
    return [("record_smoke", A.record_smoke, [6.0]), ("idle", A.idle, [2.0]),
            ("play_after_record", A.play, ["401"]), ("idle", A.idle, [5.0]), ("stop", A.stop, [])]


def sc_maintenance(ctx):
    r = ctx.r
    return [
        ("play", A.play, ["516"]), ("idle", A.idle, [r.rng.uniform(3, 5)]),
        ("web_visit_during_play", A.web_visit, []), ("stop", A.stop, []), ("idle", A.idle, [2.0]),
        ("stops_edit", A.stops_edit, ["407"]), ("idle", A.idle, [2.0]),
        ("pc_restart_idle", A.pc_restart, []), ("idle", A.idle, [3.0]),
        ("play", A.play, ["517"]), ("idle", A.idle, [r.rng.uniform(4, 6)]),
        ("pc_restart_during_play", A.pc_restart, []), ("idle", A.idle, [3.0]),
        ("play_after_restart", A.play, ["403"]), ("idle", A.idle, [4.0]), ("stop", A.stop, []),
    ]


SCENARIOS = {"sunday": sc_sunday, "mistakes": sc_mistakes, "recording": sc_recording, "maintenance": sc_maintenance}


def summarize(recs, out):
    lines = ["# Step-3 scenarios\n", "Each step = an action from actions.py with expected observations written down "
             "before the run. ok = all its checks passed.\n"]
    for name in SCENARIOS:
        rr = [x for x in recs if x.get("scenario") == name]
        if not rr:
            continue
        runs = sorted({x["run"] for x in rr})
        lines.append(f"\n## {name} ({len(runs)} runs)\n")
        lines.append("| run | steps | ok | failed steps (checks) |\n|---|---|---|---|")
        for k in runs:
            st = [x for x in rr if x["run"] == k and x["action"] != "idle"]
            bad = [f"{x['step']}({','.join(c for c, v in x['checks'].items() if not v)})" for x in st if not x["ok"]]
            lines.append(f"| {k} | {len(st)} | {sum(x['ok'] for x in st)} | {'; '.join(bad) or '-'} |")
        lines.append("\nBLOCKED (not run, reason):")
        lines += [f"- {b}" for b in BLOCKED.get(name, [])] or ["- none"]
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rundir"); ap.add_argument("pcbin"); ap.add_argument("src")
    ap.add_argument("--runs", type=int, default=3); ap.add_argument("--only", default=",".join(SCENARIOS))
    ap.add_argument("--seed", type=int, default=11)
    a = ap.parse_args()
    os.makedirs(a.rundir, exist_ok=True)
    cfg = A.prepare(a.rundir, a.src)
    r = rigmod.Rig(a.rundir, a.pcbin, cfg, xdg_state=os.path.join(os.path.abspath(a.rundir), "state"), seed=a.seed)
    r.start()
    ctx = A.Ctx(r)
    recs = []
    try:
        if not r.pc_line("Remote state resynced", 40, 0):
            print("WARN: no resync seen at start", flush=True)
        for k in range(a.runs):
            for name in a.only.split(","):
                A.cleanup(ctx)
                r.ev("scenario", name=name, run=k)
                print(f"== {name} run {k}", flush=True)
                for step in SCENARIOS[name](ctx):
                    label, fn, args = step[:3]
                    kw = step[3] if len(step) > 3 else {}
                    ctx.out = "actions.jsonl"
                    rec = A.run(ctx, label if fn is not A.idle else "idle", fn, *args, **kw)
                    rec.update(scenario=name, run=k, step=label)
                    recs.append(rec)
                    if fn is not A.idle:
                        print(f"  {label}: {'ok' if rec['ok'] else 'FAIL ' + json.dumps(rec['checks'])}"
                              f" {json.dumps(rec['info'])[:200]}", flush=True)
                time.sleep(r.rng.uniform(3, 6))
    finally:
        A.cleanup(ctx)
        r.stop()
        with open(os.path.join(a.rundir, "scenarios.jsonl"), "w") as f:
            for x in recs:
                f.write(json.dumps(x) + "\n")
        print(summarize(recs, os.path.join(a.rundir, "scenarios.md")))


if __name__ == "__main__":
    main()
