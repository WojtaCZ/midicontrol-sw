# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Read first: system spec and hardware

`docs/SYSTEM.md` is the owner's specification of what the whole MIDIcontrol system (PC software, base, remote,
bootloader, protocol) is expected to do, with requirement IDs, owner decisions and a gap list. Its main copy lives in
`midicontrolpy-sw/docs/SYSTEM.md`; the copies in the other repos are kept identical by
`midicontrolpy-sw/tools/sync-system-doc.sh` (`--check` reports drift). Edit only the main copy. When the code and the spec
disagree, the spec says what is wanted: ask the owner instead of changing the spec to match the code.
`docs/HARDWARE-base.md` / `docs/HARDWARE-remote.md` (or `docs/HARDWARE.md` in the matching firmware repo) describe the
base (STM32G431) and remote (STM32G071) hardware. The firmware pin maps are authoritative; the KiCad repos are historical.

- **User-visible changes** (screen text, menus, keys, timeouts, web page, CLI output, config keys, install steps): in the same commit series update midicontrolpy-sw/docs/SYSTEM.md (status/glossary/timing; then run midicontrolpy-sw/tools/sync-system-doc.sh), this CLAUDE.md/README, midicontrolpy-sw/docs/status-web-and-service.md for the PC, the screen scenarios in tools/screens/main.cpp, and the Czech user manual.

This repo holds the MAIN copy of `docs/SYSTEM.md`. After editing it run `tools/sync-system-doc.sh` and commit the copies in the sibling repos.

## Project Overview

MIDIControl PC companion app — a CLI tool that communicates with a MIDIControl hardware device (organ/MIDI controller) over a serial port. It plays and records MIDI files, controls power to the organ, and maintains a keepalive connection with the Base unit.

The app is written in C++ (the original Python implementation was removed; only stale `__pycache__/` remains).

The project language is English (comments, UI strings, documentation).

## Building & running (C++)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/midicontrol            # in a terminal: attaches to the running service, else runs standalone
./build/midicontrol --headless # service mode (implied when stdin is not a TTY); stops on SIGINT/SIGTERM
./build/midicontrol --cmd "update base fw-update.bin"  # one command (service or standalone), exit 0 = ok
./build/midicontrol_tests      # run unit tests (no hardware required)
ctest --test-dir build         # same tests via CTest
cmake --install build          # binary, share/midicontrol/config.conf.example, lib/systemd/user/midicontrol.service
```

Device: USB serial VID:PID=ABCD:1234 (base bootloader: ABCD:1235) plus ALSA MIDI ports named "MIDIControl"; both are re-discovered after a USB reconnect. Uses `arecordmidi` for recording.

Config file: `--config PATH`, else `$MIDICONTROL_CONFIG`, else `$XDG_CONFIG_HOME/midicontrol/config.conf` (`~/.config/...`), else `./config.conf`. Sections: `[FILES]` (PlayFolder/RecordFolder; `~/` and config-relative paths allowed), `[PLAYBACK]` (WarmupMs, KeepPowerOn, VerseGapMs), `[WEB]` (Enabled=true, Port=50118, Bind=0.0.0.0), `[DIAG]` (LogPort= override of the log CDC auto-detection, Dir= store directory, default `$XDG_STATE_HOME/midicontrol`, else `~/.local/state/midicontrol`, else `/tmp/midicontrol-<uid>/diag`, created recursively), `[ORGAN]` (stops layout, Opus 910 defaults), `[BINDINGS]` (parsed, reserved, unused: do not remove). See `config.conf`.

## Service, status page, control socket

Full operator guide, behaviour, design decisions, known issues and the bench checklist: [`docs/status-web-and-service.md`](docs/status-web-and-service.md).

- **Status page**: `http://<pc>:50118/` (`GET /`, `GET /api/status` JSON schema 2, `GET /api/diag`, `GET /api/stops` and `PUT /api/stops/<code>` for the per-song organ stops in `<PlayFolder>/<CODE>.conf` (needs `Content-Type: application/json`), `POST /api/firmware/<base|remote>` with a `<name>-update.bin` as `application/octet-stream` (≤ 512 KiB, 202 then progress in Software `fw_update_*` fields)), served by the player process itself on its own thread. Two views: home "Přehled" (Czech, for the organist: notices, system topology, now playing, stops editor) and "Pokročilé" (English: Overview / Software / Base / Controller / Diagnostics / Firmware, `#adv/<page>`; Firmware = drop zone per unit, the trailer's board ID is checked in the browser, by `fwupload` and by the device, so base and controller images can't be swapped); design context in `PRODUCT.md`; Base and Controller show only what the PC sees (firmware debug data later = more `fields` in the same JSON). Port 50118 is also used by the superpowers brainstorm companion server; don't run both.
- **Single instance**: exclusive `flock` on `$XDG_RUNTIME_DIR/midicontrol.lock` (fallback `/tmp/midicontrol-<uid>/`); the serial port is opened with `TIOCEXCL`.
- **Control socket** `$XDG_RUNTIME_DIR/midicontrol.sock` (0600): `midicontrol` in a terminal attaches to the running service as a thin client (commands run in the service; `quit` detaches); `--cmd` is the one-shot form. Line protocol in `console.hpp` (reply ends with `\x1eOK` / `\x1eFAIL`).
- **systemd**: `Type=notify` (`READY=1`, `WATCHDOG=1` sent directly to `$NOTIFY_SOCKET`, no libsystemd), `WatchdogSec=30`, `Restart=always`, `StartLimitIntervalSec=0`. The watchdog ping is sent only while the CommHandler reader, keepalive and control threads all stamp their heartbeats. `loginctl enable-linger $USER` is required for start at boot. Divergence: on main the systemd user unit IS the production service; the unmerged branch `docker-deployment` (worktree `midicontrolpy-sw-docker`) proposes Docker as production instead. Recorded only; nothing merged, the owner decides.

