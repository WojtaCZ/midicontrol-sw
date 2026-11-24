# TODO

## No play/record API on the daemon (owner 2026-11-22) - holds, nothing to change
Owner: "the daemon shall have no play/record api, we want playing and recording from the devices only", then
"console should accept play". HTTP is clean (routes: /api/status, /api/diag, /api/firmware, /api/stops); never add
play/record routes. The console (stdin, control socket `midicontrol.sock`, one-shot CLI) keeps `play <code>` and its
other commands (`src/console.cpp`), as used by tools/systest/bench/scenarios.py.

## Organ stops: verify at the organ

Per-song stops are implemented (stops editor on the home page, one `<CODE>.conf` per song, set at song start). Still to check on the Opus 910, see the spec's open items (`docs/superpowers/specs/2026-10-27-organ-stops-design.md`) and `docs/status-web-and-service.md`:

- Program numbers: press ST01 and ST60 with a MIDI monitor on the organ's OUT; adjust `ProgramOffset` or `ProgramN`.
- The all-off SysEx works and 100 ms after it is enough.
- Bench: the stops arrive before the first note.

## Organ power behaviour between songs

- Already decided in `docs/SYSTEM.md` (D2, D5, D27; section 6 gaps 3 and 4): the organ stays on between songs and
  switches off after 30 minutes idle. `KeepPowerOn` now defaults to true (435e1e6; power stays on between songs, `false` is the opt-out):
  the requirement is met in code. Do not change the requirement.
- Power-cycling to reset the stops remains a separate question, see "Organ stops" above.

## Owner answers 2026-11-16: implementation backlog (PC, web, manual, spec)

Source: audit/OWNER-ANSWERS.md, plans/2026-11-16-owner-answers-impl.md, plans/2026-11-23-owner-answers.md (the 2026-11-23 answers override older plans). Czech OLED strings have no diacritics. Cleaned up 2026-11-23: finished items are one line, open work stays below.

### Finished (B-items, all merged into main)
- B1 DONE 2026-11-22/23: idle-off timer arms after KeepPowerOn song end/Stop/failed start/record stop and after an off give-up; since 2026-11-23 also after a manual console `on` and an adopted base-menu `on` (5c3cc67, d3dbf3a).
- B2 DONE 2026-11-22: `TimeoutStopSec=300` in the unit, `stop_grace_period: 300s` in compose, unbounded `waitForUpload` (cd71521, 53274f8). Open bench bullet: stop the service during a remote update.
- B3 DONE 2026-11-22/23: `[PLAYBACK] OrganIdleOffMs`, web chip and notice for idle-off armed / organ_power_fault; `0` = never off, documented (7f73d49, d3dbf3a).
- B4 DONE 2026-11-22: song codes ignore leading zeros, `[FILES] SongCodeNormalise` (50b1658).
- B5 DONE 2026-11-22: a newer SIGNAL_ANCHOR supersedes older pending anchors, supersede group 4 (3f76531). Owner 2026-11-23: did not follow the question; explained in chat (a newer anchor cancels retries of the older one so the remote never jumps back), owner OK 2026-11-24: CLOSED.
- B6 DONE 2026-11-22: SIGHUP/SIGINT/interactive quit/EOF run the SIGTERM shutdown (aa866bc).
- B7 DONE 2026-11-22/23: `POST /api/diag/ack`, Host/Origin check on writes, `[WEB] AllowedHosts` (default `midicontrol, midicontrol.local`, own `gethostname()` auto-accepted, `*` = any) (664ff50, b504bec). Docker rejects `hostname:` with `network_mode: host`, so compose carries only a comment; the host's name is auto-accepted.
- B8 DONE 2026-11-22/23: `[ORGAN] DefaultStops` plus a default editable in the stops editor (`<PlayFolder>/_default.conf`); an existing empty song file = all off (c8abd5d, d3dbf3a). Owner 2026-11-23: an EMPTY default (empty `_default.conf` or empty `DefaultStops` key) = all off too; not set = organ left alone; `/api/stops` `"default"` is null or an array (d58494d).
- B9 DONE 2026-11-22: rejstriky.mid moved to docs/examples, `songcheck_real_files` strict (927cf7d).
- B10 DONE 2026-11-22 (partly): shutdown tests at CommHandler level and process level (9054591, B6 systest). Not done: an automated test that shutdown waits for a running update (covered by service_unit_test.sh + bench only).
- B11 DONE 2026-11-21: PC side of the base-initiated recording (dispatch RECORD_SONG start/stop, NAK BUSY on a second request, console `record` stays admin) (6e6b42d).
- PC side of A1..A9 DONE 2026-11-16..21: A1 ad31ae1, A2 56ab445, A3 37a33e8, A4 f7843ae, A5 595327c, A6 9b4c713+e33ad31, A7 6e6b42d, A8 20a768d, A9 36d0671.
- D13 PC side DONE 2026-11-23 (option b, owner): GET_INFO to the remote at once on the ble 0->1 STATUS_RESP edge, no protocol change (d3dbf3a). Residual: a remote reboot shorter than the STATUS poll is found only by the next boot id; the real fix is the remote's own hello (midicontroller-fw D13).
- D3 (pairing confirmation) docs DONE 2026-11-24 (branch docs/pair-1124): SYSTEM.md SYS-PAIR-08..10 + routing/messages/strings, manual nastaveni.md + opravy.md, facts base.md + remote.md. Pending: tools/sync-system-doc.sh after merge; bench check of the Ano/Ne flow on both devices; screenshots of the new popup not taken.
- E1 DONE (superseded): the manual was rewritten as a simplified manual (dde5c03); `grep TODO docs/manual/docs` finds no markers. The old per-file M-* list no longer applies (files ovladac/postupy/zakladna/... are gone).

