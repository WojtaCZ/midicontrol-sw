# Facts: PC software and web page (phase 1, audit step 4)

Repo `midicontrol-sw`, branch `audit/step4-manual`, built from `42f3a9a` (status field `version`). Paths are relative to the repo root.
Code wins over docs. UI strings are verbatim (Czech on the Přehled page, English on Pokročilé). Citation format `path:line`.
Line numbers were captured on 2026-11-16; re-check them if the file has changed since.

## 1. Web page

### 1.1 Where and how to open
- Process `midicontrol` serves a status page and a JSON API. Default port **50118**, bind **0.0.0.0** (IPv4), enabled by default: `src/config.cpp:117-134`. Open `http://<pc-address>:50118/` in any browser on the same network. Port 50118 is reserved by `[WEB] Port`; if it is busy, change it in `config.conf`.
- No login, no HTTPS. Anyone who can reach the port can change song stops and flash firmware (SYS-FW-09, `docs/SYSTEM.md`).
- The page is one self-contained HTML document with inline CSS/JS in `src/web_page.cpp` (`statusPageHtml()`, served for `GET /`; routing in `src/http.cpp:140-173`). There is no separate web source tree: edit that file and rebuild. `<html lang="cs">`, `<title>MIDIControl</title>` (`src/web_page.cpp`).
- Polls `/api/status` every 1 s; a poll that does not answer within 3 s turns the top-right text into "bez spojení s přehrávačem" (`src/web_page.cpp:1174`). The top-right text otherwise reads "připojování…" (before first data, `:287`, `:1174`) or "aktualizováno před N s" (`:1174`).
- Top bar: logo "MIDIControl" and two tabs "Přehled" and "Pokročilé". Routes: empty hash = Přehled, `#stops` = Přehled scrolled to the stops editor, `#adv/<page>` = Pokročilé (`src/web_page.cpp:1129-1135`).
- Layout works at phone width (screenshot `docs/img/web/prehled-telefon.png`).
- Note: port 50118 is also used by an unrelated tool in the dev container (the brainstorm companion); not relevant to an installation.

### 1.2 Page "Přehled" (Czech, for the organist)
Top to bottom: notices, panel "Systém", panel "Rejstříky písní" (`src/web_page.cpp:905-915`). Before the first answer the system panel shows "Načítám stav systému…" (`:909`).

**Notices** (`src/web_page.cpp:793-810`, shown as a yellow bar; red = urgent; most have a link "Podrobnosti" to the Advanced page, `:816`):

| Text | When | Colour |
|---|---|---|
| Přehrávač neodpovídá, údaje na stránce nemusí být aktuální. | the page gets no answer from the player (`:795`) | red |
| Probíhá aktualizace firmwaru základny. | base firmware update running (`:798`) | yellow |
| Základna není připojena. Zkontrolujte USB kabel mezi počítačem a základnou. | base not connected (`:799`) | yellow |
| Základna neodpovídá. | base connected but not answering keepalives (`:800`) | red |
| (base restarted itself after a firmware fault; text at `:801-803`, read it there before quoting) | base reports a fault restart | see code |
| Baterie ovladače je vybitá. Nabijte ho. | controller battery critical (`:804`) | red |
| Baterie ovladače je slabá. Nabijte ho. | battery low: at most 15 % and not charging (`:805`) | yellow |
| Soubory písní 512, 513, REJSTRIKY mají chybu a nemusí hrát správně. Dejte vědět správci. | song files with a problem; the list of names comes from the folder scan (`:808`) | yellow |

**Panel "Systém"** (`src/web_page.cpp:820-900`): a picture of the chain: node "Počítač" -USB- node "Základna" -BLUETOOTH- node "Ovladač". Each node has a coloured dot with a health label (`:821`): "V pořádku" (green), "Vyžaduje pozornost" (yellow), "Chyba" (red), "Nedostupné" (grey). The USB and Bluetooth lines are solid when connected, dashed when not. Facts under the nodes:
- PC: "Přehrávač běží"; chip "spuštěn N s · N písní" (uptime and song count; screenshot `prehled-1100.png`).
- Base: "Připojena" / not connected; chip "Varhany: neznámo" / "Varhany zapnuty" / "Varhany vypnuty" (`:861`).
- Controller: "Stav neznámý", "Připojen", "Nepřipojen", with suffixes " · porucha firmwaru", "vybitá baterie", "slabá baterie" (`:864-877`); chip "Baterie N %" plus ", nabíjí se" or ", vybitá" (`:869`).
- Badge top right of the panel: "Vše v pořádku" or "N upozornění" (`:898`). Clicking a node opens its Advanced page (title "Podrobnosti: <name>", `:823`).
- Now-playing strip (`:881-895`): "Právě hraje" (playing) or "Připravuji varhany" (warming up) with "Píseň <code>" and the section label (see 2.5; the labels "Predehra"/"Sloka N" have no diacritics); "Nic nehraje" when idle, followed by "naposledy píseň <code> (<end>)". <end> is one of "dohrála", "zastavena, základna se odpojila", "zastavena", "nepodařilo se ji přehrát", "skončila" (`:834-841`).
- While the PC does not answer, the panel is dimmed (stale).

