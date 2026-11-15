# Status web page and always-on service

The PC player (`midicontrol`) runs as an unattended systemd user service, serves a status page with a Stops editor on port **50118**, and accepts terminal commands through a local control socket. This document covers how to install and use it and how it behaves. It also records how it was built and tested, the decisions taken, and what is still open.

- Design (the binding spec): [`superpowers/specs/2026-10-19-status-web-design.md`](superpowers/specs/2026-10-19-status-web-design.md)
- Implementation plan: [`superpowers/plans/2026-10-19-status-web.md`](superpowers/plans/2026-10-19-status-web.md)

> **Status (2026-11-14):** implemented and merged to `main`. The web firmware update, its busy answers and the base reconnect were re-tested on the bench on 2026-11-12 (`FAULTS.md` 2.1). The rest of the [Bench checklist](#bench-checklist-not-run-yet) (systemd watchdog kill, port squatter, `record`) has not been run. On `main` the service is the systemd user unit described here; the unmerged branch `docker-deployment` proposes a Docker container as production instead (see `CLAUDE.md`).

---

## 1. Install and run as a service

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build                       # binary, config example, systemd user unit

mkdir -p ~/.config/midicontrol
cp /usr/local/share/midicontrol/config.conf.example ~/.config/midicontrol/config.conf

systemctl --user daemon-reload
systemctl --user enable --now midicontrol.service
loginctl enable-linger $USER                # REQUIRED: start at boot without a login
journalctl --user -u midicontrol -f         # logs
```

`loginctl enable-linger` is required. Without it the user's systemd manager, and so the player, only runs while someone is logged in.

The user needs access to the serial device and the ALSA sequencer (usually the `uucp`/`dialout` and `audio` groups).

### What the unit does

The unit is `packaging/midicontrol.service.in`, configured at build time.

| Setting | Value | Why |
|---|---|---|
| `Type=notify`, `NotifyAccess=main` | the player sends `READY=1` | "Started" means the config is loaded and the status page thread is running. It does **not** wait for the base, which may be unplugged. |
| `WatchdogSec=30` | the player sends `WATCHDOG=1` every 5 s | Sent **only** while the serial reader, keepalive and control threads have all made progress in the last 10 s. A hung player is therefore restarted. |
| `Restart=always`, `RestartSec=5` | | It comes back after any exit, crash or watchdog kill. |
| `StartLimitIntervalSec=0` | | systemd never gives up restarting it. |

The web thread is deliberately **not** part of the watchdog check. A stuck web page never restarts playback.

No libsystemd is needed: the datagrams go straight to `$NOTIFY_SOCKET`, and nothing is sent when it is unset (started by hand).

### Single instance

- The player takes an exclusive `flock` on `$XDG_RUNTIME_DIR/midicontrol.lock`, falling back to `/tmp/midicontrol-<uid>/` when `XDG_RUNTIME_DIR` is unset.
- It opens the serial port with `TIOCEXCL`, so a second process can never steal the base link.

---

## 2. The status page

Open `http://<pc>:50118/` from any machine on the LAN. The page is one self-contained HTML document: inline CSS and JS, no CDN, fonts or external requests. It polls `GET /api/status` once a second and follows the browser's light or dark setting.

The page has two views, switched in the top bar: **Přehled** (home, in Czech, for the organist) and **Pokročilé** (Advanced, in English, the full debug detail). The top bar also shows the live connection ("aktualizováno před N s"). If a request fails or takes longer than 3 s, it shows a red dot and "bez spojení s přehrávačem", a red notice appears on the home page, and the last data stays on screen greyed out.

Addresses: `#` home, `#stops` home scrolled to the stops editor, `#adv/overview|software|base|controller|diag|firmware` the Advanced pages (`firmware` is the upload tab, see "Firmware tab") (the old `#base`, `#diag`, ... still open the matching Advanced page).

### Home (Přehled)

- **Notices**, only when something needs attention, in plain Czech, each with a "Podrobnosti" link to the Advanced page that explains it: player not answering, base not connected / not answering / firmware update, an unseen firmware fault (base or remote), remote battery low or flat, song-file problems.
- **Systém**: the three parts as a chain Počítač —USB— Základna —BLUETOOTH— Ovladač. Each has its health ring (the node's dot), a state line and one fact: the player's uptime and song count, the organ power (from the base's `STATUS_RESP`), the remote's battery. A wire is green and animated while the link is up, dashed while down or unknown, red when the base's keepalives are lost; the Bluetooth wire uses the base's report (fresh within 15 s), as on the Controller page. Below it: what is playing (song, section) or the last song and how it ended.
- **Rejstříky písní**: the stops editor (see "Stops editor and the per-song stop files"). It needs no status data, so it works before the first status poll succeeds; leaving or reloading the page with unsaved edits asks first.

### Advanced (Pokročilé)

A side menu (a scrolling row on phones) with a health dot per page.

**Overview:** one row per node (Software, Base, Controller), each with its dot and a one-line summary. Below that, **Problems** lists the most recent WARN/ERROR events from all nodes, newest first. Problems keeps the last 50, so routine traffic can't push a warning out.

**Software, Base and Controller** all have the same four blocks (status chips, then panels) in the same order:

| Block | Software | Base | Controller |
|---|---|---|---|
| **Status** | running; idle / warming up / playing `<code>` `<section>`; song-file warnings; activity (e.g. firmware update) | connected / waiting / keepalives lost / firmware update; MIDI port; organ power; "GET_STATUS not answered" | BLE connected / disconnected (per base), or no fresh report; last heard |
| **Details** | version (git hash + build date), start time and uptime, config path, play folder and song count, current or last song with its sections and how it ended, song codes with ⚠ on problem files; the stops of the current song | serial device and VID:PID, MIDI out port, last keepalive ACK age, connected since and reconnect count, latest `STATUS_RESP` flags (BLE, relay, display, PC) and their age, church display; sections **Firmware** and **Bluetooth** from `INFO_RESP` (below) | route ("via base, BLE", with the age of the base's report), last command received and its ACK/NAK, last packet sent to it and its result; sections **Power**, **Firmware** and **Bluetooth** from `INFO_RESP` |
| **Counters** | songs played (ended / stopped), warnings / errors | packets tx / rx, retries / gave up, NAK sent / received, CRC errors / duplicates | packets tx / rx, retries / gave up, NAK sent / received, received commands by type |
| **Recent events** | log lines (INFO/WARN/ERROR) | packets to and from the base, plus link events | packets to and from the controller, plus link events |

Base and Controller also show an "Unseen fault (Diagnostics)" chip while their dot is amber for an unmarked fault.

#### Counter attribution

| Traffic | Counts towards |
|---|---|
| Packets to or from BASE (0x02) | Base |
| Packets to or from REMOTE (0x03) | Controller |
| Packets to BROADCAST | both |
| CRC and framing errors (can't be attributed to a node) | Base, which owns the serial link |

#### What becomes an event

| Traffic | Recent events | Journal |
|---|---|---|
| Routine: KEEPALIVE / KEEPALIVE_ACK, the 5 s GET_STATUS / STATUS_RESP poll and their ACKs | no (counters and "last keepalive" / "base reports" only) | no |
| Other packets (`TX NOW_PLAYING_INFO id=17 [40 6A 01 FF FF]`, `RX REPLAY_ANCHOR id=8`) | yes, as DEBUG | no |
| INFO, WARN, ERROR log lines | yes | yes |

Each node keeps its last 200 events in memory. Nothing survives a restart: the journal is the long-term log.

### Dot rules

| Node | Green | Yellow | Red | Grey |
|---|---|---|---|---|
| Software | otherwise | a Software WARN in the last 10 min, or a song file with a problem | a Software ERROR in the last 10 min | never |
| Base | connected, keepalive OK | waiting for the base (unplugged), GET_STATUS not answered, or a firmware update in progress | serial open but keepalives lost | never |
| Controller | the base's last STATUS_RESP (≤ 15 s old) says BLE connected | BLE reported disconnected, or the remote reports a critical battery | never: the PC can't tell a fault from "switched off" | base not connected, or no fresh STATUS_RESP |

An unplugged base is a state, not an error: the dot is yellow "Waiting for the base" and the player reconnects on its own.

Base field **`in_bootloader`**: true while the serial port is closed, no update is running and the base shows up as its USB bootloader (ABCD:1235), i.e. a firmware update did not finish (P1-13). The player logs an ERROR once; the console `status` command then advises `update base <file>` to re-flash it. Nothing is re-flashed automatically.

### Base status poll

While connected, the player sends `GET_STATUS` to the base every 5 s. A NAK, or no answer within 2 s, shows "not answered" (yellow), not an error. The poll and keepalives pause during a base firmware update, because the bootloader ignores them.

**Organ power** on the page is the base's SSR as `STATUS_RESP` reports it, whoever switched it (PC, the base menu "Napajeni varhan", the remote). The player also uses it to decide whether a song needs the warm-up: switched on at the base, a song starts at once. A `STATUS_RESP` to a `GET_STATUS` sent before the PC's last `SET_CURRENT` is not used (it may predate the switch). Unknown (grey) while the base is not connected.

### Device details (GET_INFO)

The player sends `GET_INFO` to the base and to the remote: once when the node comes up (serial connected; the base reports the BLE link), then every 15 s. The answer `INFO_RESP` is a list of TLV fields (`midicontrol-protocol` `inc/protocol/info.hpp`): firmware revision, branch, build time, "uncommitted changes" flag, uptime, reset cause, faults since power-on, Bluetooth flags (module ready, connected, stream open, secured, peer alive, PC alive), the paired/connected peer's MAC, the device name and the base's pairing state; the base adds church display and SSR, the remote battery voltage, state of charge in % (older remote firmware without it: the page computes it from the voltage with the same curve), charger state (charging, charged, USB present, critical) and the USB voltage. The page advances the uptime between reports. When the node goes away (serial lost, BLE down) its details are cleared at once. Older firmware answers `NAK UNKNOWN_MSG`: the page says "firmware without GET_INFO" and the player asks again only after the node comes up again. `NAK UNREACHABLE` (remote link not secured yet) retries after 5 s. Routine traffic: counters only, no events. A remote battery below the cut-off turns the Controller dot yellow.

### Song-file check

The play folder is scanned at start and rescanned every 60 s on the web thread, never on the control thread. Only files whose mtime or size changed are parsed again, so songs added while the service runs show up. Each problem is logged as **one** WARN when first found.

| Problem text | Meaning |
|---|---|
| `cannot parse: <reason>` | the MIDI file reader rejected it |
| `empty display anchor` | an empty SysEx (`F0 F7`), or one with the MIDIControl header that doesn't decode, before the first note (e.g. `523.mid`) |
| `no display anchors` | no display SysEx with a verse field at all (not repeated when an empty anchor was already reported) |
| `file name is not a valid song code` | the name without `.mid` is not a song code: 1-4 digits, value <= 1999, optional A-D (e.g. `072 - Lide muj.mid`, `2000.mid`, `rejstriky.mid`) |

### JSON API

`GET /api/status` returns **schema version 2** (`/api/diag` and `/api/stops` are schema 1, each with its own `schema` field):

```json
{"schema":2,"now_ms":1792432422052,
 "nodes":{
   "software":{"dot":"green","fields":{"version":"73ebcbf","playback":"idle"},
               "counters":{"songs_ended":3},
               "events":[{"t":1792432422052,"level":"INFO","text":"Playing 406A: 3 sections"}]},
   "base":{"dot":"yellow","fields":{"connected":false},"counters":{"tx":51},"events":[]},
   "controller":{"dot":"grey","fields":{},"counters":{},"events":[]}},
 "problems":[{"t":1792432422052,"level":"WARN","node":"software","text":"song 523.mid: empty display anchor"}],
 "songs":[{"code":"401","problem":""},{"code":"523","problem":"empty display anchor"}]}
```

- Each node object also carries `last_fault` (the newest decoded fault of that node, `null` if none) and `unseen_faults` (count not yet acknowledged with **Mark seen**); both are written for every node. They drive the yellow dot and the home page notice.
- Events and problems are newest first.
- Times are ms since the epoch.
- Text is escaped so the JSON is valid for any bytes: invalid UTF-8 becomes `�`.
- Firmware-reported data, when the base and controller start sending it, becomes extra `fields` and counters on the same node objects, so the shape doesn't change.

Field names, which are the contract between `CommHandler`, `main` and the page:

| Node | Fields | Counters |
|---|---|---|
| software | `version`, `build_date`, `started_ms`, `config_path`, `play_folder`, `song_count`, `song_problems`, `playback`, `stops`, `song`, `section`, `last_song`, `last_sections`, `last_end`, `activity` | `songs_ended`, `songs_stopped`, `warnings`, `errors` |
| base | `connected`, `alive`, `updating`, `device`, `vidpid`, `midi_port_found`, `midi_out`, `keepalive_ack_ms`, `connected_since_ms`, `reconnects`, `organ_power` (base SSR, null = unknown), `status_ms`, `status_ble`, `status_relay`, `status_display`, `status_pc`, `status_answered` (the `status_*` fields are null while not connected) | `tx`, `rx`, `retries`, `busy_retries`, `gave_up`, `nak_sent`, `nak_received`, `crc_errors`, `duplicates`, `warnings`, `errors` |
| controller | `last_rx_ms`, `last_cmd`, `last_cmd_reply`, `last_cmd_ms`, `last_tx`, `last_tx_result`, `last_tx_ms` | the same packet counters, plus `cmd.<MSGTYPE>` per received command |
| base, controller (INFO_RESP) | `info_ms`, `info_unsupported`, `fw_revision`, `fw_branch`, `fw_built`, `fw_dirty`, `uptime_ms`, `reset_cause`, `fault_count`, `ble_connected`, `ble_stream_open`, `ble_secured`, `ble_bonded`, `ble_module_up`, `ble_peer_alive`, `ble_pc_alive`, `ble_peer`, `ble_name`, `ble_pairing`, `link_rssi`, `link_peer_rssi`, `link_interval_ms`, `link_latency`, `link_timeout_ms`, `link_phy`, `link_losses`, `link_err_conn`, `link_module_reboots`, `link_secure_retries`, `link_reconnect_ms` (RSSI fields are omitted when not measured); base `display`, `ssr`; controller `battery_mv`, `battery_level`, `battery_percent`, `charging`, `charge_done`, `vbus`, `battery_critical`, `vbus_mv` (all null when not reported) | |

Routes: `GET /`, `GET /api/status`, `GET /api/diag`, `GET /api/stops`, `PUT /api/stops/<code>`, `POST /api/firmware/<base|remote>`. Any other path returns 404. A wrong method on a known resource returns 405 with an `Allow` header: `POST` for `/api/firmware/...` and `/api/firmware`, `PUT` for `/api/stops/<code>`, `GET` for the rest. Malformed requests get 400, oversized ones 413, chunked ones (no `Content-Length`) 411, and incomplete ones 408 after 3 s. Limits (`include/midicontrol/http.hpp`): head plus body together 8 KiB (`kMaxRequest` 8192), body 4 KiB (`kMaxBody` 4096), firmware upload body 512 KiB (`kMaxFirmwareBody`, that route only).

### Isolation from playback

- The web server runs on its own thread and only reads copies of the status registry, taken under the registry's short lock. It never takes `CommHandler` or `Player` locks.
- It serves up to 8 connections at once (`poll()`), so an idle browser connection can't hold up the others. Each request has a 3 s deadline and an 8 KiB head+body limit (4 KiB body; 512 KiB for the firmware upload).
- Writes use `MSG_NOSIGNAL`, and every exception is caught and logged.
- If the port can't be bound (in use, or an invalid `Bind`), the server logs **one** ERROR and retries every 10 s while playback runs normally.

---

### Stops editor and the per-song stop files

Each song can have a saved set of organ stops (rejstriky). The stops editor on the home page lists the songs of the play folder in its own scrolling list, built for 100+ songs: search by song number (Enter opens the first match), filter all / set / not set, sort by number up or down or by stop count, and a footer with the count shown and how many are set; the list follows the editor while the page scrolls and keeps its position while stops are clicked. It shows the organ's stops for the selected song and saves the set (while there are unsaved changes the Save bar stays at the bottom of the screen); at song start the player puts the organ in that state. The `.mid` files are never modified. Unlike the rest of the page this is a write path (`PUT /api/stops/<code>`): there is no login, so `Bind = 0.0.0.0` exposes it to the whole LAN (trusted LAN only), and `Bind = 127.0.0.1` keeps it local.

**Files:** one `<CODE>.conf` per song in the play folder, next to its `.mid` and named like it (`401.conf` beside `401.mid`, `406a.conf` beside `406a.mid`; found in any case). Written only by the page (temp file, `fsync`, `rename`, so a crash or two phones saving at once cannot corrupt it) and editable by hand only while the player is stopped (the next save rewrites the file, dropping comments; `[stops]` in any case and a missing header are accepted):

```ini
[STOPS]
Stops = 3 4 5 14
```

`Stops =` takes numbers separated by spaces, each in 1..stop count. A save writes only that song's file; files of songs no longer in the folder stay and are ignored. A bad line or number is skipped with a warning in the log. An unreadable file (permissions) is an error for the page (500) and leaves the song "not set" at playback. The old single `stops.conf` is no longer read; if one is still in the play folder the player logs a warning at start.

**Not set vs all off:** a song with **no file** is "not set": nothing is sent and the organ stays as it is. A song whose file has an **empty** `Stops =` is "all off": the all-off SysEx and nothing else. The page only enables Uložit (Save) after a tab was changed, and its "Zrušit nastavení" button deletes the file. A song shows "nenastaveno" (not set) or "vše vypnuto" (all off).

**At song start** (`CommHandler::doPlay`), after the organ warm-up, if the song has a file and `[ORGAN] StopsEnabled` is true: the all-off SysEx, `AllOffDelayMs`, one Program Change per stop (ascending, on `Channel`, `StopDelayMs` apart and after the last), then the song, all on the same MIDI output port. A stop or a new song during the sequence ends it as during the warm-up. While such a song plays, its own Program Changes are dropped (`DropSongProgramChanges`). Add-verse and replays do not resend stops. The log gets `Stops for 401: 3 4 5 14` (or `not set`, `all off`, `disabled`), also shown as the Software status field `stops`. The song's file is read at each song start, never through the web server's store, so a stops problem can never stop a song from playing.

**Stop names** default to Czech organ terminology as used in Czech parish dispositions (divisions "Pedál", "I. manuál – hlavní stroj", "II. manuál – žaluziový stroj"; stops noun first, e.g. "Flétna trubicová 8'", mixtures by ranks, e.g. "Mixtura 3řadá"), mapped from the Opus 910 console's Dutch labels (the mapping is commented in `src/organ.cpp`). `NameN` and `Groups` override them.

**`[ORGAN]` keys** (all optional, defaults are the Johannus Opus 910; see `config.conf`): `StopsEnabled`, `Channel`, `ProgramOffset` (program = octal ST number + offset), `ST1`..`ST42`, `Program1`..`Program42` (override), `DropSongProgramChanges`, `AllOffSysex`, `AllOffDelayMs`, `StopDelayMs`, `Groups`, `Name1`..`Name42`.

**API** (`Content-Type: application/json` required on PUT, else 415, so a plain cross-site form cannot post):

- `GET /api/stops` -> `{"schema":1,"groups":[{"label":"PEDAL","first":8,"last":16},...],"names":{"1":"..."},"songs":[{"code":"401","stops":[3,4,5]},{"code":"403","stops":null}]}`; `null` = not set. 500 with the error text if a song's file is unreadable.
- `PUT /api/stops/<CODE>` with `{"stops":[...]}` saves the set, `{"stops":null}` deletes the song's file. The code is upper-cased like song codes and must be a song in the folder; unknown code, bad body or numbers out of range -> 400; a failed write or delete -> 500 with the error text. Success returns `{"code":"401","stops":[...]}`.

**Open items (need someone at the organ):**

1. Verify the program numbers: in a MIDI monitor on the organ's MIDI OUT press Double Bass 16' (ST01): program 0 = the default offset -1, program 1 = set `ProgramOffset=0`. Then Swell to Great (ST60): 47 (48 with offset 0) confirms the octal rule. A stop that doesn't fit gets its `ProgramN`.
2. Verify that the all-off SysEx works on the Opus 910 and that `AllOffDelayMs = 100` is enough.
3. `songs/rejstriky.mid` is a recording, not a song (its name is not a valid code, so the song check flags it); move it out of the play folder when no longer needed.

---

### Diagnostics tab and `/api/diag`

The **Diagnostics** tab shows, per node (base, remote), the last fault (kind, decoded CFSR/HFSR bits, pc/lr, interrupted exception = `xpsr & 0x1FF`, BFAR/MMFAR only when valid, firmware revision, uptime), the reset-cause history (`?kind=reset`: the newest base `reset:` and remote `boot, reset:` lines, searched over the last 4000 info entries per node, refreshed every 10 s) and a filterable log table (node, level, tag, text search). It polls `GET /api/diag` every second while it is open; **Mark seen** calls `?ack=<id>` and acknowledges that node only. Until then the node's health dot is yellow (`unseen_faults > 0` in `/api/status`, schema 2). Data: the base's log CDC ("MIDIControl log", config `[DIAG] LogPort=` to override the auto-detection) and the remote's `DIAG_GET`/`DIAG_RESP` records, stored as JSONL in `[DIAG] Dir=`. The terminal command `diag` prints the last fault per node and the last 20 lines. Details: `CLAUDE.md`, section Diagnostics.

### Firmware tab and `POST /api/firmware/<base|remote>`

Advanced → Firmware (`#adv/firmware`) has one card per unit: the revision it runs now (GET_INFO), a drop zone and a **Flash** button. The file is the `<name>-update.bin` that every firmware build already writes next to the `.elf` (`midicontrol-fw/build/midicontrol-fw-update.bin` for the base, `midicontroller-fw/build/midicontroller-fw-update.bin` for the controller); `.elf`, `.hex` and a plain `.bin` are refused with a hint, because only the update file carries the trailer with the board ID and CRCs.

The base and controller images can't be swapped. Each check stops it on its own:

1. **Browser**, as soon as a file is dropped: it reads the 24-byte trailer (magic, trailer CRC, format, board ID, size, image CRC-32). If the board ID isn't the card's unit, it says "This is the controller firmware, not the base firmware. Drop it on the Controller card." and Flash stays off; nothing is sent.
2. **Service**, `fwupload::handlePost`: the same check with `fwimage::check` against the unit in the URL (`base` = 0x02, `remote` = 0x03). 422 `{"error": ...}` for any failure (a board ID that is neither unit is "built for an unknown unit (board ID N)", as on the page); the update is never started.
3. **Device**: `FW_UPDATE_START` carries the target and the firmware NAKs a foreign one; at `FW_UPDATE_END` the bootloader / remote app checks trailer, board ID and CRC again before committing (midicontrol-bootloader README).

The POST needs `Content-Type: application/octet-stream` (415 otherwise, so a cross-site form can't post), may carry up to 512 KiB (only this route; everything else keeps the 4 KiB body limit, and 8 KiB for head plus body) and gets 60 s to arrive instead of 3 s. The body must have a `Content-Length`: a chunked upload gets 411. `/api/firmware` without a target is 404, and any method other than POST on `/api/firmware/<target>` is 405. A valid file answers **202** `{"target","version","size"}` and the update runs on a worker thread of `ConsoleExecutor`. It answers **409** while another update runs (checked first, so a second upload during a base update says so rather than "the base is not connected"), while a song is playing, or while the base isn't connected; a CLI `update` and a page upload share one busy flag and the same playing refusal (`fwupload::updateRefusal`). Progress is in `/api/status` as Software fields `fw_update_target`, `fw_update_state` (`running` / `done` / `failed`), `fw_update_pct`, `fw_update_msg` (last updater message; a base update shows "Sending the image to the base bootloader..." during the transfer) and `fw_update_ms`, also for a CLI `update`, so the card shows a bar however the update was started. The Flash button asks for confirmation and stays off while a song plays, the base is disconnected or (for the controller) the base reports no BLE link. Stopping the service waits for an upload to finish (`ConsoleExecutor::waitForUpload` before `CommHandler::shutdown`).

There is no login: anyone who can open the page on the LAN can flash a valid image, just as they can edit stops. Set `[WEB] Bind=127.0.0.1` if that's too open.

## 3. Commands: terminal attached to the service

The service listens on a Unix socket, `$XDG_RUNTIME_DIR/midicontrol.sock`, mode `0600` (owner only). The web page cannot control the player (its writes are the per-song stop files and the firmware upload); all other control goes through the terminal.

```bash
midicontrol                                        # in a terminal
midicontrol --cmd "update base midicontrol-fw-update.bin"   # one-shot, for scripts
```

- **Service running:** `midicontrol` attaches as a thin client, with the same prompt and commands as before: `play <code>`, `stop`, `verse`, `list`, `record`, `on`, `off`, `status`, `diag` (last faults and log lines), `update <base|remote> <name-update.bin>`, `quit`. `verse` has the alias `repeat`. `quit` only **detaches**; the service keeps running.
- **No service running:** it runs standalone as before and takes the single-instance lock itself, so a service started later would fail until it exits.
- **`--cmd`:** prints the replies and exits **0** on success, **1** on failure (unknown command, song not found, nothing playing, base didn't answer, update failed, connection lost). With no service running it runs that one command standalone.
- **Where commands run:** inside the service, through the same `CommHandler` paths the remote uses. The client only sends a line and prints the text that comes back.
- **Firmware update:**
  - `update` streams its progress to the client until done.
  - The client turns a relative `.bin` path into an absolute one before sending. The service reads the file itself, so the service user needs permission to read it.
  - Only one update runs at a time, including one started from the page's Firmware tab; a second gets `busy: a firmware update is already running`.
  - `update` is refused while a song is playing (`refused: a song is playing; stop it first`), the same rule as the page upload, because the base reboots into its bootloader. This also covers `--cmd` and the control socket (audit SB-1, 2026-11-14).
  - While it runs, the Software tab shows the activity ("firmware update to base in progress") and the Base dot is yellow "Firmware update in progress".
- **Disconnects:** a client that drops mid-command never cancels it or blocks the service; its output is discarded. Stopping the service waits for a running firmware update to finish, so the base is never abandoned half-flashed.
- **Limits:** at most 4 clients at a time; a 5th gets `busy: too many clients`.
- **Logging:** each command from a client is an INFO event on the Software tab and in the journal (`console: play 406A`). Commands typed at a standalone prompt appear on the page but aren't echoed to stdout.
- **Recording:** `arecordmidi` is started with an empty signal mask, and `stop` gives it 2 s to exit on SIGTERM before killing it. A stuck recorder can't hang the control thread.

Socket protocol, for scripts: send one command per line. The reply is the command's text, ending with a line `\x1eOK` or `\x1eFAIL`. See `include/midicontrol/console.hpp`.

---

## 4. Configuration

A new optional section in `config.conf`:

```ini
[WEB]
# Enabled = true
# Port = 50118
# IPv4 address to listen on; 0.0.0.0 = every interface (trusted LAN only).
# Bind = 0.0.0.0
```

| Key | Default | Rules |
|---|---|---|
| `Enabled` | `true` | boolean (`true/false/yes/no/on/off/1/0`) |
| `Port` | `50118` | 1–65535, else the config fails to load |
| `Bind` | `0.0.0.0` | non-empty; must be an IPv4 address. This is checked only when binding: an invalid value logs an ERROR and keeps retrying. |

There is no login and no HTTPS, by design: the page is meant for a trusted LAN (its only write is the stops editor). Use `Bind = 127.0.0.1` to keep it local.

**Port clash:** the superpowers brainstorm companion server also uses 50118. Don't run it while the service is running or being tested.

---

## 5. Troubleshooting

| Symptom | Look at |
|---|---|
| Page doesn't load | `journalctl --user -u midicontrol`: an ERROR `status page: cannot listen on …` means the port is taken (e.g. the brainstorm server) or `Bind` is wrong. It retries every 10 s. |
| Page shows "no connection" | The service is stopped, restarting or frozen. `systemctl --user status midicontrol`. |
| Base dot yellow "Waiting for the base" | USB unplugged or not enumerated; the player reconnects by itself. |
| Base dot red | Serial open but the base doesn't answer keepalives: firmware hung or wrong device. |
| Base `in_bootloader` true / ERROR "The base is in its USB bootloader" | A base firmware update did not finish. Re-flash: `midicontrol --cmd "update base <file>-update.bin"`. |
| Controller dot grey | The base isn't connected, or hasn't answered GET_STATUS in the last 15 s. |
| `error: another midicontrol instance is running … but does not answer` | A second player holds the lock but its socket is gone. Find it with `pgrep -a midicontrol`. |
| `warning: a worker thread stalled; withholding the watchdog ping` | A CommHandler thread made no progress for 10 s; systemd will restart the service within 30 s. |
| `arecordmidi did not stop on SIGTERM; killed` | The recording file may be incomplete. |

---

## 6. Code map

Core library (`midicontrol_core`, no external dependencies, unit-tested):

| File | Role |
|---|---|
| `status.hpp/.cpp` | `StatusRegistry` (thread-safe; per-node fields, counters, 200-event rings, 50-entry problems list; injected clock), `nodesForPeer`, the dot rules (`evaluateDot`) |
| `status_json.hpp/.cpp` | `/api/status` JSON, UTF-8-safe `jsonEscape` |
| `http.hpp/.cpp` | request parsing (with body), routing, response building (no sockets) |
| `organ.hpp/.cpp` | `[ORGAN]` layout (`OrganConfig`, Opus 910 defaults) and the song-start preamble bytes |
| `stops_file.hpp/.cpp` | per-song `<CODE>.conf` parse/save/remove, `Store`, `/api/stops` JSON, `planFor`, `handlePut` |
| `web_page.hpp/.cpp` | the embedded page |
| `songcheck.hpp/.cpp` | per-file checks and the incremental `Scanner` |
| `console.hpp/.cpp` | command-line parsing (shared by standalone and attached mode), path absolutizing, reply markers |
| `log.hpp/.cpp` | process-wide logger: console plus registry events |
| `process.hpp/.cpp` | child spawn with a clean signal mask, and bounded termination |
| `protocol/names.cpp` | `msgTypeName`, `nakReasonName`, `describePacket` |

App layer:

| File | Role |
|---|---|
| `comm.cpp` | `CommHandler` instrumentation: counters per peer, link and playback fields, GET_STATUS poll, thread heartbeats (`threadsAlive`), `requestStatus`, `setUpdating` |
| `web_server.cpp` | the page thread |
| `console_exec.cpp` | runs commands for the local prompt and for socket clients |
| `control_socket.cpp` | socket server and the thin client (`runClient`) |
| `instance.cpp` | runtime directory and single-instance lock |
| `sd_notify.cpp` | `READY=1` / `WATCHDOG=1` / `STOPPING=1` |
| `main.cpp` | attach-or-run, `--cmd`, the watchdog thread |

---

## 7. How it was built and verified

It was built test-first, one task per commit, following the plan. A fresh reviewer then reviewed the whole branch, and the review fixes went in as one more commit.

**Unit tests:** 122 at the time of the first implementation, all passing, no compiler warnings (285 on 2026-11-14; one real-songs check fails because of the tracked `songs/rejstriky.mid`, see `TODO.md`). They cover:
- the registry and dot rules;
- JSON escaping (control characters, invalid and overlong UTF-8);
- HTTP parsing and routing (good, incomplete, oversize, malformed, unknown paths and methods);
- the embedded page (no external references, no `innerHTML`, fetch timeout);
- the song check against the real `songs/` files and temp folders;
- `[WEB]` config;
- command parsing;
- the logger;
- child-process signal handling;
- the new parser and dispatcher hooks.

**Container limits (historical, 2026-10-19):** RtMidi was not installed in the dev container then, so the CMake app target was skipped (it is built only when `pkg-config` finds `rtmidi`; the 2026-11-14 container builds it). Instead:

- **Stub build:** every app source was compiled with `-Wall -Wextra -Werror` and linked against a small RtMidi stub header, then run.
- **Real base visible:** the base is visible in the container as `/dev/ttyACM0`. All local runs after the first hid it with an `LD_PRELOAD` shim. The one earlier run exchanged only keepalive and status-request packets with it.
- **Web smoke test:**
  - a port squatter caused one ERROR, and the server bound on the retry;
  - responses were 404 / 405 / 200 / 400 / 413 as expected;
  - idle connections didn't block other requests;
  - SIGTERM gave a clean shutdown.
- **Socket smoke test:**
  - the socket had mode 0600;
  - `--cmd` exit codes were correct;
  - relative update paths were made absolute;
  - a second instance was refused;
  - a client dropping mid-command did no harm;
  - a 5th client got "busy";
  - an interactive attach worked through a pty, and `quit` detached while the service kept running;
  - the socket file was removed on exit.
- **systemd notify test:** `READY=1` arrived at start, `WATCHDOG=1` every 5 s, and `STOPPING=1` on SIGTERM.
- **Serial integration harness:** a real `CommHandler` ran against a fake base on a pseudo-terminal. It confirmed that the background poll creates no events, that the reader recovers after a failed reconnect, and that a firmware update raises no Base warnings and the dot is yellow.

### Fixed after the whole-branch review

Each fix has a test that failed before it and passes after.

1. **`record` over the socket could hang the control thread** (Critical). In service mode SIGTERM is blocked in every thread, so `arecordmidi` inherited a blocked SIGTERM. `stop` then waited forever, the watchdog restarted the service, and the recording was lost.
   - Fix: spawn with an empty signal mask; wait at most 2 s, then SIGKILL.
   - Test: `process_child_gets_sigterm_despite_blocked_parent`.
2. **The background GET_STATUS poll was logged every 5 s.** It filled the Base tab's events, against the routine-traffic rule.
   - Fix: register the packet id before sending.
   - Test: pty harness "poll".
3. **A failed reconnect after a firmware update left the base disconnected for good.**
   - Fix: the I/O threads keep running after a failed open, so the reader looks for the base again. `TIOCEXCL` is cleared on close so the same tty can be reopened.
   - Test: pty harness "reconnect". Both changes were shown to be needed.
4. **Every base firmware update showed up as a red fault.** The bootloader ignores keepalives.
   - Fix: `CommHandler::setUpdating` stops keepalives, the poll and loss warnings during an update; the dot shows yellow "firmware update".
   - Tests: `status_dot_base_updating`, pty harness "updating".
5. **The page kept looking fine when the service was frozen.**
   - Fix: fetch times out after 3 s and shows "no connection".
   - Test: `web_page_fetch_times_out`.

### Decisions taken during implementation

Each decision is listed with what it costs if it turns out wrong.

| Decision | Why | Cost if wrong |
|---|---|---|
| App layer verified by stub compile/link, local runs and a harness instead of unit tests | it needs RtMidi and serial hardware | logic bugs surface on the bench |
| "Empty display anchor" = empty SysEx, or a MIDIControl-header SysEx that doesn't decode, before the first note; other vendors' SysEx isn't flagged | a GM reset or similar must not be flagged | a broken non-MIDIControl anchor goes unreported |
| "No display anchors" isn't reported when "empty display anchor" already was | avoids two warnings for one file | one fewer line |
| NAKs received are INFO events on the peer node; a NAK to the poll only sets "not answered" | `NAK NOT_PLAYING` and similar are normal answers | a real refusal is less visible |
| Firmware-update messages are copied only to socket clients | the local terminal already gets them through the log on stdout | none |
| The web server handles up to 8 connections with `poll()`, not one at a time | an idle browser preconnect stalled every request by 2 s | about 60 more lines |
| Local runs hide the hardware with an `LD_PRELOAD` shim | the real base is attached; `unshare` isn't permitted in the container | none |
| The socket smoke test ran the real binary rather than a fake executor | covers more | none |
| Commands at the standalone prompt are page events but aren't echoed to stdout or the journal | avoids echoing what was just typed | they're missing from the journal |
| A standalone `--cmd` (no service) starts no page and no socket | it's a one-shot process | none |
| Stopping the service waits for a running firmware update | safer than abandoning a flash | a stop can take as long as an update (systemd's `TimeoutStopSec`, 90 s by default) |
| The review finding "page looks fine while frozen" was raised from Minor to Important | a health page must not look healthy while the service hangs | a few lines of JS |
| The serial-handling fixes are proven by the pty harness, which isn't part of CI | `CommHandler` can't join the core tests without RtMidi | the harness can rot |
| Version and build date come from configure time (`git rev-parse`, `string(TIMESTAMP)`) | simple and dependency-free | stale after a rebuild without reconfiguring |

### Known minor issues (open)

These came out of the review and are not fixed yet:

- Port-busy retries add a DEBUG line to the Software events every 10 s. An invalid `Bind` is only caught when binding, not when the config loads.
- Commands from other clients, or the remote, aren't refused while a firmware update runs. This is harmless today, because the bootloader ignores them.
- A retransmitted command is counted twice in `cmd.<TYPE>`, and it resets `last_cmd_reply`.
- Small file-descriptor race at control-socket shutdown: the fd is closed before it is removed from the client set.
- Ages and the 10-minute / 15-second windows use wall-clock time, so an NTP step at boot skews them.
- A song whose warm-up gets superseded doesn't record `last_song` / `last_end`.
- After a failed post-update reconnect, the message says it reconnects automatically. That is true only while the base comes back as the application (PID 1234); a base stuck in its bootloader isn't found.
- A request with bare-LF line endings waits 3 s and gets 408 instead of 400.
- An error partway through a folder scan stops song-list updates (an ERROR every 60 s).
- When `XDG_RUNTIME_DIR` is unset, the owner of the `/tmp/midicontrol-<uid>` fallback isn't checked. systemd always sets the variable for user services.

The reviewer also flagged a few things it did not judge:
- how robust the existing MIDI file parser is against crashing (as opposed to throwing) files, now that it runs every 60 s;
- a pre-existing data race on `SerialPort::_failed`;
- in the interactive standalone mode, Ctrl-C exits without cleanup, so the organ may stay on (pre-existing).

---

## Bench checklist (not run yet)

1. Build on the church PC with RtMidi. Make sure the brainstorm companion server isn't running (port 50118).
2. Run `midicontrol --headless`, or the service. Open `http://<pc>:50118/` and play a song from the remote. Watch the counters and events on each tab; Problems should show the `523` song warning.
3. Unplug the base: Base should go yellow "waiting" and Controller grey. Replug: Base goes green and `reconnects` goes up by 1.
4. Under systemd, run `kill -STOP <pid>`. The service should restart within about 30 s (`journalctl --user -u midicontrol`), and the page should show "no connection" in the meantime.
5. Hold the port before starting, e.g. `nc -l 50118`. Expect one ERROR, retries every 10 s, and normal playback. Stop `nc` and the page comes up.
6. Run `midicontrol` in a terminal: it should attach, and `quit` should detach while the service keeps running. Run `midicontrol --cmd "update base <file>-update.bin"`: it should stream progress and exit 0, and the Base dot should be yellow "firmware update" with no warnings. Kill the client mid-update: the update should still complete.
7. Run `record` from an attached terminal, then `stop`: the recording file should be complete and nothing should restart.
