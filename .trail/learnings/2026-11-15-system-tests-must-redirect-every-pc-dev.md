---
title: "System tests must redirect every PC device lookup, and read the OLED from frameBuffer, not screenBuffer"
author: Dev
date: 2026-11-15
branch: audit/step3-tests
cost: { minutes: 15, tokens: 277357 }
id: l-8b65ffa1
scope: repo
status: active
src: agent
touches:
  - tools/systest/DESIGN.md
---

## Decided
- The virtual tier (tools/systest, DESIGN.md sec. 2) runs the real binary against a fake base. The redirect is done through test-only env overrides MIDICONTROL_SYSFS_ROOT / MIDICONTROL_DEV_ROOT / MIDICONTROL_MIDI_KEY, not config keys, so nothing changes for the user.
- Device screens are observed non-halting over SWD from the global `frameBuffer` (Oled::OledBuffer, SH1106 stride 131, x offset 2); `screenBuffer` is dead code.

## Watch out
- Every lookup has to honour the overrides: findMidiControlPort, findMidiControlLogPort, baseInBootloader and usbIdOf, which has a literal "/sys" in serial_port.cpp. Miss one and a virtual instance opens the real log CDC with TIOCEXCL+flock and takes it from a bench run.
- MIDI port discovery matches names containing "MIDIControl" and takes the last hit, and recording (arecordmidi) uses that same input port. A test capture client must not contain that string.
- A fresh midicontrol-fw worktree lacks the untracked lib/bootloader, lib/menu and lib/ui content, so host UI builds there can differ from the flashed build.
