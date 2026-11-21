---
title: The main SYSTEM.md (expected behaviour of the whole MIDIcontrol system) lives in midicontrolpy-sw; other repos hold copies
author: Dev
date: 2026-11-12
branch: main
cost: { minutes: 1, tokens: 1213 }
id: l-58312655
scope: repo
status: active
src: agent
touches:
  - CLAUDE.md
---

## Decided
2026-11-12, owner's decision: the main copy of SYSTEM.md, the expected-behaviour spec covering PC + base + remote + bootloader + protocol, lives in midicontrolpy-sw. Reason: the PC software controls everything. All the other repos (midicontrol-fw, midicontroller-fw, midicontrol-bootloader, midicontrol-protocol, midicontrol-hw, midicontroller-hw) carry an identical copy, and their CLAUDE.md says the main copy is here. Edit the main copy first, then propagate it.

## Tried and ruled out
- Main copy in midicontrol-protocol (the fw repos already include it as a submodule): offered, but the owner wants the controlling software to own the spec.
- Pointers only, no copies: a repo cloned on its own would have no spec.

## Watch out
Copies can drift. When you change behaviour in any repo, update the main SYSTEM.md here and re-copy it.
