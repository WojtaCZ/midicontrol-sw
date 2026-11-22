# Facts: BASE unit ("zakladna") and church indicator ("tabule")

Source: repo midicontrol-fw (branch audit/step4-manual), checked 2026-11-16. Paths are relative to
`/workspaces/MIDICONTROL-b81bba4a/midicontrol-fw/` unless prefixed (`boot:` = `bootloader/`, `proto:` = `lib/protocol/`).
Screen text is Czech without diacritics, quoted verbatim. Rule: code wins over docs; disagreements are in the last two sections.
Nothing was run on hardware. The bench has no church indicator (the SSR is only an LED there; docs/HARDWARE.md).

## 1. Front panel and controls

- Rotary encoder with push button. Pins: A PB0, B PB2, button PB1, organ SSR PA4 (src/base.cpp:19-22). Keys are scanned every 10 ms (src/base.cpp:15).
- Turn one way = Up, other way = Down (quadrature, src/base.cpp:211-217). Press = Enter, fired on RELEASE of the button (src/base.cpp:189-191).
- There is no long-press handling on the base encoder (grep of src/base.cpp, src/menu.cpp: no timing on the button). A "hold" is only used at reset (see 9).
- No other buttons. Hold PB1 while resetting/powering up = USB bootloader (boot:src/main.cpp:168-181, debounced about 20 ms); see section 9.
- Menus: Up/Down move the selection, Enter activates the item. Item types: submenu, checkbox, action, "Zpet" (Back = pop one screen) (src/menu.cpp:94-115, :639-698).
- Any key while the display sleeps only wakes the display and is not executed (src/menu.cpp:653-657).
- Splash: a key press cancels the 3 s auto-close; then Enter is needed to leave it; Up/Down alone do not close it (src/menu.cpp:639-650, :279-281 main.cpp; docs/SYSTEM.md SYS-USE-25).
- Connectors (from firmware; exact panel labels are not in firmware): USB device to PC (composite: CDC protocol, USB-MIDI, CDC log) (src/usb.cpp, docs/HARDWARE.md:41); DIN MIDI in and out on USART3 (src/midi.cpp:208-221); indicator bus on USART1; organ power output via SSR (PA4); BLE module on USART2 (RNBD350). See Open questions Q1 (panel labels).
- Indicators: OLED SH1106 128x64 on I2C 400 kHz; 4 front WS2812B LEDs and 6 rear WS2812B LEDs (src/led.cpp:42-44, :33-40).

## 2. LEDs

Rear strip, 6 LEDs in this order (src/led.cpp:33-40, inc/led.hpp:19-24): USB, Display, Current (organ power), MIDI A (DIN in), MIDI B (DIN out), Bluetooth.
Brightness is fixed at 10 % (INTENSITY 0.1, src/led.cpp:263). Activity blink = 100 ms white (BLINK_DURATION, src/led.cpp:24). Logic: src/led.cpp:260-317.
The menu item "LED" is the master switch (default on, src/settings.cpp:9-27).

| LED | Colour and meaning | Condition | Master off ("LED" unchecked) |
|---|---|---|---|
| USB | green = PC alive (any valid PC packet in the last 6 s); white blink 100 ms on USB-MIDI traffic or PC frames | src/led.cpp:268-275 | off |
| Display | green = indicator connected (sense pin PC13 low); white blink 100 ms when a frame is sent | src/led.cpp:277-283, src/display.cpp:148 | off |
| Current | green = organ power (SSR) on; white blink 100 ms on every switch | src/led.cpp:285-291, src/base.cpp:140-159 | off |
| MIDI A (DIN in) | red constant if "Ident. MIDI" on; off if "Ident. MIDI" off; white blink 100 ms on each received DIN byte | src/led.cpp:293-297, src/midi.cpp:218 | no blink; still red if "Ident. MIDI" on |
| MIDI B (DIN out) | green constant if "Ident. MIDI" on; off if off; white blink 100 ms on each USB-to-DIN packet | src/led.cpp:299-303, src/main.cpp:403-407 | no blink; still green if "Ident. MIDI" on |
| Bluetooth | green = BLE link connected; white blink 100 ms on data | src/led.cpp:310-316, src/bluetooth.cpp:783 | off |

