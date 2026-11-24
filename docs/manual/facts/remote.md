# Facts: REMOTE (handheld controller, "ovladac")

Phase 1 of the user manual (audit step 4). Source tree: `midicontroller-fw/` (branch audit/step4-manual).
Paths are relative to it unless stated. Code wins over docs; disagreements are in the Open questions.
Screen strings are verbatim (Czech UI, no diacritics).

## 1. Hardware facts relevant to the user

- Display: OLED SH1106 128x64, I2C 0x3C (`inc/oled.hpp:16-21`; `docs/HARDWARE.md` sec. 4.2). Fonts 7x10 and 11x18.
- Keys: 15 matrix keys + OK button; separate ON/OFF button on PB13 (`src/base.cpp:217-218, 254`).
- Layout (`docs/HARDWARE.md` keymap table, switch-by-switch checked):

| Row | Col 1 | Col 2 | Col 3 |
|---|---|---|---|
| R1 | 1 | 2 | 3 |
| R2 | 4 | 5 | 6 |
| R3 | 7 | 8 | 9 |
| R4 | MF | 0 | UP |
| R5 | DOWN | LEFT | RIGHT |

plus OK (separate switch). Physical legends on the case are NOT in the code: see Open questions.

## 2. Keys

Index 0..15 (`src/base.cpp:112-119`): 1=0, 2=1, 3=2, 4=3, 5=4, 6=5, 7=6, 8=7, 9=8, MF=9, 0=10, UP=11, DOWN=12, LEFT=13, RIGHT=14, OK=15.

GUI mapping (`src/base.cpp:123-144`): digits -> Digit0..9; UP -> Up; DOWN -> Down; OK -> Enter; LEFT -> Back ("<", Zpet); RIGHT -> Right (">"); MF -> Left ("unused by the player" comment). Every key press also resets the idle timer (`Power::touch()`).

Press behaviour:
- A key fires on RELEASE, after 20 consecutive ms of release (debounce `RELEASE_MS = 20`, `src/base.cpp:149`). Queue 16 events (`:154`), scan every 10 ms (`:221`, `main.cpp:21`).
- There is NO short/long distinction on matrix keys and OK. No auto-repeat found.
- Only ON/OFF has a hold: see sec. 3.
- While the OLED sleeps the first key only wakes it and is swallowed (`src/menu.cpp:456-463`; flow `tools/screens/flows/24_oled_sleep.sim`).

Key meaning per screen:

| Screen | Up | Down | OK | Left "<" (Back) | Right ">" | Digits | MF |
|---|---|---|---|---|---|---|---|
| Splash | - | - | dismisses to main menu | skips (flow 03) | - | - | - |
| Main menu (Hlavni menu) | previous item | next item | open item | - | - | - | - |
| Settings (Nastaveni) | previous | next | open item / Zpet leaves | back | - | - | - |
| Info screens (Baterie, Bluetooth, Verze FW) | - | - | dismiss | back | - | - | - |
| Pairing window (Parovani) | - | - | leave (closes the window if still waiting) | leave | - | - | - |
| Pairing dialog (Sparovat?) | informational only (ruling R34), no accept/reject needed; see Open questions | | | | | | |
| Player (Prehravac) | no digits: open song list; digits: cycle letter forward | no digits: open song list; digits: cycle letter backward | digits: play; none while playing: stop (or repeat anchor if Repeat selected) | delete letter, then last digit; Idle+empty: leave; playing+empty: select Stop | select Repeat (only when playing and an anchor was received) | enter code | unused |
| Song list (Pisne) | previous row / previous page | next row / next page | play selected; leaves on "Zpet", error or empty list | leave | - | - | - |
| Overlay Odpojeno | - | - | opens Nastaveni | ignored | ignored | ignored | ignored |
| FW update screen | all keys ignored while running (flow 27) | | | | | | |
| Power-hold / message screens | no key handling | | | | | | |

Sources: `src/menu.cpp:456-470, 469`; `src/player_screen.cpp` (keypress, addDigit); `src/song_list_screen.cpp`; flows `03`, `20`, `27`.

## 3. Power on / off

