#!/usr/bin/env python3
"""Step-3 burn-in (plan sec. 3.5): a seeded weighted random mix of the scenario actions (actions.py) for a fixed time.

usage: burnin.py <rundir> <pcbin> <src-run-with-play-folder> --hours H [--seed S] [--pilot]
  stop early: touch <rundir>/STOP (graceful) or SIGTERM the pid in <rundir>/burnin.pid; the final summary is written
  either way. summary.json is rewritten atomically every 30 min and at the end; `criteria.py <rundir>` evaluates BURN-*.
  --pilot runs one of every episode kind first (resets and PC restarts idle and playing, record, add verse to the end,
  switch, stop with extras, maintenance), then the random mix.

Pass criteria are fixed BEFORE the run in criteria.toml (BURN-1..16; summary.criteria_sha256). Counting rules:
  - disturbance windows: remote_reset 45 s, pc_restart 40 s from the action start (actions.WINDOW_S). A stale-link kick,
    module restart, remote reset or PC exit inside a window is expected; outside every window it counts
    (kicks_outside_windows, restarts, unexpected_resets, pc_died). A base reset counts anywhere. Resets are seen as SysTick
    going backwards between SWD samples. Positive controls: expected_resets vs remote_reset actions, windows vs actions.
  - zombie (advisor #4): both units report connected and neither base lastRemoteRx nor remote ka_last moved for > 15 s,
    judged only while both SWD samplers advance (ticks change); a sampler frozen > 5 s counts as swd_stale instead.
  - play_failures: failed play / stop / wait_end actions not overlapping a window; overlapping ones are
    play_failures_in_window; every other failed action is other_failures.
  - leak: the longest PC lifetime (pc_restart episodes only in the first hour of runs >= 4 h): RSS least-squares slope from
    10 min after start; fd growth = min fds over its last 10 min minus min fds over minutes 10-20 (idle floors).
"""
import argparse, hashlib, json, os, random, signal, subprocess, sys, threading, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rig as rigmod  # noqa: E402
import actions as A  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SONGS = list(A.SONGS)
EPISODES = {  # weights
    "song": 50, "idle": 8, "web": 6, "stops": 3, "list": 4, "wrong": 4, "notfound": 3, "refused": 2,
    "remote_reset": 2, "pc_restart": 1.2, "record": 0.8,
}
PILOT = [("remote_reset", {"playing": False}), ("remote_reset", {"playing": True}), ("pc_restart", {"playing": False}),
         ("pc_restart", {"playing": True}), ("record", {}), ("song", {"verse": True, "end": True}),
         ("song", {"switch": True}), ("song", {"extras": True, "end": False}), ("stops", {}), ("web", {}), ("list", {}),
         ("wrong", {}), ("notfound", {}), ("refused", {}), ("idle", {"s": 30})]
SONG_ACTIONS = ("play", "play_while_playing", "stop", "wait_end")
STALE_S = 5.0


