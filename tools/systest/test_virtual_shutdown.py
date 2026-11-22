"""SYS-PC-05 end to end (B6, B10): every way the daemon is told to end (SIGTERM, SIGINT, SIGHUP, interactive
quit, EOF, and Ctrl-C in interactive mode) runs the same shutdown: stop playback, clear the display, SIGNAL_STOPPED,
organ off, exit 0."""
import os
import signal
import time

import pytest

import smf
from fakebase import MT
from helpers import note_ons, wait_connected, wait_until

pytestmark = pytest.mark.virtual
SONG = lambda **kw: [(smf.build_song(401, **kw), "401.mid")]


def _playing_rig(make_rig, interactive=False):
    rig = make_rig(songs=SONG(verses=6, beats_per_section=2), start_pc=False)
    rig.pc.start(interactive=interactive)
    assert wait_connected(rig), rig.pc.log()
    rig.base.play("401")
    assert wait_until(lambda: note_ons(rig.seq.snapshot()), 8), rig.pc.log()
    return rig


def _assert_clean_shutdown(rig, t0):
    assert rig.pc.proc.wait(10) == 0, rig.pc.log()
    assert "Done." in rig.pc.log()
    assert rig.base.received(MT.SIGNAL_STOPPED), "remote was not told the song stopped"
    assert rig.base.received(MT.SET_CURRENT)[-1][1].payload == b"\x00"   # organ off, last
    clears = [p for _, p in rig.base.received(MT.NOW_PLAYING_INFO) if p.payload is not None]
    assert clears, "no display frame"
    assert not [n for t, n in note_ons(rig.seq.snapshot()) if t > t0 + 1.5]   # nothing plays after the end


@pytest.mark.req("SYS-PC-05")
@pytest.mark.parametrize("sig", [signal.SIGTERM, signal.SIGINT, signal.SIGHUP])
def test_signal_shutdown_while_playing(make_rig, sig):
    rig = _playing_rig(make_rig)
    t0 = time.monotonic()
    rig.pc.proc.send_signal(sig)
    _assert_clean_shutdown(rig, t0)


@pytest.mark.req("SYS-PC-05")
def test_interactive_quit_while_playing(make_rig):
    rig = _playing_rig(make_rig, interactive=True)
    t0 = time.monotonic()
    os.write(rig.pc.tty_master, b"quit\n")
    _assert_clean_shutdown(rig, t0)


@pytest.mark.req("SYS-PC-05")
def test_interactive_eof_while_playing(make_rig):
    rig = _playing_rig(make_rig, interactive=True)
    t0 = time.monotonic()
    os.write(rig.pc.tty_master, b"\x04")   # Ctrl-D on the line discipline
    _assert_clean_shutdown(rig, t0)


@pytest.mark.req("SYS-PC-05")
@pytest.mark.parametrize("sig", [signal.SIGHUP, signal.SIGINT])
def test_interactive_signal_while_playing(make_rig, sig):
    rig = _playing_rig(make_rig, interactive=True)
    t0 = time.monotonic()
    rig.pc.proc.send_signal(sig)
    _assert_clean_shutdown(rig, t0)