- On: ON/OFF switch (SW1) or VBUS raises 3V3_ON; firmware holds it with PA1 (`docs/HARDWARE.md` sec. 4.4).
- MF key (D14): held 3 s = emergency organ off. After the debounce the screen "Vypnout organ" / "Drzte 3 s" with a progress bar appears; at 3 s the remote sends EMERGENCY_OFF (0x15) to the base, which switches the organ relay off and sends All Notes Off itself, without the PC, and forwards EMERGENCY_OFF to the PC (2026-11-24: the PC stops playback, stops and saves a recording, clears pending state, organ state off; base shows a splash, then main menu) (`src/base.cpp` emergencyTick, `src/menu.cpp` emergencyHoldScreen). A tap (screen not shown) is still Left. An older base NAKs the command.
- Off by hand: hold ON/OFF 3 s (`HOLD_MS = 3000`, `src/base.cpp`; 5 s until 2026-11-23, item 12). After 20 ms of holding the screen "Vypnuti" / "Drzte 3 s" with a progress bar appears (percent = held ms * 100 / 3000). Releasing early cancels. At 3 s the unit powers off immediately with no message (`:302, 338`).
- A press counts only after the ON/OFF button has been released once since boot (`armed`, `:267`).
- Off automatically (idle): message "Vypnuti" / "Necinnost: vypnuti", shown 1 s (`src/base.cpp:329, 332-366`).
- Off for low battery: message "Vypnuti" / "Slaba baterie" (`src/main.cpp:467-470`).
- After shutdown the firmware waits for an ON/OFF press, then resets (`src/base.cpp:307-320, 364-365`).
- Splash after start: device `MIDIControl`, type `CONTROLLER`, git revision, branch; closes by itself after 3 s (`main.cpp:206`) or on OK (`src/menu.cpp:368-380`).

## 4. Battery

- Measured on battery voltage, smoothed (about 8 s time constant), shown as percent and a 5-step icon (`src/logic/battery_soc.hpp:11-50`).
- 100 % = 4180 mV and above; 0 % = 3300 mV and below, linear between points (`battery_soc.hpp:45-47`).
- Icon level = (percent+12)/25: 0 empty .. 4 full (`battery_soc.hpp:50`).
- Shown percent only moves down on battery, up on USB; the opposite direction needs 3 points of margin (`battery_soc.hpp:52-57`).
- Icon: top right at x=101, shown on main, settings, player and a ready song list (`src/menu.cpp:404-449`). Charging animation: one level per 13 frames, 5 levels.
- Battery screen (Nastaveni > Baterie, `src/menu.cpp:191-212`): lines `Napeti: x.xx V` (or `Napeti: --`), `Uroven: n %`, `Stav:` Nabiji (charging) / Nabito (charge complete) / Nenabiji (USB present, not charging) / Vybiji (discharging), `USB: Pripojeno|Odpojeno`, `CC:` 3.0 A / 1.5 A / USB (0.5 A) / -- (`src/logic/input_limit.cpp:26-33`).
- Warning (D2): at 15 % a 5 s "Slaba baterie" splash is shown once (Enter/Back dismiss it, USB suppresses it, re-arms at 20 %); a diagnostic LOW_BATTERY record is also written (`src/logic/low_warning`, `src/battery.cpp`).
- Invalid measurement (D1): no ADC samples, ADC init failure or 3 empty seconds -> "Chyba mereni", no percentage, no critical shutdown (`src/logic/battery_validity`).
- Charger watchdog (D4): REG05 is read back in every poll; a mismatch is rewritten and logged as CHARGER_FAULT arg 0x200 | REG05 (`src/logic/bq_regs.hpp`).
- Song list (D12): GET_SONG_LIST carries a u16 offset (1 byte below 256, 2 bytes from 256), the next page starts after the entries received, no page cap (`src/logic/song_page.hpp`).
- Replace-base confirm "Nahradit zakl.?" (D5), unknown state "Stav neznamy" (D7), recording info screen "Nahravani" (D10); no record entry on the remote (D9).
- Critical: batt mean < 3300 mV for 5 consecutive 1 s measurements, not on USB -> shutdown with "Slaba baterie" (`src/battery.cpp:28-29, 158-163, 204`; `main.cpp:467-470`).
- Charging: BQ24195 charger; charging when pre-charge or fast-charge state (`src/battery.cpp:176-178`). Charger termination 256 mA, 12 h timer (`docs/HARDWARE.md` 4.3).
- Display does not sleep while the Baterie or Bluetooth info screens are open (`src/menu.cpp`, noted in sec. earlier).

