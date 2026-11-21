---
title: "Bench rig: remote OLED sleeps after 30 s idle and swallows the next key; rig.key() must wake it first"
author: Dev
date: 2026-11-15
branch: audit/step3-tests
cost: { minutes: 30, tokens: 228230 }
id: l-12f51430
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
  - docs/status-web-and-service.md
  - src/comm.cpp
  - tests/test_comm_organ.cpp
  - tools/systest/bench/actions.py
  - tools/systest/bench/burnin.py
  - tools/systest/bench/criteria.py
  - tools/systest/bench/criteria.toml
  - tools/systest/bench/frame.py
  - tools/systest/bench/latency.py
---

## Decided
- tools/systest/bench/rig.py key() reads remote `Oled::sleeping` (SWD byte read via swd.symbols) and, if set, sends BACK
  (taken as a wake press only, no navigation) and waits for it to clear before the real key. Counted in key_stats["wakes"].
- burnin.py waits for Monitor.ready (one SWD sample of base and remote) before the first action; otherwise a remote_reset
  right after rig start is not seen as a SysTick drop and expected_resets undercounts.

## Tried and ruled out
- Treating the 2026-11-15 pilot play failures (codes "03", "12", "06A", "01", first digit missing) as PC/link bugs: all
  came after more than 30 s idle; the firmware (flow tools/screens/flows/24_oled_sleep.sim) drops the first key as a wake
  press. Product behaviour, rig bug.

## Watch out
- Any new bench script that injects keys through the mailbox directly (not rig.key) hits the same drop after idle.
