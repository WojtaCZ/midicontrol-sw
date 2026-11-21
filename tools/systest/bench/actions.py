"""Action library for the step-3 scenarios (scenarios.py) and the burn-in (burnin.py).

Every action drives the real rig (rig.Rig) the way the organist would (remote keys through the RAM mailbox, human gaps from
the seeded RNG) or the way maintenance would (web, PC restart), and checks its EXPECTED observations, written down here
before any run (advisor #7). The expectations come from the code:
  remote  midicontroller-fw src/player_screen.cpp PlayerScreen::handleInput (OK = play entered code / stop / add verse
          with Repeat selected; BACK = clear letter, then last digit, while playing select Stop; RIGHT = select Repeat once
          the first anchor arrived; UP/DOWN = cycle the letter none->A->B->C->D while entering, else open the song list;
          a 5th digit starts a new code; a digit that would exceed 1999 is dropped on the remote ("2000" -> "200"; bench 2026-11-15))
  PC      midicontrol-sw src/comm.cpp ("PLAY_SONG \"<code>\" from controller", "Song not found: <code>",
          "Organ power on, warming up", "Playing <code>:", "Add verse: <label>", "STOP_SONG from controller",
          "Playback stopped.", "Playback finished, organ power off.", "Recording to <path>")

An action returns a record {"action", "ok", "checks": {name: bool}, "info": {...}, "t0", "dur_s"}; ok = all checks true.
Disturbances (remote reset, PC restart) open a window event (kind "window") so the burn-in counts kicks/resets inside
windows as expected and everything outside as unexpected.
"""
import json
import os
import time
import urllib.request

AIRCR, SYSRESETREQ = 0xE000ED0C, 0x05FA0004
# song lengths (one section each, from the SMF tempo map; audit/runs/2026-11-15-step3-c100/play)
SONGS = {"401": 41.9, "403": 35.9, "406A": 23.9, "406B": 17.9, "407": 17.9, "512": 31.9, "513": 31.9, "516": 31.9,
         "517": 31.9, "519": 31.4, "523": 31.9}
LETTER_UPS = {"": 0, "A": 1, "B": 2, "C": 3, "D": 4}
WINDOW_S = {"remote_reset": 45.0, "pc_restart": 40.0}  # fixed before the burn-in (criteria.toml BURN-2..4 note)


def prepare(rundir, src):
    """Own copy of the play folder (+ per-song .conf) and a config pointing into the rundir, so stops edits and
    recordings never touch another run's files. Returns the config path."""
    import shutil
    rundir = os.path.abspath(rundir)
    os.makedirs(os.path.join(rundir, "record"), exist_ok=True)
    play = os.path.join(rundir, "play")
    if not os.path.isdir(play):
        shutil.copytree(os.path.join(src, "play"), play)
    cfg = os.path.join(rundir, "config.conf")
    with open(cfg, "w") as f:
        f.write(f"[FILES]\nPlayFolder = {play}\nRecordFolder = {rundir}/record\n[PLAYBACK]\nWarmupMs = 3000\n"
                f"KeepPowerOn = false\nVerseGapMs = 0\n[WEB]\nEnabled = true\nPort = 28120\nBind = 0.0.0.0\n"
                f"[DIAG]\nDir = {rundir}/diag\n")
    return cfg


def cleanup(ctx):
    """Between scenarios/draws: nothing playing, remote on playerScreen."""
    r = ctx.r
    if r.api("software.playback") not in ("idle", None):
        r.pc_cmd("stop")
        r.wait_api("software.playback", "idle", 5)
    ctx.playing = None
    if r.screen("remote") != "playerScreen":
        r.to_player()


def pc(text):
    return lambda e: e["kind"] == "pc" and text in e["line"]


class Ctx:
    """Shared state between actions: what the rig believes is playing (from the PC log, never from the action's hope)."""

    def __init__(self, r, out_name="actions.jsonl"):
        self.r, self.out = r, out_name
        self.playing = None  # code or None
        self.started, self.verses = 0.0, 0
        self.records = []

    def log(self, rec):
        self.records.append(rec)
        self.r.raw(self.out, rec)
        self.r.ev("action", name=rec["action"], ok=rec["ok"], failed=[k for k, v in rec["checks"].items() if not v])
        return rec