- "Ident. MIDI" (midiRoleIdentifier) only drives the colours of the two MIDI LEDs (grep: settings::midiRoleIdentifier used only in src/led.cpp:262 and src/menu.cpp:505,571). Master off dims only the MIDI LED blinks, not their steady colour (src/led.cpp:299-306).
- Front strip (4 LEDs): all off in normal operation. Only use found: unrecoverable error, 3 blinks of all four LEDs red at 50 % brightness, 500 ms on / 500 ms off, then the unit resets (src/main.cpp:742-757). No other blink patterns exist for LEDs.
- LED colour of the indicator (red/yellow/green/blue) is a different thing: the tabule's own LED, see section 12.

## 3. OLED: sleep and wake

- Sleeps after 30000 ms without input (inc/oled.hpp:24 OLED_SLEEP_INTERVAL; scheduler src/oled.cpp:16). Sleep = display off (command 0xAE), wake = frame resent, then 0xAF (src/oled.cpp:169-184, :222-231).
- Wake by any encoder key (src/menu.cpp:653-657; the key itself is swallowed) and automatically by events: overlay "Odpojeno" appearing/disappearing, now-playing screen changes, pairing results, remote update progress, request failures (src/menu.cpp:142-154, :585, :724-829).
- Every wake restarts the 30 s timer (src/oled.cpp:307). The LEDs stay on while the display sleeps.
- Startup splash: lines "MIDIControl", "BASE", revision (git hash), branch (src/menu.cpp:555-562; src/base.hpp DEVICE_NAME/TYPE). Closes itself after 3000 ms unless a key was pressed (src/main.cpp:279-281).
- Status icons (top right on Hlavni menu, Nastaveni, Pisne and now-playing screens only): small monitor icon = PC alive; wireless icon = BLE connected, animated 75 ms per step for 300 ms after data (src/menu.cpp:595-635).

## 4. Menu tree (exact text, default, storage)

Main menu title "Hlavni menu"; title is "Sparuj ovladac" while no remote is stored (src/menu.cpp:591). Items (src/menu.cpp:109-115):

| Item | Action | Note |
|---|---|---|
| Prehraj | Requests the song list from the PC and opens "Pisne" | Does nothing if the PC is not alive (src/menu.cpp:449) |
| Nahraj | Starts a recording on the PC, or stops a running one (recording::Machine, src/menu.cpp:464-468); no state is shown yet (TODO C3) | Q2 |
| Nastaveni | Opens the settings menu | |

Settings menu "Nastaveni" (src/menu.cpp:94-103). Long labels are cut by the screen width (e.g. "Ident. MI", "Napajeni", "Sparovat ovladac" shows as "at ovladac" when scrolled; see PNGs 06-13):

| Item | Type | Range / default | Stored | Behaviour |
|---|---|---|---|---|
| LED | checkbox | on/off, default on (src/settings.cpp:9-27) | flash, saved at once (src/menu.cpp:497-501) | Master switch for rear LEDs (section 2) |
| Ident. MIDI | checkbox | on/off, default on | flash, saved at once (src/menu.cpp:503-507) | MIDI A red / MIDI B green steady colours |
| Napajeni varhan | checkbox | on/off, default off, NOT stored | not stored; SSR is off after every reset (src/base.cpp:19-22; settings hold only LED, midiRole, remoteAddr, src/settings.cpp:85-112) | Toggles the organ power SSR immediately (src/menu.cpp:465-472) |
| Verze FW | page | - | - | 4 lines (below) |
| Displej | page | - | - | 5 lines (below) |
| Bluetooth | page | - | - | 5 lines (below) |
| Sparovat ovladac | action | - | remote address stored in flash on success | Starts pairing (section 6) |
| Zpet | back | - | - | Returns one screen |