**Panel "Rejstříky písní"** (organ stops editor; `src/web_page.cpp:925-1125`, id `stops`, anchor `#stops`):
- Built once; `/api/stops` is loaded on first open and after each save, never polled (`:925-960`).
- Hint under the heading: "Na začátku písně přehrávač vypne všechny rejstříky a zapne vybrané. Píseň bez nastavení nechá varhany beze změny." (`:1119`)
- Song list (left/top): search box "Hledat píseň…" (aria-label "Hledat píseň podle čísla", `:1078`). Enter opens the first match; ArrowDown goes into the list; rows react to Enter, Space, ArrowUp, ArrowDown.
- Filter buttons: "Vše", "Nastavené", "Nenastavené". Sort select: "Řadit: číslo vzestupně", "Řadit: číslo sestupně", "Řadit: nejvíc rejstříků", "Řadit: nenastavené a nejméně rejstříků" (`:1094`).
- Row right side: "nenastaveno" (no file), "vše vypnuto" (file with empty list), or "N rejstřík/rejstříky/rejstříků" and the numbers under the code (screenshot: 406B has "6 rejstříků", numbers 1 2 3 17 18 19).
- Empty list: "Žádná píseň neodpovídá výběru." (filter) or "Ve složce nejsou žádné písně." (`:1031`). Footer: "Zobrazeno X z Y" or "N písní" plus " · nastaveno N" (`:1035`).
- Editor heading "Píseň <code>" (`:972`) or "Ve složce nejsou žádné písně" / "Rejstříky nenačteny" / "Načítám rejstříky…". Stop buttons are grouped under the group names (defaults: "Spojky a pomocná zařízení" 1-7, "Pedál" 8-16, "I. manuál – hlavní stroj" 17-29, "II. manuál – žaluziový stroj" 30-42; `src/organ.cpp:140`). Each button shows the number and name; a click toggles it; counter "N z M zapnuto" (`:978`).
- State badge: "neuložené změny", "nenastaveno: varhany zůstanou, jak jsou", "uloženo", "uloženo: vše vypnuto".
- Buttons: "Uložit" (after a save shows "Uloženo" for 1.2 s), "Vše vypnout", "Zrušit nastavení" (title "Smaže uložený výběr: při spuštění písně se rejstříky nemění", `:1107-1117`).
- Messages: "Ukládám…" (`:1055`), "Neuloženo: <reason>" (`:1063`), load error "Rejstříky se nepodařilo načíst: <reason> " with button "Zkusit znovu" (`:948-949`).
- Confirm dialogs: "Zahodit neuložené změny písně <code>?" when switching song with edits (`:1046`); "Zrušit nastavení rejstříků písně <code>?" plus " Neuložené změny se také zahodí." and " Při spuštění této písně pak varhany zůstanou, jak jsou." (`:1114`). Closing or reloading the tab with unsaved edits triggers the browser's own warning (`:1198`).
- Error reasons mapped from HTTP (`:925-930`): 400 "chybný požadavek", 404 "nenalezeno", 415 "nepodporovaný formát", 500 "chyba přehrávače při ukládání", 409 "přehrávač je zaneprázdněn", 413 "příliš velká data", other "chyba N"; timeout (5 s) "přehrávač neodpovídá"; network error "spojení s přehrávačem selhalo".

**Step by step: set the stops of a song**
1. Open the page, find the panel "Rejstříky písní" (or open `#stops`).
2. Type the song number in "Hledat píseň…" or click the row. The editor below shows "Píseň <code>".
3. Click stops to switch them on (highlighted) or off. The counter shows "N z M zapnuto"; badge "neuložené změny".
4. Click "Uložit". Wait for "Uloženo". The row in the list now shows the count and numbers.
5. "Vše vypnout" = save an explicit empty list (the organ is cleared at the song start). "Zrušit nastavení" = delete the file (organ left as it is). Confirm the dialog.
6. The stops are used the next time the song starts (see 2.4). An already playing song is not changed.

### 1.3 Page "Pokročilé" (English, for the administrator)
- Section has `lang="en"`, sub-navigation aria-label "Advanced" (`src/web_page.cpp`). Left menu: Overview, Software, Base, Controller, Diagnostics, Firmware. Coloured dots show health; a yellow dot on Diagnostics means unseen faults. Shows "Loading…" until data arrives. Old links `#base` etc. still work (`:1129-1135`).
- Overview: tables "Summary" (one line per Software/Base/Controller) and "Problems" (`:560-569`).
- Software/Base/Controller: fact tables and event lists (version, build date, config path, play folder, diag folder, uptime, song count and problems, counters, events). Fields come from `/api/status`: `version` (git hash), `build_date`, `config_path`, `play_folder`, `diag_dir`, `song_count`, `song_problems`, `playback`, `started_ms` (`src/main.cpp`, `src/web_server.cpp:208-224`). Not every cell was transcribed; open the page for the exact table text.
- Diagnostics (`src/web_page.cpp:573-668`): panels "Base: last fault", "Remote: last fault" (rows Kind, When, Interrupted, CFSR, HFSR, BFAR, MMFAR, Revision, Uptime at fault; "no fault recorded"; button "Mark seen", badge "unseen"), "Reset causes" ("none recorded"), "Log" with filters (node: "both nodes"/"base"/"remote"; level: "all levels"/"info+"/"warnings+"/"errors"; placeholders "tag" and "search text"); empty "no entries". Polls once a second only while the tab is open.
- Firmware: see 1.4.
- Screenshots: `docs/img/web/pokrocile-software.png`, `pokrocile-diagnostika.png`, `pokrocile-firmware.png`.

