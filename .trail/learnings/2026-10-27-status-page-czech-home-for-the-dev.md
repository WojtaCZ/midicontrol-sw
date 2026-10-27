---
title: "Status page: Czech home for the organist (health + now playing + stops), English Advanced for debug"
author: Dev
date: 2026-10-27
branch: device-info
cost: { minutes: 15, tokens: 272858 }
id: l-18501bfa
scope: repo
status: active
src: agent
touches:
  - PRODUCT.md
  - src/web_page.cpp
  - tests/test_web_page.cpp
  - docs/Screenshot From 2026-10-27 17-50-05.png
---

## Decided
User (2026-10-27): the organist opens src/web_page.cpp's page about monthly, on PC, phone or tablet. Home ("Přehled", Czech): plain-language notices linking to Advanced, system topology PC—USB—Base—BLE—Remote with health, organ power and battery, now playing, and the per-song stops editor. "Pokročilé" (Advanced, English) keeps every existing debug page under #adv/<page>; old #base etc. still route there. UniFi console style (neutral panels, one blue accent). Product context in PRODUCT.md.
## Tried and ruled out
Health-only home (too little for the organist to trust the system) and a home with the song list and file warnings (debug detail; it lives in Advanced → Software). Czech for Advanced: dropped, the debug pages stay English for the maintainer.
## Watch out
The repo's build/ is a stale host build (CMakeCache from another path); build in a scratch dir. The page must keep test strings in tests/test_web_page.cpp (no innerHTML, no http:// so SVG icons come from an inline <symbol> sprite cloned via a <template>, never createElementNS).