## 5. Auto-off

- Timeout 20 min (`src/logic/idle_off.hpp:6`), checked every 1 s (`main.cpp:195`).
- The clock is stopped (never off) if: auto-off disabled by a service flag (`autoOffDisabled`), or player busy AND PC alive, or USB (VBUS) present (`src/logic/idle_off.cpp:3`).
- Any key resets it.

## 6. OLED sleep

- Display switches off after 30 s without input (`inc/oled.hpp:24`; `src/oled.cpp:16`; flow 24 expects sleeping=1 after 31 s).
- The first key press only wakes it (the key is swallowed). Wake also happens on screen change events (`Oled::wakeupCallback()` in `src/menu.cpp`, many sites).
- While the display sleeps the unit stays on; auto-off still counts.

## 7. Screens and menus

Main menu "Hlavni menu": items "Prehravac", "Nastaveni" (`src/menu.cpp:98-103`).

Settings "Nastaveni": "Baterie", "Bluetooth", "Verze FW", "Sparovat zakl.", "Zpet" (`:81-89`).

Info screens (OK or Back dismiss):
- Verze FW: `Revize: %s`, `Vetev: %s`, `Datum: %s`, `Cas: %s` (`:334-341`).
- Baterie: sec. 4.
- Bluetooth (`:214-226`): line 0 module health `Modul: OK` / `Modul: OK, bez RST` / `Modul: neodpovida` / `Modul: mlci` / `Modul: ?` (`src/logic/ble_status.cpp:57-64`); `Spojeni: Ano|Ne` (BLE connected); `Sparovano: Ano|Ne` (bonded); `Zakladna: Ano|Ne` (base keepalive alive); `MAC: <addr>|--`.

Pairing a new base (Nastaveni > Sparovat zakl.; `src/menu.cpp:247-309`):
1. Window "Parovani" opens: "Cekam na zakladnu" / "Zbyva 60 s", counts down every second (`Zbyva %lu s`). Window lasts 60 s (SYSTEM.md; `:296`).
2. On success: "Sparovano" + the bonded address.
3. On timeout: "Cas vyprsel" / "Nesparovano".
4. OK or Back leaves (closes the window if still waiting). The base side also opens the window via `Bluetooth::pairRequest`.
- The remote is bonded to a single base. Passkey dialog "Sparovat?" or "Sparovat? Kod %06lu" is informational (R34), never shown with SA,2 (`:230-240, 514-530`).

Player "Prehravac" (`src/player_screen.cpp`):
- Title shows the state: "Zastaveno" (idle), "Spoustim" (starting), "Prehravam" (playing), "Zastavuji" (stopping) (`:306-309`).
- Code field: 4 dash slots + 1 letter slot, underline cursor.
- Info line: transient message for 2.5 s (`MESSAGE_MS = 2500`) or the section label. Label "Sloka 2" is shown as "Sloka: 2", "Sloka 2 +3" as "Sloka: 2 (x3)" (`formatSection`, `:89-91`). 18 characters, longer labels scroll (`:374`).
- Bottom icon buttons (no text): Play while entering; Stop while playing, plus Repeat once an anchor is known.

Song list "Pisne" (`src/song_list_screen.cpp`): loading "Nacitani..." / "Cekam na PC"; timeout 3 s (`inc/song_list_screen.hpp:36`); pages up to 32 (`:35`); last row "Zpet"; empty "Zadne pisne"; errors Zaneprazdneno / Nepripojeno / PC nedostupne / Odmitnuto / Bez odpovedi. No retry, leave and open again.

Overlay "Odpojeno" (since 2026-11-23 it also shows the status icons, as the main menu does): pushed only over the main menu when the base is not alive ("Cekam na zakladnu") or the PC is not alive ("Cekam na PC"); second line "Stisk: Nastaveni". Disappears by itself when both are back. Only OK works (opens Nastaveni) (`src/menu.cpp:120-143, 380, 469`).