def run(ctx, name, fn, *a, **k):
    r = ctx.r
    t0 = time.monotonic()
    rec = {"action": name, "args": [str(x) for x in a], "t": round(time.time(), 3), "t0": round(t0, 4), "checks": {},
           "info": {}}
    r.ev("action_start", name=name, args=rec["args"])
    try:
        fn(ctx, rec, *a, **k)
    except Exception as e:  # an action that crashes is a failed action, never a dead run
        rec["checks"]["no_exception"] = False
        rec["info"]["exception"] = repr(e)[:300]
    rec["dur_s"] = round(time.monotonic() - t0, 2)
    rec["ok"] = all(rec["checks"].values())
    if not rec["ok"]:
        r.snapshot(f"action {name} failed: {[k for k, v in rec['checks'].items() if not v]}")
    return ctx.log(rec)


def chk(rec, name, cond, **info):
    rec["checks"][name] = bool(cond)
    if info:
        rec["info"].update(info)
    return bool(cond)


# ---------------------------------------------------------------- building blocks
def ensure_player(ctx, rec):
    """Navigate to playerScreen only while idle: during playback LEFT selects Stop and UP opens the song list."""
    r = ctx.r
    if r.screen("remote") == "playerScreen":
        return True
    if ctx.playing:
        return chk(rec, "on_player_while_playing", False, screen=r.screen("remote"))
    return chk(rec, "nav_to_player", r.to_player(), screen=r.screen("remote"))


def enter(ctx, code):
    """Digits with human gaps, then the letter with UP presses."""
    r = ctx.r
    digits = [c for c in code if c.isdigit()]
    letter = code[len(digits):]
    ok = r.keys(*digits)
    if letter:
        time.sleep(r.rng.uniform(0.3, 0.7))
        ok = r.keys(*(["UP"] * LETTER_UPS[letter])) and ok
    return ok


def notes_after(ctx, rec, name="no_hanging_notes", settle=0.8):
    time.sleep(settle)
    n = len(ctx.r.tap.notes_on) if ctx.r.tap else 0
    return chk(rec, name, n == 0, **{name + "_n": n})


def _ok_key(ctx, rec):
    t = time.monotonic()
    ok = ctx.r.key("OK")
    chk(rec, "ok_key_acked", ok)
    ks = ctx.r.events(t, "key")
    return ks[-1]["mono"] if ks else t


# ---------------------------------------------------------------- organist actions
def play(ctx, rec, code, expect="play"):
    """expect: play (found, starts), notfound (PC NAKs, nothing plays), refused (remote refuses, nothing is sent)."""
    r = ctx.r
    if not ensure_player(ctx, rec):
        return
    was = ctx.playing
    time.sleep(r.rng.uniform(0.3, 0.8))
    chk(rec, "keys_acked", enter(ctx, code))
    time.sleep(r.rng.uniform(0.3, 0.7))
    t = _ok_key(ctx, rec)
    if expect == "refused":
        # bench 2026-11-15: the remote drops the digit that would exceed 1999 ("2000" -> entry "200"), OK then sends
        # the shorter code (PC: not found). Pass = the out-of-range code itself never reaches the PC.
        e = r.wait_for(pc('PLAY_SONG "'), 2.5, t)
        chk(rec, "no_out_of_range_code", e is None or f'"{code}"' not in e["line"], sent=e and e["line"])
        _back_to_player(ctx, rec)
        return
    e = r.wait_for(pc('PLAY_SONG "'), 3, t)
    chk(rec, "pc_play_song", e is not None and f'"{code}"' in e["line"], line=e and e["line"])
    if expect == "notfound":
        chk(rec, "pc_not_found", r.wait_for(pc(f"Song not found: {code}"), 3, t) is not None)
        chk(rec, "nothing_new_plays", r.wait_for(pc(f"Playing {code}:"), 2, t) is None)
        return
    if was:  # switch-over: the old song must be released before the new one sounds
        st = r.wait_for(pc(f"Stops for {code}"), 8, t)
        chk(rec, "switch_stops_line", st is not None)
        rec["info"]["switch_notes_on"] = len(r.tap.notes_on) if r.tap else None
    e = r.wait_for(pc(f"Playing {code}:"), 9, t)
    chk(rec, "pc_playing", e is not None, key_to_playing_ms=e and round((e["mono"] - t) * 1000))
    if e:
        ctx.playing = code
        ctx.started = e["mono"]
        ctx.verses = 0
        chk(rec, "api_playing", r.wait_api("software.playback", "playing", 3))
        chk(rec, "organ_on", r.wait_api("base.organ_power", True, 6))


