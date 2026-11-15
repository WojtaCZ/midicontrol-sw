import os
import shutil
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def pytest_configure(config):
    # markers live in pytest.ini; this keeps `req` valid even when run from another rootdir
    config.addinivalue_line("markers", "req(id): requirement ID exactly as docs/SYSTEM.md writes it")


def pytest_collection_modifyitems(config, items):
    import pcproc
    for item in items:
        if item.get_closest_marker("virtual") and not os.path.exists(pcproc.PC_BIN):
            item.add_marker(pytest.mark.skip(reason=f"PC binary not built: {pcproc.PC_BIN} (set SYSTEST_PC_BIN)"))
        if item.get_closest_marker("virtual") and not os.path.exists("/dev/snd/seq"):
            item.add_marker(pytest.mark.skip(reason="no ALSA sequencer (/dev/snd/seq)"))
        if item.get_closest_marker("virtual") and not shutil.which("arecordmidi"):
            pass  # only the recording tests need it; they skip themselves
        if item.get_closest_marker("bench") and not os.environ.get("SYSTEST_BENCH"):
            item.add_marker(pytest.mark.skip(reason="bench tier: set SYSTEST_BENCH=1 (needs both units, bench owner only)"))


@pytest.fixture(scope="session")
def seq():
    import alsaseq
    c = alsaseq.SeqClient()
    yield c
    c.close()


class Rig:
    def __init__(self, tree, base, pc, seq):
        self.tree, self.base, self.pc, self.seq = tree, base, pc, seq


@pytest.fixture
def make_rig(seq):
    """Factory: make_rig(songs=[(bytes, "401.mid")], warmup_ms=200) -> Rig with a running PC on a fake base."""
    made = []

    def make(songs=(), warmup_ms=200, extra_conf="", start_base=True, start_pc=True):
        import fakebase
        import pcproc
        seq.clear()
        tree = fakebase.FakeTree()
        base = fakebase.FakeBase(tree)
        if start_base:
            base.start()
        pc = pcproc.PcInstance(tree, seq.key, warmup_ms=warmup_ms, extra_conf=extra_conf, songs=songs)
        if start_pc:
            pc.start()
        made.append((tree, base, pc))
        return Rig(tree, base, pc, seq)

    yield make
    for tree, base, pc in made:
        pc.stop()
        base.stop()
        tree.cleanup()
        shutil.rmtree(pc.tmp, ignore_errors=True)
