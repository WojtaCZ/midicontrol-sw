# systest: MIDIcontrol system test harness, design

Written 2026-11-15 (audit step 3.1.3, plan `plans/2026-11-14-system-audit-fable.md` in the workspace). Facts behind the
choices are in the workspace file `audit/findings/step3/harness-research.md`.

## Goal

One requirement-traced harness that tests PC + base + remote together, in four tiers:

| Tier | Needs | Runs | Where |
|---|---|---|---|
| offline | nothing | harness self-tests (frame decode, timeline, criteria) | `pytest -m offline` |
| virtual | PC build only | the real `midicontrol` binary against a fake base on a pty, MIDI captured on an ALSA seq port | `pytest -m virtual` |
| bench | both units + probes | user-like scenarios through the real PC service; observation over log CDC, web API, SWD (non-halting) | `pytest -m bench` / `run_scenario.py` |
| burn-in | bench, hours | seeded random mix of scenario actions, checkpointed summary, pass criteria fixed beforehand | `burnin.py` |

Device UI simulation and fuzzing are separate host builds in the firmware / PC repos (sec. 5, 6); the systest README points at them.

## 1. Location and reuse (decision)

`midicontrol-sw/tools/systest/`, Python + pytest.

- The PC repo holds the system spec (`docs/SYSTEM.md`), so requirement IDs and tests live next to each other.
- basetest modules (`usblink`, `loglink`, `dbg`, `proto`, `fwclient`) are imported **by path**, not copied:
  `SYSTEST_BASETEST` (default `../../../midicontrol-fw/tools/basetest` relative to this dir, i.e. the sibling checkout).
  A missing sibling skips the bench tier with a clear reason; offline and virtual tiers don't need it except `proto`
  (frame codec), which the virtual fake base also uses.
- Own venv: `uv venv .venv && uv pip install pytest pyserial` (system python has no pytest).
- Rejected: putting it in `midicontrol-fw/tools/basetest` (that is a firmware-unit harness; the system view and the PC
  service belong with the PC), copying the modules (drift).

## 2. Virtual tier: PC binary vs fake base

- **Seam needed in the PC (small source change)**: the binary finds the base only through a hard-coded `/sys` walk.
  Add test-only environment overrides (not config keys, nothing user-visible; documented in the PC CLAUDE.md test
  section):
  - `MIDICONTROL_SYSFS_ROOT`: used by **every** sysfs lookup: `findMidiControlPort`, `findMidiControlLogPort`,
    `baseInBootloader` (the `_bootloaderProbe`) and `usbIdOf` (serial_port.cpp:236 has a literal `"/sys"`).
  - `MIDICONTROL_DEV_ROOT`: prefix for the `/dev/<name>` paths those functions return (the fake tree points at ptys).
  - `MIDICONTROL_MIDI_KEY`: the name key passed to `findMidiControlPorts` (default "MIDIControl"); it also picks the
    `arecordmidi` input port, so recording tests record the harness client, never the real base.
  Isolation test (required): with the env set, the binary opens nothing under the real `/dev` (checked through
  `/proc/<pid>/fd`), so the virtual tier cannot take the log CDC away from a bench run (TIOCEXCL + flock). The same
  seam lets the fake bootloader switch identity `abcd:1234 -> 1235 -> 1234` by rewriting the fake sysfs tree.
- **Fake base** (`fakebase.py`): pty master + the `proto` codec; answers KEEPALIVE, GET_STATUS/STATUS_RESP, GET_INFO
  TLVs, DIAG, SET_CURRENT (models the SSR), ACK/dedup by PacketId, router NAK UNREACHABLE when the "remote" is absent,
  relays remote commands (PLAY_SONG, STOP_SONG, ADD_VERSE...) as a "remote" would send them. Knobs to drop / delay /
  corrupt / reorder frames, go silent, vanish (close pty + remove sysfs node), come back.
- **Fake bootloader** (`fakeboot.py`): same pty, bootloader message set (FW_UPDATE_START/DATA/END/STATUS), verifies
  CRC32 and size, can inject CRC_FAIL / WRITE_ERROR / timeout.
