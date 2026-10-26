---
title: KeepPowerOn=false switches the organ off after a song even when it was switched on at the base
author: Dev
date: 2026-10-26
branch: device-info
cost: { minutes: 1, tokens: 372677 }
id: l-692aea19
scope: repo
status: active
src: agent
---

## Decided
User (2026-10-26): keep it. CommHandler follows the base's SSR from STATUS_RESP (a song started with power already on
skips the warm-up), but at the song's end organOff() runs whenever KeepPowerOn=false, no matter who switched the power
on, because the setting means "no power between songs" and the user wants it to apply to manual power-on too.
## Watch out
Do not add "leave manually enabled power alone" logic without asking: it was offered and declined.
