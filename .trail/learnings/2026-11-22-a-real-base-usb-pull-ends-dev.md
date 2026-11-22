---
title: "A real base USB pull ends the PC recording; docker pause does not, so it is no stand-in for 2.10"
author: Dev
date: 2026-11-22
branch: main
cost: { minutes: 1, tokens: 1547 }
id: l-15bca00d
scope: repo
status: active
src: agent
touches:
  - src/comm.cpp
---

## Decided
Bench 2026-11-22 (BENCH-CHECKLIST 2.10, Docker player, real cable pull with notes from the Otraki):
the serial port closes, src/comm.cpp logs "Serial link to the base lost", and within ~4 s recording goes
false and the file is written (708 B). After replug the base reconnects (~15 s) and new recordings start/stop normally.

## Tried and ruled out
`docker pause midicontrol` as a USB-pull approximation (2026-11-21): the port stays open, the PC keeps
recording and the base can only get BUSY (src/comm.cpp keepaliveLoop only warns). That gap is real only
for a frozen PC, not for a lost base; do not treat it as the 2.10 result.

## Watch out
Base is USB-powered: pulling the cable power-cycles it, so the base screen goes black (no "PC lost" screen to see).
