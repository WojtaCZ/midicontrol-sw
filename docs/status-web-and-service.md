# Status web page and always-on service

The PC player (`midicontrol`) runs as an unattended systemd user service, serves a read-only status page on port **50118**, and accepts terminal commands through a local control socket. This document covers how to install and use it and how it behaves. It also records how it was built and tested, the decisions taken, and what is still open.

- Design (the binding spec): [`superpowers/specs/2026-10-19-status-web-design.md`](superpowers/specs/2026-10-19-status-web-design.md)
- Implementation plan: [`superpowers/plans/2026-10-19-status-web.md`](superpowers/plans/2026-10-19-status-web.md)

> **Status (2026-10-19):** implemented and merged to `main`, but **not yet tested on the bench hardware**. See [Bench checklist](#bench-checklist-not-run-yet).

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

The top bar shows **MIDIControl**, the host:port and "updated N s ago". If a request fails or takes longer than 3 s, the top bar shows a red dot and "no connection", and the last data stays on screen.

### Tabs

**Overview:** one row per node (Software, Base, Controller), each with its dot and a one-line summary. Below that, **Problems** lists the most recent WARN/ERROR events from all nodes, newest first. Problems keeps the last 50, so routine traffic can't push a warning out.

**Software, Base and Controller** all have the same four blocks in the same order:

| Block | Software | Base | Controller |
|---|---|---|---|
| **Status** | running; idle / warming up / playing `<code>` `<section>`; song-file warnings; activity (e.g. firmware update) | connected / waiting / keepalives lost / firmware update; MIDI port; organ power; "GET_STATUS not answered" | BLE connected / disconnected (per base), or no fresh report; last heard |
| **Details** | version (git hash + build date), start time and uptime, config path, play folder and song count, current or last song with its sections and how it ended, song codes with ⚠ on problem files | serial device and VID:PID, MIDI out port, last keepalive ACK age, connected since and reconnect count, latest `STATUS_RESP` flags (BLE, relay, display, PC) and their age, church display; sections **Firmware** and **Bluetooth** from `INFO_RESP` (below) | route ("via base, BLE", with the age of the base's report), last command received and its ACK/NAK, last packet sent to it and its result; sections **Power**, **Firmware** and **Bluetooth** from `INFO_RESP` |
| **Counters** | songs played (ended / stopped), warnings / errors | packets tx / rx, retries / gave up, NAK sent / received, CRC errors / duplicates | packets tx / rx, retries / gave up, NAK sent / received, received commands by type |
| **Recent events** | log lines (INFO/WARN/ERROR) | packets to and from the base, plus link events | packets to and from the controller, plus link events |

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

### Base status poll

While connected, the player sends `GET_STATUS` to the base every 5 s. A NAK, or no answer within 2 s, shows "not answered" (yellow), not an error. The poll and keepalives pause during a base firmware update, because the bootloader ignores them.

**Organ power** on the page is the base's SSR as `STATUS_RESP` reports it, whoever switched it (PC, the base menu "Napajeni varhan", the remote). The player also uses it to decide whether a song needs the warm-up: switched on at the base, a song starts at once. A `STATUS_RESP` to a `GET_STATUS` sent before the PC's last `SET_CURRENT` is not used (it may predate the switch). Unknown (grey) while the base is not connected.

### Device details (GET_INFO)

The player sends `GET_INFO` to the base and to the remote: once when the node comes up (serial connected; the base reports the BLE link), then every 15 s. The answer `INFO_RESP` is a list of TLV fields (`midicontrol-protocol` `inc/protocol/info.hpp`): firmware revision, branch, build time, "uncommitted changes" flag, uptime, reset cause, faults since power-on, Bluetooth flags (module ready, connected, stream open, secured, peer alive, PC alive), the paired/connected peer's MAC, the device name and the base's pairing state; the base adds church display and SSR, the remote battery voltage and level (0–4), charger state (charging, charged, USB present, critical) and the USB voltage. The page advances the uptime between reports. When the node goes away (serial lost, BLE down) its details are cleared at once. Older firmware answers `NAK UNKNOWN_MSG`: the page says "firmware without GET_INFO" and the player asks again only after the node comes up again. `NAK UNREACHABLE` (remote link not secured yet) retries after 5 s. Routine traffic: counters only, no events. A remote battery below the cut-off turns the Controller dot yellow.

### Song-file check

The play folder is scanned at start and rescanned every 60 s on the web thread, never on the control thread. Only files whose mtime or size changed are parsed again, so songs added while the service runs show up. Each problem is logged as **one** WARN when first found.

| Problem text | Meaning |
|---|---|
| `cannot parse: <reason>` | the MIDI file reader rejected it |
| `empty display anchor` | an empty SysEx (`F0 F7`), or one with the MIDIControl header that doesn't decode, before the first note (e.g. `523.mid`) |
| `no display anchors` | no display SysEx with a verse field at all (not repeated when an empty anchor was already reported) |
| `file name is not a valid song code` | the name without `.mid` isn't `[0-9A-Z]{1,8}` (e.g. `072 - Lide muj.mid`) |

### JSON API

`GET /api/status` returns schema version 1:

```json
{"schema":1,"now_ms":1792432422052,
 "nodes":{
   "software":{"dot":"green","fields":{"version":"73ebcbf","playback":"idle"},
               "counters":{"songs_ended":3},
               "events":[{"t":1792432422052,"level":"INFO","text":"Playing 406A: 3 sections"}]},
   "base":{"dot":"yellow","fields":{"connected":false},"counters":{"tx":51},"events":[]},
   "controller":{"dot":"grey","fields":{},"counters":{},"events":[]}},
 "problems":[{"t":1792432422052,"level":"WARN","node":"software","text":"song 523.mid: empty display anchor"}],
 "songs":[{"code":"401","problem":""},{"code":"523","problem":"empty display anchor"}]}
```

- Events and problems are newest first.
- Times are ms since the epoch.
- Text is escaped so the JSON is valid for any bytes: invalid UTF-8 becomes `�`.
- Firmware-reported data, when the base and controller start sending it, becomes extra `fields` and counters on the same node objects, so the shape doesn't change.

Field names, which are the contract between `CommHandler`, `main` and the page:

| Node | Fields | Counters |
|---|---|---|
| software | `version`, `build_date`, `started_ms`, `config_path`, `play_folder`, `song_count`, `song_problems`, `playback`, `song`, `section`, `last_song`, `last_sections`, `last_end`, `activity` | `songs_ended`, `songs_stopped`, `warnings`, `errors` |
| base | `connected`, `alive`, `updating`, `device`, `vidpid`, `midi_port_found`, `midi_out`, `keepalive_ack_ms`, `connected_since_ms`, `reconnects`, `organ_power` (base SSR, null = unknown), `status_ms`, `status_ble`, `status_relay`, `status_display`, `status_pc`, `status_answered` (the `status_*` fields are null while not connected) | `tx`, `rx`, `retries`, `gave_up`, `nak_sent`, `nak_received`, `crc_errors`, `duplicates`, `warnings`, `errors` |
| controller | `last_rx_ms`, `last_cmd`, `last_cmd_reply`, `last_cmd_ms`, `last_tx`, `last_tx_result`, `last_tx_ms` | the same packet counters, plus `cmd.<MSGTYPE>` per received command |
| base, controller (INFO_RESP) | `info_ms`, `info_unsupported`, `fw_revision`, `fw_branch`, `fw_built`, `fw_dirty`, `uptime_ms`, `reset_cause`, `fault_count`, `ble_connected`, `ble_stream_open`, `ble_secured`, `ble_bonded`, `ble_module_up`, `ble_peer_alive`, `ble_pc_alive`, `ble_peer`, `ble_name`, `ble_pairing`; base `display`, `ssr`; controller `battery_mv`, `battery_level`, `charging`, `charge_done`, `vbus`, `battery_critical`, `vbus_mv` (all null when not reported) | |

Any other path returns 404 and any method other than GET returns 405. Malformed requests get 400, oversized ones (> 8 KiB) 413, and incomplete ones 408 after 3 s.

### Isolation from playback

- The web server runs on its own thread and only reads copies of the status registry, taken under the registry's short lock. It never takes `CommHandler` or `Player` locks.
- It serves up to 8 connections at once (`poll()`), so an idle browser connection can't hold up the others. Each request has a 3 s deadline and an 8 KiB limit.
- Writes use `MSG_NOSIGNAL`, and every exception is caught and logged.
- If the port can't be bound (in use, or an invalid `Bind`), the server logs **one** ERROR and retries every 10 s while playback runs normally.

---

### Diagnostics tab and `/api/diag`

The **Diagnostics** tab shows, per node (base, remote), the last fault (kind, decoded CFSR/HFSR bits, pc/lr, interrupted exception = `xpsr & 0x1FF`, BFAR/MMFAR only when valid, firmware revision, uptime), the reset-cause history (`?kind=reset`: the newest base `reset:` and remote `boot, reset:` lines, searched over the last 4000 info entries per node, refreshed every 10 s) and a filterable log table (node, level, tag, text search). It polls `GET /api/diag` every second while it is open; **Mark seen** calls `?ack=<id>` and acknowledges that node only. Until then the node's health dot is yellow (`unseen_faults > 0` in `/api/status`, schema 2). Data: the base's log CDC ("MIDIControl log", config `[DIAG] LogPort=` to override the auto-detection) and the remote's `DIAG_GET`/`DIAG_RESP` records, stored as JSONL in `[DIAG] Dir=`. The terminal command `diag` prints the last fault per node and the last 20 lines. Details: `CLAUDE.md`, section Diagnostics.

## 3. Commands: terminal attached to the service

The service listens on a Unix socket, `$XDG_RUNTIME_DIR/midicontrol.sock`, mode `0600` (owner only). The web page stays read-only; all control goes through the terminal.

```bash
midicontrol                                        # in a terminal
midicontrol --cmd "update base midicontrol-fw-update.bin"   # one-shot, for scripts
```

- **Service running:** `midicontrol` attaches as a thin client, with the same prompt and commands as before: `play <code>`, `stop`, `verse`, `list`, `record`, `on`, `off`, `status`, `update <base|remote> <name-update.bin>`, `quit`. `quit` only **detaches**; the service keeps running.
- **No service running:** it runs standalone as before and takes the single-instance lock itself, so a service started later would fail until it exits.
- **`--cmd`:** prints the replies and exits **0** on success, **1** on failure (unknown command, song not found, nothing playing, base didn't answer, update failed, connection lost). With no service running it runs that one command standalone.
- **Where commands run:** inside the service, through the same `CommHandler` paths the remote uses. The client only sends a line and prints the text that comes back.
- **Firmware update:**
  - `update` streams its progress to the client until done.
  - The client turns a relative `.bin` path into an absolute one before sending. The service reads the file itself, so the service user needs permission to read it.
  - Only one update runs at a time; a second gets `busy: a firmware update is already running`.
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

There is no login and no HTTPS, by design: the page is read-only and meant for a trusted LAN. Use `Bind = 127.0.0.1` to keep it local.

**Port clash:** the superpowers brainstorm companion server also uses 50118. Don't run it while the service is running or being tested.

---

## 5. Troubleshooting

| Symptom | Look at |
|---|---|
| Page doesn't load | `journalctl --user -u midicontrol`: an ERROR `status page: cannot listen on …` means the port is taken (e.g. the brainstorm server) or `Bind` is wrong. It retries every 10 s. |
| Page shows "no connection" | The service is stopped, restarting or frozen. `systemctl --user status midicontrol`. |
| Base dot yellow "Waiting for the base" | USB unplugged or not enumerated; the player reconnects by itself. |
| Base dot red | Serial open but the base doesn't answer keepalives: firmware hung or wrong device. |
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
| `http.hpp/.cpp` | request parsing, routing, response building (no sockets) |
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

**Unit tests:** 122, all passing, no compiler warnings. They cover:
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

**Container limits:** RtMidi isn't installed in the dev container and there's no network, so the CMake app target was skipped. Instead:

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
