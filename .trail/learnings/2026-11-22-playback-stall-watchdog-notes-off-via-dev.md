---
title: "Playback stall watchdog: notes-off via fresh MIDI out then exit 70; no organ-off from the watchdog, comm limits unchanged"
author: Dev
date: 2026-11-22
branch: main
cost: { minutes: 25, tokens: 154780 }
id: l-e9244e5b
scope: repo
status: active
src: agent
touches:
  - docs/bench-acceptance-docker.md
  - include/midicontrol/playback_guard.hpp
  - tools/systest/test_virtual_stall.py
  - src/player.cpp
  - src/main.cpp
---

## Decided
- B13 (owner 2026-11-22: stuck notes silent within 10 s). The playback thread `mc-play` beats a lock-free `PlaybackHeartbeat` (include/midicontrol/playback_guard.hpp) inside every 10 ms slice of `WallClock::waitUntil` (src/player.cpp), so held notes are not stalls. `Watchdog::loop` (src/main.cpp) polls every 0.5 s; 2 s stale -> `emergencyNotesOff` (fresh RtMidiOut, helper thread, max 1 s wait) -> `_Exit(70)` with --exit-on-stall. Measured 2.2 s on the virtual rig.
- The comm-thread limits stay 10 s / 30 s: those threads send no notes, and a false exit mid-song cuts the music.
- The watchdog never switches the organ off itself: a raw SET_CURRENT write could interleave with a half-written frame from a stalled thread holding the serial mutex. The restart's startup reset does the organ off.
- The watchdog must not call `isPlaying()` (takes `_playerMutex`); use `playbackHeartbeatAgeMs()` / `playbackPort()`.

## Watch out
- tools/systest/test_virtual_stall.py freezes one thread with ptrace from the test process (yama ptrace_scope 1 allows tracing a child). When the traced process exits, the test must `waitpid(tid, __WALL)` the traced thread or the process can never be reaped and Popen.wait hangs forever.
- In Python, a module constant named `__WALL` used inside a class body is name-mangled (`_Frozen__WALL`) -> NameError after PTRACE_INTERRUPT, leaving the thread stopped; name it without leading underscores.
- `pkill -f "<pattern>"` in the same shell command line matches that shell itself and kills the rest of the command.