class Monitor(threading.Thread):
    """1 Hz watcher over the rig state: resets, kicks, restarts, zombies, PC deaths, proc samples, windows."""

    def __init__(self, r):
        super().__init__(daemon=True, name="monitor")
        self.r, self.stop_ev, self.ready = r, threading.Event(), threading.Event()
        self.windows = []
        self.c = {"kicks_total": 0, "kicks_outside_windows": 0, "restarts": 0, "restarts_in_window": 0,
                  "unexpected_resets": 0, "expected_resets": 0, "zombies": 0, "zombie_s_max": 0.0, "pc_died": 0,
                  "pc_exit_in_window": 0, "swd_stale": 0}
        self.events, self.proc = [], {}
        self.seen = 0.0
        self.adv = {}  # unit -> [last ticks, mono of last change, flagged]

    def in_window(self, t, t2=None):
        t2 = t if t2 is None else t2
        return any(s - 1.0 <= t2 and t <= e for s, e in self.windows)

    def note(self, what, **k):
        rec = dict(what=what, mono=round(time.monotonic(), 2), t=round(time.time(), 1), **k)
        self.events.append(rec)
        self.r.ev("burnin", **rec)
        if what in ("kick_outside", "restart", "unexpected_reset", "zombie", "pc_died", "swd_stale"):
            self.r.snapshot(f"burn-in: {what} {k}")

    def run(self):
        last, zs = {}, None
        while not self.stop_ev.wait(1.0):
            try:
                self.tick(last)
                zs = self.zombie(zs)
            except Exception as e:
                self.r.ev("rig_error", thread="monitor", err=repr(e)[:300])

    def tick(self, last):
        now = time.monotonic()
        for e in self.r.events(self.seen):
            if e["mono"] <= self.seen and self.seen:
                continue
            self.seen = max(self.seen, e["mono"])
            if e["kind"] == "window":
                self.windows.append((e["start"], e["end"]))
            elif e["kind"] == "proc":
                self.proc.setdefault(e["pid"], []).append((e["mono"], e["rss_kb"], e["fds"], e["threads"]))
            elif e["kind"] == "pc_died":  # the proc loop can see a scripted SIGTERM exit before stop_pc clears r.pc
                if self.in_window(e["mono"]):
                    self.c["pc_exit_in_window"] += 1
                else:
                    self.c["pc_died"] += 1
                    self.note("pc_died", rc=e.get("rc"))
        for u in ("base", "remote"):
            d = self.r.state["swd"].get(u)
            if not d or "ticks" not in d:
                continue
            a = self.adv.setdefault(u, [d["ticks"], now, False])
            if d["ticks"] != a[0]:
                a[0], a[1], a[2] = d["ticks"], now, False
            elif now - a[1] > STALE_S and not a[2]:
                a[2] = True
                self.c["swd_stale"] += 1
                self.note("swd_stale", unit=u, s=round(now - a[1], 1))
            p = last.get(u)
            if p and d["ticks"] < p["ticks"]:
                if u == "remote" and self.in_window(now):
                    self.c["expected_resets"] += 1
                else:
                    self.c["unexpected_resets"] += 1
                    self.note("unexpected_reset", unit=u, cause=d.get("resetCause"))
            if u == "base" and p and d["ticks"] >= p["ticks"]:
                dk = (d.get("staleKicks") or 0) - (p.get("staleKicks") or 0)
                dr = (d.get("staleRestarts") or 0) - (p.get("staleRestarts") or 0)
                if dk > 0:
                    self.c["kicks_total"] += dk
                    if not self.in_window(now):
                        self.c["kicks_outside_windows"] += dk
                        self.note("kick_outside", n=dk)
                if dr > 0:
                    if self.in_window(now):
                        self.c["restarts_in_window"] += dr
                    else:
                        self.c["restarts"] += dr
                        self.note("restart", n=dr)
            last[u] = {k: d.get(k) for k in ("ticks", "staleKicks", "staleRestarts")}
        if "base" in last and "remote" in last:
            self.ready.set()  # reset detection needs one sample of each unit before the first action

    def zombie(self, since):
        b, rm = self.r.state["swd"].get("base", {}), self.r.state["swd"].get("remote", {})
        key = (b.get("lastRemoteRx"), rm.get("ka_last"))
        both = b.get("connected") == 1 and rm.get("connected") == 1
        live = all(not a[2] for a in self.adv.values()) and len(self.adv) == 2
        now = time.monotonic()
        if not (both and live) or since is None or since[0] != key:
            return (key, now, False)
        age = now - since[1]
        self.c["zombie_s_max"] = max(self.c["zombie_s_max"], round(age, 1))
        if age > 15 and not since[2]:
            self.c["zombies"] += 1
            self.note("zombie", age_s=round(age, 1))
            return (key, since[1], True)
        return since


def slope(samples, col):
    """least-squares slope per hour over (mono, ...) tuples"""
    n = len(samples)
    if n < 3:
        return None
    xs = [s[0] / 3600 for s in samples]
    ys = [s[col] for s in samples]
    mx, my = sum(xs) / n, sum(ys) / n
    den = sum((x - mx) ** 2 for x in xs)
    return None if den == 0 else round(sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / den, 1)


def leak_stats(proc):
    life = []
    for pid, ss in proc.items():
        t0, t1 = ss[0][0], ss[-1][0]
        use = [s for s in ss if s[0] - t0 >= 600]
        early = [s[2] for s in ss if 600 <= s[0] - t0 < 1200]
        late = [s[2] for s in ss if s[0] >= t1 - 600]
        life.append({"pid": pid, "span_h": round((t1 - t0) / 3600, 2), "rss_kib_per_h": slope(use, 1),
                     "fd_floor_growth": (min(late) - min(early)) if early and late and t1 - t0 >= 1800 else None,
                     "fds_max": max(s[2] for s in ss), "threads_max": max(s[3] for s in ss),
                     "rss_kb_first": ss[0][1], "rss_kb_last": ss[-1][1]})
    if not life:
        return life, None, None, None
    best = max(life, key=lambda x: x["span_h"])
    return life, best["rss_kib_per_h"], best["fd_floor_growth"], best["span_h"]


