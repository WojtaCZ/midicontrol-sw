---
title: "Base update from the web page needs openWhenReady: bootloader and app CDC are found in sysfs before /dev is openable"
author: Dev
date: 2026-10-31
branch: web-firmware-update
cost: { minutes: 6, tokens: 67588 }
id: l-69a77a76
scope: repo
status: active
replaces: l-9fff5a6a
src: agent
touches:
  - "src/firmware_update.cpp#FirmwareUpdater::updateBase"
  - "src/serial_port.cpp#openWhenReady"
  - "src/console_exec.cpp#ConsoleExecutor::startUpload"
---

## Decided
- `FirmwareUpdater::updateBase` opens the bootloader port (abcd:1235) and, after the reboot, the app port through `mc::openWhenReady`: retry `_comm.reconnect` every 50 ms for up to 5 s, only while `access(R_OK|W_OK)` passes. This is the fix midicontrol-fw learning l-cc33b571 asked for; its "updateBase is only for recovery" no longer holds, since the web page starts updates from the running app.
- `startUpload` checks the busy flag before "base connected": a base update takes the base off USB, so a second upload otherwise got 409 "the base is not connected".
- SIGTERM during an update: the service finishes and waits for the unit to come back (base ~15 s, controller ~32 s) before exiting. Keep it: the base ends in its application, not the bootloader.

## Tried and ruled out
- One open right after `waitForPort`: failed 2 of 2 from the page (bench 2026-10-28), base left in its bootloader, and the page said "base did not reappear yet" although it returned ~8 s later.

## Watch out
- After a controller update over BLE the controller first reconnects still reporting the OLD build, then the bootloader swaps the image (~70 s from start). Read fw_built only once it has settled.
