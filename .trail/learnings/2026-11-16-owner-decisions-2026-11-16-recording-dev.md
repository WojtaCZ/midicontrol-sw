---
title: "Owner decisions 2026-11-16: recording only from base, no pause, add-verse hidden without verses, default registration"
author: Dev
date: 2026-11-16
branch: audit/step4-manual
cost: { minutes: 29, tokens: 107669 }
id: l-02963c0a
scope: repo
status: active
src: agent
---

## Decided
- Recording starts only from the base menu `Nahraj` (SYS-REC-01); the remote gets no record button. The PC console `record` stays as an admin/debug tool (sprava only).
- Pause/resume (SYS-USE-09/21) is dropped from the spec and the manual; Stop + start again.
- The remote hides add-verse when the song file has no verses (needs a "has sections" flag from the PC).
- Base: SYS-USE-24 Stop / Pridat sloku on the knob while playing gets built. Counter-clockwise = down.
- PC: configurable default registration for songs without a stop file (SYS-USE-20).
- Indicator: short codes right-aligned with blanks; clear on stop only its own number (with Q2-19).
- Master LED off = all LEDs off (B3). Remote warns at 15 % battery. `Sparovat?` Ano/Ne must take effect.
- The system always has a PC; the manual does not cover a no-PC setup.
- Full list: audit/OWNER-ANSWERS.md in the workspace root (not in git).
## Tried and ruled out
- Remote record button / web "save recording as song": rejected; the admin copies and renames recordings by hand.
- "Organ stays as is" for songs without a stop file: rejected in favour of a default registration.
- Shortening the cut base menu labels: rejected; the selected line scrolls.
## Watch out
- The manual leaves out physical descriptions, panel labels, package contents and backups on purpose; don't re-add those TODOs.
