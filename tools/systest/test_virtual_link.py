"""Virtual tier, link behaviour: keepalive cadence, loss detection and recovery, USB loss."""
import time

import pytest

from fakebase import MT
from helpers import wait_connected, wait_until

pytestmark = [pytest.mark.virtual, pytest.mark.slow]


def _base_fields(rig):
    return rig.pc.status()["nodes"]["base"]["fields"]


# Q3-1: SYSTEM.md has no ID for "PC detects keepalive loss"; SYS-PC-04 (reconnect) is the nearest.
@pytest.mark.req("SYS-PC-04")
def test_keepalive_cadence_loss_and_recovery(make_rig):
    rig = make_rig()
    assert wait_connected(rig)
    time.sleep(7)
    ts = [t for t, _ in rig.base.received(MT.KEEPALIVE)]
    gaps = [b - a for a, b in zip(ts, ts[1:])]
    assert len(gaps) >= 2 and all(1.7 <= g <= 2.4 for g in gaps), gaps
    assert _base_fields(rig)["alive"] is True
    rig.base.silent = True
    assert wait_until(lambda: _base_fields(rig)["alive"] is False, 10), rig.pc.log()
    assert "not answering keepalives" in rig.pc.log()
    rig.base.silent = False
    assert wait_until(lambda: _base_fields(rig)["alive"] is True, 8), rig.pc.log()


@pytest.mark.req("SYS-PC-04")
def test_usb_loss_and_reconnect_reruns_startup_reset(make_rig):
    rig = make_rig()
    assert wait_until(lambda: rig.base.received(MT.SET_CURRENT), 8)
    n_off = len(rig.base.received(MT.SET_CURRENT))
    rig.base.vanish()
    assert wait_until(lambda: _base_fields(rig)["connected"] is False, 12), rig.pc.log()
    rig.seq.clear()
    rig.base.restore()
    assert wait_until(lambda: _base_fields(rig)["connected"] is True, 15), rig.pc.log()
    assert wait_until(lambda: len(rig.base.received(MT.SET_CURRENT)) > n_off, 8)
