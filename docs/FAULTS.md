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

## 2. Faults seen on the bench

| # | Fault | State | Where |
|---|---|---|---|
| 2.1 | Web base update failed with "cannot open bootloader port /dev/ttyACM0" about 1 s in. The base was left in the bootloader (reproduced 2 of 2). Cause: the service opened the new CDC port only once, while the `/dev` node was still appearing | **Fixed and pushed** (`0dbd924`, `4a58151` on `origin/main`: `mc::openWhenReady` plus tests). **Not re-tested on hardware**: re-run the web update of the base and the remote several times, busy (409 + CLI during web), page reload, and unplug mid-transfer | `midicontrolpy-sw` |
| 2.2 | The first BLE attempt after a base boot fails (P1) | Open, gap 11 | `midicontrol-fw` |
| 2.3 | The base ST-Link V3 gets only the IDCODE, AP access fails ("examination failed"). Probe, cable or MCU state? | Open; the base is updated over USB meanwhile | bench |
| 2.4 | `basetest` on 2026-11-07: 96 passed, 21 failed. Most failures are setup: no logic analyzer, remote ELF missing, `BASETEST_UPDATE_*` unset, DIN dead. Real ones: `test_set_current_on_off` uses a stale menu index; `test_log::test_positions_survive_uint32_wrap` gets a stray partial line in the `level d` reply | Open | `midicontrol-fw/TODO.md` |
| 2.5 | Pairing tests still use the RN4870 address `D88039FE5996` (also in `src/logic/ble_status.hpp`). The RNBD350 remote is `40843204625F` | Open | `midicontrol-fw` |
| 2.6 | Minor web issues: POST `/api/firmware` without a target answers 413 (expected 404); a chunked body says "the file is empty"; GET answers 404 (expected 405); board ID 7 shows "other unit" in the service but "unknown unit" in the page; `fw_update_msg` stays at "Requesting reboot into bootloader..." during the transfer | Open, cosmetic | `midicontrolpy-sw` |
| 2.7 | The HTTP server does not check the Host header (DNS rebinding could PUT stops) | Open | `midicontrolpy-sw/TODO.md` |
| 2.8 | Song files `512.mid` (verse shifted) and `513.mid` (song number shifted) contain misaligned display SysEx digits. These are authoring mistakes (D24). songcheck should warn about them (gap 31) | Open | `midicontrolpy-sw` |

## 3. Robustness and security faults found in code review

These come from the review: "F" items are review faults, "P" items are developer-delegated (D30). See the gap list for
the details.

- **Bootloader.**
  - A power cut during the vector-page commit can brick the board (F4/P6, gap 20).
  - An image without a trailer still boots (F5/P6, gap 21).
- **Remote.**
  - It accepts connections, frames and OTA from any base, not only the bonded one (D3, gap 6).
  - It has no "pair new base" action (gap 7).
  - It trusts RN status text injected by a peer (F2/F4, gap 8).
- **Base.** GET_STATUS reports BLE "connected" before the link is secured (gap 10).
- **Indicator bus.**
  - The `0xB0` flag of received frames is not checked (SYS-BUS-08, gap 24).
  - There is no wait for a quiet bus before transmitting (SYS-BUS-05, gap 29).
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
