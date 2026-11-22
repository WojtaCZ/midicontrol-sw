---
title: "sw ctest also runs inside the docker image build: tests must not read real /sys or files outside the build context"
author: Dev
date: 2026-11-22
branch: main
cost: { minutes: 19, tokens: 93851 }
id: l-3244e992
scope: repo
status: active
src: agent
touches:
  - tests/service_unit_test.sh
  - tests/test_port_select.cpp
  - docs/SYSTEM.md
  - docs/manual/docs/opravy.md
  - docs/manual/facts/remote.md
  - CMakeLists.txt
---

## Watch out
- The Dockerfile build stage runs ctest after the build. It copies only CMakeLists.txt, include, src, tests, songs and packaging, so docker/ (compose.template.yaml) is absent there. tests/service_unit_test.sh now skips the compose check when the file is missing.
- The build sees the host's /sys. A test that calls mc::baseInBootloader() without MIDICONTROL_SYSFS_ROOT fails whenever the bench base sits in its bootloader. Point every port/sysfs test at a FakeSys root (tests/test_port_select.cpp).
- Before pushing sw main, check that `docker build .` passes, not only the local ctest. CI pushes the image from main.