FW update (remote receives an image): title "Aktualizace", hint "Prijem dat" (receiving), "Instalace" (installing), "Chyba aktualizace" (failed), with a percent bar (`src/fwupdate.cpp:140-142`; `src/menu.cpp:328, 559`). Keys ignored. SYSTEM.md: stall 20 s, result 10 s; duration about 75 s (1M PHY) / 2 min 32 s (Coded S8).

## 8. Song code entry, play, stop

- Code: 1-4 digits, value <= 1999, optional letter A-D (rule SYS-USE-22; `src/logic/song_code.hpp:7`, `song_code.cpp:18-25`). "012" and "12" are different codes.
- Digit keys 0-9 add a digit. A fifth digit clears the entry and starts a new code (`addDigit`). A 4-digit value above 1999 is refused with "Nejvyse 1999".
- Letter: UP/DOWN with at least one digit cycles none, A, B, C, D, none (UP forward, DOWN backward). Example 406B = digits 4,0,6 + UP x2.
- Corrections: LEFT ("<") deletes the letter first, then digits one by one.
- Play: OK with digits -> PLAY_SONG; works from any state (the PC switches over). Unknown code: "Neznama <kod>".
- Stop: OK with no digits while Starting/Playing -> STOP_SONG; if Repeat is selected (RIGHT) -> REPLAY_ANCHOR instead ("add verse"/repeat the section). LEFT with empty entry while playing selects the Stop button.
- Pause / resume: NOT AVAILABLE on the remote ("Pause is not offered: the protocol has no resume" comment in `src/player_screen.cpp`). SYSTEM.md SYS-USE-09/21: see Open questions.
- Add verse: the section label "Sloka 2 +3" means verses were added; Repeat button replays the anchor. Screens 24, 25.
- Empty entry + UP/DOWN opens the song list; OK plays the selected song.

## 9. Link / PC loss

- Base keepalive every 2 s, loss after 6 s (`lib/protocol/inc/protocol/constants.hpp:42-43`). Command retry 500 ms (`:39`).
- No BLE: no wireless icon. BLE up, no keepalive: icon `wireless_1`. Base alive: icon `wireless_4` (`src/menu.cpp:404-449`).
- Overlay "Odpojeno" (sec. 7). PC gone signalled by the base (KEEPALIVE pcAlive=0): player is reset to idle (`src/link.cpp:190`; `main.cpp:407`).
- Messages in player: "PC nedostupne", "PC zaneprazdneno", "Bez odpovedi", "Nepripojeno", "Zkuste znovu".

## 10. Timeouts (all)

| What | Value | Source |
|---|---|---|
| Key debounce (release) | 20 ms | `src/base.cpp:149` |
| ON/OFF press recognised | 20 ms | `src/base.cpp:258` |
| ON/OFF hold to power off | 3 s | `src/base.cpp:259` |
| Splash auto-dismiss | 3 s | `main.cpp:206` |
| Player message | 2.5 s | `src/player_screen.cpp` MESSAGE_MS |
| Message before shutdown | 1 s | `src/base.cpp:332-366` |
| OLED sleep | 30 s | `inc/oled.hpp:24` |
| Auto-off | 20 min | `src/logic/idle_off.hpp:6` |
| Song list response | 3 s | `inc/song_list_screen.hpp:36` |
| Pairing window | 60 s | `src/menu.cpp:296`, SYSTEM.md |
| Keepalive period / loss | 2 s / 6 s | `constants.hpp:42-43` |
| Command retry | 500 ms | `constants.hpp:39` |
| Critical battery | 5 x 1 s below 3300 mV | `src/battery.cpp:28-29` |
| Battery scheduler / sample | 1 s / 47 ms | `main.cpp` |
| Advertise retry | 1 s, then 30 s | `main.cpp` |

## 11. Strings table (display strings)