- **MIDI capture**: an ALSA sequencer client created by the harness, named `systest-<pid>` (never containing
  "MIDIControl", so the bench service's last-hit name match cannot pick it), with one input and one output port; the
  virtual PC instance gets `MIDICONTROL_MIDI_KEY=systest-<pid>`. Timestamps from the seq event queue.
- **Isolation**: each test instance gets its own `XDG_RUNTIME_DIR`, `--config`, song/record/diag folders in a tmp dir,
  `[WEB] Port` from `$DEV_PORT_RANGE` (never the bench instance's). The virtual tier never opens a real tty, so it
  can run in parallel with bench work.
- Tests check: playback structure (prelude, verses, add verse, verse gap), stops before the first note, indicator SysEx
  anchors, pause/stop/All Notes Off, PC restart (gap 2), recording naming and trimming, malformed/out-of-order frames,
  link loss and recovery, firmware update vs fake bootloader. Keepalive (2 s / 6 s) and idle-off are hard-coded: those
  tests run in real time and are marked `slow` (no new timing config key: that would be user-visible).

## 3. Bench tier

### Observation (all into one JSONL timeline per run, monotonic + wall clock)
- base log CDC (`loglink`), PC service stdout/journal, PC diag store;
- `GET /api/status` and `/api/diag` at 1 Hz;
- SWD, **non-halting only**, switchable (`--swd off|1hz`; BE-1 arm B decides whether 1 Hz is safe for hours; the
  reader comes from the BE-1 toolkit `audit/runs/2026-11-15-be1/tools/`, moved here, not rewritten). An attached probe
  changes the remote's sleep / auto-off behaviour, so those scenarios are marked not faithful while it is attached (openocd TCL memory reads, AHB-AP while running): both units' `frameBuffer` (global
  `Oled::OledBuffer`, 1048 B, SH1106 stride 131, x offset 2; the plan's `screenBuffer` is dead code), screen id
  (`GUI::nav` current screen), player state, link state, lastKA, counters, fault record, reset cause. Addresses from
  the flashed ELF (`nm`), never hard-coded; a run records the ELF sha. Torn frames: re-read once, keep both if they
  differ, classify later;
- PC process health from `/proc/<pid>`: RSS, fds, threads, CPU.

### Inputs
- PC: control socket (`midicontrol --cmd`) and web API.
- Link disturbance: `dbg.hold_ble_reset` (module reset pin), base reset, USB unbind/bind in sysfs, service restart.
- Keys/encoder: **pending BE-1** (`audit/findings/step3/be1.md`). gdb key injection halts the MCU and is the main
  suspect for the zombie link. If BE-1 confirms halts matter, the firmware gets a RAM input mailbox polled by the main
  loop (written over SWD while running); otherwise `dbg.key` is used with a settle delay. Base Up/Down already work
  non-halting through `Encoder::pos`.

### Latency
Each measured pair is two timeline events (e.g. remote key mailbox write -> PC log `PLAY_SONG from controller`),
reported as min / median / p95 / max per run. One clock: host `CLOCK_MONOTONIC` stamped by the harness when it receives
the event; all taps are unbuffered (the PC runs with stdout line-buffered through a pty, log CDC read in a dedicated
thread; ALSA seq events carry the queue time). Never derived from the 1 Hz polls. The report states the error bound per
pair (tap latency) and what a pair excludes (a mailbox write skips the key debounce, so it is not finger-to-sound).

## 4. Scenarios and burn-in
- `scenarios/*.py`: a scenario is a list of steps (action + expected observation + human-like delay drawn from a seeded
  RNG). Sunday service, mistakes, recording, maintenance (plan 3.4). Each run writes `audit/runs/<date>-<scenario>-<n>/`
  (timeline.jsonl, summary.md, contact sheet PNG of distinct frames; raw captures stay out of git).
- `burnin.py`: seeded random mix of the safe actions (no flash writes: no firmware update, no pairing), checkpoint
  summary every 30 min, pass criteria read from a file written **before** the run (`criteria.toml`).

## 5. Device UI simulator (firmware repos)
The midicontrol-fw checkout has untracked content in `lib/bootloader`, `lib/menu`, `lib/ui`; a fresh worktree would
compile different UI code than the flashed build. The sim build therefore takes those dirs from the main checkout
(read-only symlinks in the worktree) and records `git diff --stat` + untracked-file hashes next to its output.
Extend `tools/screens` in both firmware repos with a scripted driver (`sim` executable): commands `key <name>`,
`tick <ms>`, `state <k>=<v>`, `event <protocol-event>`, `expect screen <id>` / `expect text <line>`; output JSONL per
step with screen id, visible text lines (captured from the text-draw calls) and the frame. Flow scripts from
`audit/ui-map.md` run as a ctest. The real UI code is already compiled on the host there.

## 6. Fuzz
libFuzzer + ASan/UBSan (clang 23 verified) targets in the PC repo (`tests/fuzz/`: Parser::feed, decodeFrame, parseRequest,
smf::parse, parseConfig, parseLogLine, sanitizeCode, decodeRecord, PC-parser vs firmware-C-codec differential) and a
seeded-random fallback that runs under ctest in a few seconds, so the normal suite keeps them alive. Long fuzz runs are
time-boxed and their corpora kept outside git.

## 7. Traceability
Every test carries `@pytest.mark.req("<ID>")` (or a comment `// req: <ID>` in C++) with the requirement IDs exactly as
SYSTEM.md writes them (format taken from `audit/test-matrix.md`, not assumed). Source of truth: the hand-built
`audit/test-matrix.md` for existing tests; new systest tests add their markers and `tools/systest/matrix.py` prints the
rows to merge, so the two never diverge silently (`matrix.py --check` reports IDs a marker names that the matrix lacks).
Open gaps get `xfail(reason="gap NN")`.
