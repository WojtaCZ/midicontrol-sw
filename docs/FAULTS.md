# Known faults: handoff list (2026-11-12)

This is the starting point for a fresh agent. It lists faults that were **seen on hardware or in the code**, and work
that is not yet verified, in priority order. What the system *should* do is in `docs/SYSTEM.md`. That file holds the
requirements (SYS-*), the owner decisions D1-D31, the gap list in sec. 6 and the open items O-*. The hardware is in
`midicontrol-fw/docs/HARDWARE.md` (base) and `midicontroller-fw/docs/HARDWARE.md` (remote). Read
`.trail/learnings/` in the repo you work in before you explore.

Workspace layout (`/workspaces/MIDICONTROL-*`):

| Directory | What it is |
|---|---|
| `midicontrolpy-sw` | PC software, service and status web. Holds the main copy of `docs/SYSTEM.md` |
| `midicontrol-fw` | Base firmware (STM32G431) |
| `midicontroller-fw` | Remote firmware (STM32G071) |
| `midicontrol-bootloader` | Bootloader |
| `midicontrol-protocol` | Packet protocol + `INDICATOR-BUS.md`. It is the `lib/protocol` submodule of both fw repos |
| `midicontrol-hw`, `midicontroller-hw` | KiCad (historical) |

## 1. Blocking for installation

### 1.1 DIN MIDI link dead (SYS-NOTE-05, D31, gap 28)

- **Symptom.** The base's 5-pin DIN MIDI has never worked on the bench, in either direction.
  - On the bench the Otraki USB-MIDI interface stands in for the organ.
  - `tools/basetest` `test_midi` fails ("DIN dead").
  - The watchdog stuck-note cleanup (`midi::sendStuckNoteCleanup`) is therefore unverified on a real organ.
- **Owner decision D31.** The fault must be found and fixed. Both directions must be verified before installation.
- **Signal path** (`midicontrol-fw/docs/HARDWARE.md` sec. 4.2 and the "MIDI" line in the hardware section):
  - TX: PB10 USART3 TX, 31250 Bd → U7 74LVC1G17 (+5VL) → R7 → 3157 port switches (U1/U2/U9/U10/U12) → DIN pins 4/5.
    Pin 2 goes through R2/R3 330R. R21 1k also goes to the ATtiny (U14.14, PB2).
  - RX: port → switches → U13 6N138 opto (D5, R15) → PB11.
  - The **ATtiny1607 U14** runs its own fixed firmware and decides the port routing:
    - It drives MIDI_TX_SEL (PA7), MIDI_RX_SEL (PA6) and MIDI_TX_VCC_ON (PC0 → U6).
    - It also drives MIDI_RX_SENSE_ON/SAFE (PA5/PA4, Q3).
    - The G431 leaves its own PA5/PA6/PA7 unconfigured.
- **Suspects** (none checked yet):
  1. The cable or connector, J2 "MIDI A" / J1 "MIDI B" (JST XH 5p).
  2. The ATtiny chose the wrong port direction. Which of MIDI A / B is OUT and which is IN is unknown (HARDWARE base
     open item 8).
  3. MIDI_TX_VCC_ON is never enabled, so the TX buffer side has no supply.
  4. TX polarity to the DIN pins has not been verified (open item 9).
  5. The ATtiny firmware is not available (open item 7). It also sits on I2C1, together with the OLED at 0x3C.
- **Suggested approach.**
  1. Scope PB10 while the base sends.
  2. Follow the signal through U7 to the switches and the DIN pins, and check the +5VL / U6 supply.
  3. Read PC0/PA6/PA7 from the ATtiny side.
  4. Loop TX back to RX with a DIN cable.
  - Use `superpowers:systematic-debugging`.
  - Record the cause as a trail learning in `midicontrol-fw`.
  - Close SYS-NOTE-05 and gap 28 in `SYSTEM.md`, then run `tools/sync-system-doc.sh`.