## Diagnostics (base log + remote DIAG records)

- **Sources**: the base's second CDC "MIDIControl log" (text lines `[ssssss.mmm] L tag: text`, read by `LogReader` thread -> `LogIngest` -> `DiagStore`, node "base") and the remote's binary records (`DIAG_GET` every 10 s while the remote link is up, answered by `DIAG_RESP`, driven by `DiagPoller` inside `CommHandler`, node "remote").
- **Port selection**: `findMidiControlPort()` = USB interface 00 of ABCD:1234 (never the log CDC); `findMidiControlLogPort()` = interface string "MIDIControl log", fallback interface 04. Both take a sysfs root for tests. `[DIAG] LogPort=` overrides the log port. Every serial port is opened with TIOCEXCL plus `flock(LOCK_EX|LOCK_NB)`: TIOCEXCL keeps out programs that open it later, the flock keeps us out when picocom/minicom (which honour it) opened it first. Busy = open fails and the log reader retries every 1 s without logging, so close the terminal and the service picks the port up by itself.
- **Store**: JSONL per node, rotating (1 MiB x 3), global monotonic ids, in-memory tail cache (2000 entries per node) so the 1 s web polling does not re-read files; fault acknowledgement per node in `diag-ack.json` (`{"ack":legacy,"base":id,"remote":id}`; `ack=<id>` acknowledges the node that fault belongs to). `replace()` rewrites one entry (fault upgrades).
- **Replay window rule** (`LogIngest`): after a base reset the pre-reset log is replayed between `--- log before reset ---` and `--- end of log before reset ---`, then the boot banner prints the full fault record. Only inside that window (up to the first non-`sys`, non-fault line after the banner) a fault with the same kind and count as the last stored one *replaces* that entry. Outside it the same kind/count is a new entry: the base's fault counter restarts after a power loss.
- **Replay dedupe**: the replayed ring is the run that just ended; the part the PC already read live is a prefix of it ending with the last stored line. `LogIngest` buffers the replay body and skips only such a prefix (exact (uptime, tag, text) match of the stored tail, seeded from the store at start), so a reset no longer stores ~20 lines twice, while an identical banner of a run the PC never saw is still kept.
- **API**: `GET /api/diag?node=base|remote&level=E|W|I|D&before=<id>&limit=<=200>&ack=<id>&kind=reset` -> `{"schema":1,"entries":[...],"next_before":id|null}`. `/api/status` (schema 2) has per node `last_fault` and `unseen_faults`; base/controller health dot is yellow while `unseen_faults > 0` (until "Mark seen" = `ack`).
- **Console**: `diag` prints the last fault per node and the last 20 lines.
- **Remote polling rules**: no DIAG_GET during a firmware update (base update: `setUpdating`, which also stops keepalive/status; remote update: `CommHandler::pauseDiag` = `DiagPoller::setPaused`, keepalive/status continue); every DIAG_RESP is ACKed (dispatcher); NAK UNKNOWN_MSG stops the polling until the next link-up; NAK UNREACHABLE pauses it, flushes a half-received FAULT (as a link loss does) and forgets the link state, so the next STATUS_RESP with ble=1 (every 5 s) is a fresh link-up edge with one GET (R27 + R33: the base reports ble=1 from `%CONNECT%` but NAKs UNREACHABLE until the stream is open and secured, and a drop shorter than 5 s never shows ble=0); NAK BUSY only backs off to the next 10 s tick and never stops it (BUSY with the link up must not silence diagnostics); cursor = next_cursor, reset on boot_id change; a full page (n = 8) is followed up at once.

