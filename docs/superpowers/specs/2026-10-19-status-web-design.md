# Status web page and always-on service: design

Date: 2026-10-19 · Repo: `midicontrolpy-sw` (C++ PC player)

## Intent

The PC player has to run as a service that is always on and never needs anyone to touch it. A technical person, usually the author, needs a quick way to see whether the whole system is healthy and to dig into debug detail when it isn't. They open a web page served by the player, usually from a PC on the same network.

This first version shows **only what the PC software can observe**. The base (G431) and the controller/remote (G071) will later report their own debug data, such as battery, firmware version, uptime and reset cause. That needs protocol and firmware changes and is out of scope here, but the page must have an obvious place for it.

## Goals

1. A read-only status page on **port 50118**, served by the `midicontrol` process itself.
2. Four tabs: **Overview**, **Software**, **Base**, **Controller**.
3. The player runs unattended: it starts at boot without a login, comes back after any exit or hang, and nothing about the web page can stop or delay playback.
4. Terminal commands, firmware update included, are sent to the running service rather than to a second process.

## Non-goals

- Controlling anything from the **web page** (play, stop, power, config editing, firmware update). Control goes through the terminal command line, attached to the service (see "Commands to the running service").
- Debug data that only the firmware knows. New protocol messages are not part of this work.
- Keeping history across restarts. The journal (`journalctl`) is the long-term log.
- Login, HTTPS, or exposure to the internet.

## Anti-goals (a failure even if it "works")

- A bug, slow client or port conflict in the web part makes playback stutter, stop, or crash the process.
- Inconsistent structure: tabs that each look different, or the same kind of data shown in different shapes.
- A heavy look, or a page that needs internet access: CDN scripts, fonts, frameworks.

## The page (agreed with the user, mockup `layout-v2.html`)

Design: plain and clean. Text in tables, small uppercase section headings, and a coloured dot per state (green ok, yellow warning, red error, grey off or idle). No cards, badges or decoration. A top bar shows "MIDIControl", the host:port and "updated N s ago". The page works in light and dark (`prefers-color-scheme`).

**Overview tab:** one row per node (Software, Base, Controller): its dot plus a one-line summary. Below it, **Problems** lists the most recent WARN and ERROR events from all three nodes, newest first, with the node named.

**Software, Base and Controller tabs:** each has the same four blocks in this order:

| Block | Software | Base | Controller |
|---|---|---|---|
| **Status** | running, playback state (idle / playing `<code>` `<section>`), song-file warnings | connected / waiting, MIDI port found, organ power | last heard (time ago), or "never heard" |
| **Details** | version (git hash + build date), start time and uptime, config path, play folder and song count, current or last song with its sections and how it ended, the song codes with a warning mark on bad files (e.g. 523 empty anchor) | serial device and VID:PID, MIDI out port name, last keepalive ACK age, connected since and reconnect count, latest `STATUS_RESP` flags (BLE, relay, display, PC) and their age, then a greyed "Firmware debug: later" row | route ("via base, BLE"), last command received and its ACK/NAK, last packet sent to it and its result, then a greyed "Firmware debug: later" row |
| **Counters** | songs played (ended / stopped), warnings / errors | packets tx/rx, retries / gave up, NAK sent / received, CRC errors / duplicates | packets tx/rx, retries / gave up, NAK sent / received, received commands by type |
| **Recent events** | log lines from the software (INFO/WARN/ERROR) | packets to and from the base, plus link events | packets to and from the controller, plus link events |

Counters are per peer address: BASE 0x02 goes on the Base tab, REMOTE 0x03 on the Controller tab. A CRC or framing error can't be attributed to a node, so it counts towards the base, which owns the serial link. Packets to BROADCAST count on both.

The Base and Controller layouts must take the later firmware data as **extra rows in Details**, plus their own Counters and Events, without changing the layout.

## Decided by the user

- Variant B (tabs), with at least the Software, Base and Controller tabs, and the same structure on each.
- Only data the PC can see, for now.
- Port 50118.
- Design as simple as possible, yet clean.
- Service reliability as proposed below ("okay like that").

## Claude's calls (proposed, accepted without changes)

