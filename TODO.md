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
- Move `songs/rejstriky.mid` out of the play folder. OWNER QUESTION (audit Q-US-5 / Q-PC-1): not moved yet. The tracked recording has an invalid song-code name, so the test `songcheck_real_files` fails until it is moved or renamed.

## Organ power behaviour between songs

- Already decided in `docs/SYSTEM.md` (D2, D5, D27; section 6 gaps 3 and 4): the organ stays on between songs and
  switches off after 30 minutes idle. The code still defaults to `KeepPowerOn=false` (power off after each song):
  this is a spec gap to close in code, not an open decision. Do not change the requirement.
- Power-cycling to reset the stops remains a separate question, see "Organ stops" above.

## Web: Host header not checked (accepted, SYS-WEB-05 / D4)

- The HTTP server does not check the Host header, so a DNS-rebinding page on a LAN device could PUT stops. Owner decision 2026-11-12 (FAULTS 2.7): accepted, the church LAN is trusted; no check. SUPERSEDED 2026-11-16 by Q2-25: see B7 below.

## Owner answers 2026-11-16: implementation backlog (PC, web, manual, spec)

Source: audit/OWNER-ANSWERS.md; plan: plans/2026-11-16-owner-answers-impl.md. Work stays on branch audit/step4-manual.

Line numbers are from branch audit/step4-manual on 2026-11-16 and will drift; re-grep the symbol. Czech OLED strings have no diacritics.

### B1 Idle-off timer after fault give-up and after a KeepPowerOn song end (Q2-1)
- DONE 2026-11-22: armIdleOffIfOn() after KeepPowerOn song end/Stop/failed start/record stop and after an off give-up with relay on; tests comm_b1_*, comm_pcc2_*. Manual console on still arms nothing (open question).
- Goal: an organ the PC knows is on never stays on without an armed idle-off timer.
- Current code: `armIdleOff()`/`disarmIdleOff()`/`doIdleOff()` at src/comm.cpp ~1457-1483. `armIdleOff` is called only by the startup reset (~1446) and by recordSong (~1213, only if already armed). `reconcileOrgan()` (~1130-1170) gives up with `_organFault` after kOrganReassertMax and does not arm. `finishPlayback()` (~1609), `doStop()` (~1620) and the doPlay fail lambda (~1544) only call `organOff()` when `!keepPowerOn`; with KeepPowerOn=true nothing arms. doPlay disarms at ~1517, `setOrganLocked()` (~1114) disarms.
- Change: add helper `armIdleOffIfOn()` (arms when `_organOn` and not armed). Call it (a) in `reconcileOrgan()` on give-up when the base reports relay on, (b) in finishPlayback/doStop/doPlay-fail when keepPowerOn is true and the organ is on. Keep the keepalive check (~1011-1015) as is.
- Depends on: nothing.
- Tests: tests/test_comm_organ.cpp. Extend `comm_pcc2_gives_up_loudly` (~267) with a relay-on give-up: expect `organ_idle_off_armed=1`. New test: play a song with KeepPowerOn=true (use `songConfig()`, `setMidiSinkForTest`, `fakePorts()`), after song end expect armed; after organOffMs elapsed expect SET_CURRENT{00}. Use a small organIdleOffMs in the test config.
- Manual/facts: facts/pc.md:134, :157-158 (idle-off description); docs/SYSTEM.md SYS-ORG-05 (~209) and ~429.
- Risk / open points: VERIFY: should a manual console `on` or an adopted base-menu "on" also arm (outside Q2-1; ask the owner, FAULTS question 12 is related). Recording warm-up cancel (Stop during warm-up) leaves the organ on with no timer; the same helper covers it.

