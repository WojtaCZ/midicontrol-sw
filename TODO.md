# TODO

## Organ stops: verify at the organ

Per-song stops are implemented (Stops tab, `stops.conf`, set at song start). Still to check on the Opus 910, see the spec's open items (`docs/superpowers/specs/2026-10-27-organ-stops-design.md`) and `docs/status-web-and-service.md`:

- Program numbers: press ST01 and ST60 with a MIDI monitor on the organ's OUT; adjust `ProgramOffset` or `ProgramN`.
- The all-off SysEx works and 100 ms after it is enough.
- Bench: the stops arrive before the first note.
- Move `songs/rejstriky.mid` out of the play folder.

## Organ power behaviour between songs

- Decide how organ power behaves between songs and during a session: stay on, turn off
  after a pause, power-cycle to reset the stops (see "Organ stops" above),
  what happens at the start and end of a session.

## Web: Host header not checked

- The HTTP server does not check the Host header, so a DNS-rebinding page on a LAN device could PUT stops. Consider accepting only IP-literal or own-hostname Host values.
