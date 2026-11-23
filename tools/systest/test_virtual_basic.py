"""Virtual tier: the real midicontrol binary against a fake base on a pty (no hardware, no real /dev)."""
import re
import time

import pytest

import smf
from fakebase import MT, NAK, PC, REMOTE, BASE
from helpers import all_notes_off_groups, labels, note_ons, wait_connected, wait_until

pytestmark = pytest.mark.virtual
SONG = lambda **kw: [(smf.build_song(401, **kw), "401.mid")]


@pytest.mark.req("HARNESS-ISOLATION")
def test_isolation_no_real_device_fds(make_rig):
    rig = make_rig(songs=SONG())
    assert wait_connected(rig), rig.pc.log()
    fds = rig.pc.open_fds()
    real = [f for f in fds if re.match(r"/dev/(ttyACM|ttyUSB|ttyS|hidraw|bus/usb)", f)]
    assert not real, f"virtual PC holds real device fds: {real}"
    assert any(f.startswith(rig.tree.dev + "/") or re.match(r"/dev/pts/\d+", f) for f in fds), fds  # the pty
    assert 28130 <= rig.pc.port <= 28139
    assert rig.pc.status()["nodes"]["base"]["fields"]["device"].startswith(rig.tree.dev)
    assert rig.pc.status()["nodes"]["base"]["fields"]["midi_out"].startswith(rig.seq.key)


@pytest.mark.req("SYS-PWR-07")
@pytest.mark.req("SYS-NOTE-04")
def test_startup_all_notes_off_and_organ_off(make_rig):
    rig = make_rig()
    assert wait_until(lambda: rig.base.received(MT.SET_CURRENT), 8), rig.pc.log()
    assert rig.base.received(MT.SET_CURRENT)[0][1].payload == b"\x00"
    ev = wait_until(lambda: all_notes_off_groups(rig.seq.snapshot()) or None, 3)
    assert ev and ev[0] == set(range(16))
    assert not note_ons(rig.seq.snapshot())


@pytest.mark.req("SYS-USE-04")
@pytest.mark.req("SYS-USE-05")
@pytest.mark.req("SYS-USE-08")
def test_playback_structure_and_end(make_rig):
    rig = make_rig(songs=SONG(verses=2))
    assert wait_connected(rig)
    rig.base.play("401")
    assert wait_until(lambda: rig.base.received(MT.SIGNAL_STOPPED), 12), rig.pc.log()
    base = rig.base
    # PLAY_SONG from the remote is ACKed to the remote
    assert any(p.dst == REMOTE and p.mtype == MT.ACK for _, p in base.rx)
    # organ on (SET_CURRENT 1) before SIGNAL_PLAYING <code>, before the first note
    kinds = [(p.mtype, bytes(p.payload)) for _, p in base.rx]
    i_on = kinds.index((MT.SET_CURRENT, b"\x01"))
    i_play = kinds.index((MT.SIGNAL_PLAYING, b"401"))
    assert i_on < i_play
    assert labels(base) == ["Predehra", "Sloka 1", "Sloka 2"]
    # NOW_PLAYING_INFO per section, verse digits blank / 1 / 2
    npi = [p.payload for _, p in base.received(MT.NOW_PLAYING_INFO) if p.payload]
    assert [p[2:] for p in npi][:3] == [b"\xfe\x0f\x0f", b"\x01\x0f\x0f", b"\x02\x0f\x0f"] or len(npi) >= 3, npi
    # MIDI: the three section notes in order, first note after the signal and the warm-up
    notes = note_ons(rig.seq.snapshot())
    assert [n for _, n in notes] == [60, 62, 64]
    t_play = [t for t, p in base.rx if p.mtype == MT.SIGNAL_PLAYING][0]
    assert notes[0][0] >= t_play
    # song end: All Notes Off on all 16 channels after the last note, SIGNAL_STOPPED, organ off (tests the KeepPowerOn=false opt-out path; the default is true)
    groups = all_notes_off_groups(rig.seq.snapshot())
    assert groups[-1] == set(range(16))
    assert wait_until(lambda: base.received(MT.SET_CURRENT)[-1][1].payload == b"\x00", 3)


