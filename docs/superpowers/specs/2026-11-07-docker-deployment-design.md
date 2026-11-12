# Docker deployment: one-command install, run as a service, safe auto-update

Date: 2026-11-07. Status: draft for review. Scope: `midicontrolpy-sw` (the PC player). Firmware repos are untouched.

Marking used below: **[user]** = decided by the user in conversation; **[call]** = my choice, open to override.

## 1. Intent

The PC player (`midicontrol`, C++, serves the status page on port 50118) runs on a screenless Linux mini PC next to an organ. Today it is installed by building from source, `cmake --install`, copying a config, enabling a systemd *user* unit and remembering `loginctl enable-linger`. That is too much work and too easy to get wrong.

The goal is that someone with moderate PC skills, logged in over SSH with nothing else, runs **one command** and ends up with the player running, starting at boot, and keeping itself up to date, with no dependency hunting. **[user]**

## 2. Goals

1. One command on a fresh Linux box that has Docker: afterwards the player runs, the status page answers on `http://<pc>:50118/`, the USB serial and ALSA MIDI devices work, and it starts at boot. **[user]**
2. Everything the player needs (RtMidi, ALSA utils for `arecordmidi`, the binary) ships inside the image. The host needs only Docker. **[user]**
3. Pushing a new image automatically updates every box. **[user]**
4. An update never interrupts a playing organ. Updates only happen in a night window (about 02:00 local time) **[user]**, plus an idle guard (section 5) **[call]**.
5. A bad image does not leave a box dead: the previous image comes back. **[call]**
6. Config, songs, stops files, recordings and diagnostics live on the host and survive every update. **[call]**
7. Re-running the installer is safe (it repairs or upgrades). **[call]**

## 3. Non-goals and anti-goals

Non-goals: Windows or macOS hosts **[user: "always Linux, never Windows"]**; the installer installing Docker (the user does that; the installer only checks for it and prints a pointer) **[user]**; a GUI installer; remote fleet management or telemetry; automatic update of the device firmware (still done from the status page, `fw-update.bin` upload, unchanged); Kubernetes or other orchestrators.

Anti-goals (a failure even if everything "works"): an update during playback, recording or a firmware update; losing config, songs or stops on update; needing a second manual step after the one command; a box that stays down after a bad release; an update path that needs credentials on the box.

## 4. Constraints from the existing software (verified in `midicontrolpy-sw`)

- Linux only: ALSA sequencer through RtMidi, `arecordmidi` for recording (`src/comm.cpp`), termios serial.
- Device: USB serial `ABCD:1234` (interface 00 = control, "MIDIControl log" = log CDC) and ALSA MIDI ports named "MIDIControl". In bootloader mode it re-enumerates as `ABCD:1235`; the firmware update path re-opens the port after re-enumeration. Devices are found by sysfs walk (`findMidiControlPort`, `findMidiControlLogPort`), so `/sys/bus/usb` must be visible in the container.
- Serial ports are opened with `TIOCEXCL` plus `flock`; the process also holds an exclusive `flock` on `$XDG_RUNTIME_DIR/midicontrol.lock` (single instance) and a control socket `$XDG_RUNTIME_DIR/midicontrol.sock` used by the terminal client and `--cmd`.
- systemd integration today: `Type=notify`, `READY=1`/`WATCHDOG=1` sent to `$NOTIFY_SOCKET`, watchdog pinged only while the reader, keepalive and control threads all show progress (`src/sd_notify.cpp`). Without `NOTIFY_SOCKET` this is a no-op, so in Docker **nothing restarts a hung player today**.
- `GET /api/status` (schema 2) reports `playback` as `idle`, `playing` or warming up, and the player knows when it is recording or running a firmware update. Config search order: `--config`, `$MIDICONTROL_CONFIG`, `$XDG_CONFIG_HOME/midicontrol/config.conf`, `./config.conf`.
- Port 50118 is the web port (`[WEB] Port`, `Bind=0.0.0.0`).

## 5. Design

### 5.1 Host layout and installer

Everything lives under `/opt/midicontrol` **[call]**:

```
/opt/midicontrol/
  compose.yaml          written by the installer (app + updater)
  .env                  IMAGE_TAG (default "stable"), UPDATE_WINDOW, UPDATE_ENABLED
  data/                 mounted into the app at /data
    config.conf         seeded from the example on first run, never overwritten
    songs/ recordings/ diag/
```

The installer is a POSIX `sh` script, run as `curl -fsSL <url>/install.sh | sh` (it uses `sudo` itself where needed). It:

