---
title: "Organ stops are one <CODE>.conf per song; errors lean to 'not set'"
author: Dev
date: 2026-10-27
branch: status-page-redesign
cost: { minutes: 4, tokens: 101737 }
id: l-1d26b21a
scope: repo
status: active
replaces: l-9e304dfd
src: agent
touches:
  - src/stops_file.cpp
---

## Decided
- User (2026-10-27): one stop file per song, named like the song: `<PlayFolder>/<CODE>.conf` next to `<CODE>.mid` (`[STOPS]`, `Stops = 3 4 5`). No file = not set, `Stops =` empty = all off, "Zrušit nastavení" deletes the file (src/stops_file.cpp `save`/`remove`, `Store::put`). The old single `stops.conf` is not read or migrated (feature never deployed); WebServer only warns if one is left.
- Lookup is case-insensitive like `findSongFile`; a new file takes the `.mid`'s stem (`406a.mid` -> `406a.conf`).
- Still true from the stops.conf design: `load()` returns not set only for ENOENT and throws on any other error; a Stops line with tokens but no valid stop is not set (a typo must never silence the organ); in CommHandler::doPlay any stops error is logged and the song plays as not set.
## Watch out
- "I saved stops but no file appeared": the browser-check player in the dev container runs from a session scratchpad with its own config (PlayFolder = scratchpad/run/play holding copies of songs/), so saves land there, not in the repo's songs/. Check `ps` and its --config before debugging the save path.
