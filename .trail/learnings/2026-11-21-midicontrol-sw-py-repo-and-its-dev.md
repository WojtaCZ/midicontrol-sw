---
title: midicontrol-sw-py repo and its GHCR image are public so the one-line installer works without auth
author: Dev
date: 2026-11-21
branch: main
cost: { minutes: 26, tokens: 171705 }
id: l-5c409e13
scope: repo
status: active
src: agent
touches:
  - docs/bench-acceptance-docker.md
  - CMakeLists.txt
  - docker/tests/test_install.sh
  - docker/tests/test_updater.sh
  - docker/updater/lib.sh
  - tests/test_diag_store.cpp
  - install.sh
---

## Decided
Owner decision 2026-11-21: the GitHub repo WojtaCZ/midicontrol-sw-py and the GHCR package (the player/updater image) are public. install.sh is fetched with `curl ... | sh` from raw.githubusercontent.com, and it runs `docker compose pull` with no registry login. Both steps need anonymous access.

## Tried and ruled out
- Private repo plus a login token on the mini PC: the one-line install and the unattended updater would then need credentials stored on the host. Dropped.

## Watch out
- GHCR packages are private when they are first published. A newly added image name needs its visibility set to public once, after its first CI publish.
- The local gh token has no read:packages scope, so `gh api` cannot show package visibility. Test with an anonymous `docker pull` instead.
- install.sh always runs `compose pull`, so it cannot install an image that exists only locally.