### 1.4 Firmware update from the web page (Pokročilé > Firmware)
Two cards: "Base firmware" and "Controller firmware" (`src/web_page.cpp:724-790`). Each shows "Running now" (revision, branch, " · uncommitted changes", or "not reported") and "Built".
1. Stop any playing song (otherwise blocked, see below). The base must be connected by USB.
2. Get the update file from the firmware's build folder: `midicontrol-fw-update.bin` (base) or `midicontroller-fw-update.bin` (controller) (`:785-786`). It is the `.bin` with a 24-byte trailer (magic 0x4D43544C, board ID 2 = base, 3 = controller, CRCs). `.elf`, `.hex` and the plain `.bin` are refused.
3. Drop it on the card ("Drop <name> here or choose it", hint "from build/ of the <base|controller> firmware; .elf, .hex and plain .bin are refused", `:729-730`). The browser checks it at once and shows rows "File", "Built for", "Version / size", "Check: trailer, board ID and CRC OK" (`:753-756`). Maximum 512 KiB (`:675`).
4. Click "Flash base" / "Flash controller" (disabled until the check is OK; the reason is written next to the button). Confirm: "Flash <name> firmware <version> from <file>?" plus for base " The base restarts; playback and the controller link stop for about a minute." or for controller " The controller restarts when it is done." (`:713-714`).
5. Progress: bar, "Flashing N %" and a message ("Sending the image: X of Y bytes", then "Image sent (N bytes)"; `src/firmware_upload.cpp:79-80`); badge "flashing". At the end: "Last update finished <ago>" (badge "updated") or "Last update FAILED <ago>" (badge "update failed") plus the message (`:768-773`). The base reboots; the PC log line is "status page: firmware update to <target> requested".
6. The same flow is available from the console: `update <base|remote> <file>` (see 3.4).

Browser-side messages (`:681-706`): "The file is empty."; "This is an ELF file. Use the <name>-update.bin from the firmware's build folder."; "This looks like an Intel hex file. ..."; "No update trailer: this looks like a plain .bin. ..."; "The update trailer is corrupt or of an unknown format."; "This is the base firmware, not the controller firmware. Drop it on the Base card." (and the reverse); "Built for an unknown unit (board ID N)."; "The image size in the trailer does not match the file."; "The image CRC does not match: the file is damaged."; "The file is too large for an update file (N bytes)." Blockers beside the button: "A firmware update is running.", "A song is playing; stop it first.", "The base is not connected.", "The controller is not connected to the base." Upload errors: "Not flashed: <text>", "Refused (N).", "The upload did not reach the player.", "Uploading…", "Reading <file>…".

Server-side checks and replies (`POST /api/firmware/<base|remote>`, `src/firmware_upload.cpp:9-70`, `src/firmware_image.cpp:56-69`):
- 202 accepted, JSON target, version, size; update runs in the background (`src/console_exec.cpp:194-223`).
- 422 `{"error": ...}`: "the file is empty"; "this is an ELF file; use the <name>-update.bin next to it in the firmware's build folder"; "this looks like an Intel hex file; use the <name>-update.bin from the firmware build"; "this is built for an unknown unit (board ID N); nothing was sent"; "this is the base/controller firmware, not the controller/base firmware; nothing was sent"; "no image trailer - this looks like a plain .bin; use the <name>-update.bin from the firmware build"; "image trailer is corrupt or of an unknown format"; "image is built for the other unit (board ID mismatch)"; "image size in the trailer does not match the file"; "image CRC does not match the trailer".
- 409 `{"error": ...}` checked in this order: "a firmware update is already running"; "a song is playing; stop it first"; "the base is not connected" (`firmware_upload.cpp:9-11`).
- 404 for another target; 415 if Content-Type is not application/octet-stream; 413 oversize (limit 512 KiB); 411 chunked body; 408 after 60 s (`src/http.cpp:96-104`, `src/web_server.cpp:259-262`).
- Status fields: `fw_update_target`, `fw_update_state` (running/done/failed), `fw_update_pct`, `fw_update_msg`, `fw_update_ms`; activity "firmware update to <target> in progress".
- The device checks the image again (bootloader). If the base update stops half way, the base stays in its USB bootloader (ABCD:1235); `status` then says "Base is in its USB bootloader (ABCD:1235): a firmware update did not finish; re-flash it with: update base <file>" (`src/console_exec.cpp:87`).