## Dependencies (C++)

- C++17 compiler, CMake ≥ 3.16
- **RtMidi** (pkg-config `rtmidi`) — MIDI playback/port discovery (only the `midicontrol` app target; core + tests have no external deps)
  - The dev container has the `rtmidi` package (6.0, checked 2026-10-26), so the full app builds there too (bench runs in a scratch build dir). Earlier bench builds (2026-10-19/20) used an ALSA shim instead.
- POSIX termios (serial) + pthreads
- `arecordmidi` (ALSA utils) for recording

## C++ Architecture

The code is split into a **pure core library** (`midicontrol_core`, no hardware deps, fully unit-tested) and a thin **app layer** (RtMidi + serial). Public headers live in `include/midicontrol/`, sources in `src/`.

Core (testable anywhere):
- **`protocol/`** (`crc8`, `codec`, `parser`, `constants`, `types`) — binary protocol v2: frame format (SOH/EOT, byte stuffing, CRC-8 poly 0x31, init 0, non-reflected, no final XOR, check value 0xA2 for "123456789"; this is NOT CRC-8/MAXIM), `Packet`, `MsgType`, NAK reasons, and a byte-by-byte `Parser` state machine. Vendored from `midicontrol-protocol` (the spec is its `PROTOCOL.md`); wire format is byte-identical. The PC copy uses `std::vector`/`size_t` (no fixed buffers).
- **`sysex`** — MIDIControl display SysEx (header `4D 43`, device `01`, targets `0x00`–`0x04`). Per-digit spec (each digit one byte ≤ `0x0F`). `DisplayInfo` keeps the decoded numbers and the digits as authored (`songDigits`/`verseDigits`).
- **`smf`** — Standard MIDI File reader (format 0/1, running status, tempo map → seconds). Accepts both the project files' raw `F0 … F7` SysEx (no length) and standard `F0 <len> … F7`.
- **`display`** — NOW_PLAYING_INFO encoding (per-digit BCD, `0xF` blank digit, `0xFF` no change, `0xFE` blank field, empty = clear), song-code fallback digits, verse-digit placement.
- **`songs`** — song codes (`[0-9A-Z]{1,8}`, sanitised), case-insensitive `<code>.mid` lookup, sorted song list and SONG_LIST_RESP paging.
- **`playback`** — `scanSections` (the file defines the song: every display SysEx with a verse field starts a section; blank verse = prelude), `sectionLabel` ("Predehra"/"Sloka N +k") and `PlaybackEngine` (plays the file; add-verse replays the current section from its anchor once it ends, presses accumulate, the indicator keeps the verse number; clock and outputs injected, so it is tested without time or ports).
- **`device_info`** — `INFO_RESP` TLV decoder (fields `fw_*`, `uptime_ms`, `reset_cause`, `ble_*`, battery/charger, link quality `link_rssi`, `link_peer_rssi`, `link_interval_ms`, `link_latency`, `link_timeout_ms`, `link_phy`, `link_losses`, `link_err_conn`, `link_module_reboots`, `link_secure_retries`, `link_reconnect_ms`; shown on the Bluetooth page, no chart yet) and `InfoPoller` (GET_INFO at node up + every 15 s, UNREACHABLE retry 5 s, UNKNOWN_MSG stops until the next up, down clears the fields).
- **`dispatch`** — `Dispatcher`: ACK/NAK per spec, duplicate suppression per `(source, packetId)` over the last 8 frames per source (a node can have several reliable frames in flight), NAK UNKNOWN_MSG / INVALID_PAYLOAD, song-list reply to the requester. Commands go to a `CommandSink`.
- **`firmware_image`** — validation of `<name>-update.bin` (24-byte trailer: magic, board ID, size, CRCs) + CRC-32.
- **`firmware_upload`** — `POST /api/firmware/<base|remote>`: target → board, `fwimage::check`, refusal texts (ELF, Intel hex, "this is the controller firmware, not the base firmware"), 202/404/409/422; the update itself is started through an injected `Start`.
- **`verse`** — scans a parsed file for verse anchors (legacy helper, tests only).
- **`organ`** — `[ORGAN]` layout (`OrganConfig`: stop count/groups/names, octal ST + `ProgramOffset` or `ProgramN` -> Program Change, channel, all-off SysEx, delays) with Opus 910 defaults, and `buildPreamble` (all-off SysEx + one Program Change per stop as timed `MidiStep`s).
- **`stops_file`** — one `<CODE>.conf` per song next to its `.mid` (`[STOPS]`, `Stops = numbers`; no file = not set, empty = all off; found in any case like the `.mid`; a legacy `stops.conf` is only warned about): lenient `parse`, atomic `save`, `remove` (= not set), mutex-guarded `Store` (owned by `WebServer`), `/api/stops` JSON, `handlePut`, `planFor` (preamble + drop-Program-Change flag per song). `doPlay` reads the song's file itself via `load`; an unreadable file means "not set", never a failed song.
- **`config`** — INI loader and config-path resolution.
- **`status`** — `StatusRegistry` (thread-safe; per-node counters, fields, 200-event rings, 50-entry problems list of WARN/ERROR; injected clock), `nodesForPeer` (BASE→Base, REMOTE→Controller, BROADCAST→both, rest→Base) and the dot rules (`evaluateDot`).
- **`status_json`** — `/api/status` JSON (schema 2: per node `last_fault`, `unseen_faults`) and a UTF-8-safe `jsonEscape`.
- **`http`** — request parsing (8 KiB limit for head+body together (`kMaxRequest`); bodies via Content-Length up to `kMaxBody` = 4 KiB, firmware uploads up to `kMaxFirmwareBody` = 512 KiB), routing (`/`, `/api/status`, `/api/diag`, `GET /api/stops`, `PUT /api/stops/<code>`, 404, 405, 415 for a non-JSON PUT) and response building, no sockets.
- **`web_page`** — the embedded status page (inline CSS/JS, no external requests).
- **`log_line`, `fault_decode`, `diag_codes`, `diag_store`, `diag_api`, `diag_poller`, `log_reader`** — diagnostics (see the section above): line parser, fault accumulation/decoding, DIAG record decoder, JSONL store, `/api/diag` + `diag` text, remote polling state machine, base log ingest + reader thread.
- **`songcheck`** — per-file song checks (parse failure, empty display anchor, no anchors, bad file name) and an incremental `Scanner` (mtime/size).
- **`console`** — terminal command parsing shared by standalone and attached mode, update-path absolutizing, socket reply markers.
- **`log`** — process-wide logger: console (INFO stdout, WARN/ERROR stderr, DEBUG not printed) plus registry events per node.
- **`process`** — child spawn with an empty signal mask and default SIGTERM/SIGINT (the service blocks them in all threads), bounded `terminate` (SIGTERM, then SIGKILL after 2 s).
- **`protocol/names`** — `msgTypeName`, `nakReasonName`, `describePacket` (event text such as `REPLAY_ANCHOR id=8`).

