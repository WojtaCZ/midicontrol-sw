# systest: system test tiers

| Tier | Marker | Needs | Run |
|---|---|---|---|
| offline | `offline` | nothing (harness self-tests, codec) | `.venv/bin/pytest -m offline` |
| virtual | `virtual` | built `midicontrol`, `/dev/snd/seq` | `.venv/bin/pytest -m "virtual and not slow"` |
| virtual, slow | `virtual and slow` | same, adds ~25 s | `.venv/bin/pytest -m "virtual and slow"` |
| bench | `bench` | real hardware, `SYSTEST_BENCH=1` (skipped otherwise; owned by the bench agent) | `SYSTEST_BENCH=1 .venv/bin/pytest -m bench` |
| burn-in | (separate runner, see audit/DESIGN.md sec. 7) | bench, hours | not in this directory yet |

Setup: `uv venv .venv && uv pip install pytest pyserial` (set `UV_CACHE_DIR` if the default cache is not writable).

## How the virtual tier works
The real `midicontrol` binary is started with `MIDICONTROL_SYSFS_ROOT` / `MIDICONTROL_DEV_ROOT` pointing at a temp
tree (`fakebase.FakeTree`) in which `ttyACM0` is an ABCD:1234 device whose node is a pty. `fakebase.FakeBase` is the
base on the other end of the pty, using the firmware repo's `proto.py` codec. `MIDICONTROL_MIDI_KEY=systest-<pid>`
makes the PC pick the ALSA seq capture client (`alsaseq.SeqClient`) instead of the real organ. Each instance gets its
own `XDG_*`/`HOME`, config and a web port from 28130-28139 (28120 is the bench). `pcproc.start()` refuses a binary
without the seam strings so a stale build can never open real `/dev/ttyACM*`.

## Environment
- `SYSTEST_BASETEST`: directory with the firmware `proto.py` (default: the main checkout's `midicontrol-fw/tools/basetest`).
- `SYSTEST_PC_BIN`: PC binary (default `../../build/midicontrol`).
- `SYSTEST_BENCH=1`: enable bench tests.

## Requirement traceability
Tests carry `@pytest.mark.req("SYS-...")` with IDs as written in `docs/SYSTEM.md`. `python matrix.py` prints the rows
to merge into `audit/test-matrix.md`; `python matrix.py --check` fails on IDs the matrix lacks (`HARNESS-*` ignored).

## Not here (yet)
Fake bootloader, UI simulators and fuzzing live in other branches (audit/step3-sim, audit/step3-fuzz).