Info pages (any key goes back; src/menu.cpp:477-531, :206-218):
- "Verze FW": `Revize: <git hash>`, `Vetev: <branch>`, `Datum: <build date>`, `Cas: <build time>` (src/menu.cpp:477-480).
- "Displej": `Pripojen: Ano|Ne` (indicator sense), `Pisen: [<digits>]`, `Sloka: [<digits>]`, `Pismeno: <letter>`, `LED: cervena|zelena|modra|zluta|vyp` (src/menu.cpp:512-531). Shows what the base last put on the indicator bus or received from it.
- "Bluetooth": line 1 module state, `Spojeni: Ano|Ne` (BLE link), `Sparovano: Ano|Ne` (peer accepted/secured), `Ovladac: Ano|Ne` (remote answers), `MAC: <12 hex>` or `MAC: --` if no remote stored (src/menu.cpp:206-218). Module line: `Modul: start...`, `Modul: OK`, `Modul: OK, bez RST`, `Modul: neodpovida`, `Modul: mlci` (src/logic/ble_status.cpp:34-42). Meaning: start = module starting; OK = configured and reset confirmed; OK, bez RST = configured but reset not confirmed; neodpovida = module sends bytes but does not answer commands; mlci = module sends nothing (check soldering/jumper JP1, docs/HARDWARE.md:20).
- Settings stored: flash page 63 at 0x0801F800, magic 0xBEEFCAFE, version 2; an older version 1 is migrated but the remote must be paired again (src/settings.cpp:29-46, :85-112).

## 5. Organ power

- SSR on PA4, off after every reset (also after watchdog/fault reset) (src/base.cpp:19-22, src/main.cpp:331,:354-357). Switched on/off by the menu checkbox "Napajeni varhan" or by the PC (SET_CURRENT, src/router.cpp:361-368). The Current LED follows.
- The PC normally switches it on at Play and runs the idle-off policy (PC side, see pc.md). Base backstop: SSR on and no valid PC packet for 2 hours = All Notes Off and SSR off; any PC packet restarts the 2 h; switching the SSR on restarts it too (src/router.cpp:532-537, src/logic/router_core.hpp:107-126).
- PC lost (no valid PC packet for 6 s): SSR off, All Notes Off (CC120 and CC123), overlay "Odpojeno" (src/router.cpp:521-530; proto:inc/protocol/constants.hpp:42-43).
- After a fault or watchdog reset the base sends All Notes Off at start (src/main.cpp:331,:354-357).
- Power cut/power-up: SSR off, display splash, settings (LED, Ident. MIDI, remote address) kept; organ power state NOT kept (see above). Pending songs are not resumed.

## 6. Pairing the remote

- Menu: Nastaveni, "Sparovat ovladac" (src/menu.cpp:101, :490). Screen "Parovani" with text "Hledam ovladac..." while scanning for the BLE device named `MIDIControlRemote` (inc/bluetooth.hpp:26). If the module is not up the text is "BLE nefunguje" (src/menu.cpp:490).
- Timeout 15 s (PAIRING_TIMEOUT, inc/bluetooth.hpp:28). Results: "Sparovano" (src/main.cpp:78), "Ovladac nenalezen" if nothing was found while scanning, "Parovani selhalo" if it failed later (src/main.cpp:83). A key press dismisses the result screen.
- Success stores the remote's address (shown as MAC on the Bluetooth page) in flash. Only one remote can be stored; pairing again replaces it.
- With a stored remote the base reconnects by itself, retry every 5000 ms (src/main.cpp:42, :444); BLE module restart retry 60000 ms (src/main.cpp:110); a stale link is dropped by a watchdog (10 s / 5 s, src/main.cpp:167-204).
- The remote shows its own "Sparovat?" prompt during security setup; the base sends the pairing confirmation only after the data stream is open, one retry (src/main.cpp:558; trail learning "pairing B only after STREAM_OPEN").
- Without a stored remote: main menu title "Sparuj ovladac"; the "Odpojeno" overlay (no PC) shows "Sparuj ovladac" as its hint instead of "Cekam na PC" (src/menu.cpp:138).

## 7. Recording

- "Nahraj" sends RECORD_SONG start/stop to the PC (A7, src/menu.cpp:464-468, src/logic/recording.cpp). The state machine tracks Starting/Recording/Stopping/Busy/NoSupport/Failed, but no screen shows it yet (TODO C3). The PC console `record` also works.
- DIN-in to USB-MIDI forwarding exists (section 8), which is what a PC recording would read.

## 8. DIN and USB MIDI

- USB-MIDI to DIN out: every packet from the PC is sent on DIN; USB and MIDI B LEDs blink (src/main.cpp:400-407).
- DIN in to USB-MIDI: parsed on USART3 (src/midi.cpp:208-221), queued and sent to USB while the USB-MIDI port is mounted; if nothing is mounted the data is dropped so old data does not arrive later (src/midi.cpp:240-248). SysEx from DIN is not interrupted by the display echo SysEx (src/midi.cpp:253-285). USB LED blinks per packet.
- Receive errors on DIN (overrun/framing/noise) drop the message in progress (src/midi.cpp:222-230).
- PC loss sends All Notes Off on DIN (section 5).
- docs/SYSTEM.md SYS-NOTE-05: DIN in/out never worked on the bench (cause not traced). Not bench-verified here.

