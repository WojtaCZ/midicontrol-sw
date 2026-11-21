# Deploying the PC player with Docker

The supported production path is a container on the mini PC: one command installs it, it starts at boot without anyone logging in, and a night-window updater keeps it current. The systemd user unit (`packaging/midicontrol.service.in`) is for developers only.

Image: `ghcr.io/wojtacz/midicontrol-sw` (linux/amd64 and linux/arm64).

## Prerequisites

- Linux host, x86_64 or aarch64, with Docker Engine and the **Docker Compose plugin** (`docker compose version` must work). The installer checks this and says what is missing.
- The user running the installer can use `docker` (docker group) or runs it through `sudo`. System locations (`/opt/midicontrol`, `/usr/local/bin`) are written with `sudo` when you are not root.
- The USB cable to the MIDIControl base. The container uses the host network, `/dev` and the ALSA devices.

## Install

```sh
curl -fsSL https://raw.githubusercontent.com/WojtaCZ/midicontrol-sw/main/install.sh | sh
```

Options (put them after `sh -s --` when piping):

| Option | Effect |
|---|---|
| `--tag TAG` | image tag to run; default `stable` (`edge` follows `main`, `1.2.3` pins a release) |
| `--no-updates` | writes `UPDATE_ENABLED=0` (the updater stays idle) |
| `--uninstall [--purge]` | see [Uninstall](#uninstall) |

Environment overrides: `MC_PREFIX` (install directory, default `/opt/midicontrol`; written into `compose.yaml`, the bind mounts and the updater follow it; only letters, digits and `_ . / + -` are accepted), `MC_BINDIR` (wrapper location, default `/usr/local/bin`), `MC_DOCKER` (docker command), `MC_RAW_URL` (where `compose.template.yaml` and `midicontrol-wrapper.sh` are fetched), `MC_WAIT_SECONDS` (how long to wait for the status page, default 60, 0 = do not wait). If the status page does not answer in time the installer says so and points to `docker logs midicontrol` (exit code 0: the containers are started).

Re-running the installer is safe for your files: an existing `.env` and `data/config.conf` are never overwritten (so `--tag` and `--no-updates` are ignored then; edit `.env`). It does, however, pull and recreate the containers **without the updater's idle guard or health gate**, so run it at night or when the organ is idle. See also the rollback warning below.

### What gets installed

| Path | Content |
|---|---|
| `/opt/midicontrol/compose.yaml` | two services: `midicontrol` (the player) and `updater` (`container_name: updater`, runs `midicontrol-updater`, host network, Docker socket mounted); both `restart: always`; the player has `stop_grace_period: 300s` so `docker stop` does not kill a running firmware update |
| `/opt/midicontrol/.env` | `IMAGE_TAG`, `UPDATE_WINDOW=02:00-04:00`, `UPDATE_ENABLED` |
| `/opt/midicontrol/data/` | `songs/`, `recordings/`, `diag/`, `firmware/`, `config.conf` (seeded from the image on first start); the container sees it as `/data` |
| `/usr/local/bin/midicontrol` | wrapper that runs the console inside the container |

`data/` is group-writable and setgid and owned by the installing user, so files can be copied in over SSH without `sudo`.

The status page is at `http://<host>:50118/`. If the old systemd user unit `midicontrol.service` is running, the installer stops and disables it (it would hold the serial port and port 50118). If something else holds port 50118 the installer stops with a message.

## Songs, recordings, configuration

```sh
cp *.mid /opt/midicontrol/data/songs/        # per-song stop files (<CODE>.conf) are written here too
ls /opt/midicontrol/data/recordings/         # recordings made with "record"
$EDITOR /opt/midicontrol/data/config.conf    # then: docker restart midicontrol
```

## Console

```sh
midicontrol                                          # interactive console (attaches to the running player)
midicontrol --cmd "status"                           # one command, exit code 0 = ok
midicontrol --cmd "update base /data/firmware/fw-update.bin"
```

Paths in commands are paths inside the container: put firmware files in `/opt/midicontrol/data/firmware/` and refer to them as `/data/firmware/...`. Firmware can also be uploaded from the status page.

## Automatic updates

The `updater` container polls the registry only inside the update window (`UPDATE_WINDOW` in `.env`, local time, `HH:MM-HH:MM`, may cross midnight; default `02:00-04:00`). Inside the window it tries every 15 minutes until the attempt ends in updated, nothing new, or rolled back.

For each attempt it:

1. pulls `IMAGE:IMAGE_TAG` and compares the image id with the running one; "nothing new" ends the attempt. An image that already failed the health gate is skipped until a different one is pulled;
2. applies the **idle guard** (below); if busy, nothing is touched and it retries later in the window;
3. tags the running image `:previous`, recreates the player with the new image, and runs a **health gate** (60 s): the container stays up without restarts, `/api/status` answers, and the base reconnects if it was connected before;
4. on success, recreates itself last through a one-shot helper container; on failure, **rolls back** to `:previous` and records the failed image id.

The result of the last attempt is in `/opt/midicontrol/data/diag/update-status.json` (`result`: `updated`, `nothing_new`, `busy`, `pull_failed`, `rolled_back`, `failed`; plus `time`, `tag`, `digest`, `failed_digest`, `detail`).

### Idle guard

An update only starts when `GET /api/status` says all of this: `nodes.software.fields.playback` is `idle`, `recording` is not true, `fw_update_state` is not `running`, and the organ is off (`nodes.base.fields.organ_power` false; if it is unknown, the base must not be connected). An unreachable or unreadable status counts as busy while the app container is running (if it is not running, the updater proceeds).

**`KeepPowerOn=true` consequence:** with `KeepPowerOn = true` in `config.conf` the organ stays switched on between songs, so the guard never sees it off and the nightly update never runs. Switch the organ off (or leave `KeepPowerOn = false`, the container default), or update by hand.

### Update now

```sh
docker exec updater midicontrol-updater --once
```

This runs one attempt immediately and prints `result: <name> (<code>)` (codes: 0 updated, 10 nothing new, 11 busy, 12 pull failed, 13 rolled back; 11 is also returned when another attempt is already running; code 13 with result `failed` means the new image failed the health gate and there was no earlier app container to roll back to). The idle guard still applies. It works with `UPDATE_ENABLED=0` too.

### Switch updates off, pin a version

In `/opt/midicontrol/.env`:

```sh
UPDATE_ENABLED=0       # the updater only logs and idles
IMAGE_TAG=1.2.3        # pin a release (stable follows the newest release, edge follows main)
```

Then `cd /opt/midicontrol && docker compose up -d`. A fresh install can pin directly: `... | sh -s -- --tag 1.2.3`. Note the published image tag has no leading `v` (git tag `v1.2.3` gives `1.2.3`, `stable` and `sha-<short>`), so `--tag v1.2.3` would not find an image.

## Rollback

Automatic: a failed health gate returns to `:previous` (see above). By hand:

```sh
cd /opt/midicontrol
sed -i 's/^IMAGE_TAG=.*/IMAGE_TAG=1.2.2/' .env
docker compose up -d
```

**After a `rolled_back` result:** the app now runs `:previous`, but `IMAGE_TAG` still says `stable`, which resolves to the bad image again. A later `docker compose up -d` or an installer re-run recreates the app from that bad image, with no health gate, and the box can sit in a crash loop until the next release. Before running `compose up` or the installer, pin `IMAGE_TAG` in `.env` to a known good version (for example `IMAGE_TAG=1.2.2`), or wait for the next `stable` release to be pulled by the updater (which skips the known bad image).

Setting `UPDATE_ENABLED=0` only matters while `IMAGE_TAG` stays `stable` or `edge`; a pinned numeric tag such as `1.2.2` already stops forward movement (the updater then only re-pulls that same tag).

## Stall detection

The player started by the container runs with `--exit-on-stall` (env `MIDICONTROL_EXIT_ON_STALL=1` does the same; it is ignored when `NOTIFY_SOCKET` is set, i.e. under systemd, which has its own watchdog). When a worker thread stops making progress the player exits with status **70** and `restart: always` brings it back. Detection takes up to about 45 s after the last good heartbeat.

## Uninstall

```sh
curl -fsSL https://raw.githubusercontent.com/WojtaCZ/midicontrol-sw/main/install.sh | sh -s -- --uninstall          # keeps data/
curl -fsSL https://raw.githubusercontent.com/WojtaCZ/midicontrol-sw/main/install.sh | sh -s -- --uninstall --purge  # also deletes data/
```

This runs `docker compose down` and removes `compose.yaml`, `.env` and the wrapper.

## Troubleshooting

| Symptom | What to do |
|---|---|
| `docker logs midicontrol` | player log (device found/lost, stall exits, status page errors) |
| `docker logs updater` | window, guard decisions ("busy (...)"), pull and rollback messages |
| Device not found | check `lsusb` shows `ABCD:1234`; unplug and replug (the player rediscovers the port by itself); `docker exec midicontrol ls /dev/ttyACM*`. If the kernel assigns other device classes, see the device-access decision in `docs/bench-acceptance-docker.md` |
| Installer: port 50118 in use | something else holds it: `ss -ltnp \| grep 50118`. A running old systemd unit is stopped by the installer automatically |
| Installer: Compose plugin missing | install the Docker Compose plugin (`docker compose version`) and run again |
| Nightly update never happens | `docker logs updater`; check `update-status.json` (`busy` detail), the organ switched on (`KeepPowerOn=true`), `UPDATE_ENABLED`, and `UPDATE_WINDOW` |
| Pull fails | no network or the package is private, see below |

### Notes and known caveats

- **GHCR visibility (one time, by hand):** the first push creates the package as private. In GitHub (package settings, Change visibility) set `midicontrol-sw` to public, otherwise `docker pull` on the mini PC fails. The image carries the `org.opencontainers.image.source` label (set in the `Dockerfile`), which links the package to the repository; check that the package page shows the repository link after the first publish.
- **Image tags** (CI, `.github/workflows/ci.yml`): pull request builds push nothing; push to `main` publishes `edge`; a tag `v1.2.3` publishes `stable`, `1.2.3` and `sha-<short>`.
- **First release:** the installer's default tag is `stable`, which exists only after the first `v*` git tag is pushed (the repository has none yet). Until then install with `--tag edge`.
- **Known limitation:** `--exit-on-stall` detects a stalled or deadlocked worker thread, NOT a frozen or SIGSTOPped whole process (the watchdog runs inside the process). The image HEALTHCHECK is informational; nothing restarts a frozen process. Spec section 6's `kill -STOP` test therefore does not apply to this design.
- If `arecordmidi` dies by itself, `recording` turns false at the next recorder check (the player polls it), so the updater is not blocked forever.
- `recording` and `fw_update_state` are in the status JSON under `nodes.software.fields` (`recording` is the player's recording state, `fw_update_state` is `running`, `done` or `failed`).
- The image installs the Debian `docker-compose` package for the updater's `docker compose` calls; that this provides a working `docker compose` plugin is assumed and still to be verified on the bench.
- Whether stopping the container ever powers the organ off is an open bench question (`docs/bench-acceptance-docker.md`).

### Before publishing the repository

The repository contains third-party PDFs in `docs/` and has history; before it is made public the owner must decide what to remove or rewrite. The installer and CI assume the public repo `WojtaCZ/midicontrol-sw`.