### 1.5 HTTP API (for the administrator)
`GET /`, `GET /api/status`, `GET /api/diag?node=&level=&before=&limit=&kind=reset`, `GET /api/stops`, `PUT /api/stops/<code>` (JSON), `POST /api/firmware/<base|remote>`, `POST /api/diag/ack` (body `id=<n>`, "Mark seen") (`src/http.cpp`). Writes (PUT/POST) are refused with 403 unless the Host header is an IP address, `localhost` or in `[WEB] AllowedHosts`, and an Origin header, if sent, equals Host; there is no login. GET with `ack=` is 400. No play/stop/organ route exists. Other methods: 405 with `Allow`. Request limits 8 KiB headers, 4 KiB body (firmware 512 KiB), 3 s per request, 60 s for firmware (`src/http.cpp:96-104`). `PUT /api/stops` errors: 400 (invalid code, code not among the songs, bad body), 500 (cannot save, text of the error) (`src/stops_file.cpp:335-349`).
The song folder is rescanned every 60 s (`src/web_server.cpp:28`); problems are written to the log as "song <file>: <problem>" (`:208-224`). Problem texts: "empty display anchor", "no display anchors", "file name is not a valid song code", "cannot parse: <reason>" (`src/songcheck.cpp:67-89`).

## 2. Player behaviour visible to the organist

The organist normally controls the player from the remote (separate facts file); the PC does the work. Console commands are the administrator's route (3.4).

### 2.1 Song lookup by code
- A code is 1 to 4 digits, value 1 to 1999, optionally followed by one letter A-D (e.g. 406B) (`src/songs.cpp:28-41`, `:33`). Input is trimmed and upper-cased; file lookup is case-insensitive (`findSongFile`, `src/songs.cpp:57`): `<code>.mid` exact spelling first, then a case-insensitive scan.
- Invalid code: reply NAK INVALID_PAYLOAD (log `Invalid song code: "..."`, `src/comm.cpp:1071`; SYS-USE-22). Valid code but no file: NAK NOT_FOUND (log "Song not found: <code> (in <play folder>)", `:1076`; SYS-USE-02).
- Only files whose stem is itself a code are offered in the song list (`listSongs`, `src/songs.cpp`); sorted naturally (numbers first). Files such as `rejstriky.mid` are not listed and the web page warns about them (screenshot).
- When the remote asks for the list, it is sent in pages separated by `;` (`buildSongListPage`, `src/songs.cpp`).

### 2.2 Playing, add verse, stop
- Play (PLAY_SONG): if the organ is off the PC switches it on and waits `WarmupMs` (default 3000 ms); status "warming up", log "Organ power on, warming up N ms..." (`src/comm.cpp:1530`). A second play during warm-up supersedes the first.
- Add verse (REPLAY_ANCHOR from the remote, console `verse`): the current section is repeated once more after it ends; counted in the label as " +k" (`src/playback.cpp:57-61`). Refused when nothing is playing (NAK NOT_PLAYING; console "Nothing is playing", `src/console_exec.cpp:60`). The stops are not re-sent on a repeat. Optional gap `VerseGapMs` between repeats (default 0).
- Stop (STOP_SONG): payload 0x00 stops; payload 0x01 (pause) is **treated as stop**; other payloads: NAK INVALID_PAYLOAD (`src/dispatch.cpp:100`). There is **no resume** (SYS-USE-09/21 not met; see Possible bugs).
- When the song ends or is stopped: indicator cleared, SIGNAL_STOPPED sent, organ switched off unless `KeepPowerOn = true` (log "Playback finished" plus ", organ power off." `src/comm.cpp:1610-1632`). The organ is also switched off after 30 min idle (see 2.6).
- Status fields (web, console): `playback` (idle/warming up/playing), `song`, `section`, `stops`, `last_song`, `last_sections`, `last_end` ("ended normally", "stopped", "failed: <why>").

### 2.3 Recording a new song
- Entry points (A7): the base menu item `Nahraj` (RECORD_SONG start/stop, `src/dispatch.cpp:102-113`) and the console commands `record` / `stop`. The remote cannot start it. No web button. One recording at a time; a second request or a request during a song gets NAK BUSY, and a song during a recording is refused too.
- Behaviour (`src/comm.cpp:1192-1276`): second `record` logs "Already recording; Stop first."; no MIDI input logs "MIDIControl MIDI input port not found; cannot record."; if the organ is off it is switched on first (warm-up). It runs `arecordmidi -p <alsa port> <file>`; log "Recording to <path>"; failure "Failed to start arecordmidi (rc=N)". `stop` ends it (SIGTERM, else kill). Console reply on failure: "Recording not started (see the log)".
- File: `<RecordFolder>/YYYY-MM-DDTHH:MM:SS.mid`; on a name clash `-2` to `-99` is appended; "Cannot create a recording file in <folder>" if impossible (`:1235`).
- To play it back: it is **not** in the play folder. The owner has to copy/rename it to a valid code in PlayFolder (e.g. `408.mid`); presumably so, not documented (open question).
- Still not implemented compared with SYSTEM.md SYS-REC-*/D29: name `REC_YYYY-MM-DD_HHMM.mid` (the file keeps the timestamp name above), cutting the leading silence, writing `<name>.conf` from the stops at the first note, SIGNAL_RECORDING (`.trail/learnings/2026-11-12-recordings-take-their-stops-from-the*.md`).