def _back_to_player(ctx, rec):
    """after a code that did not start a song: the remote must still be usable (on the player, or navigable back)."""
    time.sleep(1.0)
    scr = ctx.r.screen("remote")
    rec["info"]["screen_after"] = scr
    if scr != "playerScreen" and not ctx.playing:
        chk(rec, "renav_ok", ctx.r.to_player())


def stop(ctx, rec, twice=False):
    r = ctx.r
    if not chk(rec, "was_playing", ctx.playing):
        return
    t = _ok_key(ctx, rec)
    if twice:
        time.sleep(r.rng.uniform(0.05, 0.4))
        r.key("OK")  # second press: in Stopping/Idle with no digits the remote does nothing
    if not chk(rec, "pc_stop_song", r.wait_for(pc("STOP_SONG from"), 3, t) is not None):
        # the remote did not send STOP (e.g. it lost its playing state after a reset, R33): clean up through the CLI so
        # the next action starts idle; the failed check stays in the record
        rec["info"]["fallback"] = "pc_cmd stop"
        rec["info"]["remote_screen"] = r.screen("remote")
        r.pc_cmd("stop")
    e = r.wait_for(pc("Playback stopped."), 4, t)
    chk(rec, "pc_stopped", e is not None, key_to_stopped_ms=e and round((e["mono"] - t) * 1000))
    if twice:
        time.sleep(2.0)
        n = len([x for x in r.events(t, "pc") if "STOP_SONG from" in x["line"]])
        rec["info"]["stop_song_count"] = n
        chk(rec, "no_second_play", r.wait_for(pc("Playing "), 0.1, t) is None)
    ctx.playing = None
    chk(rec, "api_idle", r.wait_api("software.playback", "idle", 3))
    chk(rec, "organ_off", r.wait_api("base.organ_power", False, 7))  # KeepPowerOn=false: off at stop (gap 3)
    notes_after(ctx, rec)


def add_verse(ctx, rec, presses=1, rapid=False):
    """RIGHT selects Repeat (available after the first anchor), OK replays the section once per press."""
    r = ctx.r
    if not chk(rec, "was_playing", ctx.playing):
        return
    t = time.monotonic()
    r.key("RIGHT")
    time.sleep(0.1 if rapid else r.rng.uniform(0.3, 0.6))
    for i in range(presses):
        r.key("OK")
        time.sleep(r.rng.uniform(0.08, 0.2) if rapid else r.rng.uniform(0.4, 0.9))
    time.sleep(1.0)
    n = len([x for x in r.events(t, "pc") if "Add verse:" in x["line"]])
    chk(rec, "pc_add_verse_each_press", n == presses, presses=presses, add_verse_lines=n)
    ctx.verses += n
    r.key("BACK")  # back to Stop so the next OK stops (a human would see the selection)
    time.sleep(0.2)


def wait_end(ctx, rec, extra_s=20.0):
    """Let the song play to its end: section length x (1 + verses added) after Playing, plus margin."""
    r = ctx.r
    if not chk(rec, "was_playing", ctx.playing):
        return
    code = ctx.playing
    left = SONGS.get(code, 45) * (1 + ctx.verses) - (time.monotonic() - ctx.started) + extra_s
    e = r.wait_for(pc("Playback finished"), max(5.0, left), ctx.started)
    chk(rec, "pc_finished", e is not None, played_s=e and round(e["mono"] - ctx.started, 1),
        expected_s=round(SONGS.get(code, 0) * (1 + ctx.verses), 1))
    ctx.playing = None
    if e:
        chk(rec, "api_idle", r.wait_api("software.playback", "idle", 3))
        chk(rec, "organ_off_after_end", r.wait_api("base.organ_power", False, 7))  # gap 3 (SYS-ORG-04 wants it on)
        notes_after(ctx, rec)
        time.sleep(1.5)
        chk(rec, "remote_back_on_player", r.screen("remote") == "playerScreen", screen=r.screen("remote"))


