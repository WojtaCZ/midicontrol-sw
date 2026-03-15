# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MIDIControl PC companion app — a Python CLI tool that communicates with a MIDIControl hardware device (organ/MIDI controller) over a serial port. It plays and records MIDI files, controls power to the organ, and maintains a keepalive connection with the Base unit.

The project language is English (comments, UI strings, documentation).

## Running

```bash
python main.py
```

Requires a connected MIDIControl device (USB serial, VID:PID=ABCD:1234) and ALSA MIDI ports. Uses `aplaymidi`/`arecordmidi` for playback/recording. Configuration is in `config.conf` (play/record folder paths, song-to-button bindings).

## Dependencies

- `python-rtmidi` — MIDI port discovery and note-off messages
- `pyserial` — serial communication with the hardware

## Architecture

**`protocol.py`** — Binary protocol implementation mirroring the C++ `lib/protocol/` on the embedded side. Defines frame format (SOH/EOT delimiters, byte stuffing, CRC-8/MAXIM), packet structure (`Packet` dataclass), message types (`MsgType` enum), device addresses (`Addr` enum), and a byte-by-byte `PacketParser` state machine.

**`comm.py`** — `CommHandler` class managing the serial connection. Runs a reader thread (feeds bytes into `PacketParser`), a keepalive thread (pings Base every 2s), and handles ACK/NAK/retry logic. Provides high-level commands: `play_song()`, `stop_song()`, `record_song()`, `organOn()`/`organOff()`. Playback/recording spawn `aplaymidi`/`arecordmidi` subprocesses.

**`midi.py`** — Discovers ALSA MIDI ports matching "MIDIControl" using `rtmidi`. Provides `notes_off()` (all-notes-off on all channels).

**`config.py`** — Loads `config.conf` (INI format) via `configparser`. Exposes globals: `playFolder`, `recFolder`, `bindings`.

**`main.py`** — Interactive CLI loop. Registers a packet callback on `CommHandler`, then accepts commands: `play <song>`, `stop`, `record`, `list`, `on`, `off`, `status`, `quit`.

## Protocol Details

- Frame: `[SOH] [stuffed: length, src, dest, pkt_id, msg_type, payload..., crc8] [EOT]`
- Byte stuffing escapes SOH, EOT, ESC, `$`, `%` (RN4870 BLE module reserved chars)
- Reliable delivery via ACK/NAK with 3 retries at 500ms timeout
- Addresses: PC=0x01, BASE=0x02, REMOTE=0x03, BROADCAST=0xFF
