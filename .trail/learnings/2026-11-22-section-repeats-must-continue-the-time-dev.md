---
title: "Section repeats must continue the time grid (continuePass), not restart from now"
author: Dev
date: 2026-11-22
branch: main
cost: { minutes: 31, tokens: 212212 }
id: l-703e5850
scope: repo
status: active
src: agent
touches:
  - src/playback.cpp
---

## Decided
src/playback.cpp used tb.startPass() (= steady_clock::now()) after a section repeat and before the verse gap. Each sleep_for wake-up overshoot (0.1-0.5 ms, no RT priority) then shifted every later note permanently, accumulating per repeat. Timebase::continuePass(seconds) now starts the next pass at the planned moment (old start + seconds); WallClock falls back to now if more than 20 ms behind, so a stalled thread does not rush notes out.
## Watch out
tools/systest/test_virtual_timing.py catches this (drift limit 0.1 ms); it needs alsaseq.py's poll()-based capture, the old 5 ms sleep swamped the numbers. Bench measurement of the base itself: midicontrol-fw/tools/basetest/test_midi_timing.py (LA on DIN OUT = D5).
