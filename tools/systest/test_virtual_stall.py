"""Virtual tier, B13: a stalled playback thread must not leave notes sounding (owner: silent within 10 s).

Only the playback thread ("mc-play") is stopped, with ptrace from this process (the PC is our child, so
yama ptrace_scope 1 allows it); every other thread keeps running, like a hang inside the MIDI stack."""
import ctypes
import os
import time

import pytest

import smf
from helpers import all_notes_off_groups, note_ons, wait_connected, wait_until

pytestmark = pytest.mark.virtual

PTRACE_DETACH, PTRACE_SEIZE, PTRACE_INTERRUPT = 17, 0x4206, 0x4207
WALL_FLAG = 0x40000000
_libc = ctypes.CDLL(None, use_errno=True)
_libc.ptrace.argtypes = [ctypes.c_long, ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p]


def _thread_by_name(pid, name):
    for tid in os.listdir(f"/proc/{pid}/task"):
        try:
            with open(f"/proc/{pid}/task/{tid}/comm") as f:
                if f.read().strip() == name:
                    return int(tid)
        except OSError:
            pass
    return None


class Frozen:
    """Stops one thread until close()."""

    def __init__(self, tid):
        self.tid = tid
        if _libc.ptrace(PTRACE_SEIZE, tid, None, None) != 0:
            raise OSError(ctypes.get_errno(), "PTRACE_SEIZE")
        if _libc.ptrace(PTRACE_INTERRUPT, tid, None, None) != 0:
            raise OSError(ctypes.get_errno(), "PTRACE_INTERRUPT")
        os.waitpid(tid, WALL_FLAG)
        self.at = time.monotonic()

    def reap(self):
        """The process exited: collect the traced thread, or the process can never be reaped."""
        try:
            os.waitpid(self.tid, os.WNOHANG | WALL_FLAG)
        except ChildProcessError:
            pass

    def close(self):
        if _libc.ptrace(PTRACE_DETACH, self.tid, None, None) != 0:
            self.reap()


def _start_long_note(make_rig, exit_on_stall):
    # One 20 s section per note (0.5 s per beat), so the note is held while the thread is frozen.
    rig = make_rig(songs=[(smf.build_song(401, verses=1, beats_per_section=40), "401.mid")], start_pc=False)
    if exit_on_stall:
        rig.pc.env["MIDICONTROL_EXIT_ON_STALL"] = "1"
    rig.pc.env.pop("NOTIFY_SOCKET", None)
    rig.pc.start()
    assert wait_connected(rig), rig.pc.log()
    rig.base.play("401")
    assert wait_until(lambda: note_ons(rig.seq.snapshot()) or None, 8), rig.pc.log()
    tid = wait_until(lambda: _thread_by_name(rig.pc.pid, "mc-play"), 2)
    assert tid, "no mc-play thread"
    return rig, tid


def _notes_off_after(rig, t0):
    ev = [e for e in rig.seq.snapshot() if e[0] >= t0]
    groups = all_notes_off_groups(ev)
    if groups and groups[0] == set(range(16)):
        return [t for t, b in ev if len(b) == 3 and b[0] & 0xF0 == 0xB0 and b[1] == 123][0]
    return None


@pytest.mark.req("SYS-NOTE-04")
def test_playback_stall_sends_all_notes_off(make_rig):
    rig, tid = _start_long_note(make_rig, exit_on_stall=False)
    fr = Frozen(tid)
    try:
        t_off = wait_until(lambda: _notes_off_after(rig, fr.at), 6)
        assert t_off, rig.pc.log()
        assert t_off - fr.at < 4.0, t_off - fr.at   # 2 s limit + 0.5 s poll + slack
        assert "playback thread stalled" in rig.pc.log()
        assert rig.pc.proc.poll() is None           # no exit without --exit-on-stall
        # once per stall episode: no second burst while still frozen
        time.sleep(2)
        assert len(all_notes_off_groups([e for e in rig.seq.snapshot() if e[0] >= fr.at])) == 1
    finally:
        fr.close()


@pytest.mark.req("SYS-NOTE-04")
def test_playback_stall_exits_in_container_mode(make_rig):
    rig, tid = _start_long_note(make_rig, exit_on_stall=True)
    fr = Frozen(tid)
    try:
        t_off = wait_until(lambda: _notes_off_after(rig, fr.at), 6)
        assert t_off, rig.pc.log()
        def exited():
            fr.reap()
            return rig.pc.proc.poll() is not None
        assert wait_until(exited, 3), rig.pc.log()
        assert rig.pc.proc.returncode == 70
        assert time.monotonic() - fr.at < 10
        assert "exiting for a restart (playback stall)" in rig.pc.log()
    finally:
        fr.close()


def test_normal_long_note_is_not_a_stall(make_rig):
    rig, tid = _start_long_note(make_rig, exit_on_stall=True)
    t0 = time.monotonic()
    time.sleep(5)   # a held note: the thread waits in 10 ms slices and keeps beating
    assert _notes_off_after(rig, t0) is None, rig.pc.log()
    assert rig.pc.proc.poll() is None
    assert "stalled" not in rig.pc.log()