def git_rev(path):
    try:
        out = subprocess.run(["git", "-C", path, "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout
        dirty = subprocess.run(["git", "-C", path, "status", "--porcelain", "--untracked-files=no"],
                               capture_output=True, text=True).stdout.strip()
        return out.strip() + ("+dirty" if dirty else "")
    except Exception:
        return None


class Burn:
    def __init__(self, a):
        self.a = a
        self.rundir = os.path.abspath(a.rundir)
        os.makedirs(self.rundir, exist_ok=True)
        cfg = A.prepare(self.rundir, a.src)
        self.r = rigmod.Rig(self.rundir, a.pcbin, cfg, xdg_state=os.path.join(self.rundir, "state"), seed=a.seed)
        self.erng = random.Random(a.seed)  # episode schedule only (reproducible apart from timing)
        self.ctx = A.Ctx(self.r)
        self.mon = Monitor(self.r)
        self.t_start = time.time()
        self.m_start = time.monotonic()
        self.m_end = self.m_start + a.hours * 3600
        self.restart_until = self.m_start + (3600 if a.hours >= 4 else a.hours * 3600)
        self.episodes, self.recoveries, self.consec_fail, self.ladder = {}, [], 0, 0
        root = os.path.abspath(os.path.join(HERE, "../../../.."))
        crit = open(os.path.join(HERE, "criteria.toml"), "rb").read()
        self.meta = {"hours_planned": a.hours, "seed": a.seed, "pilot": a.pilot,
                     "start": time.strftime("%Y-%m-%d %H:%M:%S"),
                     "planned_end": time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(self.t_start + a.hours * 3600)),
                     "criteria_sha256": hashlib.sha256(crit).hexdigest()[:16],
                     "revs": {d: git_rev(os.path.join(root, d)) for d in ("midicontrol-sw", "midicontrol-fw",
                                                                             "midicontroller-fw")},
                     "windows_s": A.WINDOW_S, "weights": EPISODES, "end_reason": None}

    # ------------------------------------------------------------ helpers
    def song_left(self):
        c = self.ctx
        return A.SONGS.get(c.playing, 30) * (1 + c.verses) - (time.monotonic() - c.started) if c.playing else 0.0

    def ended(self):
        """the song ran out by itself (e.g. during a disturbance): sync ctx with the PC log"""
        c = self.ctx
        if c.playing and self.r.wait_for(A.pc("Playback finished"), 0.01, c.started):
            c.playing = None
            return True
        return False

    # ------------------------------------------------------------ episodes
    def episode(self, kind, f):
        r, ctx, rng = self.r, self.ctx, self.erng
        A.cleanup(ctx)
        recs = []

        def do(label, fn, *a, **k):
            rec = A.run(ctx, label, fn, *a, **k)
            rec["episode"] = kind
            rec["in_window"] = self.mon.in_window(rec["t0"], rec["t0"] + rec.get("dur_s", 0))
            recs.append(rec)
            return rec
        if kind == "song":
            do("play", A.play, rng.choice(SONGS))
            if not ctx.playing:
                return recs
            if f.get("verse", rng.random() < 0.15):
                time.sleep(rng.uniform(4, 8))
                do("add_verse", A.add_verse, presses=rng.choice([1, 1, 2]), rapid=rng.random() < 0.3)
            if f.get("switch", rng.random() < 0.1) and not self.ended() and self.song_left() > 10:
                time.sleep(rng.uniform(3, 6))
                do("play_while_playing", A.play, rng.choice(SONGS))
            end = f.get("end", rng.random() < 0.4)
            if not end and f.get("extras", rng.random() < 0.2) and self.song_left() > 12:
                if rng.random() < 0.5 or f.get("extras"):
                    do("web_during_play", A.web_visit)
                if (rng.random() < 0.5 or f.get("extras")) and self.song_left() > 8:
                    do("list_during_play", A.song_list_and_back)
            budget = self.song_left() - 6
            if end or budget < 3 or self.ended():
                if ctx.playing:
                    do("wait_end", A.wait_end)
            else:
                time.sleep(rng.uniform(1.5, min(20, budget)))
                if not self.ended():
                    do("stop", A.stop, twice=rng.random() < 0.1)
        elif kind == "idle":
            do("idle", A.idle, f.get("s", rng.uniform(20, 120)))
        elif kind == "web":
            do("web", A.web_visit)
        elif kind == "stops":
            do("stops_edit", A.stops_edit, rng.choice(SONGS))
        elif kind == "list":
            do("list", A.song_list_and_back)
        elif kind == "wrong":
            do("wrong_code", A.wrong_code_corrected, "409", "401")
            if ctx.playing:
                time.sleep(rng.uniform(3, 10))
                if not self.ended():
                    do("stop", A.stop)
        elif kind == "notfound":
            do("notfound", A.play, rng.choice(["1999", "88", "1234", "999"]), expect="notfound")
        elif kind == "refused":
            do("refused", A.play, "2000", expect="refused")
        elif kind == "remote_reset":
            if f.get("playing", rng.random() < 0.6):
                do("play", A.play, "401")  # longest song (41.9 s): still playing when the link returns
                time.sleep(rng.uniform(3, 6))
            do("remote_reset", A.remote_reset)
            self.cooldown()
            if ctx.playing and not self.ended():
                do("stop", A.stop)
        elif kind == "pc_restart":
            if f.get("playing", rng.random() < 0.5):
                do("play", A.play, rng.choice(["401", "403"]))
                time.sleep(rng.uniform(3, 8))
            do("pc_restart", A.pc_restart)
            self.cooldown()
        elif kind == "record":
            do("record_smoke", A.record_smoke, rng.uniform(3, 8))
        return recs

    def cooldown(self):
        """stay quiet until the last disturbance window closes, so later events are judged outside it"""
        if self.mon.windows:
            left = self.mon.windows[-1][1] - time.monotonic()
            if left > 0:
                time.sleep(left)

    def pick(self):
        kinds, weights = list(EPISODES), list(EPISODES.values())
        while True:
            k = self.erng.choices(kinds, weights)[0]
            if k != "pc_restart" or time.monotonic() < self.restart_until:
                return k

    # ------------------------------------------------------------ recovery ladder
    def recover(self, recs):
        if all(x["ok"] for x in recs):
            self.consec_fail, self.ladder = 0, 0
            return True
        self.consec_fail += 1
        if self.consec_fail < 3:
            return True
        self.ladder += 1
        self.consec_fail = 0
        step = {1: "cleanup+nav", 2: "pc_restart"}.get(self.ladder, "abort")
        self.recoveries.append({"mono": round(time.monotonic(), 1), "t": round(time.time(), 1), "step": step})
        self.r.snapshot(f"recovery ladder step {self.ladder}: {step}")
        self.r.ev("recovery", step=step)
        if step == "cleanup+nav":
            A.cleanup(self.ctx)
            self.r.to_player()
            return True
        if step == "pc_restart":
            A.run(self.ctx, "pc_restart_recovery", A.pc_restart)
            self.cooldown()
            return True
        return False

    def pc_alive(self):
        if self.r.pc is None:  # died (Monitor counts it); restart and continue
            self.r.ev("recovery", step="pc_start after death")
            self.recoveries.append({"mono": round(time.monotonic(), 1), "t": round(time.time(), 1),
                                    "step": "pc_start after death"})
            t = time.monotonic()
            self.r.start_pc()
            self.r.pc_line("Remote state resynced", 30, t)
            self.ctx.playing = None

    # ------------------------------------------------------------ summary
    def summary(self, final=False):
        recs = list(self.ctx.records)
        for x in recs:  # re-judged here: the monitor learns of a window up to 1 s after the action that opened it
            x["in_window"] = self.mon.in_window(x["t0"], x["t0"] + x.get("dur_s", 0))
        bad = [x for x in recs if not x["ok"]]
        song_bad = [x for x in bad if x["action"] in SONG_ACTIONS and not x.get("in_window")]
        song_bad_w = [x for x in bad if x["action"] in SONG_ACTIONS and x.get("in_window")]
        other_bad = [x for x in bad if x["action"] not in SONG_ACTIONS]
        life, rss, fd, span = leak_stats(dict(self.mon.proc))
        notes = [v for x in recs for k, v in x["info"].items()
                 if k.startswith("no_hanging") and k.endswith("_n") and isinstance(v, int)]
        lat = {}
        for k in ("key_to_playing_ms", "key_to_stopped_ms"):
            vals = [(x["t0"], x["info"][k]) for x in recs if isinstance(x["info"].get(k), (int, float))]
            if vals:
                v = sorted(b for _, b in vals)
                hours = {}
                for t0, b in vals:
                    hours.setdefault(int((t0 - self.m_start) // 3600), []).append(b)
                lat[k] = {"n": len(v), "median": v[len(v) // 2], "p95": v[int(0.95 * (len(v) - 1))], "max": v[-1],
                          "median_per_hour": {h: sorted(a)[len(a) // 2] for h, a in sorted(hours.items())}}
        ks = self.r.key_stats
        acts = {}
        for x in recs:
            acts[x["action"]] = acts.get(x["action"], 0) + 1
        s = dict(self.meta, final=final, elapsed_h=round((time.monotonic() - self.m_start) / 3600, 3),
                 written=time.strftime("%Y-%m-%d %H:%M:%S"), episodes=self.episodes, action_counts=acts,
                 actions=len(recs), action_failures=len(bad),
                 failed_actions=[{"action": x["action"], "t": x["t"], "in_window": x.get("in_window"),
                                  "checks": [c for c, v in x["checks"].items() if not v]} for x in bad][-60:],
                 play_failures=len(song_bad), play_failures_in_window=len(song_bad_w), other_failures=len(other_bad),
                 notes_on_after_stop_max=max(notes) if notes else 0,
                 fwd_err=self.r.tap.counts.get("fwd_err", 0) if self.r.tap else None,
                 tap=dict(self.r.tap.counts) if self.r.tap else None,
                 key_timeouts=ks["timeouts"], keys=ks["keys"], keys_rejected=ks["rejected"],
                 key_ack_ms_max=max(ks["ack_ms"]) if ks["ack_ms"] else None,
                 rss_growth_kib_per_h=rss, fd_growth=fd, leak_lifetime_h=span, pc_lifetimes=life,
                 windows=len(self.mon.windows),
                 controls={"remote_reset_actions": acts.get("remote_reset", 0),
                           "pc_restart_actions": acts.get("pc_restart", 0) + acts.get("pc_restart_recovery", 0),
                           "expected_resets": self.mon.c["expected_resets"], "windows": len(self.mon.windows),
                           "pc_exit_in_window": self.mon.c["pc_exit_in_window"]},
                 recoveries=self.recoveries, monitor_events=self.mon.events[-100:], latency=lat, **self.mon.c)
        tmp = os.path.join(self.rundir, "summary.json.tmp")
        with open(tmp, "w") as fh:
            json.dump(s, fh, indent=1)
        os.replace(tmp, os.path.join(self.rundir, "summary.json"))
        return s

    def run(self):
        with open(os.path.join(self.rundir, "burnin.pid"), "w") as fh:
            fh.write(str(os.getpid()))

        def on_term(*_):
            raise SystemExit("SIGTERM")
        signal.signal(signal.SIGTERM, on_term)
        r = self.r
        stopfile = os.path.join(self.rundir, "STOP")
        reason = "time"
        try:
            r.start()
            self.mon.start()
            if not r.pc_line("Remote state resynced", 40, 0):
                print("WARN: no resync seen at start", flush=True)
            if not self.mon.ready.wait(15):
                print("WARN: monitor has no SWD baseline of both units", flush=True)
            A.cleanup(self.ctx)
            self.summary()
            next_ck = time.monotonic() + 1800
            plan = list(PILOT) if self.a.pilot else []
            while time.monotonic() < self.m_end:
                if os.path.exists(stopfile):
                    reason = "STOP file"
                    break
                self.pc_alive()
                kind, f = plan.pop(0) if plan else (self.pick(), {})
                self.episodes[kind] = self.episodes.get(kind, 0) + 1
                recs = self.episode(kind, f)
                print(f"{time.strftime('%H:%M:%S')} {kind}{f or ''}: " + ", ".join(
                    f"{x['action']}={'ok' if x['ok'] else 'FAIL:' + ','.join(c for c, v in x['checks'].items() if not v)}"
                    for x in recs if x["action"] != "idle"), flush=True)
                if not self.recover(recs):
                    reason = "recovery ladder exhausted"
                    break
                time.sleep(r.rng.uniform(2, 8))
                if time.monotonic() >= next_ck:
                    self.summary()
                    next_ck += 1800
        except BaseException as e:
            reason = f"exception {e!r}"[:200]
            if not isinstance(e, (SystemExit, KeyboardInterrupt)):
                raise
        finally:
            self.meta["end_reason"] = reason
            try:
                try:
                    A.cleanup(self.ctx)
                except Exception:
                    pass
                self.mon.stop_ev.set()
                s = self.summary(final=True)
                print(json.dumps({k: s[k] for k in (
                    "end_reason", "elapsed_h", "actions", "action_failures", "play_failures", "play_failures_in_window",
                    "other_failures", "zombies", "zombie_s_max", "kicks_outside_windows", "restarts",
                    "unexpected_resets", "pc_died", "swd_stale", "rss_growth_kib_per_h", "fd_growth",
                    "leak_lifetime_h", "key_timeouts", "controls")}), flush=True)
            finally:
                r.stop()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rundir"); ap.add_argument("pcbin"); ap.add_argument("src")
    ap.add_argument("--hours", type=float, required=True); ap.add_argument("--seed", type=int, default=29)
    ap.add_argument("--pilot", action="store_true")
    Burn(ap.parse_args()).run()


if __name__ == "__main__":
    main()