### B2 TimeoutStopSec and shutdown waits for a running update (Q2-4)
- DONE 2026-11-22: unit TimeoutStopSec=300 (compose already 300s); wait in main.cpp is unbounded (waitForUpload joins); check tests/service_unit_test.sh. Bench: stop during remote update pending.
- Goal: a service stop or reboot never kills an update in progress (remote update measured 152 s, base 13 s, docs/FAULTS.md:82).
- Current code: packaging/midicontrol.service.in has no TimeoutStopSec (systemd default 90 s). src/main.cpp ~276-281: `control.stop()`, `web->stop()`, `exec.waitForUpload()`, then `handler.shutdown()`; ControlServer::stop (src/control_socket.cpp:77-87) waits for client commands, so a running update finishes.
- Change: add `TimeoutStopSec=180` to the [Service] section (>= 180 s as owner said; VERIFY: 180 s is 18% over the 152 s measured; 300 s matches the manual's "up to 5 minutes" and is safer). Keep the wait in main.cpp. Make sure the wait is not bounded by a shorter internal timeout (VERIFY in exec.waitForUpload). Update docs/status-web-and-service.md:404 (mentions 90 s).
- Depends on: nothing. Docker variant: VERIFY the worktree midicontrol-sw-docker compose file; it has no `stop_grace_period` (audit docker finding DK-1); DONE 2026-11-21: `stop_grace_period: 300s` added to docker/compose.template.yaml at the merge.
- Tests: no unit test. Add a check to a packaging test if one exists (VERIFY: grep tests/ for midicontrol.service); otherwise a smoke grep in tests/ that the unit contains TimeoutStopSec >= 180. Bench: stop the service during a remote update.
- Manual/facts: postupy.md:190 and ovladac.md:360 (M-update-time: "up to 5 minutes", see E1); sprava.md service chapter (stop waits for update).
- Risk / open points: SIGKILL after the timeout still kills a hung update; the bootloader recovery stays the fallback.

### B3 `organIdleOffMs` config key; web shows organ_power_fault and idle-off armed (Q2-7)
- DONE 2026-11-22: [PLAYBACK] OrganIdleOffMs (ms >= 1000; 0 rejected, no disable), chip + Czech notice + Advanced rows for idle-off armed / organ_power_fault; tests config_organ_idle_off_ms, web_page_shows_*.
- Goal: the idle limit is configurable and the status page shows the organ power fault and an armed idle-off.
- Current code: include/midicontrol/config.hpp:35 `int64_t organIdleOffMs = 30LL*60*1000;` ("not a config.conf key yet"). [PLAYBACK] keys parsed in src/config.cpp ~99-114 (WarmupMs, KeepPowerOn, VerseGapMs). Status registry sets `organ_power`, `organ_power_fault`, `organ_idle_off_armed` on Node::Base. The web page does not show the last two: src/web_page.cpp base() summary ~478-498, home-view `notices()` ~793-810, overview fields ~859.
- Change: parse `[PLAYBACK] OrganIdleOffMs` (milliseconds, same style as WarmupMs; VERIFY: 0 = idle-off disabled, and reject negative values like the other keys). Add it to config.conf with a comment. Web: a chip "idle-off armed" and a Czech notice for `organ_power_fault` on the home view (text without diacritics only on OLEDs; the web page may use diacritics), plus both fields in the English Pokrocile overview.
- Depends on: B1 (the armed state is meaningful only when B1 arms reliably).
- Tests: tests/test_config.cpp (default 30 min, custom value, invalid value); tests/test_web_page.cpp (string-contains for the new chip/notice ids); tests/test_comm_organ.cpp uses the configured value (see B1).
- Manual/facts: facts/pc.md:134, :157-158; web.md (describe the new notice); docs/SYSTEM.md SYS-ORG-05 (~209) and ~429 (key name).
- Risk / open points: keep the config key name stable once documented; unit in ms vs minutes is an owner style choice (ms matches the neighbours).

### B4 Song code normalisation, configurable (Q2-15)
- Goal: "0406", "406" and " 406 " mean the same song; leading zeros are stripped by default; configurable.
- Current code: src/songs.cpp `validCode` (1-4 digits, value <= 1999, optional A-D suffix), `sanitizeCode` (trim + upper-case, keeps leading zeros, so "0", "0000", "0406" are valid), `findSongFile` (case-insensitive stem match), `listSongs` (`code == upper(stem)`). Call sites: src/dispatch.cpp:86, src/comm.cpp:1069/1074/1101 (playSong is the single place), src/stops_file.cpp:113/338, src/songcheck.cpp:84, src/display.cpp:97, src/web_server.cpp:168/172.
- Change: add `normalizeCode(code, bool stripZeros)` in songs.cpp ("0" and "0000" become "0"; keep suffix: "0406b" -> "406B"). Config key (proposed `[FILES] SongCodeNormalise=true`; VERIFY the section name). Apply in CommHandler::playSong, in the stops-file lookups, and on the stem side in findSongFile/listSongs (so a file 0406.mid is still found as 406). songcheck warns on a leading-zero file name and on duplicates after normalisation (406.mid + 0406.mid). Display (display.cpp:97) shows the normalised code, right-aligned (see E1 M-short-codes).
- Depends on: nothing.
- Tests: tests/test_songcode.cpp (normalise table incl. "0", "0000", "0406B", "1999", "2000" invalid), tests/test_songs.cpp (find 0406.mid by "406"), tests/test_songcheck.cpp (duplicates warning), tests/test_config.cpp (key on/off), tests/test_comm_organ.cpp or test_dispatch (play "0406" plays 406).
- Manual/facts: facts/pc.md:109; tabule.md:21 (E1); sprava.md song-file naming.
- Risk / open points: VERIFY: whether "0" (all zeros) must stay valid; with strip enabled it is song 0. VERIFY: stops file names `<CODE>.conf` follow the same rule (406B.conf vs 0406B.conf). Existing files with leading zeros keep working only if the stem side is normalised too.

### B5 A newer SIGNAL_ANCHOR supersedes an older pending one (Q2-21a, Q-W2-1)
- Goal: while the remote is slow, only the newest section/verse anchor is retried; no stale anchor after a newer one. (The part of Q2-21 on the base skipping a repeated PacketId is base work, see midicontrol-fw/TODO.md.)
- Current code: src/comm.cpp `supersedeGroup()` (~37-46) has groups 1 (SET_CURRENT) and 2 (PLAYING/RECORDING/STOPPED); SIGNAL_ANCHOR is group 0. The supersede loop is in sendWithAckImpl (~395-430): a group-2 signal already drops a pending ANCHOR (`staleAnchor`, Item 2, test `comm_item2_stopped_supersedes_pending_anchor`, tests/test_comm_organ.cpp:880). `sendRemoteSignalLocked` (~447) keeps `_lastAnchor` (the newest one, used by the resync at ~502).
- Change: give SIGNAL_ANCHOR its own group 3 (same destination only), so a new ANCHOR erases older pending ANCHORs. Keep ANCHOR superseding nothing in group 2 (a lost PLAYING must still be retried; guard test `comm_item2_anchor_does_not_supersede_playing`). VERIFY: the add-verse request itself (`CommHandler::addVerse`/`requestAddVerse`, comm.cpp:1046/1085, PlaybackEngine::addVerse playback.cpp:131) has no pending queue on the PC; "newer add-verse supersedes older pending" is implemented at the anchor level here; confirm the owner meant that.
- Depends on: nothing (A-items in protocol do not change it).
- Tests: tests/test_comm_organ.cpp: new `comm_b5_newer_anchor_supersedes_pending_anchor` (FakeBase drops the remote ACK; two anchors; the first one must not be retried after the second), plus the Item 2 guard stays green; resync still resends only `_lastAnchor`.
- Manual/facts: none; docs/SYSTEM.md PM-13 supersede rule row (add ANCHOR group).
- Risk / open points: a superseded pid that was part of a resync stays in the awaited set (known, accepted in Item 2).

### B6 Interactive quit and SIGHUP do the same cleanup as SIGTERM (Q2-24)
- Goal: Ctrl-D/quit/EOF in interactive mode and SIGHUP (terminal closed) end the program through the normal shutdown path (stop playback, clear display, SIGNAL_STOPPED, organ off).
- Current code: src/main.cpp: `interactive()` (~65-74) loops on getline, returns on ExecResult::Quit; headless uses `sigwait` on SIGINT/SIGTERM (termSignals ~175-180, `waitForTermination` ~61); SIGHUP is not handled. After the wait the shared cleanup runs (~250-262): sdNotify, watchdog.reset, control.stop, web stop, waitForUpload, logReader.stop, `handler.shutdown()`. `Kind::Shutdown` (comm.cpp ~1400) does releasePlayer, displayClear, SIGNAL_STOPPED, stopRecorder, organOff.
- Change: add SIGHUP to the blocked/waited set (headless) and handle it in interactive mode (a signal thread or sigwait with the same set plus a stdin reader; VERIFY the simplest design: interactive() polls stdin and a std::atomic flag set by a SIGHUP handler or a dedicated sigwait thread). Make sure the interactive branch falls through to the same cleanup (it does after return; the gap is SIGHUP/SIGINT killing the process by default). Ctrl-C in interactive mode: VERIFY the intended result (same cleanup).
- Depends on: nothing.
- Tests: a process-level test next to tests/test_process.cpp: start the binary against a FakeBase pty, send SIGHUP, expect SET_CURRENT{00} (if the organ was on), SIGNAL_STOPPED and exit code 0. Interactive: feed "quit\n" and EOF on stdin.
- Manual/facts: facts/pc.md (shutdown rows); docs/SYSTEM.md SYS-PC-05 row text (remove "Interactive Ctrl-C may exit without cleanup", E2) and the test-matrix entry.
- Risk / open points: the shutdown wait for an upload (B2) now also applies to SIGHUP; a closed terminal must not hang more than TimeoutStopSec.

### B7 Diag ack via POST only, Origin/Host check, no play controls on the web (Q2-25)
- Goal: a web page on another site cannot change state through the status server (CSRF / DNS rebinding).
- Current code: the ack is a GET: `/api/diag?...&ack=<id>` (src/diag_api.cpp ~84-92, `store.ack`), called from src/web_page.cpp:580 (`fetch("/api/diag?limit=1&ack="+...)`). src/http.cpp ~141-170: PUT for stops (`kStopsPrefix`), POST for firmware, GET for the rest; `/api/diag` at ~167. No Host/Origin check (owner decision 2026-11-12, FAULTS 2.7, was "accepted"; TODO section "Web: Host header not checked" above is superseded).
- Change: (1) Move ack to `POST /api/diag/ack` (body `id=<n>`) and make GET with `ack=` ignored or 400. (2) In http.cpp for every non-GET (PUT/POST): require an `Origin` (if present) whose host equals the `Host` header, and a `Host` that is an IP literal, `localhost` or the configured hostname/allowed list (VERIFY the allowed-name list with the owner; M-fixed-ip says the PC will also get a hostname). Respond 403 otherwise. (3) Keep the rule: no play/stop/organ controls on the web page; add a test that the page contains no play endpoint.
- Depends on: nothing. Update the SYS-WEB-05 / D4 wording (E2).
- Tests: tests/test_diag_api.cpp (ack only via POST; GET ack ignored), tests/test_http.cpp (PUT stops and POST firmware/ack with a foreign Origin -> 403; same Origin -> OK; no Origin (curl) -> VERIFY allowed), tests/test_web_page.cpp (page uses POST for ack; no "play" controls).
- Manual/facts: web.md and sprava.md (access by IP or hostname; why a foreign Host is refused); facts/pc.md web rows (no auth on status page stays).
- Risk / open points: VERIFY: scripts or the console using the old GET ack (grep tools/ and docs); proxies in the Docker deployment change the Host header.

### B8 Configurable default registration for songs without a stop file (SYS-USE-20, M-default-registration)
- Goal: a song with no `<CODE>.conf` gets a configured default set of stops instead of "not set" (organ unchanged).
- Current code: src/stops_file.cpp `planFor()` (~299-313) returns text "not set" and no preamble when `!stops`; used at comm.cpp:1559. Organ config parsing in src/organ.cpp (~163-170: StopsEnabled, DropSongProgramChanges); OrganConfig in include/midicontrol/organ.hpp:33-39.
- Change: new organ key (proposed `[ORGAN] DefaultStops=1,5,12` using the same stop-number list syntax as the .conf files; empty = today's behaviour). `planFor` builds the preamble from the default set when there is no file; plan text "default <list>". The stops editor on the home page shows "default" for a song without a file. Validate against the stop count in config load.
- Depends on: nothing.
- Tests: tests/test_stops_file.cpp (planFor with default; empty default = not set; stopsEnabled=false wins), tests/test_organ_config.cpp (parse, invalid numbers), tests/test_comm_organ.cpp (preamble sent before the first note for a file-less song).
- Manual/facts: sprava.md:156 (replace the "varhany zustanou, jak jsou" TODO with the default behaviour); facts/pc.md; docs/SYSTEM.md SYS-USE-20 row (E2).
- Risk / open points: VERIFY: whether the default also applies on a song whose file exists but is empty (today "all off"): owner said "without a stop file" only. All-off SysEx plus power-cycling is a separate bench question.

### B9 Fix the rejstriky.mid test data (Q3-2)
- Goal: `songcheck_real_files` passes.
- Current code: songs/rejstriky.mid is a tracked recording with an invalid song-code name; the test `songcheck_real_files` (tests/test_songcheck.cpp) fails only on it (FAULTS/step 2 baseline 299/300). The existing TODO "Move `songs/rejstriky.mid` out of the play folder" (top of this file) is the same item.
- Change: owner says fix the test/data: `git mv songs/rejstriky.mid` to a non-play location (VERIFY: `docs/examples/` or a `recordings/` folder, and that nothing references the path: grep tests/ docs/). Alternatively rename it to a valid code if it is meant to be playable (owner to say). Then remove the open TODO line at the top.
- Depends on: B4 (file-name rules) only if renamed.
- Tests: `songcheck_real_files` green; full suite 300/300.
- Manual/facts: sprava.md:179 and M-recording-to-song (how a recording becomes a song) uses this as the example of "copy and rename".
- Risk / open points: the file is tracked history; a move keeps it.

### B10 Full test coverage for SYS-PC-05 (Q3-6)
- Goal: the whole SYS-PC-05 row (shutdown: stop, clear, SIGNAL_STOPPED, organ off; plus the B6 paths) is tested end to end, not only at CommHandler level.
- Current code: audit/test-matrix.md:75 and :310: organ-off on CommHandler::shutdown is asserted at tests/test_comm_organ.cpp ~178-232; the SIGTERM handler, the display clear and SIGNAL_STOPPED are not tested; tests/test_process.cpp covers only a child SIGTERM.
- Change: tests only: (1) CommHandler::shutdown while playing: SET_CURRENT off, SIGNAL_STOPPED to the remote, display clear frame to the base (FakeBase log). (2) while recording: recorder stopped. (3) process-level: SIGTERM, SIGHUP (B6), SIGINT, interactive "quit" and EOF each end with the same log. (4) update in progress: shutdown waits (B2).
- Depends on: B6 (SIGHUP/interactive), B2.
- Tests: the items above; then flip SYS-PC-05 in audit/test-matrix.md to [T] and update the Q3-6 line.
- Manual/facts: none.
- Risk / open points: process-level tests need the binary and a pty; keep them in a separate ctest target (like midicontrol_comm_tests) so unit runs stay fast.

### B11 Console `record` stays admin-only; PC side of base-initiated recording (M-recording-start, M-console-record, Q2-22)
- Goal: recording is started from the base (SYS-REC-01); the PC console `record` stays as an admin/debug tool and is documented only in sprava.
- Current code: console `record` in src/console.cpp:40/62 and src/console_exec.cpp:63 -> `CommHandler::recordSong()` (comm.cpp:1192, declared comm.hpp:106). The PC has no handler for a base-initiated record request; the base `Nahraj` is dead (base fw). dispatch.cpp:130 handles SIGNAL_RECORDING only as a received type.
- Change: PC side of A7: see midicontrol-protocol/TODO.md. PC touch points: new dispatch case in src/dispatch.cpp (next to GET_SONG_LIST ~105) that calls a new `CommHandler::recordFromBase()` which reuses recordSong() (warm-up, recorder start, idle timer disarm at ~1213, SIGNAL_RECORDING to the remote, Stop cancels); ACK/NAK mapping (already recording -> NAK BUSY). The console `record` stays unchanged; do not add any remote record request.
- Depends on: A7 (protocol), C3 (base menu Nahraj).
- Tests: tests/test_recording.cpp (record request from the base starts the recorder; a second request is refused), tests/test_dispatch (new message routed), tests/test_console.cpp (console record still works).
- Manual/facts: postupy.md:144-146, zakladna.md:333, sprava.md:179 (E1); facts/pc.md:226.
- Risk / open points: PCC-1 behaviour (second Record refused) stays. VERIFY: who stops a base-started recording (base Stop only, or also the console).

### B12 Cleanup leftovers; track .trail and graft the same way in every repo (Q2-32)
- Goal: a list of files for the owner to approve. NOTHING is deleted or committed until the owner approves this exact list.
- Current state (read-only check 2026-11-16):
  - Untracked in midicontrol-sw: docs/superpowers/plans/2026-11-07-docker-deployment.md, docs/superpowers/specs/2026-11-07-docker-deployment-design.md, songs/406B.conf (runtime stops file from a bench run). Q2-32 replaces the old rule "leave them uncommitted": propose delete or commit per file. DONE 2026-11-21 for the docker plan/spec: identical copies are tracked since the docker-deployment merge, the untracked duplicates were deleted. songs/406B.conf still open.
  - Untracked tool dirs in several repos: .claude/ .ignore (protocol, fw, remote, bootloader), .cache/ (remote). fw also shows ` ?` submodule entries lib/bootloader, lib/menu, lib/ui (VERIFY: submodule state, not a leftover).
  - .trail: tracked in all five repos (README.md, hook.cjs, .gitignore, learnings/; counts: protocol 7, fw 44, remote 14, pc 21, bootloader 3 files); each .trail/.gitignore lists `cache/` and `statusline.cjs`. `graft`: no graft file is tracked in any of the five repos (git ls-files shows none); `graft/`, `wt/`, `fault/`, `outIndex/`, `backups/`, `reference/` exist only at the workspace root /workspaces/MIDICONTROL-b81bba4a (not a git repo).
  - Workspace-root candidates for the owner: backups/ (many base-g431-flash-*.bin and remote-*.hex, dated 2026-10-18 to 2026-11-15), wt/, fault/, outIndex/, reference/, graft/, MIDICONTROL/midicontroller-fw (stray nested dir). VERIFY each before listing for deletion; do not touch backups/ (the manual says backups are not documented, M-backups, but they are flash images from real flashes).
- Change: after approval only: `git rm`/delete the approved files; add a uniform ignore block (`.claude/`, `.ignore`, `.cache/`, `.claude-notes/`) to each repo's .git/info/exclude or .gitignore; decide one rule for .trail (tracked learnings + `.trail/.gitignore`) and for graft (ignored everywhere, or tracked everywhere) and apply it to all five repos in one commit each.
- Depends on: owner approval of the list; E3/D commits finished first so no work is lost.
- Tests: `git status --short` clean in every repo; `trail check` unchanged.
- Manual/facts: none.
- Risk / open points: deleting is irreversible; show the final list first. The docker branch is merged (2026-11-21).

### B13 Stuck notes silenced within 10 s of a player stall (owner 2026-11-22, bench 10.7) - SAFETY, PC part DONE 2026-11-22, bench pending
- Goal (owner): if a player thread hangs while notes sound, the organ must be silent within at most 10 s. Before: worst case ~45 s of screaming organ plus the restart time.
- Done (PC): the playback thread (named `mc-play`) beats a lock-free `PlaybackHeartbeat` (include/midicontrol/playback_guard.hpp) on entry and in every 10 ms slice of `WallClock::waitUntil` (src/player.cpp), so a long held note is not a stall; -1 = no song. `Watchdog::loop` (src/main.cpp) now polls every 0.5 s: heartbeat older than 2 s -> raw-write log line -> `emergencyNotesOff` (All Notes Off + All Sound Off on 16 channels through a FRESH RtMidiOut on the song's port, from a helper thread, the watchdog waits at most 1 s) -> with `--exit-on-stall` immediate `_Exit(70)` (the restart's startup reset sends notes-off + organ off again). Under systemd (no exit-on-stall) the ping is withheld while the player is stalled. Measured on the virtual rig: notes-off 2.2 s after the freeze, exit 70 right after.
- Deviations from the first plan (decided 2026-11-22): (a) the comm-thread limits stay 10 s / 30 s: those threads send no notes, and a false exit during a song cuts the music; (b) the watchdog does NOT switch the organ off itself: a raw SET_CURRENT write could interleave with a half-written frame of a stalled thread holding the serial mutex; notes-off + restart (startup reset switches the organ off) is the path; (c) no pre-opened MIDI handle: a fresh output avoids sharing RtMidi state with the hung player; the 1 s budget bounds a hang in ALSA.
- Tests: tests/test_playback_guard.cpp (heartbeat + once-per-episode guard); tools/systest/test_virtual_stall.py freezes only `mc-play` with ptrace from the test process (yama scope 1 allows a child) and checks notes-off < 4 s, exit 70 in container mode, and that a 20 s held note is not a stall.
- Not covered: a hang in the control thread while a song plays (the playback thread keeps beating; the 30 s comm-thread path applies), and a whole-process freeze (external supervisor, open idea).
- Bench (owner): redo 10.7 freezing the playback thread (`mc-play` TID in /proc/<pid>/task/*/comm) mid-song with .claude-notes/frz/freeze; measure time to silence on the Otraki/LA; expect ~2.5 s and a container restart.
- Second layer (fw, see midicontrol-fw TODO): the base silences the organ itself when the PC stops talking while a song plays.
- Seen while testing B13 (pre-existing, fails on 2026-11-22 HEAD too): tools/systest/test_virtual_link.py::test_keepalive_cadence_loss_and_recovery - the first two KEEPALIVEs go out 0.15 ms apart at connect (gap check 1.7-2.4 s). Decide: harmless double send at link-up (fix the test) or a keepalive-loop bug.
- Related owner request (2026-11-22, LATER): a failsafe on the remote controller so the user can quickly cut the organ power (emergency off) - see midicontroller-fw/TODO.md D14.

### PC side of A-items (protocol changes are specified in midicontrol-protocol/TODO.md)
- PC side of A1 (session nonce): see midicontrol-protocol/TODO.md. PC touch points: src/dispatch.cpp (per-source remembered frames, kRemembered 8; reset on the new nonce), src/comm.cpp `nextPacketId()` / session start (~395-400) and the DISABLED_ PM-13 tests to re-enable (Q3-3).
- PC side of A3 (base tracks last indicator writer): see midicontrol-protocol/TODO.md. PC touch points: src/display.cpp:97 (indicator write), src/comm.cpp displayClear call in Kind::Shutdown/doStop (clear only if this PC wrote it last, M-clear-on-stop); VERIFY the PC needs no new state if the base arbitrates.
- PC side of A4 (FW_UPDATE_STATUS ACK): see midicontrol-protocol/TODO.md. PC touch points: src/comm.cpp:989 (`FW_UPDATE_STATUS` handling), src/dispatch.cpp:133 (currently falls to the no-answer group); must answer ACK and tolerate the retry (dedup).
- PC side of A6 (song-has-sections flag): see midicontrol-protocol/TODO.md. PC touch points: src/comm.cpp:1588 (SIGNAL_ANCHOR send) and the SIGNAL_PLAYING send in doPlay (~1592) to carry the flag; PlaybackEngine section info (src/playback.cpp:131 area); `_lastAnchor` resync (~452, ~502) must resend the flag too.
- PC side of A7 (recording): see midicontrol-protocol/TODO.md. PC touch points: src/dispatch.cpp (new case near ~105), src/comm.cpp:1192 recordSong (see B11).
- PC side of A8 (update in progress): see midicontrol-protocol/TODO.md. PC touch points: src/comm.cpp `setUpdating` and the update path in src/console_exec.cpp (`update()`, `startUpload()`, `tryBeginUpdate`); VERIFY whether the PC must send or only receive the flag.
- PC side of A9 (song list paging): see midicontrol-protocol/TODO.md. PC touch points: src/dispatch.cpp:105-127 (GET_SONG_LIST / SONG_LIST_RESP), src/comm.cpp:1054-1056 (`songListPage`, `buildSongListPage`), include/midicontrol/songs.hpp:43 (`buildSongListPage` offset/limit); relates to the base "more than 32 songs shows nothing" gap (zakladna.md:202).
- PC side of D13 (remote asks for the state on connect, bench 2.3, owner 2026-11-22): see midicontroller-fw/TODO.md D13. PC touch points: src/dispatch.cpp (new state-request case, answer = set `_remoteDirty` and run the resync in src/comm.cpp ~560-601), or the PC-only variant: send GET_INFO to the remote as soon as the remote link comes up instead of waiting for the InfoPoller.

### E1 Manual edits for every M-* answer (docs/manual/docs/*.md)
- Goal: apply each M-* answer from audit/OWNER-ANSWERS.md (lines 64-98) and remove the TODO markers it settles; `cd midicontrol-sw && docs-build todo` lists them (49 markers on 2026-11-16, read-only).
- Current code: n/a (documentation). File:line below is from `docs-build todo`; line numbers shift as edits are made, so work from the bottom of each file upward.
- Change (marker -> answer -> replacement intent):
  - index.md:29 -> M-version: delete the firmware-version sentence; say the manual carries only its issue date.
  - index.md:44 -> M-intended-use + M-physical: write intended use (organists and the admin; one church organ over MIDI; other use not intended); drop package contents and panel-label TODOs.
  - ovladac.md:21-22 -> M-physical: remove the note; keep the function table.
  - ovladac.md:104 and postupy.md:207 -> M-battery: marker STAYS (bench measurement of charge time and battery life pending).
  - ovladac.md:165, problemy.md:36, problemy.md:336 (Odmitnuto) -> M-odmitnuto: replace "TODO: jake duvody" with the NAK reasons from the code (list from src/protocol NakReason and dispatch.cpp; owner reviews the list; VERIFY the mapping with B-/A-items for new reasons).
  - ovladac.md:224 -> M-add-verse: state that a song without verses offers no add-verse; depends on A6 + remote D-item (until built, mark "planned").
  - ovladac.md:230 and postupy.md:112 -> M-pause: delete the pause/resume text; Stop + start again.
  - ovladac.md:290 -> M-modul: organist power-cycles, then calls the admin; details go to sprava.
  - ovladac.md:319 and postupy.md:50 -> M-repair: pair only when the remote or the base is replaced (or factory reset); not after updates or power loss.
  - ovladac.md:360 and postupy.md:190 -> M-update-time: "up to 5 minutes".
  - problemy.md:9 -> M-midi-link: DIN from the base to the organ; the admin checks with the base MIDI LEDs; note the DIN fault is still open.
  - problemy.md:91 and ovladac.md (Sparovat?) -> M-sparovat-prompt: after R4-2 is built, `Ne` cancels, `Ano` accepts; until built keep the informational wording (VERIFY remote D-item).
  - problemy.md:112 -> M-low-battery: warning at 15 % on the remote (after the remote D-item is built).
  - problemy.md:140, tabule.md:19 -> M-led-colours: indicator LEDs will also be lit from MIDI SysEx (not implemented); describe as planned or leave out.
  - tabule.md:5-6 -> M-physical: remove (no description of the board hardware).
  - tabule.md:21 -> M-short-codes: "  12" right-aligned with blanks; first check display.cpp:97 does this (code change if not; B4).
  - tabule.md:31 -> M-other-controller: no, by design; each player screen shows only its own song; the indicator shows the last writer.
  - tabule.md:42 -> M-clear-on-stop: clear only if this PC/song wrote it last (needs A3 + Q2-19); describe the rule.
  - zakladna.md:25, :75 (labels) -> M-physical / M-rear-leds: remove :25; :75 STAYS until the bench read of the printed rear LED labels.
  - zakladna.md:29 -> M-knob-direction: counter-clockwise = down.
  - zakladna.md:96 -> M-master-led: master LED off means all LEDs off (fix B3 in base fw), then drop the TODO.
  - zakladna.md:137 -> M-cut-labels: keep scrolling; document as is.
  - zakladna.md:202 -> no direct M-* answer; relates to A9 (song list paging); STAYS until A9 is decided.
  - zakladna.md:236 -> M-base-verse: describe Stop / Pridat sloku from the knob (after base fw C-item).
  - zakladna.md:279 -> M-no-pc: the system never runs without a PC; state this; the 2 h limit only as a safety fallback.
  - zakladna.md:333 and postupy.md:146 -> M-recording-start: recording starts only from the base (`Nahraj`); no remote record button.
  - zakladna.md:405 and :417 -> M-update-risk: organists are told not to unplug; the PB1 recovery steps go only into sprava.
  - postupy.md:144 and sprava.md:179 -> M-recording-to-song: the admin copies/renames the recording into the songs folder under a song code (example uses B9).
  - postupy.md:212 -> M-autostart: DONE 2026-11-21, the Docker container starts by itself (restart: always).
  - sprava.md:49 -> M-docker: DONE 2026-11-21, Docker procedure written at the merge; Docker is the only production install.
  - sprava.md:95 -> M-programoffset: marker STAYS (bench check of ProgramOffset -1).
  - sprava.md:156 -> M-default-registration: replace with the default-registration behaviour after B8.
  - sprava.md:239 -> M-backups: delete the TODO; do not document backups.
  - web.md:35 -> M-fixed-ip: the PC has a fixed IP; a hostname will be added; name both (after B7 allowed names).
  - web.md:190 -> M-pokrocile-en: OK in English; admin-only; say so.
  - tahak.md:46 -> M-admin-phone: blank line "Spravce: ____" to fill in by hand.
- Markers with no answer (stay, listed): zakladna.md:202 (song list longer than 32; see A9), and none other found; markers that stay on purpose: ovladac.md:104, postupy.md:207, zakladna.md:75, sprava.md:49, sprava.md:95.
- Depends on: B1-B8, B11 and the base/remote D- and C-items for "planned vs built" wording; E3 covers facts/*.md for base and remote.
- Tests: `docs-build todo` count falls from 49 to 5-6; `docs-build` 0 mkdocs warnings; PDF visual check (pages and tables) as in step 4.
- Manual/facts: also correct facts/pc.md lines 109, 134-135, 157-158, 200 (backups), 226 (open question on recording) to match.
- Risk / open points: write each edit as the final behaviour only when the matching code item is built; otherwise use "planned" and keep a marker, so the manual never describes behaviour the firmware lacks. VERIFY: the line numbers before editing.

### E2 docs/SYSTEM.md edits (main copy: midicontrol-sw/docs/SYSTEM.md)
- Goal: bring the spec in line with the owner answers.
- Current code: n/a. SYSTEM.md line 17 says edit only the main copy. Copies live in the other repos (and in the lib/protocol and lib/bootloader submodule copies) and are synced by midicontrol-sw/tools/sync-system-doc.sh (`--check` reports drift; test tests/sync_system_doc_test.sh). CLAUDE.md: after editing, run tools/sync-system-doc.sh and commit the copies in the sibling repos.
- Change (each edit):
  1. Drop SYS-USE-09 and SYS-USE-21 (pause/resume), M-pause.
  2. SYS-REC-01: recording starts from the base only; the console `record` is an admin/debug tool (M-recording-start, M-console-record).
  3. SYS-USE-20: default registration is configurable (B8).
  4. SYS-USE-24: add verse from the base knob (built, M-base-verse).
  5. SYS-PC-05 (line ~197): shutdown also on interactive quit and SIGHUP; delete "Interactive Ctrl-C may exit without cleanup" (B6); update the code reference (`Shutdown` is now at comm.cpp ~1400, not 1010).
  6. SYS-ORG-05 (~209) and ~429: idle-off after fault give-up and after a KeepPowerOn song end; key `[PLAYBACK] OrganIdleOffMs` (B1, B3).
  7. SYS-WEB-05 / D4: Origin/Host check and POST-only ack; no play controls (B7).
  8. PM-13 supersede rule: ANCHOR group (B5); session nonce (A1); FW_UPDATE_STATUS ACK (A4).
  9. SYS-BUS-07: clear the indicator on stop only if this PC/song wrote it last (A3).
  10. Q3-7: rows for the gaps 34, 36 and 37 (take the text from audit/OWNER-ANSWERS.md Q3-7 and the section 6 gap list; VERIFY the wording).
  11. Add the PC "remote state dirty" rule from the wave-2 resync (set at PC start, ble 0->1 edge, un-ACKed SIGNAL_*; cleared by ACK; not during updates), as noted in the step-2 report.
  12. SYS-USE-09/21 removal affects the traceability tables (test-matrix.md rows).
- Depends on: the matching code items (edit the spec when the decision is final; the owner answers are the authority, the spec may be updated first).
- Tests: `tools/sync-system-doc.sh --check` clean; tests/sync_system_doc_test.sh passes; `trail check` (SYSTEM.md:366 and CLAUDE.md:77 keyword false positives are known).
- Manual/facts: the manual's facts/*.md cite SYS-IDs; update them (E1).
- Risk / open points: known drift: the lib/bootloader copy was NOT bumped earlier (a488f5e vs code diff to 19e5fdb, Q2-13), so its SYSTEM.md copy is stale until that submodule is bumped. Copies must be committed in each sibling repo (protocol, fw, remote, bootloader, hw), with the pushes approved by the owner.

### E4 Refresh the test matrix's last-result column (Q3-5)
- Goal: the owner answered yes: after the A-D work, refresh the "last result" column of `audit/test-matrix.md` (115 IDs).
- Current code: `audit/test-matrix.md` (workspace root, not a git repo) reflects step 3 (35 covered, 46 partial, 30 gaps).
- Change: after phases A-D, run the PC unit tests (`midicontrol_tests`), both fw host tests (`mcu test`), both sim flow sets
  (`tools/screens/run-sim.sh`), and update each row's result and date; add rows for new tests (PM-13 re-enabled in A1, B9, B10, E3 timing flows);
  drop the SYS-USE-09/21 rows (E2).
- Depends on: A1-A9, B*, C*, D*, E2, E3 (do last).
- Tests: none new; this records results.
- Manual/facts: none.
- Risk / open points: hardware-marked basetest/bletest rows stay "bench" until the BENCH session (Q3-4).

### Cross-repo index
- Protocol batch A1-A9 + version bump: midicontrol-protocol/TODO.md. Base C1-C11 + bench: midicontrol-fw/TODO.md.
  Remote D1-D12 + bench: midicontroller-fw/TODO.md. PC B1-B12, E1, E2, E4: this file. E3: both fw TODO.md.
- Owner questions answered 2026-11-16 (follow-up, also in audit/OWNER-ANSWERS.md): D6 = confirm turning the organ off on the base
  while playing/recording -> new base task C12 (nothing on the remote); A7 = base and remote behave as one (both show playing and
  recording screens, both control playback, recording only from the base, remote Stop does not end a recording); A3 = PC-lost clear
  is conditional too; C7 = "Servis" approved.