### 2.4 Organ stops per song
- File `<PlayFolder>/<CODE>.conf` next to `<CODE>.mid` (e.g. `406B.conf`). Written by the web page or by hand (`src/stops_file.cpp:118-177`).
- Format: `[STOPS]` section with `Stops = 1 2 3 17 18 19` (numbers 1..42 by default, separators any whitespace/comma). `Stops =` empty means all stops off. Comments `#` and `;`. A `Stops` line before any header also counts. Section and key are case-insensitive. Warnings in the log "<file>:<line>: ..." for ignored text, an ignored stop, a second Stops line (replaces the first) (`:118-169`).
- Meaning (`planFor`, `src/stops_file.cpp:299-313`): no file = "not set": the player sends nothing and leaves the organ as it is (log "Stops for <code>: not set"). File = send the "all stops off" SysEx, wait `AllOffDelayMs`, then one Program Change per stop (`StopDelayMs` apart). Log "Stops for <code>: <numbers>" / "all off" / "disabled" (`src/comm.cpp:1562`).
- A line with tokens but no valid stop is ignored (treated as "not set", not "all off", fail-safe). A read error logs "Stops not read, treating <code> as not set: <what>".
- Program Changes already inside the MIDI file are dropped when `DropSongProgramChanges = true` (default) (`src/playback.cpp:209`).
- The web page rewrites the file on every save (comments not kept): atomic write via a temp file and rename; header text "# Organ stops for song <CODE>. This file is rewritten by the web page on every save: / comments are not kept. Edit by hand only while the player is stopped. / Stops = stop numbers (empty = all stops off). Delete the file to leave the organ as it is." (`src/stops_file.cpp:171-177`). "Zrušit nastavení" deletes the file. Save error log: "stops: cannot save <path>: <what>: <strerror>".
- **Default registration (SYS-USE-20) is not implemented**: a song without a file does not set any stops.
- Stop numbering and names for the default organ (Johannus Opus 910): 42 stops in four groups, Czech names, `src/organ.cpp:20-142`. The name list is for display only; the number is what matters (`.trail/learnings/2026-10-27-organ-stop-names-czech-parish-disposition-dev.md`).

### 2.5 Indicator and display messages the PC sends
- Base indicator (NOW_PLAYING_INFO, 5 bytes): song digits as BCD (two bytes), verse, letter (0x0A-0x0D = A-D, 0x0F blank), LED 1-4 (0x0F blank) (`src/display.cpp:24-67`). Taken from the display SysEx anchors in the MIDI file; if the song has none, the song code from the file name is used (SYS-USE-19, `src/playback.cpp:110-123`). A new song blanks every field it does not define. Cleared with an empty payload at stop (`src/comm.cpp:1182-1190`).
- Remote: SIGNAL_PLAYING (payload = song code) before the first SIGNAL_ANCHOR; SIGNAL_ANCHOR payload = ASCII section label ("Predehra" for the intro, "Sloka N" for verse N, with " +k" for pending repeats); SIGNAL_STOPPED at the end (`src/comm.cpp:1515-1632`, `src/playback.cpp:57-61`).

### 2.6 Organ power
- Switched on by play (and record); off at the end of the song unless `KeepPowerOn`. Idle power-off after **30 min**, config key `[PLAYBACK] OrganIdleOffMs` (ms, integer >= 1000, B3; 0 is rejected: no 'never' setting). Deferred while a song plays or a recording runs. The timer is also armed after a song end, Stop or failed start with `KeepPowerOn = true`, and when an organ-off command gave up with the relay still on (B1), so an organ the PC knows is on never stays on without it. Console `on` / `off` switch the organ manually.
- Start-up log lines: "Startup: organ power off", "Startup: MIDI output port not found; no All Notes Off.", "Startup: organ found switched on; left on, idle power-off in <time>.", "Organ idle for <time>: power off." (`src/comm.cpp:1441-1484`).

## 3. Administration

### 3.1 Installation (systemd user service)
(`README.md:20-30`, `packaging/midicontrol.service.in`)
1. Build and install: `cmake --install build` places the binary in `<prefix>/bin/midicontrol`, `share/midicontrol/config.conf.example`, `lib/systemd/user/midicontrol.service`.
2. `mkdir -p ~/.config/midicontrol`, copy `config.conf.example` to `~/.config/midicontrol/config.conf`, edit it (PlayFolder, RecordFolder).
3. `systemctl --user daemon-reload` then `systemctl --user enable --now midicontrol.service`.
4. **Required**: `loginctl enable-linger $USER` (`README.md:27`), else the service does not start at boot without a login.
5. The user must be in the groups for the serial port (`uucp` or `dialout`) and `audio` (ALSA MIDI).
- Unit: `Type=notify`, `WatchdogSec=30`, `Restart=always`, `RestartSec=5`, `StartLimitIntervalSec=0`, `ExecStart=<bindir>/midicontrol --headless`, `After=sound.target`, `WantedBy=default.target` (`packaging/midicontrol.service.in:32-33`). READY is sent after the config is loaded and the web server started; the base need not be plugged in.
- Status: `systemctl --user status midicontrol`. Restart after editing the config: `systemctl --user restart midicontrol`.

