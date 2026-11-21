"""Offline tier: harness self-tests, no PC binary needed."""
import pytest

import fakebase
from fakebase import MT, PC, BASE

pytestmark = pytest.mark.offline


def test_codec_roundtrip_and_crc():
    p = fakebase.proto
    frame = p.encode(PC, BASE, 7, int(MT.KEEPALIVE), b"")
    assert frame[0] == p.SOH and frame[-1] == p.EOT
    bad = bytearray(frame)
    bad[-2] ^= 0x55
    assert bytes(bad) != frame


def test_fake_tree_hides_and_shows_tty(tmp_path):
    t = fakebase.FakeTree(str(tmp_path))
    env = t.env()
    assert env["MIDICONTROL_SYSFS_ROOT"].startswith(str(tmp_path))
    assert env["MIDICONTROL_DEV_ROOT"].startswith(str(tmp_path))