def song_list_and_back(ctx, rec):
    """UP with no digits opens the song list; BACK returns to the player. Playback (if any) continues."""
    r = ctx.r
    if not ensure_player(ctx, rec):
        return
    t = time.monotonic()
    r.key("UP")
    time.sleep(1.2)
    rec["info"]["list_screen"] = r.screen("remote")
    chk(rec, "list_opened", r.screen("remote") not in ("playerScreen", None))
    r.keys("DOWN", "DOWN", gap=(0.3, 0.6))
    time.sleep(0.3)
    r.key("BACK")
    time.sleep(1.2)
    chk(rec, "back_on_player", r.screen("remote") == "playerScreen", screen=r.screen("remote"))
    if ctx.playing:
        chk(rec, "playback_untouched", r.wait_for(pc("Playback stopped."), 0.1, t) is None
            and r.api("software.playback") == "playing")


def wrong_code_corrected(ctx, rec, wrong="409", right="401"):
    """Type a wrong code, BACK over the differing digits, type the rest, OK."""
    r = ctx.r
    if not ensure_player(ctx, rec):
        return
    common = len(os.path.commonprefix([wrong, right]))
    enter(ctx, wrong)
    time.sleep(r.rng.uniform(0.6, 1.2))
    r.keys(*(["BACK"] * (len(wrong) - common)))
    time.sleep(r.rng.uniform(0.3, 0.6))
    enter(ctx, right[common:])
    time.sleep(0.4)
    t = _ok_key(ctx, rec)
    e = r.wait_for(pc('PLAY_SONG "'), 3, t)
    chk(rec, "pc_got_corrected_code", e is not None and f'"{right}"' in e["line"], line=e and e["line"])
    e = r.wait_for(pc(f"Playing {right}:"), 9, t)
    chk(rec, "pc_playing", e is not None)
    if e:
        ctx.playing, ctx.started, ctx.verses = right, e["mono"], 0


# ---------------------------------------------------------------- disturbances (windowed)
def remote_reset(ctx, rec):
    """SYSRESETREQ on the remote over SWD (approximates the remote leaving radio range / losing power).
    Expected while playing: the base sees the link drop and return; the PC keeps playing; the remote comes back on
    mainScreen and the PC resyncs it (SIGNAL_PLAYING + anchor) - R33: a reconnect faster than the 5 s STATUS poll may
    not show ble=0, in which case the remote stays idle until the next anchor (SYS-LINK-06 residual)."""
    r = ctx.r
    r.fast("base", 25)  # down->up of base `connected` can fall between two 1 Hz samples (latency run used fast too)
    time.sleep(0.3)
    t0 = time.monotonic()
    r.ev("window", wkind="remote_reset", start=t0, end=t0 + WINDOW_S["remote_reset"])
    try:
        r.units["remote"].write32(AIRCR, SYSRESETREQ)
    except Exception as e:
        rec["info"]["write_err"] = repr(e)[:120]
    up = r.wait_for(lambda e: e["kind"] == "swd" and e.get("unit") == "base" and "changed" in e
                    and e["changed"].get("connected", [None, None])[1] == 1, 30, t0 + 0.3)
    chk(rec, "base_link_back", up is not None, link_back_s=up and round(up["mono"] - t0, 1))
    rs = r.wait_for(pc("Remote state resynced"), 15, t0)
    rec["info"]["pc_resync_s"] = rs and round(rs["mono"] - t0, 1)
    rec["info"]["pc_resync_line"] = [x["line"] for x in r.events(t0, "pc") if "resync" in x["line"]][:3]
    if ctx.playing:
        chk(rec, "playback_survived", r.wait_for(pc("Playback stopped"), 0.1, t0) is None
            and r.api("software.playback") == "playing")
        time.sleep(2)
        rec["info"]["remote_screen_after"] = r.screen("remote")
        if r.screen("remote") != "playerScreen":  # the organist opens the player again (safe: not on playerScreen)
            rec["info"]["renav"] = r.to_player()
    else:
        time.sleep(3)
        chk(rec, "nav_ok", r.to_player())


