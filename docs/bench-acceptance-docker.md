# Bench acceptance: Docker deployment

Run on the real mini PC with the real MIDIControl device, in this order (the first ones are the real risks). Fill the Result column (pass/fail, date, notes). Partly run on 2026-11-21 (see the Result column).

## Acceptance checks

| # | Check | How | Result |
|---|---|---|---|
| 1 | Fresh install | Fresh Ubuntu + Docker (with the Compose plugin). Run the one-line installer. Status page up at `http://<host>:50118/`, device found (Base dot green). | PASS 2026-11-21, partial: owner's Manjaro dev PC (host Docker 29.8), not a fresh Ubuntu. Published `install.sh --tag edge` (image ba221f4, anonymous GHCR pull) with MC_PREFIX under the workspace and MC_WAIT_SECONDS=0. Both containers healthy, Base and Controller green, serial + log CDC + ALSA + remote resync work in the container. |
| 2 | USB replug | Unplug and replug the USB cable. Device recovered without a container restart (`docker ps` uptime unchanged, `reconnects` +1). | PASS 2026-11-22: base back ~15 s after replug, container Up 11 h (no restart); a recording running at the pull was ended and saved by the PC. |
| 3 | Base firmware update | Update the base from the status page. It re-enumerates as `ABCD:1235` and the update completes inside the container; device returns as `ABCD:1234`. | PASS 2026-11-21: `POST /api/firmware/base` (78144 B). ABCD:1234 -> 1235 -> 1234 in ~14 s, `fw_update_state` done, container not restarted. Controller dot briefly yellow (BLE link lost during the base reboot, reconnected and secured). |
| 4 | Recording | `midicontrol --cmd record`, play, `stop`. `arecordmidi` writes a complete file to `/opt/midicontrol/data/recordings` (`/data/recordings`). | Partial 2026-11-21: `--cmd record` / `stop` through the wrapper wrote `/data/recordings/<time>.mid` (292 B, nothing played). Contents with notes: PASS 2026-11-22 (base Nahraj start/stop, notes 60..72 via the Otraki into DIN IN; 4 files, 30..212 note-ons each, all notes present). File is `root:dev` 0644 (deletable over SSH, not editable). |
| 5 | Stall restart (one worker thread) | Stall ONE worker thread while the process keeps running; see the method below. The container restarts within about 45 s (10 s heartbeat age + 5 s watchdog tick + 30 s limit); `docker inspect -f '{{.State.ExitCode}}' midicontrol` or `docker logs midicontrol` shows exit 70 and "a worker thread stalled for 30 s; exiting for a restart". || Not run 2026-11-21: freezing one thread from the dev container (ptrace from a privileged helper container) was refused by the session's permission policy. Needs the owner: host gdb method below, or allow the helper. <br>PASS with a caveat (2026-11-22, owner approved the ptrace helper). Froze the base reader thread (host TID 3578200, blocked in read(fd 4)) for 90 s with a static ptrace SEIZE+INTERRUPT tool (.claude-notes/frz/freeze). Timeline from the freeze at 10:22:13: +4.4 s 'Base is not answering keepalives'; +13 s 'a worker thread stalled; withholding the watchdog ping'; +43 s 'stalled for 30 s; exiting for a restart'. The process only died (docker 'die 70') at +90 s, when the helper detached: a ptrace-stopped thread holds the group exit until its tracer reaps it, so that delay is a test artifact, not product behaviour. Restart +1 s, Startup All Notes Off + organ off, health healthy, `docker exec midicontrol midicontrol --cmd status` answers (no stale socket), base/controller green. Owner 2026-11-22: 43 s is far too long for stuck notes; max 10 s -> midicontrol-sw TODO B13 (also: the playback thread has no heartbeat at all), base fallback fw C13, remote emergency organ-off D14 (later). |
| 6 | Reboot | Reboot the PC without logging in. Both containers start, status page reachable. | STATIC PASS 2026-11-22 (no real reboot, owner away): docker rootful, `docker.service` enabled in host `multi-user.target.wants` (starts at boot, no login; not socket-only), both containers `restart=always`, binds under `/home/vosahlov` on the root ext4 (no crypttab, no homed/ecryptfs, so present without login), `/dev` bind covers a late base enumeration (replug PASS). Startup reset seen on every container start (10.7). Real reboot still open. Note: updater has an anonymous `/data` volume (image VOLUME), harmless. |
| 7 | Update guard and window | Publish a `stable` image while a song plays: nothing restarts. After playback ends, the organ is off and the window opens (or `docker exec updater midicontrol-updater --once`): it updates once. Check `update-status.json`. | |
| 8 | Rollback | Publish a broken image. The health gate fails, the player returns to `:previous`, `update-status.json` shows `rolled_back` and the bad digest is not retried. | |

### Check 5 method (stall one thread, not the process)

