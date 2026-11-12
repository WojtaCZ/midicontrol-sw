# Docker Deployment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the PC player as a public multi-arch Docker image with a one-command installer, restart-on-hang, and a night-time auto-updater that never interrupts a playing organ.

**Architecture:** One image (`ghcr.io/wojtacz/midicontrol-sw-py`) contains the player and a POSIX-sh updater. `install.sh` writes `/opt/midicontrol/{compose.yaml,.env,data/}` and starts a compose project with two services (`midicontrol`, `updater`). Two small C++ changes support it (`--exit-on-stall`, a `recording` status field); everything else is shell, Docker and CI.

**Tech Stack:** C++17/CMake (existing), POSIX sh + `jq`, Docker + compose plugin, GitHub Actions, GHCR.

**Spec:** `docs/superpowers/specs/2026-11-07-docker-deployment-design.md` (sections 5 and 8 are binding)

## Global Constraints

- Hosts are Linux only; the installer never installs Docker, it only checks for it and prints a pointer.
- Image `ghcr.io/wojtacz/midicontrol-sw-py`, public, amd64 + arm64 built natively (no QEMU); tags: `stable` (moves on `vX.Y.Z` tags, also tagged with the version and `sha-<short>`), `edge` (every push to `main`).
- Host layout `/opt/midicontrol`: `compose.yaml`, `.env` (`IMAGE_TAG` default `stable`, `UPDATE_WINDOW` default `02:00-04:00`, `UPDATE_ENABLED` default `1`), `data/` (`config.conf`, `songs/`, `recordings/`, `diag/`, `firmware/`). Never overwrite an existing `.env` or `data/config.conf`.
- App service: `container_name: midicontrol`, `network_mode: host`, `restart: always`, runs as root with `umask 0002`, `/dev` bind-mounted, `device_cgroup_rules` `c 166:* rwm`, `c 188:* rwm`, `c 116:* rwm`, `/data` volume, `/etc/localtime` read-only. Fallback `privileged: true` only if the bench shows the rules are insufficient.
- Updater: runs only inside the window (default 02:00 local, 2 h), retries every 15 min inside it, proceeds only if `playback` is `idle`, `recording` is false, no firmware update running, and the organ is off; unreachable status endpoint while the container runs = busy = skip. Health gate 60 s (status answers, container stays up, base reconnects if it was connected before). Rollback via `:previous`.
- `--exit-on-stall` applies the existing watchdog semantics (reader, keepalive, control, log-reader heartbeats; web thread excluded) with the 30 s limit; active only when `NOTIFY_SOCKET` is unset.
- Port 50118 and the `/api/status` JSON schema 2 stay unchanged apart from the new `recording` field.
- C++ comments, identifiers and names are English (repo rule); UI strings stay as they are.
- Do not touch the firmware repos. Do not push, change repo visibility, or remove the third-party PDFs: those are the user's actions.

## Review Focus

1. **Organ state unknown** (`organ_power` absent while the base is unplugged or just lost): expected = base not connected counts as "organ off" (nothing can play), base connected but power unknown counts as busy. Pinned in Task 4.
2. **Window edge cases**: a window crossing midnight (`23:00-01:00`), and DST days where 02:00 does not exist or happens twice: expected = still exactly one update attempt per night, never a busy loop. Pinned in Task 4.
3. **Stall monitor false positive at startup**: heartbeats are not stamped yet when the process starts: expected = no exit during the first limit interval. Pinned in Task 1.
4. **Installer re-run** on a box with a customised `config.conf`/`.env`, and an old systemd user unit still holding port 50118: expected = files untouched, old unit stopped and disabled with a message. Pinned in Task 5.
5. **Pull fails or registry unreachable mid-update** (no network at 02:00): expected = running container untouched, retry next slot. Pinned in Task 4.

---

### Task 1: `StallMonitor` and `--exit-on-stall`

**Files:**
- Create: `include/midicontrol/stall_monitor.hpp`, `tests/test_stall_monitor.cpp`
- Modify: `CMakeLists.txt` (add the test to `midicontrol_tests`), `src/main.cpp` (Watchdog loop at ~79-114, `kUsage`, argument parsing)

**Interfaces:**
- Produces: `mc::StallMonitor` (header-only):
  `explicit StallMonitor(std::chrono::milliseconds limit)`;
  `bool shouldExit(bool alive, std::chrono::steady_clock::time_point now)` returns true once `alive` has been false continuously for `>= limit`; the first call starts the clock, so a monitor that has never seen `alive == true` still waits a full `limit` before returning true; any `alive == true` resets it.