1. checks: Linux, `docker` installed, daemon reachable, `docker compose` plugin present, architecture is amd64 or arm64, port 50118 free (or already ours). Each failure is one plain-English line with the fix, then exit non-zero. It never installs Docker. **[user]**
2. creates `/opt/midicontrol/data` (if missing), writes `compose.yaml` and `.env` (keeps an existing `.env` and `config.conf`),
3. `docker compose pull && docker compose up -d`,
4. waits until `http://localhost:50118/api/status` answers, then prints the URL, the version, and the three commands the user needs (`midicontrol` console, update-now, uninstall).

Options **[call]**: `--tag <tag>` (pin or channel), `--no-updates`, `--uninstall` (stops and removes containers and compose files, keeps `data/` unless `--purge`). It also installs a small host wrapper `/usr/local/bin/midicontrol` that runs `docker exec -it midicontrol midicontrol "$@"`, so the terminal client and `--cmd "update base fw-update.bin"` work as before.

### 5.2 Image

One multi-arch image (amd64 + arm64) **[call]**, multi-stage: build stage compiles with CMake and runs `ctest`; runtime stage is a slim Debian/Ubuntu base with `librtmidi`, `alsa-utils` and the binary only. The same image also carries the updater script (section 5.5), so there is one image to publish and one tag to pin. Entry point seeds `/data/config.conf` from the bundled example when missing, then runs `midicontrol --headless --config /data/config.conf`.

Published to GHCR as a **public** package, built by GitHub Actions. **[user: all can be public]** Tags **[call]**: `stable` moves on every release tag `vX.Y.Z` (also tagged with the version and `sha-...`); `edge` follows every push to `main` (for a test box). Sites run `stable`.

### 5.3 Running with devices

Compose service `midicontrol` **[call]**:

- `network_mode: host` (port 50118 identical to today, no port mapping, mDNS/LAN behaviour unchanged),
- `restart: always`, `stop_grace_period` long enough for a clean shutdown,
- device access without a host udev rule: runs as root inside the container, with `/dev` bind-mounted (so hot-plugged `ttyACM*` appear) and `device_cgroup_rules` allowing the serial major numbers (`c 166:* rwm` for ttyACM, `c 188:* rwm` for ttyUSB) and ALSA (`c 116:* rwm`, plus `/dev/snd` visible). The ALSA sequencer is kernel-wide, so new MIDI ports from a replugged USB device show up in the container. `/sys` is visible read-only (Docker default).
- fallback if the cgroup-rule approach fails on some kernels: `privileged: true`. The builder decides after testing (section 7).
- `/data` volume (host `/opt/midicontrol/data`), `/etc/localtime` read-only.

### 5.4 Restart on a hung player (replaces the systemd watchdog)

Docker's `restart: always` only reacts to the process exiting; an unhealthy container is not restarted. So add a flag **[call]** (for example `--exit-on-stall`, default on in the image): when `NOTIFY_SOCKET` is unset and the existing heartbeat check (reader, keepalive, control threads) fails for longer than the current 30 s watchdog interval, the player exits non-zero and Docker restarts it. Same semantics as `WatchdogSec=30`, no systemd. The web thread stays out of the check, as today.

### 5.5 Updater (the behaviour that matters most)

A second compose service `updater` (same image, different command, `/var/run/docker.sock` mounted, host timezone) runs a short shell loop **[call]** instead of Watchtower, because the guard and the rollback are custom:

1. Sleep until the update window starts (default 02:00 local, window length 2 h; `UPDATE_WINDOW` in `.env`) **[user: ~2am]**.
2. `docker pull` the configured tag. No new digest: sleep until tomorrow. No network: nothing happens, retry the next night. Retry inside the window every ~15 min.
3. **Idle guard** **[call]**: ask `GET http://127.0.0.1:50118/api/status`. Proceed only if `playback` is `idle`, `recording` is false, no firmware update is running (see section 8 for the new `recording` field), **and the organ is off** (`organ_power` false) **[user: skip while the organ is on]**. Consequence to document: a site with `KeepPowerOn=true` keeps the organ on after the first song, so it only updates when the organ is switched off (or via update-now). If the endpoint is unreachable while the container is running, treat it as busy and skip (never risk a restart during playback); proceed only if the container is not running at all. Anything busy: skip, retry later in the window, otherwise next night. Never restart a player that is playing. **[user: "should never stop a playing organ"]**
4. Retag the current image as `:previous`, recreate the app container with the new image.
5. **Health gate**: the status endpoint must answer and the container must stay up for 60 s. The base device may be unplugged, so "base connected" is not part of the gate. On failure: recreate with `:previous`, log it, keep that digest from being retried until a newer one appears.
6. Log to `docker logs updater`; keep the last result in `data/diag/update-status.json` so the status page can show it later (not in this scope).

Also provided: `midicontrol-update-now` behaviour via the same script (runs steps 2-5 immediately, with the guard) **[call]**. The updater updates itself last (same image, recreated after the app is healthy).

Open point for the builder, see section 7: confirm that stopping the app does not switch the organ power off while the organ is idle but powered.

