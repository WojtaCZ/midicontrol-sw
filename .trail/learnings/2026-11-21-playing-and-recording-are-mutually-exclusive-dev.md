---
title: Playing and recording are mutually exclusive by design
author: Dev
date: 2026-11-21
branch: docker-deployment
cost: { minutes: 41, tokens: 231697 }
id: l-338e7b62
scope: repo
status: active
src: agent
touches:
  - src/comm.cpp
---

## Decided
Owner ruling 2026-11-21: a user can never play and record at the same time. While a song plays, the base and the remote both show the playing screen; leaving it stops playback, and only then can a recording start. src/comm.cpp busyReason() returns BUSY for a recording (PLAY_SONG refused) and RECORD is refused while a song plays.

## Watch out
- Bench step 2.9 part 2 (remote STOP during a recording while a song plays) is obsolete; the R14 path in comm.cpp (remote STOP_SONG stops a song during a recording) is effectively unreachable from the UI.
- The PLAY_SONG refusal log used to say "firmware update in progress" for a recording too; it now names the real cause.
