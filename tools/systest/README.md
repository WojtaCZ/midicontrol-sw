# systest: system test tiers

| Tier | Marker | Needs | Run |
|---|---|---|---|
| offline | `offline` | nothing (harness self-tests, codec) | `.venv/bin/pytest -m offline` |
| virtual | `virtual` | built `midicontrol`, `/dev/snd/seq` | `.venv/bin/pytest -m "virtual and not slow"` |
| virtual, slow | `virtual and slow` | same, adds ~25 s | `.venv/bin/pytest -m "virtual and slow"` |
| bench | (scripts in `bench/`, not pytest) | base + remote on SWD (openocd, STLINK-V3 + ST-LINK/V2), base USB, PC build | `bench/latency.py`, `bench/scenarios.py`, see below |
| burn-in | (script) | bench, 4-8 h | `bench/burnin.py`, then `bench/criteria.py <rundir>` |

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

## Bench and burn-in tier (`bench/`)
All bench scripts drive `rig.Rig`: the PC app under a MIDI tap, the API poller and a 1 Hz non-halting SWD sampler of
both units (`swd.py`, addresses from the flashed ELFs in `$SYSTEST_ELFDIR`). Remote keys go through a RAM key mailbox
over SWD, not physical buttons. Every run writes a run directory (timeline.jsonl, swd.jsonl, api.jsonl, ...); keep run
directories outside git (the audit uses `audit/runs/`).

| script | what | run |
|---|---|---|
| `latency.py` | play / remote reset / PC restart trials -> latency.jsonl + latency.md | `latency.py <rundir> <pcbin> <config> --play 30 --resets 10 --pcrestarts 10` |
| `scenarios.py` | user-like sessions (sunday, mistakes, recording, maintenance) -> scenarios.md | `scenarios.py <rundir> <pcbin> <src-run> --runs 3` |
| `burnin.py` | seeded weighted random mix of `actions.py`; summary.json every 30 min | `burnin.py <rundir> <pcbin> <src-run> --hours 8 --seed 29 [--pilot]` |
| `criteria.py` | evaluates `criteria.toml` (LAT-*, BURN-*) on a run dir; exit 1 on a hard fail | `criteria.py <rundir>`; `criteria.py --selftest` offline |

Stop a burn-in with `touch <rundir>/STOP` or SIGTERM the pid in `<rundir>/burnin.pid`; the final summary is written
either way. Fix `criteria.toml` before a run: its sha256 goes into the result. Two things to know:
- The remote OLED sleeps after 30 s idle, and the next key only wakes it. `rig.key()` checks `Oled::sleeping` over
  SWD and sends BACK first (`key_stats.wakes`).
- Never use halting SWD (gdb) during a run. Long halting batches cause the BE-1 zombie link (harness artefact).

## Not here (yet)
Fake bootloader, UI simulators and fuzzing live in other branches (audit/step3-sim, audit/step3-fuzz).
