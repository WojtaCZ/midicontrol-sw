---
title: Freezing one player thread with ptrace delays the stall exit until the tracer detaches
author: Dev
date: 2026-11-22
branch: main
cost: { minutes: 4, tokens: 32652 }
id: l-6ad9d922
scope: repo
status: active
src: agent
touches:
  - src/main.cpp
---

## Decided
Bench 10.7 (2026-11-22) froze one player thread from a helper container: `docker run --rm --pid=host --cap-add SYS_PTRACE --security-opt apparmor=unconfined -v <HOST path>:/s:ro alpine /s/freeze <host TID> <s>` (static C tool, PTRACE_SEIZE + PTRACE_INTERRUPT on a single TID). The base reader thread is the one blocked in read(fd 4) (/proc/<pid>/task/*/syscall). Measured with the current Watchdog in src/main.cpp: stall seen +13 s, `_Exit(70)` at +43 s.

## Tried and ruled out
`kill -STOP` on one TID stops the whole process (watchdog too); PID 1 ignores signals from inside its namespace.

## Watch out
After `_Exit(70)` the process does not die while a thread is still ptrace-stopped: docker reported `die 70` only when the helper detached (+90 s). Stop the helper right after the "exiting for a restart" log line. The `-v` source must be a HOST path (the host daemon resolves it), so put the tool in the project folder, not the dev container scratchpad.
