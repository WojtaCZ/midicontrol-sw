---
title: The daemon always runs as the Docker deployment; the systemd unit is developer-only
author: Dev
date: 2026-11-21
branch: docker-deployment
cost: { minutes: 24, tokens: 111085 }
id: l-5f6b047e
scope: repo
status: active
src: agent
touches:
  - install.sh
  - docker/compose.template.yaml
  - docs/deployment.md
  - packaging/midicontrol.service.in
  - CLAUDE.md
  - docs/manual/docs/sprava.md
---

## Decided
Owner ruling 2026-11-21: production always runs the PC daemon as the Docker deployment (install.sh, docker/compose.template.yaml, docs/deployment.md). The systemd user unit (packaging/midicontrol.service.in) and a standalone `cmake --install` build are for developers only and must never run alongside the container (both hold the base serial port and port 50118). README, CLAUDE.md and the Czech manual (docs/manual/docs/sprava.md) say so.

## Watch out
- Bench sessions that start build*/midicontrol by hand from the dev container contradict this rule; the dev container cannot run the image itself (no mount privilege, dockerd fails on iptables/bind mounts), so a real Docker run happens on the host / mini PC.
- The installer's default tag `stable` only exists after the first `v*` git tag is pushed; until then use `--tag edge`.
- compose has `stop_grace_period: 300s` so `docker stop` does not kill a remote firmware update (~2.5 min).