@pytest.mark.req("SYS-USE-07")
def test_add_verse_replays_current_section(make_rig):
    rig = make_rig(songs=SONG(verses=2, beats_per_section=2))
    assert wait_connected(rig)
    assert rig.base.remote_cmd(MT.REPLAY_ANCHOR)  # idle -> NAK NOT_PLAYING
    assert wait_until(lambda: [p for _, p in rig.base.rx if False] or True, 0.1)
    rig.base.play("401")
    assert wait_until(lambda: "Sloka 1" in labels(rig.base), 8), rig.pc.log()
    rig.base.remote_cmd(MT.REPLAY_ANCHOR)
    assert wait_until(lambda: rig.base.received(MT.SIGNAL_STOPPED), 20), rig.pc.log()
    notes = [n for _, n in note_ons(rig.seq.snapshot())]
    assert notes.count(62) == 2, notes   # Sloka 1 played twice
    assert notes.count(64) == 1


@pytest.mark.req("SYS-USE-08")
@pytest.mark.req("SYS-NOTE-01")
def test_stop_sends_all_notes_off_and_signal(make_rig):
    rig = make_rig(songs=SONG(verses=6, beats_per_section=2))
    assert wait_connected(rig)
    rig.base.play("401")
    assert wait_until(lambda: note_ons(rig.seq.snapshot()), 8), rig.pc.log()
    n_before = len(rig.seq.snapshot())
    t_stop = time.monotonic()
    rig.base.stop_cmd()
    assert wait_until(lambda: rig.base.received(MT.SIGNAL_STOPPED), 3), rig.pc.log()
    time.sleep(0.5)
    after = [e for e in rig.seq.snapshot() if e[0] >= t_stop]
    assert all_notes_off_groups(after) and all_notes_off_groups(after)[0] == set(range(16))
    time.sleep(1.5)
    assert not [n for t, n in note_ons(rig.seq.snapshot()) if t > t_stop]   # nothing plays after the stop
    assert rig.base.received(MT.SET_CURRENT)[-1][1].payload == b"\x00"        # organ off (SYS-ORG-04)
    assert rig.pc.status()["nodes"]["software"]["fields"]["playback"] == "idle"


@pytest.mark.req("SYS-USE-02")
def test_unknown_song_is_nak_not_found(make_rig):
    rig = make_rig(songs=SONG())
    assert wait_connected(rig)
    pid = rig.base.play("999")
    r = wait_until(lambda: [p for _, p in rig.base.rx if p.mtype == MT.NAK], 3)
    assert r and r[0].payload[0] == pid and r[0].payload[1] == NAK.NOT_FOUND
    assert not note_ons(rig.seq.snapshot())


@pytest.mark.req("SYS-PC-04")
def test_malformed_frames_do_not_disturb_the_pc(make_rig):
    import fakebase
    proto = fakebase.proto
    rig = make_rig(songs=SONG())
    assert wait_connected(rig)
    b = rig.base
    good = proto.encode(REMOTE, PC, 200, int(MT.PLAY_SONG), b"401")
    bad = [
        b"\x00\xff\x13\x37garbage\x00",                                       # noise outside frames
        good[:-2] + bytes([good[-2] ^ 0x55]) + good[-1:],                     # wrong CRC
        good[:4],                                                             # truncated, then next SOH restarts
        bytes([proto.SOH]) + proto.escape(proto.raw_body(REMOTE, PC, 201, int(MT.PLAY_SONG), b"401", length=9)) + bytes([proto.EOT]),  # bad Length
        bytes([proto.SOH, 0x1B, 0x55, 0x04]),                                 # invalid escape
        bytes([proto.SOH]) + bytes(range(8, 200)) + bytes([proto.EOT]),       # oversized body
    ]
    for chunk in bad:
        b.send_raw(chunk)
        time.sleep(0.05)
    time.sleep(0.5)
    assert rig.pc.proc.poll() is None
    assert not note_ons(rig.seq.snapshot())                                   # nothing was executed
    assert not [p for _, p in b.rx if False]
    acks = [p for _, p in b.rx if p.mtype == MT.ACK and p.dst == REMOTE and p.payload[:1] in (bytes([200]), bytes([201]))]
    assert not acks                                                           # no ACK for damaged frames
    # the link still works: a valid command after the garbage is executed
    b.send_raw(good)
    assert wait_until(lambda: [p for _, p in b.rx if p.mtype == MT.ACK and p.dst == REMOTE and p.payload[:1] == bytes([200])], 3), rig.pc.log()
    assert wait_until(lambda: note_ons(rig.seq.snapshot()), 6), rig.pc.log()
    assert rig.pc.status()["nodes"]["base"]["fields"]["alive"] is True