The stall watchdog is a thread inside the player (`Watchdog::loop` in `src/main.cpp`, ends in `std::_Exit(70)`). A whole-process `kill -STOP` freezes that thread too, and PID 1 in a container ignores signals sent from inside its own namespace, so `kill -STOP` cannot test it. Instead freeze one other thread (reader, keepalive or control) and let the rest run. gdb is not in the image: run it on the host (needs gdb installed there) against the host-visible PID:

```sh
PID=$(docker top midicontrol -eo pid,comm | awk '/midicontrol/ {print $1; exit}')   # host PID
echo "PID=$PID"; sudo gdb        # then at the (gdb) prompt follow the steps below
```

Attach in non-stop mode so only the chosen thread is stopped: start `sudo gdb`, then `set non-stop on`, `attach $PID`, `info threads` (pick a reader/keepalive/control thread; `ps -T -p $PID` helps), `thread <n>`, `interrupt`. Leave gdb attached. The other threads keep running, so the watchdog thread notices the missing heartbeat and exits after about 45 s. (These gdb steps are a suggestion, not yet tried; if non-stop is not available, any method that blocks a single worker thread, such as `thread apply <n> call (void)pause()`, is equivalent.) Afterwards check the exit code and the restart as in the table.

### Known limitation

`--exit-on-stall` detects a stalled or deadlocked worker thread. It does NOT detect a frozen or SIGSTOPped whole process: the watchdog lives inside the process and stops with it. The image HEALTHCHECK is informational; nothing restarts a frozen process. Spec section 6's `kill -STOP` wording therefore does not apply to this in-process design.

## Open builder decisions (spec section 7), decide on the bench and write the decision here

| Decision | Options | Decision / notes |
|---|---|---|
| Recording file owner | root:dev 0644 (today) vs the host user | Owner 2026-11-22: write as the host user: `PUID`/`PGID` (default 1000:1000) in the compose file, the entrypoint drops to that user (or chowns new files). |
| Device access | `device_cgroup_rules` (`c 166:*`, `c 188:*`, `c 116:*`, as in `docker/compose.template.yaml`) versus `privileged: true`; whether a host udev rule is also needed for stable `/dev/serial/by-id` names. Test with the bench kernel, including after a replug. | Owner 2026-11-22: keep as is: `/dev` bind + `device_cgroup_rules`, no `privileged`, no host udev rule. Replug PASS (bench 2.10/10.4). |
| Lock and socket location | Does the container need `/run` or `XDG_RUNTIME_DIR` set? The player falls back to `/tmp/midicontrol-<uid>/` (lock and control socket). Code finding below. | Owner 2026-11-22: inside the container (`/tmp/midicontrol-<uid>/`); reached with `docker exec midicontrol midicontrol --cmd ...`; no host bind mount, so no stale socket outlives the container. |
| Stopping the player and the organ | Does stopping the container (SIGTERM, `docker stop`, update recreate) ever power the organ off? If yes, the idle guard needs "organ off or not kept on" and the spec needs a user decision. || Observation 2026-11-21: the player logs "Startup: organ power off" on every container start (so also after an update recreate or a stall restart). Shutdown behaviour on SIGTERM not yet checked. Owner 2026-11-22: organ off on start (as today) AND on stop (SIGTERM). Code: `Kind::Shutdown` in src/comm.cpp switches off only when `_organOn`; make it unconditional like the startup reset, then bench-check `docker stop midicontrol`. |

### Finding: stale control socket after `_Exit(70)` (code read, not run)

`src/main.cpp` takes the instance lock (`lock.acquire(runDir + "/midicontrol.lock")`, an `flock`) before the control socket starts. `ControlSocket::start` (`src/control_socket.cpp`) does `unlink(_path)` before `bind` and says so in a comment ("stale file from a previous run, we hold the lock"). The `flock` is released by the kernel when the process dies, including `_Exit(70)`. In the container `/tmp/midicontrol-0` lives in the container's writable layer: it survives a restart (`restart: always` restarts the same container), and is gone on recreate. So a leftover socket or lock file from the previous run does not block the restarted player. A client that connects to the leftover socket before the new player binds gets a connection error. Not a bug as far as the code shows; check it in test 5 (`docker exec midicontrol midicontrol --cmd status` after the restart).

## Not verified in CI or the dev container

- That the Debian `docker-compose` package provides a working `docker compose` plugin inside the image (used by the updater).
- Self-update of the updater through the one-shot helper container (`docker run -d --rm --entrypoint docker ... compose up -d updater`).
- The installer's port-ownership check with real `docker ps` and `ss` output (tests use fakes).
- `curl | sh` with real `sudo` (tests use `MC_PREFIX`/`MC_BINDIR` and no sudo).
- Stale control socket and lock after a `_Exit(70)` restart (see the finding above; read from code only).
- GHCR package visibility (public) and the multi-arch manifest from CI.