App layer (needs hardware):
- **`serial_port`** — termios wrapper + USB VID:PID/interface discovery via sysfs (compiled into the core library, tested with a fake sysfs tree).
- **`midi_ports`** — RtMidi discovery of "MIDIControl" ports.
- **`player`** — runs a `PlaybackEngine` on its own thread with the wall clock and RtMidi out. Owned by one thread only; `onFinished` just posts a message (never destroys the player on its own thread).
- **`comm`** — `CommHandler`: reader thread (RX, `Dispatcher`, retries with the same PacketId, reconnect when the port disappears), keepalive thread (2 s, base lost after 6 s), and a **control thread** with a command queue that alone creates/stops/destroys the `Player` (organ warm-up waits happen there, never on the reader). Sends NOW_PLAYING_INFO to the base and SIGNAL_PLAYING/ANCHOR/STOPPED to the remote (0x03).
- **`firmware_update`** — `update <base|remote> <name-update.bin>` (or the bytes of an upload); validates the trailer, waits for FW_UPDATE_STATUS, base bootloader on PID 0x1235.
- **`comm` status** — CommHandler also counts TX/RX/retries/gave-up/NAK/CRC/duplicates per peer, keeps link/playback fields, polls `GET_STATUS` every 5 s and `GET_INFO` via `InfoPoller` (routine traffic updates counters only, never events; background polls that time out are logged as debug and don't touch `last_tx_result`) and stamps thread heartbeats (`threadsAlive`). `organ_power` and `_organOn` follow the base's SSR from `STATUS_RESP` unless a `SET_CURRENT` went out after that `GET_STATUS` (`_setCurrentGen`). `sendReliable` retries a NAK BUSY like a timeout (firmware updates no longer abort on it).
- **`web_server`** — status page thread: poll() over ≤ 8 connections, 3 s request deadline, bind retry every 10 s, song folder rescan every 60 s. Never touches CommHandler/Player locks.
- **`console_exec`** — runs terminal commands (local prompt and socket clients) and status-page uploads (`startUpload` on a worker thread, `waitForUpload` before shutdown); one firmware update at a time (one busy flag), progress in Software `fw_update_*` fields.
- **`control_socket`** — Unix socket server (thread per client, ≤ 4) and the thin client (`runClient`).
- **`instance`** — runtime dir and the single-instance lock.
- **`sd_notify`** — `READY=1` / `WATCHDOG=1` / `STOPPING=1` datagrams.
- **`main`** — args/config/headless/`--cmd`; attach-or-run; interactive CLI: `play <code>`, `stop`, `verse` (add verse), `list`, `record`, `on`, `off`, `status`, `update` (refused while a song is playing or while another update runs; the same rule covers `--cmd`, the socket and the web upload, because ENTER_BOOTLOADER reboots the base. Only the web upload also refuses when the base is not connected; the CLI path passes `baseAlive=true` to `updateRefusal`), `quit`.
- **`packaging/midicontrol.service.in`** — systemd user unit (configured at build time).

## Tests

`tests/` uses a tiny built-in harness (`test_framework.hpp`, no external deps). Covers CRC vectors, codec/parser, SysEx, NOW_PLAYING_INFO encoding, both SMF SysEx framings, the SMF reader against the real files in `songs/`, song lookup/list paging, verse sequencing and the playback engine (fake clock), the dispatcher's ACK/NAK/duplicate logic, config (keys, path resolution, `[WEB]`), update-file validation, message names, the status registry and dot rules, JSON escaping/schema, the organ layout and preamble, per-song stop file parse/save/remove/store/PUT, HTTP parsing/routing incl. request bodies, the embedded page (self-contained, home/Advanced views, stops editor), the song-file check, console command parsing, the logger and child-process signal handling, the diagnostics core (port selection, store/cache, log ingest incl. reconnect and the replay window, DIAG polling, `/api/diag`, status schema 2). All deterministic: no real time, no ports. `MC_SONGS_DIR` is injected by CMake.

## Protocol Details

- Frame: `[SOH] [stuffed: length, src, dest, pkt_id, msg_type, payload..., crc8] [EOT]`
- Byte stuffing escapes SOH, EOT, ESC, `$`, `%` (RN4870 BLE module reserved chars)
- Reliable delivery: end-to-end ACK/NAK, 3 retries of the same PacketId at 500 ms; receivers re-ACK duplicates without executing them
- Addresses: PC=0x01, BASE=0x02, REMOTE=0x03, BROADCAST=0xFF