## 9. Bootloader and firmware update (what the user sees)

- Base update comes from the PC (web page firmware drop zone or `update base <file>` in the CLI). The PC sends ENTER_BOOTLOADER; the base stores a magic in TAMP BKP0R and resets (src/router.cpp:272-278, :601-604). The USB device then re-enumerates as PID 0x1235.
- Forced entry: hold the encoder button (PB1) while powering up or resetting, about 20 ms debounce (boot:src/main.cpp:168-181). base::dfuCheck() in the app has no callers (src/base.cpp:110; dead code).
- Bootloader OLED (title "Aktualizace", progress bar, hint line): `Cekam na PC` at start (boot:src/main.cpp:123), `Nevypinejte` while receiving and after 100 % (boot:src/main.cpp:162-164), `Chyba, opakujte` on a failed transfer or when the PC is silent for 20 s (boot:src/main.cpp:160,:264-266; 20 s = kStallCycles line 118 area). After a complete valid image the bootloader resets into the new firmware (boot:src/main.cpp:268-273).
- Image check: a stale trailer makes the bootloader refuse the app; flash the `-update.hex`/`-full.hex` only (docs/HARDWARE.md:21).
- Remote firmware update relayed through the base: base OLED shows a progress screen "Aktualizace" with a bar and the text `Prenos do ovladace`, then `Instalace ovladace`, or `Chyba aktualizace` (src/router.cpp:564-566, src/menu.cpp:442, :753). The screen wakes the display.

## 10. What each screen means (and when it appears)

See the screen list in section 13. Behaviour of the non-menu screens:
- Odpojeno (overlay): PC not alive. Covers the main menu only; "Cekam na PC" (or "Sparuj ovladac" if no remote), second hint "Stisk: Nastaveni". Enter opens Nastaveni; leaving Nastaveni returns here. It disappears by itself when the PC is back (src/menu.cpp:120-160, :567).
- Pisne (song list): up to 32 songs (MAX_SONGS, src/menu.cpp:227), names cut at 23 characters (:437 area), more pages load while scrolling near the end (src/menu.cpp:677-690). Selecting a song sends PLAY_SONG and closes the list (src/menu.cpp:430-438). Last item "Zpet". Loading screen "Nacitani..." / "Cekam na PC" appears only after 500 ms; a 3 s timeout shows "PC neodpovida"; empty folder shows "Zadne pisne" (src/menu.cpp:951-957, :872-895).
- Prehravam (now playing): song code big, section line (`Sloka: 2`, `Sloka: 2 (x2)` where x2 = 2 repeats pending, `Predehra`, `Predehra (x1)`), a Stop icon button. Opened only by SIGNAL_PLAYING from the PC. The only input is Enter = stop (STOP_SONG). The encoder turn does nothing (src/menu.cpp:274-396, :316-322). After Enter: "Zastavuji" (waiting for the PC, gives up after 5 s), then "Zastaveno" for 1.5 s, then back (src/logic/now_playing.hpp: STOPPING_TIMEOUT_MS 5000, STOPPED_SHOW_MS 1500). If stop fails the screen returns to "Prehravam" with the reason in the info line and Enter tries again (src/menu.cpp:300-308).
- Request failure texts (shown on the song-list message screen or the info line): "PC neodpovida" (no answer after retries), "Neodeslano" (send queue full), "Pisen nenalezena", "PC zaneprazdneno", "PC nedostupne", "Nehraje se", "Odmitnuto" (src/logic/router_core.cpp:74-85, src/menu.cpp:792-825).
- Parovani: section 6.

## 11. Display strings, complete table

Source file src/menu.cpp unless noted. Count: 63 distinct strings.

