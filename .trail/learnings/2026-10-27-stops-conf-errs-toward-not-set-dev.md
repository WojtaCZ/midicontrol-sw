---
title: "stops.conf errs toward 'not set': unreadable file throws, invalid-only line is skipped"
author: Dev
date: 2026-10-27
branch: device-info
cost: { minutes: 39, tokens: 159183 }
id: l-9e304dfd
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - TODO.md
  - config.conf
  - docs/status-web-and-service.md
  - include/midicontrol/config.hpp
  - src/comm.cpp
  - src/stops_file.cpp
  - src/web_page.cpp
  - tests/test_stops_file.cpp
  - tests/test_web_page.cpp
---

## Decided
- src/stops_file.cpp `load()` returns {} only for ENOENT; any other open error throws. Store::put does load-modify-save, so treating EACCES as "missing" would rewrite the file and wipe every other song's entry.
- A line with tokens but no valid stop (`401=x`, `401=99`) is skipped with a warning = "not set" (organ untouched). Only `401=` with no tokens means "all off". A typo must never silence the organ.
- In CommHandler::doPlay an unreadable stops.conf is logged and the song plays as "not set"; stops problems never block a song.
- `[SONGS]` header is case-insensitive, may carry a trailing comment, and lines before any header count as [SONGS]; otherwise hand edits were silently dropped and then wiped by the next web save.
## Tried and ruled out
- Leaving `[ORGAN]` errors non-fatal (fall back to defaults with StopsEnabled=false): kept fatal like [WEB] Port for consistency; revisit if config typos keep stopping the service.
## Watch out
- Browser-checking the player in the dev container: boards may be attached and the serial port is found via /sys with no config key; the 2026-10-27 session ran its own instance under an LD_PRELOAD shim hiding USB serial + ALSA on port 28125.