def pc_restart(ctx, rec):
    """SIGTERM the PC service and start it again (maintenance). While playing, SYS-PC-05: stop, clear, SIGNAL_STOPPED,
    organ off. After the start: serial open, startup All Notes Off, resync, /api/status answers."""
    r = ctx.r
    t0 = time.monotonic()
    r.ev("window", wkind="pc_restart", start=t0, end=t0 + WINDOW_S["pc_restart"])
    was = ctx.playing
    rc = r.stop_pc()
    chk(rec, "pc_exit_clean", rc == 0, rc=rc)
    ctx.playing = None
    t1 = time.monotonic()
    r.start_pc()
    chk(rec, "serial_open", r.wait_for(pc("Opened serial port"), 15, t1) is not None)
    chk(rec, "startup_notes_off", r.wait_for(pc("Startup: All Notes Off"), 15, t1) is not None)
    rs = r.wait_for(pc("Remote state resynced"), 30, t1)
    chk(rec, "resynced", rs is not None, resync_s=rs and round(rs["mono"] - t1, 1))
    end, okapi = time.monotonic() + 10, False
    while time.monotonic() < end and not okapi:
        try:
            okapi = r.api_get("/api/status", timeout=1).get("schema") == 2
        except Exception:
            time.sleep(0.2)
    chk(rec, "api_back", okapi)
    if was:
        notes_after(ctx, rec, settle=0.2)
        time.sleep(2)
        rec["info"]["remote_screen_after"] = r.screen("remote")


# ---------------------------------------------------------------- maintenance
def web_visit(ctx, rec):
    """What a browser does on the home view: the page, then status/diag/stops polling, 5 rounds at 1 s."""
    r = ctx.r
    t = time.monotonic()
    try:
        page = urllib.request.urlopen(r.api_url + "/", timeout=3).read()
        chk(rec, "page_200", len(page) > 1000, page_bytes=len(page))
    except Exception as e:
        chk(rec, "page_200", False, err=repr(e)[:120])
    fails = 0
    for _ in range(5):
        for p in ("/api/status", "/api/diag?limit=50", "/api/stops"):
            try:
                r.api_get(p, timeout=3)
            except Exception:
                fails += 1
        time.sleep(1)
    chk(rec, "api_all_answered", fails == 0, api_fails=fails)
    if ctx.playing:
        chk(rec, "playback_untouched", r.wait_for(pc("Playback stopped"), 0.1, t) is None)


def stops_edit(ctx, rec, code="407"):
    """PUT /api/stops/<code>, read back, play nothing, restore the previous value (null = not set)."""
    r = ctx.r
    before = {e["code"]: e for e in r.api_get("/api/stops").get("songs", [])}.get(code)
    old = before.get("stops") if before else None
    new = [2, 4, 6] if old != [2, 4, 6] else [1, 3]

    def put(v):
        req = urllib.request.Request(f"{r.api_url}/api/stops/{code}", data=json.dumps({"stops": v}).encode(),
                                     method="PUT", headers={"Content-Type": "application/json"})
        return urllib.request.urlopen(req, timeout=3).status
    chk(rec, "put_200", put(new) == 200)
    after = {e["code"]: e for e in r.api_get("/api/stops").get("songs", [])}.get(code) or {}
    chk(rec, "read_back", after.get("stops") == new, old=old, new=new, got=after.get("stops"))
    chk(rec, "restore_200", put(old) == 200)
    again = {e["code"]: e for e in r.api_get("/api/stops").get("songs", [])}.get(code) or {}
    chk(rec, "restored", again.get("stops") == old)


def record_smoke(ctx, rec, seconds=5.0):
    """`record` then `stop` over the control socket: arecordmidi starts on the tap's port, writes a file, stops cleanly.
    Content cannot be checked: organ -> base DIN input is dead on the bench (gap 28), nothing arrives to record."""
    r = ctx.r
    t = time.monotonic()
    res = r.pc_cmd("record")
    chk(rec, "record_cmd_ok", res.returncode == 0, out=res.stdout[-200:])
    e = r.wait_for(pc("Recording to "), 10, t)
    chk(rec, "recording_started", e is not None)
    path = e["line"].split("Recording to ", 1)[1].strip() if e else None
    time.sleep(seconds)
    res = r.pc_cmd("stop")
    chk(rec, "stop_cmd_ok", res.returncode == 0)
    time.sleep(1.5)
    chk(rec, "no_arecordmidi_left", not _pids("arecordmidi"))
    chk(rec, "file_written", bool(path) and os.path.exists(path), path=path,
        size=path and os.path.exists(path) and os.path.getsize(path))
    chk(rec, "organ_off", r.wait_api("base.organ_power", False, 7))


def _pids(name):
    out = []
    for p in os.listdir("/proc"):
        if p.isdigit():
            try:
                if open(f"/proc/{p}/comm").read().strip() == name:
                    out.append(int(p))
            except OSError:
                pass
    return out


def idle(ctx, rec, seconds):
    time.sleep(seconds)
    chk(rec, "slept", True)
