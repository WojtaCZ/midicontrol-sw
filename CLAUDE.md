# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MIDIControl PC companion app — a CLI tool that communicates with a MIDIControl hardware device (organ/MIDI controller) over a serial port. It plays and records MIDI files, controls power to the organ, and maintains a keepalive connection with the Base unit.

The app is written in C++ (the original Python implementation was removed; only stale `__pycache__/` remains).

The project language is English (comments, UI strings, documentation).

## Building & running (C++)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/midicontrol            # interactive CLI (waits for the base if it is not plugged in)
./build/midicontrol --headless # service mode (implied when stdin is not a TTY); stops on SIGINT/SIGTERM
./build/midicontrol_tests      # run unit tests (no hardware required)
ctest --test-dir build         # same tests via CTest
cmake --install build          # binary, share/midicontrol/config.conf.example, lib/systemd/user/midicontrol.service
```

Device: USB serial VID:PID=ABCD:1234 (base bootloader: ABCD:1235) plus ALSA MIDI ports named "MIDIControl"; both are re-discovered after a USB reconnect. Uses `arecordmidi` for recording.

Config file: `--config PATH`, else `$MIDICONTROL_CONFIG`, else `$XDG_CONFIG_HOME/midicontrol/config.conf` (`~/.config/...`), else `./config.conf`. Sections: `[FILES]` (PlayFolder/RecordFolder; `~/` and config-relative paths allowed), `[PLAYBACK]` (WarmupMs, KeepPowerOn, VerseGapMs), `[BINDINGS]` (parsed, unused). See `config.conf`.

## Dependencies (C++)

- C++17 compiler, CMake ≥ 3.16
- **RtMidi** (pkg-config `rtmidi`) — MIDI playback/port discovery (only the `midicontrol` app target; core + tests have no external deps)
- POSIX termios (serial) + pthreads
- `arecordmidi` (ALSA utils) for recording

## C++ Architecture

The code is split into a **pure core library** (`midicontrol_core`, no hardware deps, fully unit-tested) and a thin **app layer** (RtMidi + serial). Public headers live in `include/midicontrol/`, sources in `src/`.

Core (testable anywhere):
- **`protocol/`** (`crc8`, `codec`, `parser`, `constants`, `types`) — binary protocol v2: frame format (SOH/EOT, byte stuffing, CRC-8/MAXIM), `Packet`, `MsgType`, NAK reasons, and a byte-by-byte `Parser` state machine. Vendored from `midicontrol-protocol` (the spec is its `PROTOCOL.md`); wire format is byte-identical. The PC copy uses `std::vector`/`size_t` (no fixed buffers).
- **`sysex`** — MIDIControl display SysEx (header `4D 43`, device `01`, targets `0x00`–`0x04`). Per-digit spec (each digit one byte ≤ `0x0F`). `DisplayInfo` keeps the decoded numbers and the digits as authored (`songDigits`/`verseDigits`).
- **`smf`** — Standard MIDI File reader (format 0/1, running status, tempo map → seconds). Accepts both the project files' raw `F0 … F7` SysEx (no length) and standard `F0 <len> … F7`.
- **`display`** — NOW_PLAYING_INFO encoding (per-digit BCD, `0xF` blank digit, `0xFF` no change, `0xFE` blank field, empty = clear), song-code fallback digits, verse-digit placement.
- **`songs`** — song codes (`[0-9A-Z]{1,8}`, sanitised), case-insensitive `<code>.mid` lookup, sorted song list and SONG_LIST_RESP paging.
- **`playback`** — `scanSections` (the file defines the song: every display SysEx with a verse field starts a section; blank verse = prelude), `sectionLabel` ("Predehra"/"Sloka N +k") and `PlaybackEngine` (plays the file; add-verse replays the current section from its anchor once it ends, presses accumulate, the indicator keeps the verse number; clock and outputs injected, so it is tested without time or ports).
- **`dispatch`** — `Dispatcher`: ACK/NAK per spec, duplicate suppression per `(source, packetId)`, NAK UNKNOWN_MSG / INVALID_PAYLOAD, song-list reply to the requester. Commands go to a `CommandSink`.
- **`firmware_image`** — validation of `<name>-update.bin` (24-byte trailer: magic, board ID, size, CRCs) + CRC-32.
- **`verse`** — scans a parsed file for verse anchors (legacy helper, tests only).
- **`config`** — INI loader and config-path resolution.

App layer (needs hardware):
- **`serial_port`** — termios wrapper + USB VID:PID discovery via sysfs.
- **`midi_ports`** — RtMidi discovery of "MIDIControl" ports.
- **`player`** — runs a `PlaybackEngine` on its own thread with the wall clock and RtMidi out. Owned by one thread only; `onFinished` just posts a message (never destroys the player on its own thread).
- **`comm`** — `CommHandler`: reader thread (RX, `Dispatcher`, retries with the same PacketId, reconnect when the port disappears), keepalive thread (2 s, base lost after 6 s), and a **control thread** with a command queue that alone creates/stops/destroys the `Player` (organ warm-up waits happen there, never on the reader). Sends NOW_PLAYING_INFO to the base and SIGNAL_PLAYING/ANCHOR/STOPPED to the remote (0x03).
- **`firmware_update`** — `update <base|remote> <name-update.bin>`; validates the trailer, waits for FW_UPDATE_STATUS, base bootloader on PID 0x1235.
- **`main`** — args/config/headless; interactive CLI: `play <code>`, `stop`, `verse` (add verse), `list`, `record`, `on`, `off`, `status`, `update`, `quit`.
- **`packaging/midicontrol.service.in`** — systemd user unit (configured at build time).

## Tests

`tests/` uses a tiny built-in harness (`test_framework.hpp`, no external deps). Covers CRC vectors, codec/parser, SysEx, NOW_PLAYING_INFO encoding, both SMF SysEx framings, the SMF reader against the real files in `songs/`, song lookup/list paging, verse sequencing and the playback engine (fake clock), the dispatcher's ACK/NAK/duplicate logic, config (keys, path resolution) and update-file validation. All deterministic: no real time, no ports. `MC_SONGS_DIR` is injected by CMake.

## Protocol Details

- Frame: `[SOH] [stuffed: length, src, dest, pkt_id, msg_type, payload..., crc8] [EOT]`
- Byte stuffing escapes SOH, EOT, ESC, `$`, `%` (RN4870 BLE module reserved chars)
- Reliable delivery: end-to-end ACK/NAK, 3 retries of the same PacketId at 500 ms; receivers re-ACK duplicates without executing them
- Addresses: PC=0x01, BASE=0x02, REMOTE=0x03, BROADCAST=0xFF