### 3.2 Configuration `config.conf`
Search order: `--config PATH`, `$MIDICONTROL_CONFIG`, `$XDG_CONFIG_HOME/midicontrol/config.conf` (or `~/.config/...`), `./config.conf` (`src/config.cpp:195-208`). `~` is expanded; relative paths are relative to the config file. Booleans: 1/true/yes/on, 0/false/no/off, any case. INI format with `[SECTION]` and `Key = value`.

| Section | Key | Default | Meaning | Source |
|---|---|---|---|---|
| FILES | PlayFolder | required | folder with song `.mid` and `.conf` files; must exist | `src/config.cpp:83-95,166-169` |
| FILES | RecordFolder | required | folder for recordings; must exist | same |
| BINDINGS | (any) | unused | parsed, reserved | `include/midicontrol/config.hpp:18` |
| PLAYBACK | WarmupMs | 3000 | wait after switching the organ on, integer >= 0 | `src/config.cpp:112` |
| PLAYBACK | KeepPowerOn | false | leave the organ on after a song | `:113` |
| FILES | SongCodeNormalise | true | leading zeros ignored: 0406 = 406 (B4) | `src/config.cpp` |
| PLAYBACK | OrganIdleOffMs | 1800000 | idle power-off time, ms, integer >= 1000 | `src/config.cpp` |
| PLAYBACK | VerseGapMs | 0 | pause before a repeated verse, >= 0 | `:114` |
| WEB | Enabled | true | web server on/off | `:120` |
| WEB | Port | 50118 | 1-65535 | `:123` |
| WEB | Bind | 0.0.0.0 | IPv4 address to listen on | `:129` |
| DIAG | LogPort | empty = auto-detect the "MIDIControl log" USB port | serial device of the base log | `:138` |
| DIAG | Dir | `$XDG_STATE_HOME/midicontrol`, else `~/.local/state/midicontrol`, else `/tmp/midicontrol-<uid>/diag` | diagnostics store | `:139,183-190` |
| ORGAN | StopsEnabled | true | master switch for per-song stops | `src/organ.cpp:163` |
| ORGAN | Channel | 12 | MIDI channel 1-16 for Program Changes | `:164` |
| ORGAN | ProgramOffset | -1 | stop number to program number offset, -127..127 | `:165` |
| ORGAN | AllOffSysex | F0 00 4A 4F 48 41 53 7F F7 | hex bytes sent before stops | `:162-205`, `:138` |
| ORGAN | AllOffDelayMs | 100 | 0-60000 | `:167` |
| ORGAN | StopDelayMs | 20 | 0-60000 | `:168` |
| ORGAN | DropSongProgramChanges | true | drop Program Changes in the song | `:169` |
| ORGAN | Groups | the four defaults (1.2) | `LABEL:first-last, ...`, contiguous from 1 | `:92-116,173` |
| ORGAN | Name<N>, ST<N>, Program<N> | defaults for 1-42 | extra stops beyond 42 get the name "Rejstřík N" and need Program<N>; ST<N> is an octal string | `:178-205` |

Config errors stop the program with a message (exit 1): "config: missing [FILES] section", "config: PlayFolder not specified", "config: RecordFolder not specified", "config: unable to find the config file: <path>", "config: play folder not found: <path>", "config: record folder not found: <path>", "config: invalid value for <key>: '<v>'", "config: invalid boolean for <key>: '<v>'", "config: Bind is empty", "config: stop N has no valid program (check STN, ProgramN, ProgramOffset)".
Example file in the repo: `config.conf` (the one in the project root) and `share/midicontrol/config.conf.example` after install.

### 3.3 Song folder layout and naming
- One flat folder (PlayFolder): `<code>.mid` (e.g. `401.mid`, `406A.mid`), optional `<code>.conf` (stops). Example: `songs/` in the repo (401, 403, 406A, 406B, 407, 512, 513, 516, 517, 519, 523 plus `406B.conf`; `rejstriky.mid` is a recording and is flagged "file name is not a valid song code").
- Code rules in 2.1. Lower/upper case does not matter. Do not use another name pattern if the song should show in the remote list.
- A new file is noticed within 60 s (rescan), or at the next start.
- The song file is a Standard MIDI File with display anchors (SysEx) for the base indicator and section labels; the format is described in `docs/SYSTEM.md` (not repeated here).