| # | Text | Screen / where | Meaning |
|---|---|---|---|
| 1 | MIDIControl / BASE | Splash line 1/2 (base.hpp:13-14) | device name |
| 2 | (git revision) / (branch) | Splash lines 3/4 (:555-562) | firmware version |
| 3 | Hlavni menu | main menu title (:115, :591) | normal |
| 4 | Sparuj ovladac | main title (:591) / overlay hint (:138) | no remote stored |
| 5 | Prehraj | main item (:110) | open song list |
| 6 | Nahraj | main item (:111) | start/stop recording, no on-screen state |
| 7 | Nastaveni | main item / settings title (:112, :105) | settings |
| 8 | LED | settings item (:95) | master LED switch |
| 9 | Ident. MIDI | settings item (:96) | MIDI A/B colours |
| 10 | Napajeni varhan | settings item (:97) | organ SSR |
| 11 | Verze FW | settings item / page title (:98, :564) | version page |
| 12 | Displej | settings item / page title (:99, :565) | indicator state page |
| 13 | Bluetooth | settings item / page title (:100, :566) | BLE page |
| 14 | Sparovat ovladac | settings item (:101) | start pairing |
| 15 | Zpet | back item (:102, :900) | back |
| 16 | Revize: / Vetev: / Datum: / Cas: | Verze FW (:477-480) | git hash, branch, build date, time |
| 17 | Pripojen: Ano/Ne | Displej (:521) | indicator connected |
| 18 | Pisen: [..] | Displej (:527) | song digits |
| 19 | Sloka: [..] | Displej (:528) | verse digits |
| 20 | Pismeno: x | Displej (:529) | letter |
| 21 | LED: cervena/zelena/modra/zluta/vyp | Displej (:530) | indicator LED colour |
| 22 | Modul: start... | Bluetooth (ble_status.cpp:36) | module starting |
| 23 | Modul: OK | Bluetooth | module fine |
| 24 | Modul: OK, bez RST | Bluetooth | configured, reset not confirmed |
| 25 | Modul: neodpovida | Bluetooth | no reply to commands |
| 26 | Modul: mlci | Bluetooth | silent module |
| 27 | Spojeni: Ano/Ne | Bluetooth (:212) | BLE connected |
| 28 | Sparovano: Ano/Ne | Bluetooth (:213) | peer accepted |
| 29 | Ovladac: Ano/Ne | Bluetooth (:214) | remote answers |
| 30 | MAC: <hex> / MAC: -- | Bluetooth (:215) | stored remote address |
| 31 | Parovani | pairing screen title (:409) | pairing |
| 32 | Hledam ovladac... | pairing (:409, :490) | scanning |
| 33 | BLE nefunguje | pairing (:490) | BLE module not up |
| 34 | Sparovano | pairing result (main.cpp:78) | success |
| 35 | Ovladac nenalezen | pairing result (main.cpp:83) | not found in 15 s |
| 36 | Parovani selhalo | pairing result (main.cpp:83) | failed after finding it |
| 37 | Pisne | song list title (:422-428) | song list |
| 38 | Nacitani... | song list loading (:423, :922) | waiting for PC |
| 39 | Cekam na PC | loading comment (:423) / overlay (:131) | waiting for PC |
| 40 | Zadne pisne | song list (:887, :892) | folder empty |
| 41 | Prehravam | now playing title (:334) | playing |
| 42 | Zastavuji | now playing (:335) | stop sent, waiting |
| 43 | Zastaveno | now playing (:336) | stopped |
| 44 | Sloka: N / Sloka: N (xK) | now playing (:240-241) | verse, K repeats pending |
| 45 | Predehra / Predehra (xK) | now playing (:241) | prelude |
| 46 | Odpojeno | overlay title (:131) | PC not connected |
| 47 | Stisk: Nastaveni | overlay second hint (:567) | Enter opens settings |
| 48 | Aktualizace | remote update screen title (:442), bootloader (boot:main.cpp:123) | firmware transfer |
| 49 | Prenos do ovladace | remote update (router.cpp:564) | sending to remote |
| 50 | Instalace ovladace | remote update (router.cpp:565) | remote installing |
| 51 | Chyba aktualizace | remote update (router.cpp:566) | failed |
| 52 | Cekam na PC | bootloader hint (boot:main.cpp:123) | waiting for transfer |
| 53 | Nevypinejte | bootloader hint (boot:main.cpp:162-164) | do not power off |
| 54 | Chyba, opakujte | bootloader hint (boot:main.cpp:160,:264) | retry the update |
| 55 | PC neodpovida | failure text (router_core.cpp:74-85) | no answer |
| 56 | Neodeslano | failure text | send queue full |
| 57 | Pisen nenalezena | failure text (:79) | NAK NOT_FOUND |
| 58 | PC zaneprazdneno | failure text | NAK BUSY |
| 59 | PC nedostupne | failure text | NAK UNREACHABLE |
| 60 | Nehraje se | failure text (:82) | NAK NOT_PLAYING |
| 61 | Odmitnuto | failure text (:83) | other NAK |
| 62 | (song codes, e.g. 406A) | list items, now playing | from PC file names |
| 63 | (song section from PC) | now playing | label as sent by PC |