### Open
- Bench (owner): B2 stop during a remote update; B13 freeze test below; B7 in the real Docker deployment (hostname accepted).
- B7 doc drift and facts/pc.md SIGNAL_RECORDING: checked 2026-11-23, both docs already correct.

### B12 Cleanup leftovers (Q2-32)
- Owner 2026-11-23 approved: delete wt-*, fault, outIndex, MIDICONTROL/ at the workspace root; the owner runs it (classifier blocked it): `rm -rf wt-ctl-d2 wt-fw-c2 wt-sw-b2 MIDICONTROL && rm -f fault outIndex`. Keep reference/ and graft/ and backups/. songs/406B.conf is already gone (not tracked).
- graft/workspace.json already lists midicontrol-sw (the rename fix is done).
- Not proven, still to check: uniform ignore block (`.claude/`, `.ignore`, `.cache/`, `.claude-notes/`) in every repo's .git/info/exclude, and one rule for .trail/graft across the five repos.

### B13 Stuck notes silenced within 10 s of a player stall (SAFETY) - PC part DONE 2026-11-22 (1ea9ea2), bench pending
- Done: `mc-play` heartbeat, watchdog polls every 0.5 s, heartbeat older than 2 s -> emergency All Notes Off / All Sound Off on a fresh RtMidiOut, exit 70 with `--exit-on-stall`; measured on the virtual rig: notes-off 2.2 s after the freeze. Tests: tests/test_playback_guard.cpp, tools/systest/test_virtual_stall.py. Decisions: comm-thread limits stay 10 s / 30 s; the watchdog does not switch the organ off itself (the startup reset after restart does).
- Not covered: a hang in the control thread while a song plays, a whole-process freeze (external supervisor, open idea).
- Bench (owner): redo 10.7 freezing `mc-play` mid-song (.claude-notes/frz/freeze), expect ~2.5 s to silence and a container restart.
- DONE 2026-11-23 (cfc2cf7): test_keepalive_cadence_loss_and_recovery: the first two frames at connect are the session-nonce hello (A1) and the first KEEPALIVE, intended; the test drops the hello.
- DONE 2026-11-23 (b933a9a): comm race since D13 GET_INFO: an INFO_RESP before the link-edge resync ACKs clears `_resyncAwait`, so stale ACKs no longer clear `_remoteDirty`.
- Emergency abort (owner 2026-11-24, item 12), PC part DONE on backlog/abort-1124: EMERGENCY_OFF (0x15) from the base stops playback and recording (recording is saved), clears queued Plays/anchor state, organ state OFF without SET_CURRENT; tests `comm_emergency_off_*`, `dispatch_emergency_off_*`. Bench pending (needs the fw base forwarding). A SET_CURRENT{on} still in its retry window when the abort arrives is cancelled (`cancelPendingOrganCommands`, test `comm_emergency_off_cancels_pending_set_current_on`).
- Second layer is in midicontrol-fw (base silences the organ when the PC goes quiet during a song); remote emergency off is D14 in midicontroller-fw/TODO.md.

### E2 docs/SYSTEM.md edits (main copy; owner-controlled, not edited by this cleanup)
- DONE (verified in SYSTEM.md 2026-11-23): SYS-USE-20 (default registration), SYS-USE-24, SYS-PC-05 (quit/SIGHUP), SYS-ORG-05 + key table (B1/B3), SYS-WEB-05 (B7), SYS-REC-01.
- Still open: drop SYS-USE-09 / SYS-USE-21 (pause/resume, M-pause; rows and gap-list lines 12/13 still present) and the matching traceability rows; the PM-13 supersede rule rows (anchor group, session nonce A1, FW_UPDATE_STATUS ACK A4), SYS-BUS-07 A3 wording, Q3-7 gap rows 34/36/37 wording, the PC "remote state dirty" rule: not proven present, check. Run tools/sync-system-doc.sh from the main checkout after merge (it writes sibling checkouts); `--check` also reports drift in the lib/bootloader copy until that submodule is bumped.

### E4 Refresh the test matrix's last-result column (Q3-5)
- `audit/test-matrix.md` is not in the repo (audit/ holds only findings/). After the phases, run `midicontrol_tests`, both fw host tests, both sim flow sets and refresh each row; hardware rows stay "bench". Do last.

### Cross-repo index
- Protocol A1-A9: midicontrol-protocol/TODO.md. Base: midicontrol-fw/TODO.md. Remote: midicontroller-fw/TODO.md. PC: this file. E3: both fw TODO.md.
