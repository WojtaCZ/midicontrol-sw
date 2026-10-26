---
title: "Base log replay dedupe: skip only a prefix matching the stored tail, not any seen line"
author: Dev
date: 2026-10-26
branch: device-info
cost: { minutes: 33, tokens: 724685 }
id: l-00a4dcd7
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - CMakeLists.txt
  - docs/status-web-and-service.md
  - include/midicontrol/comm.hpp
  - include/midicontrol/device_info.hpp
  - include/midicontrol/diag_codes.hpp
  - include/midicontrol/diag_store.hpp
  - include/midicontrol/dispatch.hpp
  - include/midicontrol/log_reader.hpp
  - include/midicontrol/protocol/types.hpp
---

## Decided
After a base reset the base replays its whole 1.5 KB ring (debug.cpp init: _sent = tail). LogIngest (src/log_reader.cpp)
buffers the replay body and skips only the prefix that exactly equals the tail of the lines stored live
(seenOrder_, keys uptime+tag+text, seeded from the store at start); the rest is stored.
## Tried and ruled out
A set of recently seen keys: a replayed boot banner of a run the PC never saw has the same uptime (5 ms) and text as
the previous run's banner, so it was dropped (test log_ingest_replayed_banner_of_an_unseen_run_is_kept).
Clearing the set on each live banner did not fix that case either.
