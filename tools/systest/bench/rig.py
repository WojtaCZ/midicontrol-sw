"""Bench rig: real base + remote + PC software, one timeline (DESIGN.md sec. 3, bench tier).

Rig(rundir).start() brings up, in this order:
  1. timeline.jsonl   every event {t (wall), mono (CLOCK_MONOTONIC), kind, ...}; wait_for() works on the in-memory tail
  2. MidiTap          ALSA relay between the PC and the base's USB-MIDI port (miditap.py); MIDI onsets -> timeline
  3. PC software      `midicontrol --headless`, MIDICONTROL_MIDI_KEY=<tap>, stdout/stderr piped line by line -> kind "pc"
                      (Info lines are flushed per line by log.cpp, so the stamp is the arrival time of the line)
  4. API poller       /api/status every 1 s -> api.jsonl (changed, non-volatile fields only; full copy every 300 s) and
                      derived timeline events (kind "api": playback, organ_power, connected, reconnects, ...);
                      /api/diag followed by id -> kind "diag"
  5. SWD sampler      both units at 1 Hz, non-halting (swd.py) -> swd.jsonl (frame stripped) + frames.jsonl (only when the
                      frame changes) + derived events (kind "swd": screen, connected, staleKicks/Restarts, halted, err)
  6. /proc health     PC RSS, fds, threads, CPU every 10 s -> kind "proc"
Inputs: key() = remote RAM key mailbox (SYS-DIAG-07; ack-checked, kind "key"), pc_cmd() = `midicontrol --cmd` (kind "cmd").

Everything is a daemon thread; stop() ends them, stops the PC (SIGTERM) and releases both cores (C_DEBUGEN 0).
"""
import collections
import json
import queue
import os
import random
import signal
import subprocess
import sys
import threading
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import swd  # noqa: E402
import miditap  # noqa: E402

# Remote Keyboard::Key indices (midicontroller-fw); mailbox word 0xA5<<24 | seq<<8 | key
KEYS = {"1": 0, "2": 1, "3": 2, "4": 3, "5": 4, "6": 5, "7": 6, "8": 7, "9": 8, "MULTI": 9, "0": 10,
        "UP": 11, "DOWN": 12, "LEFT": 13, "BACK": 13, "RIGHT": 14, "OK": 15}
VOLATILE = ("_ms", "uptime", "battery_mv", "rssi", "vbus_mv", "now_ms", "events", "counters")
API_WATCH = {"software": ("playback", "song", "section", "last_end", "activity", "fw_update_state"),
             "base": ("connected", "alive", "organ_power", "ssr", "reconnects", "ble_connected", "ble_peer_alive",
                      "ble_stream_open", "reset_cause", "fault_count", "in_bootloader"),
             "controller": ("ble_connected", "ble_peer_alive", "reset_cause", "battery_critical", "vbus", "last_tx_result")}
SWD_WATCH = {"base": ("connected", "staleKicks", "staleRestarts", "resetCause", "moduleUp", "peerAccepted"),
             "remote": ("connected", "moduleUp", "autoOffDisabled", "stuck")}


