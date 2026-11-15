import re
import time

from fakebase import MT, REMOTE, BASE


def note_ons(events):
    return [(t, b[1]) for t, b in events if len(b) == 3 and b[0] & 0xF0 == 0x90 and b[2] > 0]


def all_notes_off_groups(events):
    """Channels that got CC123 (All Notes Off), as a list of sets, split where a note or other event intervenes."""
    groups, cur = [], set()
    for _, b in events:
        if len(b) == 3 and b[0] & 0xF0 == 0xB0 and b[1] == 123:
            cur.add(b[0] & 0x0F)
        elif cur and not (len(b) == 3 and b[0] & 0xF0 == 0xB0 and b[1] == 120):
            groups.append(cur)
            cur = set()
    if cur:
        groups.append(cur)
    return groups


def labels(base):
    """SIGNAL_ANCHOR labels the PC sent to the remote, in order."""
    return [p.payload.rstrip(b"\0").decode() for _, p in base.received(MT.SIGNAL_ANCHOR, dst=REMOTE)]


def types_in_order(base, kinds):
    return [p.mtype for _, p in base.rx if p.mtype in kinds]


def wait_until(pred, timeout, step=0.05):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        r = pred()
        if r:
            return r
        time.sleep(step)
    return None


def wait_connected(rig, timeout=8):
    return wait_until(lambda: rig.base.received(MT.GET_STATUS), timeout)