### 3.4 Logs, console and diagnostics
- Log: `journalctl --user -u midicontrol -f` (`README.md`). Every console command is logged as "console: <command>".
- Console, interactive: run `midicontrol` in a terminal; it attaches to the running service through `$XDG_RUNTIME_DIR/midicontrol.sock` (mode 0600; lock `$XDG_RUNTIME_DIR/midicontrol.lock`; fallback `/tmp/midicontrol-<uid>/`) (`src/instance.cpp`, `src/main.cpp`). Prompt "> ", banner "MIDIControl - interactive control". `quit` detaches.
- One command: `midicontrol --cmd "status"`; exit code 1 on failure. Options `--config PATH|-c`, `--headless`, `--cmd`, `-h|--help`. Unknown option: "Unknown argument: X", exit 2 (`src/main.cpp:144`). Second instance without a running socket: "error: another midicontrol instance is running (<runDir>/midicontrol.lock) but does not answer on <socket>" (`:160`).
- Commands (`src/console.cpp:40`): "Commands: play <code>, stop, verse (add verse), list, record, on, off, status, diag (last faults + log lines), update <base|remote> <name-update.bin>, quit".
  - `play`: failure "Cannot play <arg>: <reason>" with reasons "invalid song code", "song not found", "nothing is playing", "refused" (`src/console_exec.cpp:19-26,48`).
  - `verse`: "Verse added: <label>" or "Nothing is playing".
  - `list`: "<N> songs: code code ..." (`:68`).
  - `status`: "connected=.. base_alive=.. playing=.." plus ` section="<label>"`, then "Base: BLE=.., relay=.., display=.., pc_alive=.." or "Base did not answer GET_STATUS".
  - `diag`: last fault per node and last 20 log lines; "Diagnostics are not available" (`:102`).
  - `update`: progress bar `[####    ] N%  (done/total)`, ends "Firmware update complete." or "Firmware update FAILED."; refusals "busy: ..." / "refused: ..." (`:162-175`); exception "firmware update: <what>".
  - Headless messages: "Running headless; send SIGTERM or SIGINT to stop." (SIGHUP, and in the interactive prompt SIGINT/SIGTERM/SIGHUP/"quit"/EOF, give the same shutdown), "Received signal N, shutting down.", "Done.". Log "Using config file <path>".
- Diagnostics store: the base log USB port "MIDIControl log" is read by the PC; per node JSONL files (1 MiB x 3) and `diag-ack.json` in the diag folder; the remote's faults are polled every 10 s. Shown on Pokročilé > Diagnostics and via `/api/diag`. "Mark seen" writes the acknowledgement.
- Base link: keepalive every 2 s; after 6 s without reply: log "Base is not answering keepalives (no reply for 6 s)." (`src/protocol/constants.hpp:49-50`, `src/comm.cpp:1007`). Messages are retried 3 times.

### 3.5 Backups
No built-in backup. What to save: the config file, PlayFolder (`*.mid` and `*.conf`; the `.conf` files are written by the web page), RecordFolder, and the diag folder if needed. Nothing else holds state (the organ stops are only in `<code>.conf`). No script in the repo does this (open question whether the owner wants one).

### 3.6 Docker deployment (NOT MERGED)
Only on branch `docker-deployment` (worktree `midicontrol-sw-docker`, last commit `e20bde8`, pushed 2026-11-16; CI first run and bench pending per memory). Not part of `audit/step4-manual`; do not put in the manual as available until merged.
- `curl ... install.sh | sh` installs under `/opt/midicontrol` (`MC_PREFIX`): `compose.yaml` with two services (`midicontrol` player, `updater`), `.env` (IMAGE_TAG default `stable`, UPDATE_WINDOW `02:00-04:00`, UPDATE_ENABLED), `data/` (songs, recordings, diag, firmware, config.conf), and the wrapper `/usr/local/bin/midicontrol` (`docs/deployment.md` in the worktree, lines 13-45).
- Status page unchanged at `http://<host>:50118/`. Console: `midicontrol`, `midicontrol --cmd "..."`; firmware paths are `/data/firmware/...`.
- Updater: polls the registry inside the window, idle guard, health gate 60 s, automatic rollback to `:previous`; result in `data/diag/update-status.json`. `--uninstall [--purge]`.
- Needs Docker Engine + Compose plugin, host network and `/dev`; stops an existing systemd user unit.

## 4. PC messages that reach the remote or the base screens
- The PC sends to the remote only: SIGNAL_PLAYING (code), SIGNAL_ANCHOR (ASCII label, 2.5), SIGNAL_STOPPED, song list pages, replies to requests (ACK / NAK). To the base indicator: NOW_PLAYING_INFO (2.5). Free text is limited to the section label.
- NAK reasons (`include/midicontrol/protocol/types.hpp:63-71`; names `src/protocol/names.cpp`): UNKNOWN_MSG 1, INVALID_PAYLOAD 2, CRC_ERROR 3, BUSY 4, NOT_PLAYING 5, UNREACHABLE 6 (base: destination not connected), NOT_FOUND 7. The PC produces: INVALID_PAYLOAD for a bad code or bad stop payload (`src/dispatch.cpp:88,100`), NOT_FOUND for a missing song (`src/comm.cpp:1076`), NOT_PLAYING for add verse when idle, UNKNOWN_MSG for unsupported requests (RECORD_SONG is handled since A7; BUSY when a recording or song runs). A BUSY NAK to a SIGNAL_* is retried (`src/comm.cpp:551-579`).
- **No text is attached to a NAK by the PC.** Only the console translates NAK codes (to "invalid song code", "song not found", "nothing is playing", "refused"; `src/console_exec.cpp:19-26`). What the remote shows for each NAK is decided in the remote firmware (see facts/remote.md).
- Texts on the base screen such as "PC neodpovida", "PC zaneprazdneno", "PC nedostupne", "Odpojeno / Cekam na PC" come from the base firmware (`docs/SYSTEM.md`), not from this code; see facts/base.md.
- Retransmitted requests are answered again from a remembered reply and not executed twice; broadcasts are executed but not answered (`src/dispatch.cpp`).