- **Depends on it:** recording from the organ (gap 16, SYS-REC-02) and the default registration / stops at the organ
  (O-14).

### 1.2 Base safety on PC loss (gaps 1, 2)

- The base does not send All Notes Off and does not switch the SSR off when the PC goes away (D1).
- A PC start does not send All Notes Off / organ off (D14).
- These are code gaps, not hardware faults. They matter for the church.
- **Base side fixed 2026-11-12** (local, not pushed). `recovery::PcLossGuard` (`midicontrol-fw/src/logic/recovery.hpp`)
  fires once on PC present → lost (USB unmounted or keepalive timeout). `router::logChanges` then sends
  `midi::sendStuckNoteCleanup()`, switches the SSR off, syncs the menu checkbox and logs
  "PC lost: all notes off, organ power off". A base that never saw a PC keeps power (D19).
  Tests: host `PcLossGuard.*`, hw `tools/basetest/test_pc_loss.py` (keepalive pause → SSR off within 6 s + 3 s). The
  DIN half stays unverified until 1.1 is fixed.
- **PC start fixed 2026-11-12** (local, not pushed). `mc::StartupReset` (`midicontrolpy-sw/src/startup_reset.cpp`)
  sends All Notes Off on all 16 channels and organ power off on the first keepalive ACK, unless a command already claimed
  the organ. Tests: `tests/test_startup_reset.cpp` (6). The wiring in `comm.cpp` has no unit test; on the bench the
  service logs "Startup: All Notes Off sent on all 16 channels." and "Startup: organ power off." (seen in the
  `test_web_update.py` service logs).

## 2. Faults seen on the bench

