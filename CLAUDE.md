# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MIDIControl PC companion app — a CLI tool that communicates with a MIDIControl hardware device (organ/MIDI controller) over a serial port. It plays and records MIDI files, controls power to the organ, and maintains a keepalive connection with the Base unit.

The app has been **rewritten in C++** (the original Python implementation is kept in the repo root — `main.py`, `comm.py`, etc. — as a reference). The C++ build is the primary one.

The project language is English (comments, UI strings, documentation).

## Building & running (C++)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/midicontrol            # interactive CLI (needs hardware)
./build/midicontrol_tests      # run unit tests (no hardware required)
ctest --test-dir build         # same tests via CTest
```

Requires a connected MIDIControl device (USB serial, VID:PID=ABCD:1234) and ALSA MIDI ports. Uses `arecordmidi` for recording. Configuration is in `config.conf` (play/record folder paths, song-to-button bindings).

## Dependencies (C++)

- C++17 compiler, CMake ≥ 3.16
- **RtMidi** (pkg-config `rtmidi`) — MIDI playback/port discovery (only the `midicontrol` app target; core + tests have no external deps)
- POSIX termios (serial) + pthreads
- `arecordmidi` (ALSA utils) for recording

## C++ Architecture

The code is split into a **pure core library** (`midicontrol_core`, no hardware deps, fully unit-tested) and a thin **app layer** (RtMidi + serial). Public headers live in `include/midicontrol/`, sources in `src/`.

Core (testable anywhere):
- **`protocol/`** (`crc8`, `codec`, `parser`, `constants`, `types`) — binary protocol: frame format (SOH/EOT, byte stuffing, CRC-8/MAXIM), `Packet`, `MsgType`, and a byte-by-byte `Parser` state machine. Vendored faithfully from the firmware `lib/protocol/`; wire format is byte-identical.
- **`sysex`** — MIDIControl display SysEx (header `4D 43`, device `01`, targets `0x00`–`0x04`). **Current per-digit spec only** (each digit one byte ≤ `0x0F`; 7-bit safe). `DisplayInfo` parse/build.
- **`smf`** — Standard MIDI File reader (format 0/1, running status, tempo map → seconds). NOTE: the project's `.mid` files embed SysEx as a raw `F0 … F7` run *without* the SMF length prefix, so the reader scans to the terminating `F7`.
- **`verse`** — scans a parsed file for verse anchors (for verse repeat).
- **`config`** — INI loader (`[FILES]`, `[BINDINGS]`).

App layer (needs hardware):
- **`serial_port`** — termios wrapper + USB VID:PID discovery via sysfs.
- **`midi_ports`** — RtMidi discovery of "MIDIControl" ports.
- **`player`** — RtMidi playback with inline SysEx interception (SysEx → `onDisplayInfo` callback, never sent to MIDI out) and verse repeat. Port of `player.py`.
- **`comm`** — `CommHandler`: reader thread, keepalive thread (2 s), ACK/NAK retry, high-level commands. Port of `comm.py`.
- **`main`** — interactive CLI: `play <song>`, `stop`, `record`, `list`, `on`, `off`, `repeat`, `repeatmode`, `status`, `quit`.

## Tests

`tests/` uses a tiny built-in harness (`test_framework.hpp`, no external deps). Covers CRC vectors, codec round-trip + byte-stuffing + the PROTOCOL.md PLAY_SONG golden frame, the parser state machine, SysEx encode/decode (spec examples + partial targets), the config loader, and the SMF reader against the real files in `songs/` (song numbers match filenames; `523.mid`'s empty SysEx is handled). `MC_SONGS_DIR` is injected by CMake.

## Protocol Details

- Frame: `[SOH] [stuffed: length, src, dest, pkt_id, msg_type, payload..., crc8] [EOT]`
- Byte stuffing escapes SOH, EOT, ESC, `$`, `%` (RN4870 BLE module reserved chars)
- Reliable delivery via ACK/NAK with 3 retries at 500ms timeout
- Addresses: PC=0x01, BASE=0x02, REMOTE=0x03, BROADCAST=0xFF