Not user-visible: log lines on the log CDC (say RN4870 although the module is RNBD350).

## 12. Church indicator ("tabule") behaviour

- Bus: USART1, 1200 Bd, 9-byte frames, flag byte 0xB0; a sender waits for 25 ms of quiet bus before sending; multi-master (src/logic/indicator_bus.hpp: FRAME_LEN 9, FLAG 0xB0, QUIET_MS 25; proto:INDICATOR-BUS.md).
- The indicator shows: song number as 4 positions (each 0-9 or blank), intended range up to 1999 (thousands digit blank or 1), a letter A-D or blank, verse as 2 positions, and a LED colour: red, yellow, green, blue or off (bus codes 0x20 R, 0x21 Y, 0x22 G, 0x23 B, 0xE0 off) (src/display.cpp:17-29, :57; docs/SYSTEM.md SYS-USE-17, D23, D26).
- The PC sends NOW_PLAYING_INFO payload [song_h, song_l, verse, letter, led]; 0xFF = no change, 0xFE = blank field (proto:inc/protocol/constants.hpp:50-52). Applied in src/display.cpp:211-256; clear at :258.
- Frames are sent only if an indicator is connected (sense pin PC13 low), on every change and repeated every 500 ms (src/display.cpp:391-397, src/main.cpp:231-240). With the indicator unplugged nothing is sent and the Display LED is off.
- Stop: when a song stops the PC clears the indicator, but only if it still shows the number the PC set (shared bus, SYS-USE-08, D20; PC side).
- Shared bus: manual controllers can also write to the same bus. The base reads the bus back; a foreign frame is accepted into the base's current state, shown on the "Displej" page and echoed as SysEx to USB-MIDI (F0 4D 43 01 00 ..., docs/display-sysex.md; src/display.cpp:359-389, :411-420, :399-475). The base never changes playback because of a foreign number (owner 2026-11-12, trail learning "church indicator bus multi-master").
- No indicator on the bench: real bus behaviour is unverified.

## 13. Screen pictures

Folder: `midicontrol-sw/docs/manual/docs/img/screens/base/` (40 PNG, 128x64 scaled, plus `sheet.png` contact sheet and `index.html`), generated by `tools/screens/export.sh` from the real menu code on the host (stubs for hardware; scenarios in tools/screens/main.cpp). The splash shows placeholder revision `a1b2c3d` and branch `main`, the Verze FW page a placeholder date.

| PNG | Screen |
|---|---|
| 01_splash | Splash MIDIControl / BASE / revision / branch |
| 02_main_play | Hlavni menu, Prehraj selected |
| 03_main_record | Hlavni menu, Nahraj selected |
| 04_main_settings | Hlavni menu, Nastaveni selected |
| 05_settings_led | Nastaveni, LED selected |
| 06_settings_midi | Nastaveni, Ident. MIDI selected |
| 07_settings_power | Nastaveni, Napajeni varhan (off) |
| 08_settings_power_on | Nastaveni, Napajeni varhan (on) |
| 09_settings_firmware | Nastaveni, Verze FW selected |
| 10_settings_display | Nastaveni, Displej selected |
| 11_settings_bluetooth | Nastaveni, Bluetooth selected |
| 12_settings_pair | Nastaveni, Sparovat ovladac selected |
| 13_settings_pair_scrolled | same, long label scrolled |
| 14_settings_back | Nastaveni, Zpet selected |
| 15_info_firmware | Verze FW page |
| 16_info_display | Displej page |
| 17_info_bluetooth | Bluetooth page |
| 18_pairing_search | Parovani: Hledam ovladac... |
| 19_pairing_done | Parovani: Sparovano |
| 20_pairing_not_found | Parovani: Ovladac nenalezen |
| 21_songs_loading | Pisne: Nacitani... / Cekam na PC |
| 22_songs_first | Pisne list, first entry selected |
| 23_songs_middle | Pisne list, middle |
| 24_songs_last | Pisne list, Zpet selected |
| 25_songs_timeout | Pisne: PC neodpovida |
| 26_songs_empty | Pisne: Zadne pisne |
| 27_now_playing | Prehravam, song only |
| 28_now_playing_anchor | Prehravam, Sloka: 2 |
| 29_now_playing_repeats | Prehravam, Sloka: 2 (x2) |
| 30_now_playing_stopping | Zastavuji |
| 31_now_playing_stopped | Zastaveno |
| 32_fw_remote_update | Aktualizace, Prenos do ovladace, bar |
| 33_no_pc | Odpojeno overlay |
| 34_no_pc_settings | Nastaveni opened from the overlay (no PC icon) |
| 35_no_pc_pairing | Parovani from the overlay |
| 36_no_pc_back | Odpojeno again after leaving Nastaveni |
| 37_pc_back | Hlavni menu with PC icon after the PC returns |
| 38_status_no_remote | Hlavni menu, no BLE icon |
| 39_main_unpaired | Hlavni menu titled Sparuj ovladac |
| 40_no_pc_unpaired | Odpojeno with hint Sparuj ovladac |

