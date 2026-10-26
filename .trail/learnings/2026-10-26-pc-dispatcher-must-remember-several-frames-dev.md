---
title: "PC dispatcher must remember several frames per source: a node pipelines reliable frames"
author: Dev
date: 2026-10-26
branch: device-info
cost: { minutes: 1, tokens: 4014 }
id: l-91aad78f
scope: repo
status: active
src: agent
touches:
  - src/dispatch.cpp
---

## Decided
src/dispatch.cpp keeps the last 8 answered (packetId, ACK/NAK) per source (kRemembered), because a node can have several
reliable frames in flight (the remote: DIAG_RESP + INFO_RESP); when our ACK of the first is lost, its retry arrives
after the second, and a "last frame per source" check executes it again.
## Tried and ruled out
Last-frame-only dedupe: bench 2026-10-26, remote BLE drop + reconnect, four DIAG records stored twice 575 ms apart
(= RETRY_TIMEOUT). Test: dispatch_interleaved_retransmission_is_not_executed_twice.
## Watch out
The same one-frame rule in a receiver is only safe while everything pipelined to it is idempotent; check that before
the PC sends a non-idempotent reliable message alongside its background polls.
