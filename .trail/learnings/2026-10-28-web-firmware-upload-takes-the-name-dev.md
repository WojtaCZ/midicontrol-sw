---
title: "Web firmware upload takes the <name>-update.bin only; board ID checked in browser, service and device"
author: Dev
date: 2026-10-28
branch: web-firmware-update
cost: { minutes: 9, tokens: 280234 }
id: l-8213fd70
scope: repo
status: active
src: agent
---

## Decided
User (2026-10-28): Advanced → Firmware lets the maintainer drop a firmware file for the base or the controller and flash it; the two must never get swapped. The input is the `<name>-update.bin` that bootlib_add_image() already writes next to the .elf (midicontrol-fw/build/midicontrol-fw-update.bin, midicontroller-fw/build/midicontroller-fw-update.bin). Only it carries the 24-byte trailer with board ID 0x02/0x03 + CRCs, so the swap check needs no new metadata. The check runs three times: page JS (fwInspect, before upload), src/firmware_upload.cpp (fwimage::check against the URL target), device (FW_UPDATE_START target, END trailer).
## Tried and ruled out
Accepting .elf/.hex: no board ID inside, the PC would have to guess the unit from the file name. They are refused with a hint instead.
## Watch out
mc::status::jsonEscape returns the string WITH its quotes; wrapping it in "\"...\"" gave {"error":""..""}; test_firmware_upload checks the exact body now. The upload runs on a ConsoleExecutor worker thread: main.cpp must call exec.waitForUpload() before handler.shutdown(), or stopping the service abandons a half-flashed base. CLI `update` and the page share one atomic busy flag (_updating), not a mutex, because the worker unlocks on another thread.