- **The web server lives inside the player process**, so the page shows real live state and is up exactly when the service is.
- **Read-only, no auth**, and it binds `0.0.0.0` by default (the church LAN is trusted). Configurable.
- **The page polls `GET /api/status` (JSON) about once a second** with `fetch`, and the page re-renders. One self-contained HTML document is embedded in the binary: inline CSS and JS, no external requests.
- **In memory only.** A ring buffer per node holds the last 200 events. Counters reset on restart. Everything also goes to stdout/stderr, and from there to the journal.
- **The base status is polled.** The player sends `GET_STATUS` to the base every 5 s while connected. The base firmware handles it (`midicontrol-fw/src/router.cpp:295`). A NAK or timeout shows "not answered", not an error.

## Constraints and existing code

- C++17, CMake, `-Wall -Wextra` with no warnings. The **core library has no external dependencies** and is unit-tested with the built-in harness in `tests/`. The app layer holds hardware and OS code. Keep that split.
- `comm` (`CommHandler`) already has a reader thread, a keepalive thread (2 s ping, base lost after 6 s) and a control thread that owns the `Player`. Reconnecting to the serial port after USB loss already works.
- Messages are printed with `std::cout`/`std::cerr` throughout `comm.cpp` and `main.cpp`. They need to reach the event lists as well, with the level and the node.
- Headless mode exists (`--headless`, implied without a TTY). The systemd **user** unit is `packaging/midicontrol.service.in` (`Restart=on-failure`, `Type=simple`).
- The protocol constants and `MsgType` names live in `include/midicontrol/protocol/types.hpp`. Event text uses those names (e.g. `REPLAY_ANCHOR id=8`).
- The visual-companion brainstorm server also uses 50118. It must not be running while the service is tested.

## Design

### Core (pure, unit-tested)

- **`status` model**: a thread-safe `StatusRegistry`. It holds per-node counters, the latest snapshot values (strings, numbers, timestamps) and a ring buffer of `Event{time, level, node, text}` per node. Every thread writes into it, and the web thread reads a consistent **snapshot** (a copy taken under the lock). The time source is injected so tests need no real clock.
- **JSON rendering** of a snapshot. The status schema is built so the firmware data can be added later without breaking it. The output is escaped correctly for any text (song names, packet bytes).
- **HTTP request parsing and response building** (request line, path, headers, size limits) as pure functions, so they are tested without sockets.

### App layer

- **Logging hook.** A small logger that writes the line to stdout/stderr as today and records an `Event` in the registry. Existing messages in `comm.cpp` and `main.cpp` move over to it. The interactive CLI output (prompts, `list` replies) stays plain console output.
- **Instrumentation** in `CommHandler`: counters on TX/RX/ACK/NAK/retry/give-up/CRC/duplicate, keyed by peer; link state; the latest keepalive; the `STATUS_RESP` flags; and play/stop/finish together with the song and section.
- **Web server.** A small built-in HTTP/1.1 GET server on POSIX sockets, on its own thread. It serves `GET /` (HTML), `GET /api/status` (JSON), and 404 for anything else.
  - **Isolation from playback.** It never takes `CommHandler`'s or the player's locks, only the registry's short snapshot lock.
  - **Hardening.** Per-connection timeouts, request size limit, a small connection limit, `SO_REUSEADDR`, and `SIGPIPE` ignored or `MSG_NOSIGNAL`.
  - **Port failure.** If the port can't be bound, it logs an ERROR and retries every 10 s, while playback runs normally.
  - **Robustness.** Exceptions inside the server are caught and logged and never escape the thread.
- **Config.** A new optional `[WEB]` section with `Enabled` (default `true`), `Port` (default `50118`) and `Bind` (default `0.0.0.0`). Document it in `config.conf` and `CLAUDE.md`.

### Always-on service

- **Unit file.**
  - Change `Restart=always`, set `StartLimitIntervalSec=0` (never give up), keep `RestartSec=5`, and set `Type=notify` with `WatchdogSec=30`.
  - Document `loginctl enable-linger` in the install notes as required, so it runs from boot without a login.
- **systemd notify** without libsystemd. Write `READY=1` and `WATCHDOG=1` datagrams to `$NOTIFY_SOCKET` directly (a few lines; a no-op when unset, e.g. when started by hand).
  - `READY=1` is sent once config is loaded and the web server thread is started. It does not wait for the base, because the base may be unplugged.
- **Watchdog = real liveness.** `WATCHDOG=1` is sent only if the reader, keepalive and control threads have all made progress recently.
  - Each loop stamps a heartbeat, and the control thread's queue wait gets a timeout so it ticks while idle.
  - A hung thread therefore leads to a systemd restart.
  - The web thread is not part of this check, so a stuck web page alone doesn't restart playback.
- **Unplugged base.** The player already waits and reconnects. The page shows "waiting for base" with a yellow dot. This is a state, not an error.

