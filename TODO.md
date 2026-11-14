# TODO

## Organ stops: verify at the organ

Per-song stops are implemented (stops editor on the home page, one `<CODE>.conf` per song, set at song start). Still to check on the Opus 910, see the spec's open items (`docs/superpowers/specs/2026-10-27-organ-stops-design.md`) and `docs/status-web-and-service.md`:

- Program numbers: press ST01 and ST60 with a MIDI monitor on the organ's OUT; adjust `ProgramOffset` or `ProgramN`.
- The all-off SysEx works and 100 ms after it is enough.
- Bench: the stops arrive before the first note.
- Move `songs/rejstriky.mid` out of the play folder. OWNER QUESTION (audit Q-US-5 / Q-PC-1): not moved yet. The tracked recording has an invalid song-code name, so the test `songcheck_real_files` fails until it is moved or renamed.

## Organ power behaviour between songs

- Already decided in `docs/SYSTEM.md` (D2, D5, D27; section 6 gaps 3 and 4): the organ stays on between songs and
  switches off after 30 minutes idle. The code still defaults to `KeepPowerOn=false` (power off after each song):
  this is a spec gap to close in code, not an open decision. Do not change the requirement.
- Power-cycling to reset the stops remains a separate question, see "Organ stops" above.

## Web: Host header not checked (accepted, SYS-WEB-05 / D4)

- The HTTP server does not check the Host header, so a DNS-rebinding page on a LAN device could PUT stops. Owner decision 2026-11-12 (FAULTS 2.7): accepted, the church LAN is trusted; no check.