| String | Screen | Meaning | Source |
|---|---|---|---|
| MIDIControl / CONTROLLER | Splash | device name and type | `src/menu.cpp:368-380` |
| Hlavni menu | Main | title | `src/menu.cpp:103` |
| Prehravac | Main | item: player | `:99` |
| Nastaveni | Main/Settings | settings | `:89, 100` |
| Baterie | Settings, info | battery info | `:82, 378` |
| Bluetooth | Settings, info | BLE info | `:83, 379` |
| Verze FW | Settings, info | firmware version | `:84, 377` |
| Sparovat zakl. | Settings | start pairing a base | `:85` |
| Zpet | Settings, Song list | back | `:86`; `song_list_screen.cpp:47,167` |
| Napeti: %u.%02u V / Napeti: -- | Baterie | battery voltage | `:195-197` |
| Uroven: %u %% | Baterie | percent | `:199` |
| Stav: Nabiji / Nabito / Nenabiji / Vybiji | Baterie | charging / charged / USB not charging / discharging | `:202-206` |
| USB: Pripojeno / Odpojeno | Baterie | USB present | `:207` |
| CC: 3.0 A / 1.5 A / USB (0.5 A) / -- | Baterie | Type-C advertised current | `:209`; `input_limit.cpp:28-33` |
| Modul: OK / OK, bez RST / neodpovida / mlci / ? | Bluetooth | BLE module health | `ble_status.cpp:59-64` |
| Spojeni: Ano/Ne | Bluetooth | connected | `:220` |
| Sparovano: Ano/Ne | Bluetooth | bonded | `:221` |
| Zakladna: Ano/Ne | Bluetooth | base alive | `:222` |
| MAC: %s / -- | Bluetooth | peer address | `:223` |
| Revize: / Vetev: / Datum: / Cas: | Verze FW | git revision / branch / build date / time | `:336-339` |
| Parovani | Pairing window | title | `:258` |
| Cekam na zakladnu | Pairing, overlay | waiting for the base | `:120, 125, 295` |
| Zbyva %lu s | Pairing | seconds left | `:271, 296` |
| Sparovano | Pairing | paired OK | `:278` |
| Cas vyprsel / Nesparovano | Pairing | window expired | `:282-283` |
| Sparovat? / Sparovat? Kod %06lu | Passkey dialog | informational | `:230, 516-518` |
| Ano / Ne | Dialog buttons | yes / no | `lib/ui/src/confirm_screen.cpp:114-115` |
| Odpojeno | Overlay | title | `:121` |
| Cekam na PC | Overlay, Song list | PC not alive | `:125`; `song_list_screen.cpp:38` |
| Stisk: Nastaveni | Overlay | hint: press OK | `:380` |
| Vypnuti | Message, power-hold | shutdown title | `:320, 324` |
| Drzte 3 s | Power-hold / emergency hold | hold 3 s | `:324` |
| Slaba baterie | Shutdown message / 5 s splash | critical battery, 15 % warning (D2) | `main.cpp:467-470` |
| Chyba mereni | Battery screens | invalid battery measurement (D1) | `battery.cpp` |
| Necinnost: vypnuti | Shutdown message | idle auto-off | `base.cpp:329` |
| Aktualizace | FW update | title | `:328` |
| Prijem dat / Instalace / Chyba aktualizace | FW update | receiving / installing / failed | `fwupdate.cpp:140-142` |
| Zastaveno / Spoustim / Prehravam / Zastavuji | Player | idle / starting / playing / stopping | `player_screen.cpp:306-309` |
| Neznama %s | Player | unknown song code | `:124` |
| Nehraje se | Player | stop while nothing plays | `:129` |
| PC nedostupne | Player, Song list | PC unreachable | `:131`; `song_list_screen.cpp:58` |
| PC zaneprazdneno | Player | PC busy | `:132` |
| Odmitnuto | Player, Song list | other refusal | `:133`; `song_list_screen.cpp:58` |
| Bez odpovedi | Player, Song list | timeout | `:136`; `song_list_screen.cpp:60,69` |
| Nepripojeno | Player, Song list | no link | `:138, 207, 215`; `song_list_screen.cpp:41,62` |
| Zkuste znovu | Player | send failed, try again | `:189` |
| Nejvyse 1999 | Player | code above 1999 | `:164` |
| Chybny kod | Player | invalid code | `:178` |
| Sloka: N / Sloka: N (xM) | Player | section label | `:89-91` |
| Pisne | Song list | title | `song_list_screen.cpp:179` |
| Nacitani... | Song list | loading | `:38` |
| Zadne pisne | Song list | empty | `:99` |
| Zaneprazdneno | Song list | busy | `:41` |
| [cislice] OK/Zpet / Nahoru/Dolu OK | Number screen (ui lib) | hint line; used? see Open questions | `lib/ui/src/number_screen.cpp:110` |

