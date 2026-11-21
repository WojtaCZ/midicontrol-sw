---
title: The PC software repo is midicontrol-sw (was the placeholder midicontrolpy-sw / midicontrol-sw-py); the old 2019 MIDIControl-SW is MIDIControl-SW-legacy
author: Dev
date: 2026-11-21
branch: main
cost: { minutes: 12, tokens: 41906 }
id: l-05f310c8
scope: repo
status: active
src: agent
touches:
  - ".github/workflows/ci.yml"
  - CLAUDE.md
  - Dockerfile
  - README.md
  - TODO.md
  - docker/compose.template.yaml
  - docker/tests/test_updater.sh
  - docker/updater/lib.sh
  - docs/FAULTS.md
  - docs/HARDWARE-base.md
---

## Decided
Owner 2026-11-21: the PC software (player, web, Docker deployment) is named midicontrol-sw everywhere: the local folder, GitHub WojtaCZ/midicontrol-sw and the image ghcr.io/wojtacz/midicontrol-sw. The name matches midicontrol-fw, midicontrol-hw and midicontrol-protocol. The name was taken by an old 2019 C project (WojtaCZ/MIDIControl-SW; GitHub names ignore case), so that repo was renamed to MIDIControl-SW-legacy and archived. Nothing was deleted.

## Tried and ruled out
- midicontrol-daemon / midicontrol-docker: rejected. The repo also holds the C++ source and the web UI, and a bare "daemon" or "docker" is too generic for a repo or image name.
- Renaming only the folder and keeping midicontrol-sw-py on GitHub: rejected, because it leaves two names in use.

## Watch out
- Old links to WojtaCZ/MIDIControl-SW now open the current repo, not the legacy one. GitHub's redirect from the old name ended when the name was reused.
- The old name stays on purpose in dated specs (docs/superpowers), audit/ and trail learnings.
- The old GHCR package midicontrol-sw-py is separate from the new one. A new package starts private and has to be made public once after its first CI publish. Installs made before the rename keep pulling the old image until they are reinstalled.
- After the rename, the SYSTEM.md copies inside the fw repos' lib/protocol and lib/bootloader submodules stay stale until the submodule pointers are bumped. tools/sync-system-doc.sh --check reports them.
- A CMake build dir configured before the folder move has the old path in its cache: delete it and configure again.