class Rig:
    def __init__(self, rundir, pcbin, config, *, swd_mode="1hz", tap=True, seed=None, xdg_state=None, port=None):
        self.rundir = os.path.abspath(rundir)
        os.makedirs(self.rundir, exist_ok=True)
        self.pcbin, self.config = os.path.abspath(pcbin), os.path.abspath(config)
        self.swd_mode, self.use_tap = swd_mode, tap
        self.port = port or _config_port(self.config)
        self.api_url = f"http://127.0.0.1:{self.port}"
        self.xdg_state = xdg_state or os.path.join(self.rundir, "xdg-state")
        self.rng = random.Random(seed)
        self.seed = seed
        self._lock = threading.Lock()
        self._cond = threading.Condition(self._lock)
        self._tail = collections.deque(maxlen=50000)  # (n, rec)
        self._n = 0
        self._wq = queue.Queue()  # (file name, rec): written by the writer thread, never on the relay/sampler threads
        self._files = {}
        self._fast_until = {}
        self._stop = threading.Event()
        self._threads = []
        self.pc = None
        self.tap = None
        self.units = {}
        self.state = {"api": {}, "swd": {}, "screen": {}}
        self._seq = 0
        self.key_stats = {"keys": 0, "timeouts": 0, "rejected": 0, "wakes": 0, "ack_ms": []}

    # ---------------------------------------------------------------- timeline
    def _file(self, name):
        if name not in self._files:
            self._files[name] = open(os.path.join(self.rundir, name), "a")
        return self._files[name]

    def _writer(self):
        while True:
            item = self._wq.get()
            if item is None:
                break
            batch = [item]
            while len(batch) < 500:
                try:
                    nxt = self._wq.get_nowait()
                except queue.Empty:
                    break
                if nxt is None:
                    self._wq.put(None)
                    break
                batch.append(nxt)
            touched = set()
            for name, rec in batch:
                self._file(name).write(json.dumps(rec, default=str) + "\n")
                touched.add(name)
            for name in touched:
                self._files[name].flush()

    def ev(self, kind, mono=None, **k):
        rec = {"t": round(time.time(), 3), "mono": round(mono if mono is not None else time.monotonic(), 6), "kind": kind}
        rec.update(k)
        with self._cond:
            self._n += 1
            self._tail.append((self._n, rec))
            self._cond.notify_all()
        self._wq.put(("timeline.jsonl", rec))
        return rec

    def raw(self, name, rec):
        self._wq.put((name, rec))

    def wait_for(self, pred, timeout, since=None):
        """First timeline event (mono >= since) for which pred(ev) is true, or None after timeout.
        Each wake-up scans only events appended since the previous scan (sequence numbers)."""
        since = time.monotonic() if since is None else since
        end = time.monotonic() + timeout
        last = 0
        with self._cond:
            while True:
                new = []
                for n, e in reversed(self._tail):
                    if n <= last:
                        break
                    new.append((n, e))
                for n, e in reversed(new):
                    if e["mono"] >= since and pred(e):
                        return e
                if new:
                    last = new[0][0]
                left = end - time.monotonic()
                if left <= 0:
                    return None
                self._cond.wait(min(left, 0.5))

    def events(self, since=0.0, kind=None):
        with self._lock:
            return [e for _, e in self._tail if e["mono"] >= since and (kind is None or e["kind"] == kind)]

    def pc_line(self, text, timeout, since=None):
        return self.wait_for(lambda e: e["kind"] == "pc" and text in e["line"], timeout, since)

    # ---------------------------------------------------------------- start/stop
    def start(self, start_pc=True):
        self._wthr = threading.Thread(target=self._writer, name="writer", daemon=True)
        self._wthr.start()
        self.ev("rig_start", rundir=self.rundir, pcbin=self.pcbin, config=self.config, swd=self.swd_mode, seed=self.seed)
        if self.use_tap:
            base = miditap.find_client("MIDIControl")
            if base is None:
                raise RuntimeError("base USB-MIDI port (ALSA client 'MIDIControl ...') not found")
            self.tap = miditap.MidiTap(base, 0, on_event=self._on_midi)
            self.ev("tap", name=self.tap.name, client=self.tap.client, base_client=base)
        if start_pc:
            self.start_pc()
        self._spawn(self._api_loop, "api")
        self._spawn(self._diag_loop, "diag")
        if self.swd_mode != "off":
            for u in ("base", "remote"):
                s = swd.Swd(u, logdir=self.rundir)
                s.start()
                self.units[u] = s
                self._spawn(self._swd_loop, f"swd-{u}", u)
        self._spawn(self._proc_loop, "proc")
        if self.tap:
            self._spawn(self._tap_trim_loop, "taptrim")
            self._spawn(self._tap_ensure_loop, "tapensure")
        end = time.monotonic() + 15
        while self.units and time.monotonic() < end and not all(u in self.state["swd"] for u in self.units):
            time.sleep(0.1)
        if "remote" in self.units:
            ack = self.units["remote"].read32(swd.symbols("remote")["mcTestKeyAck"][0])
            self._seq = (ack >> 8) & 0xFFFF  # continue after the firmware's last echoed seq: no stale-ack match
        return self

    def _spawn(self, fn, name, *a):
        th = threading.Thread(target=self._guard, args=(fn, name) + a, name=name, daemon=True)
        th.start()
        self._threads.append(th)

    def _guard(self, fn, name, *a):
        try:
            fn(*a)
        except Exception as e:  # a dead observer must show in the timeline, not vanish
            self.ev("rig_error", thread=name, err=repr(e)[:300])

    def start_pc(self):
        env = dict(os.environ)
        env.pop("XDG_RUNTIME_DIR", None)  # lock + socket under /tmp/midicontrol-<uid>, never the host session's
        env["XDG_STATE_HOME"] = self.xdg_state
        if self.tap:
            env["MIDICONTROL_MIDI_KEY"] = self.tap.key
        self.pc = subprocess.Popen([self.pcbin, "--headless", "--config", self.config], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
                                   start_new_session=True, bufsize=0)
        with open(os.path.join(self.rundir, "pc.pid"), "w") as f:
            f.write(str(self.pc.pid))
        self.ev("pc_start", pid=self.pc.pid, midi_key=env.get("MIDICONTROL_MIDI_KEY"))
        for stream, lvl in ((self.pc.stdout, "out"), (self.pc.stderr, "err")):
            self._spawn(self._pc_reader, f"pc-{lvl}", stream, lvl, self.pc.pid)

    def stop_pc(self, sig=signal.SIGTERM, timeout=8):
        if not self.pc:
            return None
        p = self.pc
        try:
            os.kill(p.pid, sig)
        except ProcessLookupError:
            pass
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            rc = p.wait()
        self.ev("pc_exit", pid=p.pid, rc=rc, sig=int(sig))
        self.pc = None
        return rc

    def _pc_reader(self, stream, lvl, pid):
        for raw in iter(stream.readline, b""):
            mono = time.monotonic()
            line = raw.decode(errors="replace").rstrip("\n")
            self.ev("pc", mono=mono, line=line, stream=lvl, pid=pid)
        self.ev("pc_eof", stream=lvl, pid=pid)

    def stop(self):
        self._stop.set()
        self.ev("rig_stop", key_stats={k: v for k, v in self.key_stats.items() if k != "ack_ms"})
        self.stop_pc()
        for th in self._threads:
            th.join(timeout=3)
        for s in self.units.values():
            s.stop()
        if self.tap:
            self.tap.close()
        self._wq.put(None)
        self._wthr.join(timeout=10)
        for f in self._files.values():
            f.close()
        self._files.clear()

    # ---------------------------------------------------------------- observers
    def api_get(self, path, timeout=3):
        return json.load(urllib.request.urlopen(self.api_url + path, timeout=timeout))

    def _api_loop(self):
        nxt, last_full, prev = time.monotonic(), 0.0, {}
        while not self._stop.is_set():
            mono = time.monotonic()
            try:
                st = self.api_get("/api/status")
            except Exception as e:
                if prev.get("_err") != type(e).__name__:
                    self.ev("api", err=repr(e)[:200])
                prev["_err"] = type(e).__name__
                st = None
            if st is not None:
                prev.pop("_err", None)
                flat = {}
                for node, v in st["nodes"].items():
                    for k, val in v.get("fields", {}).items():
                        flat[f"{node}.{k}"] = val
                    flat[f"{node}.dot"] = v.get("dot")
                    flat[f"{node}.unseen_faults"] = v.get("unseen_faults")
                self.state["api"] = flat
                self.state["api_status"] = st
                full = mono - last_full >= 300
                diff = {k: v for k, v in flat.items()
                        if (full or prev.get(k) != v) and (full or not any(s in k for s in VOLATILE))}
                if diff:
                    self.raw("api.jsonl", {"t": round(time.time(), 3), "mono": round(mono, 4), "full": full, "f": diff})
                if full:
                    last_full = mono
                ch = {}
                for node, keys in API_WATCH.items():
                    for k in keys:
                        fk = f"{node}.{k}"
                        if fk in flat and prev.get(fk, "<unset>") != flat[fk] and "<unset>" != prev.get(fk, "<unset>"):
                            ch[fk] = [prev.get(fk), flat[fk]]
                if ch:
                    self.ev("api", mono=mono, changed=ch)
                prev.update(flat)
            nxt += 1.0
            self._stop.wait(max(0, nxt - time.monotonic()))

    def _diag_loop(self):
        last_id = None
        while not self._stop.is_set():
            try:
                ents = self.api_get("/api/diag?limit=200")["entries"]
                if last_id is None:
                    last_id = max((e["id"] for e in ents), default=0)
                    self.ev("diag_start", last_id=last_id)
                for e in sorted(ents, key=lambda e: e["id"]):
                    if e["id"] > last_id:
                        self.ev("diag", entry=e)
                        last_id = e["id"]
            except Exception:
                pass
            self._stop.wait(2.0)

    def fast(self, unit, seconds):
        """Sample `unit` back to back (~35 ms per sample) for `seconds`: frame changes then carry a bound of one sample
        interval instead of 1 s. swd.jsonl stays at ~1 Hz."""
        self._fast_until[unit] = time.monotonic() + seconds

    def _swd_loop(self, unit):
        s, prev, prev_frame, prev_mono, last_raw = self.units[unit], {}, None, None, 0.0
        nxt = time.monotonic()
        while not self._stop.is_set():
            try:
                d = s.sample()
            except Exception as e:
                d = {"unit": unit, "t": round(time.time(), 3), "err": repr(e)[:200]}
            mono = d.get("mono", time.monotonic())
            if "err" in d:
                if prev.get("err") != d["err"]:
                    self.ev("swd", unit=unit, err=d["err"])
                prev["err"] = d["err"]
            else:
                prev.pop("err", None)
                d["screen"] = swd.current_screen(d, s.screens)
                fr = d.pop("frame", None)
                if mono - last_raw >= 0.95:
                    self.raw("swd.jsonl", d)
                    last_raw = mono
                if fr and fr != prev_frame:
                    self.raw("frames.jsonl", {"unit": unit, "t": d["t"], "mono": mono, "screen": d["screen"], "frame": fr})
                    if prev_frame is not None:
                        # changed somewhere in (prev sample, this sample]
                        self.ev("frame", mono=mono, unit=unit, screen=d["screen"], lo=prev_mono,
                                bound_ms=round((mono - prev_mono) * 1000, 1))
                    prev_frame = fr
                ch = {k: [prev.get(k), d.get(k)] for k in SWD_WATCH[unit] + ("screen", "halted")
                      if k in d and k in prev and prev[k] != d[k]}
                if ch:
                    self.ev("swd", mono=mono, unit=unit, changed=ch, lo=prev_mono)
                self.state["swd"][unit] = d
                prev = {k: d[k] for k in d}
                prev_mono = mono
            if time.monotonic() < self._fast_until.get(unit, 0):
                nxt = time.monotonic()
                continue
            nxt += 1.0
            if nxt < time.monotonic():
                nxt = time.monotonic()
            self._stop.wait(max(0, nxt - time.monotonic()))

    def _proc_loop(self):
        hz = os.sysconf("SC_CLK_TCK")
        while not self._stop.is_set():
            p = self.pc
            if p and p.poll() is None:
                try:
                    st = open(f"/proc/{p.pid}/stat").read().rsplit(")", 1)[1].split()
                    status = dict(l.split(":", 1) for l in open(f"/proc/{p.pid}/status") if ":" in l)
                    self.ev("proc", pid=p.pid, rss_kb=int(status["VmRSS"].split()[0]), threads=int(status["Threads"]),
                            fds=len(os.listdir(f"/proc/{p.pid}/fd")), cpu_s=round((int(st[11]) + int(st[12])) / hz, 2))
                except (OSError, KeyError, IndexError):
                    pass
            elif p and p.poll() is not None:
                self.ev("pc_died", pid=p.pid, rc=p.returncode)
                self.pc = None
            self._stop.wait(10.0)

    def _on_midi(self, rec):
        # every event -> midi.jsonl (writer thread). Timeline: out onsets (first byte after >= 0.5 s quiet), the first
        # Note On of each burst, every non-note message; in (base->PC) only when the bytes change (the base repeats its
        # display SysEx every 500 ms).
        self.raw("midi.jsonl", rec)
        b = bytes.fromhex(rec["bytes"]) if rec["bytes"] else b""
        last = self.state.setdefault("midi_last", {})
        if rec["dir"] == "in":
            if last.get("in_bytes") == rec["bytes"]:
                return
            last["in_bytes"] = rec["bytes"]
            self.ev("midi", mono=rec["mono"], dir="in", bytes=rec["bytes"][:64], fwd_us=rec["fwd_us"])
            return
        quiet = rec["mono"] - last.get("out", -1e9)
        last["out"] = rec["mono"]
        st = b[0] & 0xF0 if b else 0
        onset = quiet >= 0.5
        if onset:
            last["note_pending"] = True
        note_on = st == 0x90 and len(b) >= 3 and b[2] > 0
        first_note = note_on and last.get("note_pending", False)
        if first_note:
            last["note_pending"] = False
        if onset or first_note or st not in (0x80, 0x90):
            self.ev("midi", mono=rec["mono"], dir="out", bytes=rec["bytes"][:64], onset=onset, first_note=first_note,
                    fwd_us=rec["fwd_us"])

    def _tap_ensure_loop(self):
        state = None
        while not self._stop.wait(1.0):
            try:
                r = self.tap.ensure()
            except Exception as e:
                r = f"error {e!r}"[:200]
            if r != state:
                self.ev("tap_link", state=r or "ok", base=self.tap.base[0])
                state = None if r == "resubscribed" else r

    def _tap_trim_loop(self):
        while not self._stop.wait(60):
            self.tap.trim(900)
            self.ev("tap_counts", **self.tap.counts, notes_on=len(self.tap.notes_on))

    # ---------------------------------------------------------------- inputs
    def oled_sleeping(self):
        """Remote OLED asleep (30 s idle). The firmware then takes the next key as a wake press only and drops it."""
        a = swd.symbols("remote")["Oled::sleeping"][0]
        return bool((self.units["remote"].read32(a & ~3) >> (8 * (a & 3))) & 0xFF)

    def key(self, name, timeout=2.0):
        """One remote key; wakes the OLED first (with BACK, which a sleeping remote swallows) so `name` is not lost."""
        if self.units.get("remote") is None:
            raise RuntimeError("key() needs SWD (swd_mode != off)")
        if self.oled_sleeping():
            self.key_stats["wakes"] += 1
            self.ev("oled_wake", key=name)
            self._key("BACK", timeout)
            end = time.monotonic() + 1.0
            while self.oled_sleeping() and time.monotonic() < end:
                time.sleep(0.01)
            time.sleep(0.05)
        return self._key(name, timeout)

    def _key(self, name, timeout=2.0):
        """One remote key through the RAM mailbox; True when the firmware echoed the word."""
        k = KEYS[name] if isinstance(name, str) else name
        rem = self.units["remote"]
        a_key, a_ack = swd.symbols("remote")["mcTestKey"][0], swd.symbols("remote")["mcTestKeyAck"][0]
        self._seq = (self._seq + 1) & 0xFFFF or 1
        w = 0xA5000000 | (self._seq << 8) | k
        t0 = time.monotonic()
        while rem.read32(a_key) != 0 and time.monotonic() - t0 < timeout:
            time.sleep(0.01)
        t_w = time.monotonic()
        rem.write32(a_key, w)
        while time.monotonic() - t_w < timeout:
            a = rem.read32(a_ack)
            if a == w:
                t_a = time.monotonic()
                self.key_stats["keys"] += 1
                self.key_stats["ack_ms"].append(round((t_a - t_w) * 1000, 1))
                self.ev("key", mono=t_w, key=name, word=hex(w), ack_ms=round((t_a - t_w) * 1000, 1))
                return True
            if a == ((w & 0xFFFFFF) | 0xEE000000):
                self.key_stats["rejected"] += 1
                self.ev("key", mono=t_w, key=name, word=hex(w), rejected=True)
                return False
            time.sleep(0.005)
        self.key_stats["timeouts"] += 1
        self.ev("key", mono=t_w, key=name, word=hex(w), timeout=True)
        return False

    def to_player(self, timeout=5.0):
        """Deterministic path from any remote menu to playerScreen: LEFT x4 (back to mainScreen), UP x6 (cursor to the
        top), OK (opens the player). Digits are honoured only on playerScreen. True when SWD shows playerScreen."""
        for k in ["LEFT"] * 4 + ["UP"] * 6 + ["OK"]:
            self.key(k)
            time.sleep(0.12)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self.screen("remote") == "playerScreen":
                self.ev("nav", ok=True)
                return True
            time.sleep(0.1)
        self.ev("nav", ok=False, screen=self.screen("remote"))
        return False

    def keys(self, *names, gap=(0.25, 0.6)):
        """Keys with human-like gaps (seeded RNG). A string like "406A" is not split: pass digits separately."""
        ok = True
        for i, n in enumerate(names):
            if i:
                time.sleep(self.rng.uniform(*gap))
            ok = self.key(n) and ok
        return ok

    def pc_cmd(self, cmd, timeout=30):
        env = dict(os.environ)
        env.pop("XDG_RUNTIME_DIR", None)
        env["XDG_STATE_HOME"] = self.xdg_state
        t0 = time.monotonic()
        r = subprocess.run([self.pcbin, "--config", self.config, "--cmd", cmd], env=env, capture_output=True, text=True,
                           timeout=timeout)
        self.ev("cmd", mono=t0, cmd=cmd, rc=r.returncode, out=(r.stdout + r.stderr)[-400:], dur_ms=round((time.monotonic() - t0) * 1000))
        return r

    # ---------------------------------------------------------------- helpers
    def api(self, key, default=None):
        return self.state["api"].get(key, default)

    def screen(self, unit):
        return self.state["swd"].get(unit, {}).get("screen")

    def wait_api(self, key, value, timeout):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if self.api(key) == value:
                return True
            time.sleep(0.2)
        return False

    def snapshot(self, reason):
        """Extended snapshot for unexpected events: last API status, both units' last samples, tap state."""
        snap = {"reason": reason, "api": self.state.get("api_status"), "swd": self.state["swd"],
                "tap": dict(self.tap.counts, notes_on=sorted(self.tap.notes_on)) if self.tap else None}
        try:
            snap["diag"] = self.api_get("/api/diag?limit=40")["entries"]
        except Exception as e:
            snap["diag"] = repr(e)
        self.raw("snapshots.jsonl", dict(snap, t=round(time.time(), 3), mono=round(time.monotonic(), 4)))
        self.ev("snapshot", reason=reason)


def _config_port(path):
    sec = None
    for line in open(path):
        line = line.strip()
        if line.startswith("["):
            sec = line.strip("[]").upper()
        elif sec == "WEB" and line.lower().startswith("port") and "=" in line:
            return int(line.split("=", 1)[1].strip())
    return 50118
