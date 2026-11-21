# midicontrolpy-sw: MIDIcontrol PC player

The PC part of the MIDIcontrol system. `midicontrol` talks to the MIDIcontrol base (USB serial, VID:PID ABCD:1234) and
plays and records MIDI files, switches the organ power, shows the song on the indicator and keeps the remote
controller informed. It runs as a systemd user service with a status page on port 50118 and a terminal console.
Written in C++17 (the name is historical: the original Python version was removed).

## Build and test

Needs CMake 3.16 or newer, a C++17 compiler, pthreads and, for the app itself, RtMidi (`pkg-config rtmidi`).
The core library and the tests have no external dependencies.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build        # unit tests and the comm tests (fake base on a pty; no hardware, no MIDI ports)
```

## Install and run

```bash
cmake --install build          # binary, share/midicontrol/config.conf.example, lib/systemd/user/midicontrol.service
mkdir -p ~/.config/midicontrol
cp /usr/local/share/midicontrol/config.conf.example ~/.config/midicontrol/config.conf
systemctl --user daemon-reload
systemctl --user enable --now midicontrol.service
loginctl enable-linger $USER   # REQUIRED: start at boot without a login
midicontrol                    # in a terminal: attaches to the running service (same commands; quit detaches)
```

Status page: `http://<this-pc>:50118/`. Full steps, permissions and behaviour: `docs/status-web-and-service.md`.

## Documentation

- [`docs/SYSTEM.md`](docs/SYSTEM.md): the system specification (main copy; the other repos hold copies made by
  `tools/sync-system-doc.sh`; `--check` reports drift, including the copies inside the firmware repos' `lib/protocol`
  and `lib/bootloader` submodules, which the script never writes).
- [`docs/status-web-and-service.md`](docs/status-web-and-service.md): operator guide for the service, status page and
  console, with the design decisions and the bench checklist.
- [`docs/FAULTS.md`](docs/FAULTS.md): known faults.
- [`CLAUDE.md`](CLAUDE.md): architecture, module list and protocol notes for developers.