## 5. Screenshots (web page)
Existing: `docs/Screenshot From 2026-10-27 17-50-05.png` is **not** a web screenshot; it is the organ's Dutch stop-label sheet (Johannus, "SPEELHULPEN, PEDAAL, HOOFDWERK, ZWELWERK"), useful only as a source for stop names (also present in the `-docker` worktree and `wt/` worktrees). `audit/` contains no images.
Made now (PC binary built in the scratchpad from `42f3a9a`, run offline with the systest fake base (`tools/systest`, port 28130, no real `/dev`); page captured with headless chromium; no real data, songs from `songs/`, `406B.conf` set to 1 2 3 17 18 19):
- `docs/manual/docs/img/web/prehled-1100.png` (Přehled, desktop; shows notice about song files, topology with a connected base, "Nic nehraje", song list)
- `docs/manual/docs/img/web/rejstriky-1100.png` (tall: stops editor with group names)
- `docs/manual/docs/img/web/prehled-telefon.png` (390 px wide)
- `docs/manual/docs/img/web/pokrocile-software.png`, `pokrocile-diagnostika.png`, `pokrocile-firmware.png`
Caveats: the controller is "Nepřipojen" (fake base has no remote), firmware "not reported"; no playing state, no dialogs, no red notices. For those a scripted session (Playwright with a fake play) or a real bench is needed. The page language and the header artefact in `rejstriky-1100.png` (sticky bar in a tall window) mean it is better retaken with scrolling clip before print.

## Open questions
1. How is a recording turned into a playable song (copy to PlayFolder, rename to a code)? Is that the intended workflow until SYS-REC-* is done? Who may do it (organist or admin)?
2. Recording starts from the base menu `Nahraj` (A7); the manual describes this. The base shows no recording state yet (TODO C3), so the manual says so.
3. Pause/resume (SYS-USE-09/21): pause behaves as stop and resume does not exist. What should the manual say?
4. Default registration for songs without a `.conf` (SYS-USE-20): not implemented; confirm "organ stays as it is" is the text for the manual.
5. Idle power-off is 30 min by default, `OrganIdleOffMs` (B3). Status page: chip "Idle-off armed", Czech notice for `organ_power_fault`.
6. SYSTEM.md lines 360-520 were not re-read in this pass; status of remaining PC-side requirements there is unverified.
7. Backups: does the owner want a recommended procedure or a script?
8. Docker deployment: when is it merged? The admin chapter must not describe it as available now.
9. The base-fault restart notice text (`src/web_page.cpp:801-803`) was not transcribed here; copy it from the source when writing.
10. Pokročilé Software/Base/Controller table labels were not transcribed completely (English, admin only); decide whether the manual lists them.
11. The Advanced page is English only, the organist page Czech: confirm this is acceptable for a Czech manual (screens captioned in English).
12. Authentication (SYS-FW-09): the manual should warn that anyone on the network can edit stops and flash firmware; confirm wording and whether the bind should be restricted by default.
13. Port 50118 and `Bind = 0.0.0.0`: should the manual recommend a fixed IP or hostname of the PC?

## Possible bugs
1. `src/web_page.cpp:925`: the page maps HTTP 409 to "přehrávač je zaneprázdněn" for the stops save, but `PUT /api/stops` never returns 409 (`src/stops_file.cpp:335-349`); the text is dead code, and saving while a song plays is allowed.
2. Recording names (`YYYY-MM-DDTHH:MM:SS.mid`, colons in the name) differ from SYS-REC/D29 (`REC_YYYY-MM-DD_HHMM.mid`), and colons are awkward on some file systems and for the song-code rules; recordings are never picked up as songs.
3. `rejstriky.mid` in `songs/` makes the Přehled notice list "REJSTRIKY" as a faulty song for the organist (a recording left in the play folder); the notice reads like an error although the file is merely not a song.
4. A NAK from the PC carries no text and the remote/base mapping is not in this code; the console prints "refused" for unknown reasons, so BUSY/UNREACHABLE would be shown as "refused" if they reach it.
5. Add verse does not re-send the stops (by design), but the manual-visible effect is that changing registration is only possible at the song start.
6. Pause is silently treated as stop (`src/dispatch.cpp:100`): an organist pressing "pause" loses the position.
7. The notice thresholds: battery warning at 15 % is a web-only rule (`src/web_page.cpp:805`); the remote itself only logs a low battery (see facts/remote.md), so the two sides can disagree.
8. Web page notices and Advanced firmware messages are partly duplicated in two languages and wordings (browser texts in English with a trailing period, server texts lowercase without), so the same refusal reads differently depending on where it is caught.