About 52 distinct strings (Czech, no diacritics).

## 12. Screen pictures

Path: `midicontrol-sw/docs/manual/docs/img/screens/remote/` (50 PNG + `sheet.png` + `index.html`, exported with `tools/screens/export.sh`, scale 4). Captions 01-38 are inferred from file names, 39-50 from the export output (39_emergency_hold added 2026-11-23); verify against `tools/screens/main.cpp`.

01_splash Splash | 02_main_player Main, Prehravac selected | 03_main_settings Main, Nastaveni selected | 04_settings_battery / 05_settings_bluetooth / 06_settings_firmware / 07_settings_pair_base / 08_settings_back Settings with the item selected | 09_info_firmware Verze FW | 10_info_battery Baterie | 11_info_battery_charging Baterie charging | 12_info_bluetooth Bluetooth | 13_pair_base_waiting, 14_pair_base_countdown, 15_pair_base_paired, 16_pair_base_expired pairing window states | 17_player_idle | 18_player_entry | 19_player_entry_letter | 20_player_starting | 21_player_playing | 22_player_anchor (section label) | 23_player_repeat_selected | 24_player_add_verse, 25_player_add_verse_2 | 26_player_stopping | 27_player_stopped | 28_player_unknown | 29_songs_loading | 30_songs_first | 31_songs_middle | 32_songs_last | 33_songs_page_more | 34_songs_page_two | 35_songs_error | 36_songs_empty | 37_pairing passkey dialog | 38_power_hold | 39_emergency_hold MF held 3 s, "Vypnout organ" / "Drzte 3 s" | 40_fw_update | 41_fw_update_failed | 42_no_base | 43_no_base_back_ignored | 44_no_base_charging | 45_no_base_settings | 46_no_base_back | 47_no_base_pairing | 48_no_pc | 49_base_back | 50_message_low_battery.

Flows (`tools/screens/flows/01..29_*.sim`) run with `tools/screens/run-sim.sh` (not re-run in this phase).

## Open questions

1. Physical key legends on the case (arrows, MF, OK, ON/OFF labels): not in code. Owner to confirm.
2. Low-battery warning below 3450 mV: SYSTEM.md lists a warning, code only records a diag entry; user sees nothing until the 3300 mV shutdown. Intended?
3. Pause/resume (SYS-USE-09/21): remote has none. Doc gap or planned?
4. Pairing dialog "Sparovat?": what does the user do (accept/reject)? Code says informational (R34).
5. Is the Number screen hint "[cislice] OK/Zpet" used on the remote at all?
6. PNG captions 01-38 not verified in `tools/screens/main.cpp`.
7. Is the 60 s pairing window and 3 s song list timeout identical in code constants (60 s read at `menu.cpp:296` text; pairing timer constant not re-read)?
8. SIGNAL_PLAYING accepts 1-8 chars `[0-9A-Z]` (`main.cpp:87-88`, Q-D-17) while entry is 4 digits + A-D.
9. ON/OFF while OLED sleeps: does the hold screen wake it? Not verified.

## Possible bugs

1. Low battery (3450 mV) gives no user-visible warning, only the sudden shutdown (`src/battery.cpp:150-156`).
2. Pairing dialog shows "Sparovat?" although no choice exists (`src/menu.cpp:516-530`), may mislead.
3. Letter can be set before further digits: "4A" then "0" gives "40A" (`addDigit`); harmless but unexpected.
4. Soft shutdown that keeps running waits in a blocking loop (`src/base.cpp:307-320`) until a press.
5. Remote stuck in command mode problem is open (`CLAUDE.md`); user-visible as "Modul: mlci"/no link.