### 5.6 Repository changes in `midicontrolpy-sw`

- add `Dockerfile`, `.dockerignore`, `docker/` (entrypoint, updater script, compose template), `install.sh`, `.github/workflows/` (build and test, multi-arch push on `main` and on `v*` tags),
- C++: the `--exit-on-stall` flag (and its host unit test via the existing heartbeat code), and a `recording` field in `/api/status` (none exists today; `comm.cpp` only logs "Recording to ..."); nothing else in the player,
- keep `cmake --install` and the systemd user unit for developers, clearly marked as not the supported production path,
- docs: new `docs/deployment.md` (install, update, pin, rollback, uninstall, troubleshooting), update `CLAUDE.md` and `docs/status-web-and-service.md` sections 1 and "single instance" to describe the container, keep the bench checklist.
- the source repo, the image and the installer are all public **[user]**. `install.sh` is served from the raw GitHub URL of this repo. Before the repo is made public, the third-party PDFs in `docs/` (Opus manual, schematics) and anything sensitive in git history must be removed or rewritten; that is the user's action, not part of the build.

## 6. Testing

- CI: `ctest` in the build stage; `shellcheck` on `install.sh` and the updater; the updater's decision logic (window, guard, rollback) unit-tested against a fake `docker` and a fake status endpoint, including: busy, unreachable, no new digest, bad image, no network.
- Bench acceptance on the real mini PC with the real device, in this order, because they are the real risks: (1) fresh Ubuntu + Docker, one command, status page up, device found; (2) unplug and replug the USB cable, device recovered without a container restart; (3) a base firmware update from the status page, which re-enumerates the device as `ABCD:1235`, completes inside the container; (4) recording via `arecordmidi` works and writes to `/data/recordings`; (5) kill -STOP the player, it restarts within the stall limit; (6) reboot the PC, everything starts without anyone logging in; (7) publish a `stable` image while a song is playing, nothing restarts; after playback ends and the window opens, it updates; (8) publish a broken image, it rolls back.

## 7. Left to the builder (decide while implementing, note the decision)

- cgroup rules versus `privileged: true` for device access (test on the bench kernel); whether a host udev rule is also needed for `/dev/serial/by-id` stability.
- Base image and exact package names for `librtmidi`/`alsa-utils`; static vs dynamic link.
- Whether the container needs `/run` or `XDG_RUNTIME_DIR` set for the lock and socket (the player falls back to `/tmp/midicontrol-<uid>/`).
- Whether the app, on SIGTERM, shuts down cleanly and whether it ever powers the organ off on exit. If it does, the idle guard must also require "organ power off or not kept on", and the spec needs a user decision.
- Compose project naming, exact install URL, and log rotation for the container logs.

## 8. Settled after the builder review (all **[call]** unless marked)

- **Recording visibility.** Add a `recording` boolean to `/api/status` (Software node fields). The builder confirms that `playback` stays `playing` for the whole song, including the wait for an add-verse press, and fixes it if not.
- **Updater mechanics.** The updater image contains the Docker CLI and compose plugin. It mounts `/opt/midicontrol` at the same path and runs `docker compose` against it. "Current digest" = the image digest the running app container was created from; "new" = the digest the pulled tag now points to. Before recreating, the running image is retagged `:previous`; rollback is `IMAGE_TAG=previous docker compose up -d`, and the failed digest is remembered in `data/diag/update-status.json` so it is not retried until a newer one appears. The updater is recreated last.
- **Health.** The image defines a Docker `HEALTHCHECK` on `/api/status` (informational: it feeds the updater's guard and `docker ps`; restarts of a hung player come from `--exit-on-stall`).
- **Health gate after an update.** The status endpoint answers and the container stays up 60 s. In addition, if the base device was connected before the update, it must be connected again within the 60 s (needs the base-connected state the status already carries); an unplugged base never blocks the update.
- **Paths and ownership.** The image ships a container example config whose `PlayFolder`, `RecordFolder` and `[DIAG] Dir` are `/data/songs`, `/data/recordings`, `/data/diag`. The player runs as root in the container with `umask 0002`; the installer makes `/opt/midicontrol/data` owned by the installing (SSH) user and group-writable, so songs can be copied in over SSH without sudo. No migration of an old `~/.config/midicontrol`: there are no old installs to preserve. If the installer finds the old systemd user unit running, it stops and disables it and says so, because it holds the serial port and port 50118.
- **Firmware update from the console.** The wrapper mounts `/opt/midicontrol/data` as `/data`; the documented way is `midicontrol --cmd "update base /data/firmware/fw-update.bin"`. The status-page upload is unchanged.
- **CI.** Build both architectures natively (amd64 and arm64 runners, no QEMU); `ctest` runs in the build stage on both.