### Tests

- Core unit tests:
  - the registry: counters per peer, ring buffer wrap, snapshot consistency;
  - the JSON output: escaping, schema keys;
  - HTTP parsing: good requests, oversize and malformed ones, unknown paths.
- A manual check on the bench, recorded in the plan:
  - Run `--headless`, open `http://<pc>:50118/` and play a song. Watch the counters and events on each tab.
  - Unplug and replug the base.
  - `kill -STOP` the process to check that the watchdog restarts it.
  - Occupy the port first with `nc -l 50118`: an error is logged, the server retries, and playback still works.

## Settled after the builder check (Claude's calls)

**Event levels and retention**
- Levels are DEBUG, INFO, WARN and ERROR.
- Routine traffic is not logged as events. That covers KEEPALIVE/KEEPALIVE_ACK, the 5 s GET_STATUS/STATUS_RESP poll and their ACKs. Those only update counters and the "last keepalive" / "base reports" fields.
- Other packets are DEBUG events. They appear in the per-node Recent events but are **not** printed to stdout or the journal. INFO and above go to the journal as today.
- The existing `logPacket` "Status:" console print happens only in reply to the interactive `status` command, not for the background poll.
- Per-node ring: 200 events.
- WARN and ERROR events are also copied into a separate **problems list** of the last 50. Overview reads that list, so a warning isn't pushed out by traffic.

**Dot rules**

| Node | Green | Yellow | Red | Grey |
|---|---|---|---|---|
| Software | otherwise | a WARN in the last 10 min, or a song file with a problem | an ERROR in the last 10 min | never |
| Base | connected, keepalive OK | waiting for the base (unplugged), or GET_STATUS not answered | serial open but keepalives lost | never |
| Controller | base's last STATUS_RESP (≤ 15 s old) says BLE connected | BLE reported disconnected | never; the PC can't tell a fault from "switched off" | base not connected, or no fresh STATUS_RESP |

- The Controller summary reads "BLE connected (per base) · last heard N ago". "Last heard" is information, not health.

**Song-file check**
- A pure `songcheck` core module, unit-tested against `songs/`. Per file it reports:
  - SMF parse failure;
  - no display anchors / no sections;
  - an empty anchor (523);
  - a file name that isn't a valid song code.
- The play folder is rescanned every 60 s on the **web thread**, never the control thread. Only files whose mtime or size changed are re-parsed, so songs added while the service runs show up.
- Each problem is one WARN event when first found.

**Single instance**
- The player takes an exclusive `flock` on `$XDG_RUNTIME_DIR/midicontrol.lock` at startup. It also sets `TIOCEXCL` on the serial port, so a second process can never steal the base link.
- The bench test for a busy port uses `nc -l 50118` as the squatter, not a second player.

**Commands to the running service (decided by the user: "commands sent to running service")**
- The service listens on a Unix control socket, `$XDG_RUNTIME_DIR/midicontrol.sock`, mode 0600 (owner only). It doesn't listen on TCP; the web page stays read-only.
- Running `midicontrol` in a terminal:
  - **If the service is running**, it attaches to it. It is a thin client with the same prompt and the same commands as today: `play`, `stop`, `verse`, `list`, `record`, `on`, `off`, `status`, `update`, `quit`. `quit` only detaches; it does not stop the service.
  - **If no service is running**, it runs standalone as today and takes the single-instance lock itself.
  - A one-shot form for scripts, e.g. `midicontrol --cmd "update base fw-update.bin"`, prints the replies and exits non-zero on failure.
- **Who executes.** Commands run inside the service, through the same `CommHandler`/control-thread paths the remote uses. The client only sends a line and prints the text that streams back.
- **Firmware update.** `update` streams its progress lines to the client until done. The client turns the `.bin` path into an absolute path before sending. The service reads the file itself, so it needs permission to read that path.
- **Disconnects.** A client that drops mid-command never cancels it or blocks the service. Output to a dead client is discarded.
- **Logging.** Each command from a client is an INFO event on the Software tab (e.g. `console: play 406A`). It also shows on the Overview while running ("firmware update to base in progress").
- **Tests.** A core unit test parses the command lines (shared by standalone and service mode). The bench test runs `update` through the socket against the base.

## Left to the builder

- The exact JSON schema and the field names.
- How the logger is wired: a global vs a passed-in reference.
- The exact HTML/CSS, within the look above and the mockup.
- Thread heartbeat thresholds, within the 30 s watchdog.
