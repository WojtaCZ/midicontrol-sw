# Bench acceptance: Docker deployment

Run on the real mini PC with the real MIDIControl device, in this order (the first ones are the real risks). Fill the Result column (pass/fail, date, notes). Nothing here has been run yet.

## Acceptance checks

| # | Check | How | Result |
|---|---|---|---|
| 1 | Fresh install | Fresh Ubuntu + Docker (with the Compose plugin). Run the one-line installer. Status page up at `http://<host>:50118/`, device found (Base dot green). | |
| 2 | USB replug | Unplug and replug the USB cable. Device recovered without a container restart (`docker ps` uptime unchanged, `reconnects` +1). | |
| 3 | Base firmware update | Update the base from the status page. It re-enumerates as `ABCD:1235` and the update completes inside the container; device returns as `ABCD:1234`. | |
| 4 | Recording | `midicontrol --cmd record`, play, `stop`. `arecordmidi` writes a complete file to `/opt/midicontrol/data/recordings` (`/data/recordings`). | |
| 5 | Stall restart | `docker exec midicontrol sh -c 'kill -STOP 1'` (or the player pid). The container restarts within the stall limit (about 45 s plus restart); `docker logs midicontrol` shows the stall and exit 70. | |
| 6 | Reboot | Reboot the PC without logging in. Both containers start, status page reachable. | |
| 7 | Update guard and window | Publish a `stable` image while a song plays: nothing restarts. After playback ends, the organ is off and the window opens (or `docker exec updater midicontrol-updater --once`): it updates once. Check `update-status.json`. | |
| 8 | Rollback | Publish a broken image. The health gate fails, the player returns to `:previous`, `update-status.json` shows `rolled_back` and the bad digest is not retried. | |

## Open builder decisions (spec section 7), decide on the bench and write the decision here

| Decision | Options | Decision / notes |
|---|---|---|
| Device access | `device_cgroup_rules` (`c 166:*`, `c 188:*`, `c 116:*`, as in `docker/compose.template.yaml`) versus `privileged: true`; whether a host udev rule is also needed for stable `/dev/serial/by-id` names. Test with the bench kernel, including after a replug. | |
| Lock and socket location | Does the container need `/run` or `XDG_RUNTIME_DIR` set? The player falls back to `/tmp/midicontrol-<uid>/` (lock and control socket). Code finding below. | |
| Stopping the player and the organ | Does stopping the container (SIGTERM, `docker stop`, update recreate) ever power the organ off? If yes, the idle guard needs "organ off or not kept on" and the spec needs a user decision. | |

### Finding: stale control socket after `_Exit(70)` (code read, not run)

`src/main.cpp` takes the instance lock (`lock.acquire(runDir + "/midicontrol.lock")`, an `flock`) before the control socket starts. `ControlSocket::start` (`src/control_socket.cpp`) does `unlink(_path)` before `bind` and says so in a comment ("stale file from a previous run, we hold the lock"). The `flock` is released by the kernel when the process dies, including `_Exit(70)`. In the container `/tmp/midicontrol-0` lives in the container's writable layer: it survives a restart (`restart: always` restarts the same container), and is gone on recreate. So a leftover socket or lock file from the previous run does not block the restarted player. A client that connects to the leftover socket before the new player binds gets a connection error. Not a bug as far as the code shows; check it in test 5 (`docker exec midicontrol midicontrol --cmd status` after the restart).

## Not verified in CI or the dev container

- The image build was never run locally (no Docker in the dev container); only CI will run it. The Dockerfile's `ctest` step is untested.
- That the Debian `docker-compose` package provides a working `docker compose` plugin inside the image (used by the updater).
- Self-update of the updater through the one-shot helper container (`docker run -d --rm --entrypoint docker ... compose up -d updater`).
- The installer's port-ownership check with real `docker ps` and `ss` output (tests use fakes).
- `curl | sh` with real `sudo` (tests use `MC_PREFIX`/`MC_BINDIR` and no sudo).
- Stale control socket and lock after a `_Exit(70)` restart (see the finding above; read from code only).
- GHCR package visibility (public) and the multi-arch manifest from CI.