Captions are matched to the contact sheet by eye; scenario names in tools/screens/main.cpp were not re-checked line by line.
Not covered by a picture: the bootloader screen (boot:), the stop-failed info line on Prehravam, the failure texts other than "PC neodpovida", the animated BLE icon.

## Open questions

- Q1. Front and rear panel labels, connector types, power input and other physical details are not in firmware: the owner must give them (or photos).
- Q2. Resolved by A7: "Nahraj" starts/stops recording. Open: the screens for the state (C3).
- Q3. docs/SYSTEM.md SYS-USE-24 wants an encoder turn on the now-playing screen to choose Stop or "Pridat sloku" (add verse). Code: only Enter = stop, a turn does nothing. Manual follows the code unless the owner says otherwise.
- Q4. Foreign indicator-bus numbers: the base copies them into its state, the Displej page and USB echo; docs say the base OLED and the remote mirror them. No code found that mirrors them on Prehravam/main screens. Confirm what the user should expect.
- Q5. The 2 h organ backstop: SYSTEM.md says the PC owns idle-off (30 min). What to tell the organist when no PC runs? The manual states 2 h from the code.
- Q6. PB1 forced bootloader entry is implemented in the bootloader (boot:src/main.cpp), not in the app: confirm it is a supported user procedure (recovery) and the wording for the manual.
- Q7. Does "Ident. MIDI" have any other effect than LED colour (the label suggests identification of MIDI direction)? Code says only LEDs.
- Q8. Indicator LED colours (red/yellow/green/blue): who chooses the colour in normal use (PC NOW_PLAYING_INFO field `led`)? The manual does not say what colour means what.
- Q9. Panel orientation of encoder (which way is Up) is set by the quadrature code; confirm clockwise = Down or Up on the real unit (bench).
- Q10. Real indicator, DIN in/out and BLE reconnect timings are not bench-verified (see docs/SYSTEM.md "not bench-verified" rows); keep the manual wording cautious.
- Q11. The song list caps at 32 entries with no message that it was cut (SYS-USE-23, gap 39). What should the manual tell the user?

## Possible bugs

- B1. "Nahraj" gives no on-screen feedback (TODO C3).
- B2. src/base.cpp:110 `dfuCheck()` has no callers (dead code); harmless because the bootloader handles PB1.
- B3. Master LED off still keeps the MIDI A/B steady colours (red/green) on, because only the blink is gated by `master` (src/led.cpp:299-306). The user may expect all LEDs off.
- B4. Song limit: `setSong` caps at 1999 but the live path applyNowPlaying (src/display.cpp:211) passes any thousands digit; the NPI payload can carry e.g. 2406 (gap 32, SYS-USE-17).
- B5. Song list cut at 32 silently (see Q11); names longer than 23 characters are cut.
- B6. Menu labels "Ident. MIDI" and "Napajeni varhan" do not fit the 128 px screen and are shown cut ("Ident. MI", "Napajeni"); "Sparovat ovladac" is cut to "Sparovat" (see PNGs 06, 07, 12).
- B7. Organ power state is not saved: after a power cut or reset the organ is off, and the menu checkbox is off. Intended (safety), but should be in the manual.
- B8. Log text names the BLE module RN4870, the hardware is RNBD350 (not user visible).
