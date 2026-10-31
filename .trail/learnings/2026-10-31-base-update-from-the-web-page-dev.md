---
title: "Base update from the web page needs openWhenReady: bootloader and app CDC are found in sysfs before /dev is openable"
author: Dev
date: 2026-10-31
branch: web-firmware-update
cost: { minutes: 1, tokens: 2399 }
id: l-9fff5a6a
scope: repo
status: active
src: agent
touches:
  - "src/firmware_update.cpp#FirmwareUpdater::updateBase"
  - "src/serial_port.cpp#openWhenReady"
  - tests/test_serial_lock.cpp
---

## Decided
- `FirmwareUpdater::updateBase` opens both the bootloader port (abcd:1235) and the application port after the reboot through `mc::openWhenReady` (src/serial_port.cpp): it retries `_comm.reconnect` every 50 ms for up to 5 s, and only while `access(R_OK|W_OK)` passes, so a root-only node does not flood the log with open errors. This is the fix midicontrol-fw learning l-cc33b571 (2026-10-24) asked for. That learning's "updateBase is used only for recovery" no longer holds: the Advanced → Firmware page starts updates from the running app.
- On SIGTERM during an update the service finishes the flash and waits for the unit to come back before it exits (base ~15 s, controller over BLE ~32 s). Do not shorten this: the base is left in its application, not in the bootloader.

## Tried and ruled out
- A single open right after `waitForPort`: bench 2026-10-28 failed 2 of 2 from the page with "cannot open bootloader port /dev/ttyACM0", leaving the base in its bootloader. The single open on the way back also made the page say "base did not reappear yet" although the base returned ~8 s later.

## Watch out
- Bench 2026-10-31 with the fix: base from the page 6/6 OK (13-14 s each), controller over BLE 2/2 OK (47 s transfer, ~70 s until the new build is reported). During a controller update the controller briefly reconnects still reporting the OLD build before the bootloader swaps the image: read fw_built only after it has settled.
- A second upload while a base update runs gets 409 "the base is not connected" rather than "already running", because the base drops off USB first; during a controller update it is 409 "already running". The CLI `--cmd "update ..."` gets "busy" and exit 1.