| # | Fault | State | Where |
|---|---|---|---|
| 2.1 | Web base update failed with "cannot open bootloader port /dev/ttyACM0" about 1 s in. The base was left in the bootloader (reproduced 2 of 2). Cause: the service opened the new CDC port only once, while the `/dev` node was still appearing | **Fixed and pushed** (`0dbd924`, `4a58151` on `origin/main`: `mc::openWhenReady` plus tests). **Re-tested on hardware 2026-11-12**, all pass: 3 base updates in one service run (~13 s each, the service reconnects by itself), one remote update (152 s, same as the CLI's 143 s; link back 12 s later), busy (409 "already running" and the CLI `update` says busy while the page and `/api/status` keep answering), probe reset at 7 % (the job ends "failed: chunk at offset 8400 failed", the base stays in its bootloader, the CLI updater recovers it). Tests: `midicontrol-fw/tools/basetest/test_web_update.py`. A real USB unplug was not done (no switchable hub); the probe reset stands in for it. Note: the web page cannot itself recover a base left in its bootloader (POST answers 409 "the base is not connected"); the CLI `update base` can | `midicontrolpy-sw` |
| 2.2 | The first BLE attempt after a base boot fails (P1) | **Fixed 2026-11-12** (local, not pushed). The remote still holds the link to the base that just reset, loses it ~2 s into the boot and restarts advertising (blocking `$$$` commands). That collides with the base's first CE: %CONNECT%, then %ERR_CONN% ~120 ms later before %STREAM_OPEN%, and the module connects again by itself ~200 ms later. Proof: with the remote module held in reset through the base boot 0 of 3 boots fail, normal boots 3 of 3. The link always came up on the first CE, only the counters said otherwise: a link that ends before STREAM_OPEN is now a failed setup (`linkstats::Stats::onLost` returns false, both fw repos), logged as "connect failed during setup". Tests: `tools/basetest/test_ble_boot.py` (3 boots, counters 0/0, stream open within 10 s), host `LinkStats.SetupFailureBeforeStreamOpenIsNotALoss` | `midicontrol-fw`, `midicontroller-fw` |
| 2.3 | The base ST-Link V3 gets only the IDCODE, AP access fails ("examination failed"). Probe, cable or MCU state? | Open; the base is updated over USB meanwhile | bench |
| 2.4 | `basetest` on 2026-11-07: 96 passed, 21 failed. Most failures are setup: no logic analyzer, remote ELF missing, `BASETEST_UPDATE_*` unset, DIN dead. Real ones: `test_set_current_on_off` uses a stale menu index; `test_log::test_positions_survive_uint32_wrap` gets a stray partial line in the `level d` reply | **Fixed 2026-11-12** (local). `test_set_current_on_off`: the power checkbox moved to `GUI::settingsItems[2]` (`test_cdc_local.py`). Wrap test: it moved the ring tail, not the head, to just below 2^32, a state the firmware never reaches (it rebases once tail >= 2^31), so the masked head jumped and the base sent line stubs; it now moves the head (`test_log.py`). Also `loglink.py` drops the partial first line after an open (`_synced`). The setup failures need the bench pieces (LA, remote ELF, `BASETEST_UPDATER`) | `midicontrol-fw/tools/basetest` |
| 2.5 | Pairing tests still use the RN4870 address `D88039FE5996` (also in `src/logic/ble_status.hpp`). The RNBD350 remote is `40843204625F` | **Already resolved**: `test_pairing.py` and `test_ble_security.py` use `40843204625F`. The RN4870 address left in `ble_status.hpp` and `test_ble_status.cpp` is only an example of the RN4870 scan/connect line format, which the parser still accepts | `midicontrol-fw` |
| 2.6 | Minor web issues: POST `/api/firmware` without a target answers 413 (expected 404); a chunked body says "the file is empty"; GET answers 404 (expected 405); board ID 7 shows "other unit" in the service but "unknown unit" in the page; `fw_update_msg` stays at "Requesting reboot into bootloader..." during the transfer | **Fixed 2026-11-12** (local, not pushed). 405 with `Allow: POST`; board ID 7 reads "unknown unit (board ID 7)" in both (`fwimage::Check::UnknownBoard`); `fwupload::TransferProgress` sets "Sending the image: N of M bytes". The 404/411 items were already fixed. Tests: `http_405_carries_allow`, `fwimage_unknown_board_id_reads_as_on_the_page`, `fwupload_transfer_progress_updates_the_message` | `midicontrolpy-sw` |
| 2.7 | The HTTP server does not check the Host header (DNS rebinding could PUT stops) | **Accepted, not a fault (owner, 2026-11-12):** `SYSTEM.md` SYS-WEB-05 (D4) says "no authentication, no Host check: accepted", the church LAN is trusted. A Host check was written and dropped on the owner's decision; nothing to test | `midicontrolpy-sw` |
| 2.8 | Song files `512.mid` (verse shifted) and `513.mid` (song number shifted) contain misaligned display SysEx digits. These are authoring mistakes (D24). songcheck should warn about them (gap 31) | **Fixed 2026-11-12** (local, not pushed). songcheck warns when the display digits are not right-aligned (`kWarnVerseShifted` for 512, `kWarnSongShifted` for 513). Tests: `songcheck_aligned_digits_no_warning`, `songcheck_warns_shifted_verse`, `songcheck_warns_shifted_song`, `songcheck_warns_real_shifted_files` | `midicontrolpy-sw` |
| 2.9 | Base BLE wedged after the remote ECC test (2026-11-13): "$$$ timeout" and "RN4870 init failed again" every 60 s, the RST restart did not help, only an MCU reset did. The bond was intact. Cause: the USART2 ISR split statuses with a plain "inside a status" flag. One lost or extra `%` left it set for good, so `CMD> ` was swallowed as status text, and `%REBOOT%` after the RST only flipped the flag (the remote had the same bug, fixed 2026-11-12) | **Fixed 2026-11-13**, bench-verified the same day: the remote ECC test passed without the teardown error and the base relinked; no wedge since. `logic/status_framer` resyncs on a control byte or a >50 ms pause inside a status; `restart()` resets it; a `$$$` timeout is followed by `\r---\r`. Tests: host `StatusFramer.*` (10 cases), then the BLE regression on the new base firmware | `midicontrol-fw` |

### Fault → test index

Run these to check that a fault stays fixed. hw = `cd midicontrol-fw/tools/basetest && ../../.venv/bin/python -m pytest -q -m hw <file>`
(OTA and web tests need `BASETEST_UPDATER`); host fw = `cmake --build build-host && ./build-host/logic_tests` in the fw
repo; PC = `ctest` / `midicontrol_tests` in `midicontrolpy-sw`.

| Fault | hw (bench) | host fw | PC |
|---|---|---|---|
| 1.1 DIN | `test_midi.py` (fails until fixed) | | |
| 1.2 PC loss / PC start | `test_pc_loss.py` | `PcLossGuard.*` | `test_startup_reset.cpp` |
| 2.1 web update | `test_web_update.py`, `test_ota.py` | | `open_when_ready_*` |
| 2.2 boot link | `test_ble_boot.py` | `LinkStats.SetupFailureBeforeStreamOpenIsNotALoss` (both fw repos) | |
| 2.4 basetest | `test_cdc_local.py::test_set_current_on_off`, `test_log.py::test_positions_survive_uint32_wrap` | | |
| 2.5 pairing address | `test_pairing.py`, `test_ble_security.py` | `test_ble_status.cpp` | |
| 2.6 web details | | | `http_405_carries_allow`, `fwimage_unknown_board_id_*`, `fwupload_transfer_progress_*` |
| 2.7 Host header | | | none: accepted per SYS-WEB-05 (D4) |
| 2.8 shifted digits | | | `songcheck_warns_*` |
| 3 base GET_STATUS (gap 10) | `test_cdc_local.py::test_status_ble_only_when_secured`, `test_get_status` | `BleLinkUsable.*` | |
| 3 remote peer guard (gaps 6, 7, 8) | `test_remote_guard.py` (8: bond, F4 `CMD>`, F2 `%CONNECT%`, unknown status, other central refused, pairing window stores / closes, own K,1 relinks) | `PeerGuard.*` (midicontroller-fw) | |
| 3 remote RX resync, stuck command mode | `test_remote_status_rx.py` (lost closing / opening '%', `%DATA%` recovery) | `StatusFramer.*`, `StuckCommandMode.*` (midicontroller-fw) | |
| 3 indicator bus (gaps 24, 29) | `test_display.py` (all 8; `test_rx_frame_flag_check`, `test_rx_off_for_the_whole_frame`, `test_quiet_bus_holds_tx`) | `IndicatorFrame.*`, `QuietBus.*` | |

## 3. Robustness and security faults found in code review

These come from the review: "F" items are review faults, "P" items are developer-delegated (D30). See the gap list for
the details.

- **Bootloader.** **Fixed 2026-11-13**, `midicontrol-bootloader` (both bootloaders):
  - A power cut during the vector-page commit could brick the board (F4/F4b, P6, gap 20). The half-programmed
    doubleword carries an ECC double error; the boot check's read took an NMI into `Default_Handler` on every boot. Now
    `NMI_Handler` -> `boot::ecc::handleNmi()` erases the failing app page and the vector page, then resets: the base stays
    in its bootloader with USB up, the remote releases its power hold.
  - An image without a trailer still booted (F5/P6, gap 21). Now only an image with a valid trailer boots. A plain ELF
    loaded over SWD stays in the bootloader; flash `-full.hex` or `-update.hex`.
- **Remote.** **Fixed 2026-11-12** (local, not pushed), `midicontroller-fw` `src/logic/peer_guard`:
  - It accepted connections, frames and OTA from any base (D3, gap 6). Now only the bonded base gets a link that carries
    frames; any other central gets K,1. The module's accept list holds the bond too.
  - It had no "pair new base" action (gap 7). Now Nastaveni -> "Sparovat zakl." opens a 60 s window; the first central
    that connects and secures becomes the bond. No trust on first use: after the upgrade the remote has no bond and must
    be paired once from this menu. Any base in range during the window can take it, so switch an old base off first.
  - It trusted RN status text injected by a peer (F2/F4, gap 8). Now a second %CONNECT% while linked and a "CMD>" we did
    not ask for are ignored, and a refused peer's text is not believed.
  - Residual risk: RN status text and data share one stream, so the bonded base itself could still write status text.
  - Found on the bench while testing this (all fixed, same repo):
    - The RX parser had no resync. One lost '%' left it inside a "status" for good, so every protocol frame went into the
      status buffer: no keepalive reached `link`, and the base dropped the remote while BLE stayed up.
      `src/logic/status_framer` now ends a status on a control byte, an overlong text or "%%".
    - The module sometimes sat in command mode while the firmware thought it was in data mode. The base's frames then came
      as `%DATA:<handle>,<char>,<hex>%` and `---` answered `Err`. The firmware now sends K,1 and leaves command mode
      (`---`) on the next accepted link (`StuckCommandMode`).
      **Likely cause (2026-11-13, from the code and earlier bench traces; not reproduced on demand):** after `$$$` with no `CMD>` within 1 s (the module is busy right after %SECURED%, when
      `requestCodedPhy()` runs), `setMode()` kept DATA and never sent `---`; the module entered command mode late, its
      prompt was ignored as unrequested (F4), and our own frames then filled its command line, so `---` got `Err`. Now a
      `$$$` timeout always sends `\r---\r` (the `\r` ends a partial line). The base engine had the same hole (a late
      `CMD> ` left it in COMMAND with no job: `canSendData()` false) and does the same.
    - The RNBD350 answers our own K,1 with `AOK` and sends **no** %DISCONNECT%. The guard kept the old link up and
      ignored the base's reconnect as F2 text: linked, but no frames. The firmware now ends the link itself after its own
      K,1 (`disconnect()`, `refusePeer()`, the stuck-mode path).
- **Base.** **Fixed 2026-11-12** (local, not pushed): GET_STATUS reported BLE "connected" from %CONNECT%, before the
  link was secured (gap 10). It now reports 1 only for a usable link: connected, stream open and peer accepted
  (SYS-LINK-07, `blestatus::linkUsable`, the same rule as BUSY vs UNREACHABLE).
- **Indicator bus.**
  - **Fixed 2026-11-12** (local, not pushed): the `0xB0` flag of received frames is now checked (SYS-BUS-08, gap 24).
  - **Fixed 2026-11-12** (local, not pushed): the base waits for 25 ms of quiet bus before transmitting (SYS-BUS-05, gap 29).
  - **Fixed 2026-11-12**: the RX DMA came back at DMA TC, about 2 bytes before our frame left the wire, so the echo tail
    could fall into the RX buffer and misalign the next foreign frame. It now comes back at USART TC after an RDR flush.
  - Still to check on a real display: `midicontrol-fw/TODO.md` (Indicator bus).
  - Reference: `midicontrol-protocol/INDICATOR-BUS.md`.

## 4. Not verified yet (no fault known)

- Range walk in the church with Coded PHY S8 at +14 dBm (O-6).
- Remote 20-minute auto-off on battery, and the current drawn with the 10 s RSSI poll.
- `bletest` suites, pairing edge cases, and a reconnect soak of 20+ resets (`midicontrol-fw/TODO.md`, RNBD350 section).
- Opus 910 stop mapping: ProgramOffset and octal numbering (O-14, `midicontrolpy-sw/TODO.md`).
- SSR type (O-4). It decides whether the PA4 drive is enough.

## 5. Housekeeping

- The `lib/protocol` submodule is not bumped in either firmware repo. The new `INDICATOR-BUS.md` is on protocol `main`
  only.
- The `docker-deployment` branch of `midicontrolpy-sw` (worktree `midicontrolpy-sw-docker`) is not merged. CI first run
  and the bench test are pending.
- Several repos have uncommitted changes that are not part of the doc work: generated `inc/git.hpp`, submodule pointers,
  KiCad caches, and `songs/406B.conf`. Ask the owner before you commit or discard them.
