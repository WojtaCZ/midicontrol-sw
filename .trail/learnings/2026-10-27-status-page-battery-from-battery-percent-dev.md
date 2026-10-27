---
title: "Status page battery %: from BATTERY_PERCENT, else computed from battery_mv on a copy of the remote's Li-ion curve"
author: Dev
date: 2026-10-27
branch: status-page-redesign
cost: { minutes: 6, tokens: 23819 }
id: l-d010daac
scope: repo
status: active
src: agent
touches:
  - docs/status-web-and-service.md
  - include/midicontrol/device_info.hpp
  - src/device_info.cpp
  - src/web_page.cpp
  - tests/test_device_info.cpp
---

## Decided
User (2026-10-27): battery is shown as a percentage, never X/4. src/web_page.cpp battPct() takes battery_percent (INFO_RESP 0x34); a remote firmware without it gets the percent from battery_mv on BATT_CURVE, so the page has 1 % steps before the remote is flashed. Low = <=15 % on battery (was level <=1).
## Tried and ruled out
Falling back to battery_level * 25: showed "100 %" at 4.02 V while charging, and only 5 values.
## Watch out
BATT_CURVE in src/web_page.cpp is a copy of CURVE in midicontroller-fw src/logic/battery_soc.cpp: change both together. The PC fallback is unfiltered, so it still jumps when the remote is held; only the remote's own percent is filtered.