- CLI: `--exit-on-stall` flag (also enabled by env `MIDICONTROL_EXIT_ON_STALL=1`).

- [ ] **Step 1: Write failing tests** in `tests/test_stall_monitor.cpp` (existing harness, see `tests/test_framework.hpp`): `stall_never_exits_while_alive`; `stall_exits_after_limit_of_not_alive` (limit 30 s, `false` at t=0 and t=29.9 s → false, at t=30 s → true); `stall_alive_resets_clock` (false until 20 s, true at 21 s, false at 22 s, still false at 51 s, true at 52 s); `stall_startup_grace` (first call `false` at t=0 returns false, at t=29 s still false) — Review Focus 3.
- [ ] **Step 2: Run** `cmake --build build-scratch -j && ./build-scratch/midicontrol_tests` (use a scratch build dir, the repo's `build/` is stale). Expected: compile failure, header missing.
- [ ] **Step 3: Implement** `stall_monitor.hpp` as specified.
- [ ] **Step 4: Run tests.** Expected: all four pass.
- [ ] **Step 5: Wire into `main.cpp`:** `Watchdog` gets a `bool exitOnStall` constructor argument; in `loop()`, when the heartbeat check fails call `monitor.shouldExit(false, now)` and, if true and `exitOnStall`, log an error and `std::_Exit(70)`; reset on success. Flag is on only when `NOTIFY_SOCKET` is unset. Add the flag to `kUsage` and to argument parsing; document it in `kUsage`.
- [ ] **Step 6: Build the app, run `./build-scratch/midicontrol --headless --exit-on-stall --config config.conf` for 10 s, send SIGTERM.** Expected: starts, status page answers on 50118, exits 0.
- [ ] **Step 7: Commit** `feat: exit-on-stall for container restarts`.

### Task 2: `recording` status field

**Files:**
- Modify: `src/comm.cpp` (`startRecorder` ~905-920, `stopRecorder` ~923-932, constructor init ~119), `tests/test_status_json.cpp`

**Interfaces:**
- Produces: `/api/status` → `nodes.software.fields.recording` boolean, `true` from a successful `arecordmidi` spawn until the recorder is stopped, else `false` (initialised `false` next to `playback`).

- [ ] **Step 1: Write failing test** in `tests/test_status_json.cpp`: `status_json_renders_recording_bool` sets `recording` true on `Node::Software` in a `StatusRegistry`, renders, asserts the output contains `"recording":true`, then false → `"recording":false`.
- [ ] **Step 2: Run tests.** Expected: pass if the registry already renders bools generically (it does, `status_json.cpp:41`); if so this test pins the contract and Step 3 is the only change.
- [ ] **Step 3: Implement:** `_status.set(Node::Software, "recording", false)` at init; `true` after a successful spawn (pid > 0); `false` in `stopRecorder()` after clearing `_recorderPid` (also on spawn failure).
- [ ] **Step 4: Verify playback stays `playing` for the whole song** including the wait for an add-verse press: read `src/comm.cpp` around 1127-1147 and the `PlaybackEngine` finish path, and record the finding in the commit message. If `playback` flips to `idle` during that wait, set it only in `playbackEnded` instead and add a test in `tests/test_playback.cpp` for the engine's "waiting for verse" state if one exists.
- [ ] **Step 5: Commit** `feat: report recording state in /api/status`.

### Task 3: Container config, entrypoint, Dockerfile, compose template

**Files:**
- Create: `docker/config.container.conf`, `docker/entrypoint.sh`, `docker/compose.template.yaml`, `Dockerfile`, `.dockerignore`, `docker/tests/test_entrypoint.sh`

**Interfaces:**
- Produces: image entrypoint `/usr/local/bin/entrypoint.sh` (`exec`s `midicontrol --headless --exit-on-stall --config /data/config.conf` by default; any other args replace the default command, so `midicontrol-updater` works as the updater command); `compose.template.yaml` with `${IMAGE}`/`${IMAGE_TAG}` substitution resolved by compose from `.env`; `HEALTHCHECK` = `curl -fs http://127.0.0.1:50118/api/status`.

- [ ] **Step 1: Write failing test** `docker/tests/test_entrypoint.sh` (plain sh, `MC_DATA_DIR`/`MC_BIN` env overrides so it runs on the host): `seeds_config_when_missing` (empty temp data dir → `config.conf` exists, contains `PlayFolder = /data/songs`, `RecordFolder = /data/recordings`, `Dir = /data/diag`, directories `songs recordings diag firmware` created); `keeps_existing_config` (pre-existing `config.conf` with a marker line is unchanged); `passes_through_other_command` (args `echo hi` prints `hi`).
- [ ] **Step 2: Run** `sh docker/tests/test_entrypoint.sh`. Expected: FAIL (script missing).
- [ ] **Step 3: Implement** `config.container.conf` from the repo's `config.conf` with the three paths above and `[WEB] Bind=0.0.0.0, Port=50118`; `entrypoint.sh` does `umask 0002`, seeding, then `exec`.
- [ ] **Step 4: Run test.** Expected: PASS.
- [ ] **Step 5: Dockerfile** (multi-stage): build stage = Debian stable + `build-essential cmake pkg-config librtmidi-dev libasound2-dev`, `cmake -DCMAKE_BUILD_TYPE=Release`, build, `ctest`; runtime stage = Debian stable-slim + `librtmidi7` (check the package name on the chosen base), `alsa-utils`, `curl`, `jq`, `docker-cli`, `docker-compose` plugin (from the base repo or Docker's apt repo), copies the binary, `config.container.conf`, `entrypoint.sh`, and the updater (Task 4). `.dockerignore` excludes `build/`, `.git`, `docs/*.pdf`, `.trail`, `.claude*`, `graft`.
- [ ] **Step 6: `compose.template.yaml`** per the Global Constraints (two services; updater: same image, `command: ["midicontrol-updater"]`, `/var/run/docker.sock`, `/opt/midicontrol:/opt/midicontrol`, `/etc/localtime:ro`, `env_file: .env`, `working_dir: /opt/midicontrol`, `COMPOSE_PROJECT_NAME=midicontrol`).
- [ ] **Step 7: Build and smoke** if `docker` exists in this dev container: `docker build -t mc-test .` then `docker run --rm mc-test midicontrol --help`. Expected: usage text incl. `--exit-on-stall`. If there is no Docker here, skip; Task 6 builds it in CI and Task 7 covers it on the bench.
- [ ] **Step 8: Commit** `feat: container image, entrypoint and compose template`.

### Task 4: Updater (window, guard, update, rollback)

**Files:**
- Create: `docker/updater/lib.sh` (functions, sourceable), `docker/updater/updater.sh` (installed as `/usr/local/bin/midicontrol-updater`; loop plus `--once` for update-now), `docker/tests/test_updater.sh`, `docker/tests/fakes/docker`, `docker/tests/fakes/curl`

**Interfaces** (all in `lib.sh`, POSIX sh, GNU `date`, `jq`):
- `window_open START_HHMM LEN_MIN NOW_EPOCH` → exit 0 when `NOW_EPOCH` (local time) is inside today's or yesterday's window starting at `START_HHMM` for `LEN_MIN` minutes (handles crossing midnight).
- `seconds_until_window START_HHMM NOW_EPOCH` → prints seconds until the next window start, always `>= 1`, recomputed from wall time on every call (DST-safe).
- `guard_decision STATUS_JSON` → prints `proceed` or `busy:<reason>`. Proceed only if `.nodes.software.fields.playback == "idle"`, `.recording != true`, `.fw_update_state != "running"`, and the organ is off: `.nodes.base.fields.organ_power == false`, or (power unknown/absent **and** `.nodes.base.fields.connected != true`). Otherwise busy.
- `update_once` → returns 0 updated / 10 nothing new / 11 busy / 12 pull failed / 13 rolled back. Steps per spec 5.5: record the app container's current image digest; `docker pull "$IMAGE:$IMAGE_TAG"`; if the digest equals the current one return 10; fetch status (`curl -s --max-time 5 http://127.0.0.1:50118/api/status`); if unreachable and the container is running return 11, else `guard_decision`; retag the running image `:previous`; `docker compose up -d midicontrol`; health gate 60 s; on failure `IMAGE_TAG=previous docker compose up -d midicontrol`, write the failed digest to `/data/diag/update-status.json` (the updater mounts `/opt/midicontrol/data` via compose, add that volume), return 13. Updates itself last (`docker compose up -d updater`).
- Env: `IMAGE`, `IMAGE_TAG`, `UPDATE_WINDOW` (`HH:MM-HH:MM` → start and length), `UPDATE_ENABLED`.

- [ ] **Step 1: Write failing tests** in `docker/tests/test_updater.sh` (plain sh; fakes on `PATH` record calls and replay canned outputs from files): `window_inside_and_outside` (02:00+120 at 02:30 yes, 04:01 no); `window_crosses_midnight` (23:00+120 at 00:30 yes, 01:01 no) — Review Focus 2; `seconds_until_window_positive_on_dst_gap` (use `TZ=Europe/Prague` and the epoch of 2026-03-29 01:59 local, assert `>= 1` and that adding the result lands at a time where `window_open` is true) ; `guard_proceeds_when_idle_and_organ_off`; `guard_busy_when_playing`; `guard_busy_when_recording`; `guard_busy_when_fw_update_running`; `guard_busy_when_organ_on`; `guard_proceeds_when_organ_unknown_and_base_disconnected`; `guard_busy_when_organ_unknown_and_base_connected` — Review Focus 1; `update_nothing_new_returns_10_without_recreate`; `update_pull_failure_returns_12_and_does_not_touch_app` — Review Focus 5; `update_busy_returns_11_and_no_recreate`; `update_unreachable_status_with_running_container_is_busy`; `update_bad_image_rolls_back` (fake docker: health gate never answers → `IMAGE_TAG=previous` compose call seen, return 13, failed digest recorded, not retried until the digest changes); `update_requires_base_reconnect_if_it_was_connected` (status connected before; after-status never connected within the gate → rollback); `update_success_retags_previous_and_recreates`.
- [ ] **Step 2: Run** `sh docker/tests/test_updater.sh`. Expected: FAIL (lib missing).
- [ ] **Step 3: Implement `lib.sh`** per the interfaces. Read time only through `date -d "@$EPOCH"`, never cache across sleeps.
- [ ] **Step 4: Run tests.** Expected: all PASS.
- [ ] **Step 5: Implement `updater.sh`:** if `UPDATE_ENABLED != 1` sleep forever; loop: sleep `seconds_until_window`, then inside the window call `update_once` every 900 s until it returns 0, 10 or 13 or the window closes; `--once` runs `update_once` immediately (the guard still applies) and prints the result. Log every decision to stdout.
- [ ] **Step 6: Run `shellcheck docker/updater/*.sh docker/tests/*.sh docker/entrypoint.sh`** (install via `sudo pacman -S --needed shellcheck` if missing). Expected: no findings.
- [ ] **Step 7: Commit** `feat: night-window updater with idle guard and rollback`.

### Task 5: `install.sh` and host wrapper

**Files:**
- Create: `install.sh`, `docker/midicontrol-wrapper.sh`, `docker/tests/test_install.sh`

**Interfaces:**
- `install.sh [--tag TAG] [--no-updates] [--uninstall [--purge]]`, env overrides `MC_PREFIX` (default `/opt/midicontrol`), `MC_DOCKER` (default `docker`), `MC_RAW_URL` (where to fetch `compose.template.yaml` and the wrapper; default the raw GitHub URL of `WojtaCZ/midicontrol-sw-py` `main`). Exit non-zero with one plain line per failed check: not Linux, no `docker`, daemon unreachable, no compose plugin, unsupported arch (not x86_64/aarch64), port 50118 in use by something other than the `midicontrol` container.
- Wrapper installed at `/usr/local/bin/midicontrol`: `docker exec -it midicontrol midicontrol "$@"` (no `-t` when stdin is not a TTY); documented `--cmd` firmware usage is `midicontrol --cmd "update base /data/firmware/fw-update.bin"`.

- [ ] **Step 1: Write failing tests** in `docker/tests/test_install.sh` (fake `docker` on PATH, `MC_PREFIX` in a temp dir, `MC_RAW_URL` pointing at the local `docker/` dir via `file://`): `fails_without_docker` (message mentions Docker's install docs, exit 1); `fresh_install_writes_files` (`compose.yaml`, `.env` with `IMAGE_TAG=stable`, `UPDATE_WINDOW=02:00-04:00`, `UPDATE_ENABLED=1`, `data/` dirs, fake docker saw `compose pull` then `compose up -d`); `rerun_keeps_customised_env_and_config` (marker lines in `.env` and `data/config.conf` survive; `compose.yaml` refreshed) — Review Focus 4; `old_systemd_unit_is_stopped` (fake `systemctl --user` reports `midicontrol.service` active → test asserts `stop` and `disable` were called and a message printed) — Review Focus 4; `no_updates_flag_sets_enabled_0`; `tag_flag_sets_image_tag`; `uninstall_keeps_data_without_purge`.
- [ ] **Step 2: Run** `sh docker/tests/test_install.sh`. Expected: FAIL.
- [ ] **Step 3: Implement `install.sh`** (POSIX sh, `set -eu`): checks, `sudo` only for creating `/opt/midicontrol` and `/usr/local/bin`, `data/` chowned to the invoking user (`SUDO_USER` or `id -un`) with mode 2775, write `compose.yaml` (from the template) and a new `.env` only if absent, `docker compose pull && up -d`, wait up to 60 s for `/api/status`, print the URL, the version and the three commands (console, `docker exec updater midicontrol-updater --once`, uninstall).
- [ ] **Step 4: Run tests and `shellcheck install.sh docker/midicontrol-wrapper.sh`.** Expected: PASS, no findings.
- [ ] **Step 5: Commit** `feat: one-command installer and host wrapper`.

### Task 6: CI and publishing

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Workflow: on push to `main` and on tags `v*`, and on pull requests (no push). Jobs: `shell` (shellcheck + the three `docker/tests/*.sh`), `build` matrix `ubuntu-24.04` / `ubuntu-24.04-arm` (`docker buildx build --load`, the Dockerfile's `ctest` runs inside), `publish` (needs both; logs in to GHCR with `GITHUB_TOKEN`, builds both arches natively and merges them into one manifest with the tags from the Global Constraints; only on `main` (`edge`) and `v*` (`stable`, version, `sha-`)). Package visibility must be public (a one-time setting in GitHub, noted in `docs/deployment.md`).

- [ ] **Step 1: Write the workflow** per the interfaces; keep each job under 30 lines.
- [ ] **Step 2: Validate** with `actionlint` if available (`yay`/`pacman`), otherwise `python3 -c 'import yaml,sys;yaml.safe_load(open(".github/workflows/ci.yml"))'`. Expected: no errors.
- [ ] **Step 3: Commit** `ci: test, build natively for amd64/arm64 and publish to GHCR`.

### Task 7: Docs, repo cleanup, bench acceptance

**Files:**
- Create: `docs/deployment.md`, `docs/bench-acceptance-docker.md`
- Modify: `CLAUDE.md` (Service section and Dependencies), `docs/status-web-and-service.md` (section 1 and single-instance), `packaging/midicontrol.service.in` (header comment: developer use only), `CMakeLists.txt` comment at the install block

- [ ] **Step 1: Write `docs/deployment.md`:** prerequisites (Docker + compose plugin), the install one-liner (`curl -fsSL https://raw.githubusercontent.com/WojtaCZ/midicontrol-sw-py/main/install.sh | sh`), what gets installed, copying songs (`/opt/midicontrol/data/songs`), console and `--cmd` usage, update window and guard (incl. the `KeepPowerOn=true` consequence), update-now, pin to a version (`--tag v1.2.3`), rollback, uninstall, troubleshooting (device not found, port busy, `docker logs midicontrol`, `docker logs updater`), and the one-time GHCR "public" setting.
- [ ] **Step 2: Update `CLAUDE.md` and `docs/status-web-and-service.md`** to describe the container as the supported production path, the new flag and the `recording` field; mark the systemd user unit as developer-only. Keep the existing facts that stay true.
- [ ] **Step 3: Write `docs/bench-acceptance-docker.md`** as a checklist with an empty result column for the eight bench checks of spec section 6 (fresh install; USB replug; base firmware update across `ABCD:1235` inside the container; `arecordmidi` writes to `/data/recordings`; `kill -STOP` restart within the stall limit; reboot without login; `stable` pushed during a song does not restart, updates once idle and the organ is off; broken image rolls back), plus the three open builder decisions of spec section 7 (cgroup rules vs `privileged`, `/run`/`XDG_RUNTIME_DIR` lock path, whether stopping the player ever powers the organ off). Leave the results to be filled on the real mini PC.
- [ ] **Step 4: Run the whole local check:** `cmake --build build-scratch -j && ctest --test-dir build-scratch --output-on-failure && for t in docker/tests/test_*.sh; do sh $t || exit 1; done`. Expected: all pass.
- [ ] **Step 5: Commit** `docs: Docker deployment guide, bench checklist, CLAUDE.md update`.

---

## Self-review

- **Spec coverage:** installer and host layout (5.1) → Task 5; image, tags, multi-arch (5.2) → Tasks 3, 6; device access (5.3) → Task 3 compose, verified in Task 7 bench; exit-on-stall (5.4) → Task 1; updater window, guard, health gate, rollback, update-now (5.5, 8) → Task 4; recording field (5.6, 8) → Task 2; paths/ownership/migration (8) → Tasks 3, 5; CI (8) → Task 6; docs and systemd demotion (5.6) → Task 7; testing section 6 → Tasks 1-5 (automated) and 7 (bench). Spec section 7 items are explicitly deferred to Task 7's bench checklist.
- **Types:** `StallMonitor::shouldExit`, `window_open`, `seconds_until_window`, `guard_decision`, `update_once` and their return codes are used identically across tasks.
- **Proportion:** the plan is about twice the spec's length and contains signatures, test names and values, no function bodies.
