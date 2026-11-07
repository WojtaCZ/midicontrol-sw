#!/bin/sh
# Tests for docker/updater/lib.sh (window, guard, update, rollback, loop).
# Runs on the host: fake docker/curl on PATH, no real sleeping, no network.
here=$(cd "$(dirname "$0")" && pwd)
MC_LIB_DIR="$here/../updater"
export MC_LIB_DIR
PATH="$here/fakes:$PATH"
export PATH
fail=0
check() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# Run lib functions in a subshell so state never leaks between tests.
# shellcheck source=docker/updater/lib.sh
lib() { ( . "$MC_LIB_DIR/lib.sh" && "$@" ); }

# All local-time logic is tested in Europe/Prague (has DST).
TZ=Europe/Prague
export TZ
# Epoch of a local time in Europe/Prague.
prague() { TZ=Europe/Prague date -d "$1" +%s; }

# /api/status shaped like src/status_json.cpp toJson():
# mkstatus PLAYBACK RECORDING FW_STATE ORGAN_POWER CONNECTED ("-" = field absent;
# ORGAN_POWER "null" = unknown, as the registry renders std::monostate).
mkstatus() {
    sw='"activity":"idle"'
    [ "$1" = - ] || sw="$sw,\"playback\":\"$1\""
    [ "$2" = - ] || sw="$sw,\"recording\":$2"
    [ "$3" = - ] || sw="$sw,\"fw_update_state\":\"$3\""
    base='"fw_version":"1.2.0"'
    [ "$4" = - ] || base="$base,\"organ_power\":$4"
    [ "$5" = - ] || base="$base,\"connected\":$5"
    printf '{"schema":2,"now_ms":123,"nodes":{"base":{"dot":"green","fields":{%s},"counters":{"rx":5},"events":[],"last_fault":null,"unseen_faults":0},"controller":{"dot":"grey","fields":{},"counters":{},"events":[],"last_fault":null,"unseen_faults":0},"software":{"dot":"green","fields":{%s},"counters":{},"events":[],"last_fault":null,"unseen_faults":0}},"problems":[],"songs":[]}\n' "$base" "$sw"
}

# Fresh fake world: app container running image OLD, registry tag points at NEW,
# status "before" = idle, organ off, base connected.
n=0
world() {
    n=$((n + 1))
    FAKE_DIR="$tmp/w$n"; MC_DATA_DIR="$FAKE_DIR/data"; MC_PREFIX="$FAKE_DIR/prefix"
    export FAKE_DIR MC_DATA_DIR MC_PREFIX
    mkdir -p "$FAKE_DIR" "$MC_PREFIX"
    : > "$FAKE_DIR/calls"
    echo "sha256:old|true|0" > "$FAKE_DIR/container"
    echo "sha256:old" > "$FAKE_DIR/tag_id"
    echo "sha256:new" > "$FAKE_DIR/registry_id"
    mkstatus idle false "done" false true > "$FAKE_DIR/status_before"
    mkstatus idle false - false true > "$FAKE_DIR/status_after"
    mkstatus idle false - false true > "$FAKE_DIR/status_rollback"
}
# shellcheck disable=SC2031 # loop() sets its own MC_SLEEP in a subshell on purpose
export IMAGE=ghcr.io/wojtacz/midicontrol-sw-py IMAGE_TAG=stable MC_SLEEP=: MC_GATE_SECONDS=60 MC_GATE_POLL=5

run_update() { lib update_once > "$FAKE_DIR/log" 2>&1; echo $?; }
calls_matching() { grep -c -- "$1" "$FAKE_DIR/calls"; }
no_calls() { ! grep -q -- "$1" "$FAKE_DIR/calls"; }
ustat() { jq -r "$1" "$MC_DATA_DIR/diag/update-status.json" 2>/dev/null; }

# ---------------------------------------------------------------- window
r=ok
lib window_open 02:00 120 "$(prague '2026-06-10 02:30')" || r=bad
lib window_open 02:00 120 "$(prague '2026-06-10 04:01')" && r=bad
lib window_open 02:00 120 "$(prague '2026-06-10 01:59')" && r=bad
lib window_open 02:00 120 "$(prague '2026-06-10 03:59')" || r=bad
check window_inside_and_outside $r

r=ok
lib window_open 23:00 120 "$(prague '2026-06-10 00:30')" || r=bad
lib window_open 23:00 120 "$(prague '2026-06-10 23:30')" || r=bad
lib window_open 23:00 120 "$(prague '2026-06-10 01:01')" && r=bad
lib window_open 23:00 120 "$(prague '2026-06-10 22:59')" && r=bad
check window_crosses_midnight $r

r=ok
now=$(prague '2026-03-29 01:59')
s=$(TZ=Europe/Prague lib seconds_until_window 02:00 "$now")
[ -n "$s" ] && [ "$s" -ge 1 ] || r=bad
[ "$r" = ok ] && { TZ=Europe/Prague lib window_open 02:00 120 $((now + s)) || r=bad; }
[ "$r" = ok ] || echo "  seconds_until_window gave '$s'"
check seconds_until_window_positive_on_dst_gap $r

# Extra: inside the window the next start is tomorrow's, never 0 (no busy loop).
r=ok
now=$(prague '2026-06-10 02:00')
s=$(TZ=Europe/Prague lib seconds_until_window 02:00 "$now")
[ "$s" = 86400 ] || r=bad
s=$(TZ=Europe/Prague lib seconds_until_window 23:00 "$(prague '2026-06-10 00:30')")
[ "$s" = $((22 * 3600 + 1800)) ] || r=bad
check seconds_until_window_at_start_is_next_day $r

r=ok
[ "$(lib parse_window 23:00-01:00)" = "23:00 120" ] || r=bad
[ "$(lib parse_window 02:00-04:00)" = "02:00 120" ] || r=bad
[ "$(lib parse_window garbage 2>/dev/null)" = "02:00 120" ] || r=bad
check parse_window_formats $r

# ---------------------------------------------------------------- guard
r=ok; [ "$(lib guard_decision "$(mkstatus idle false "done" false true)")" = proceed ] || r=bad
check guard_proceeds_when_idle_and_organ_off $r

r=ok; case "$(lib guard_decision "$(mkstatus playing false - false true)")" in busy:*) ;; *) r=bad ;; esac
case "$(lib guard_decision "$(mkstatus 'warming up' false - false true)")" in busy:*) ;; *) r=bad ;; esac
case "$(lib guard_decision "$(mkstatus - false - false true)")" in busy:*) ;; *) r=bad ;; esac
check guard_busy_when_playing $r

r=ok; case "$(lib guard_decision "$(mkstatus idle true - false true)")" in busy:*) ;; *) r=bad ;; esac
check guard_busy_when_recording $r

r=ok; case "$(lib guard_decision "$(mkstatus idle false running false true)")" in busy:*) ;; *) r=bad ;; esac
check guard_busy_when_fw_update_running $r

r=ok; case "$(lib guard_decision "$(mkstatus idle false - true true)")" in busy:*) ;; *) r=bad ;; esac
case "$(lib guard_decision "$(mkstatus idle false - true false)")" in busy:*) ;; *) r=bad ;; esac
check guard_busy_when_organ_on $r

r=ok
[ "$(lib guard_decision "$(mkstatus idle false - null false)")" = proceed ] || r=bad
[ "$(lib guard_decision "$(mkstatus idle false - - false)")" = proceed ] || r=bad
[ "$(lib guard_decision "$(mkstatus idle false - - -)")" = proceed ] || r=bad
check guard_proceeds_when_organ_unknown_and_base_disconnected $r

r=ok
case "$(lib guard_decision "$(mkstatus idle false - null true)")" in busy:*) ;; *) r=bad ;; esac
case "$(lib guard_decision "$(mkstatus idle false - - true)")" in busy:*) ;; *) r=bad ;; esac
check guard_busy_when_organ_unknown_and_base_connected $r

# Extra: string-rendered booleans and garbage input.
r=ok
[ "$(lib guard_decision '{"nodes":{"software":{"fields":{"playback":"idle","recording":"false"}},"base":{"fields":{"organ_power":"false","connected":"true"}}}}')" = proceed ] || r=bad
case "$(lib guard_decision '{"nodes":{"software":{"fields":{"playback":"idle","recording":"true"}},"base":{"fields":{"organ_power":false}}}}')" in busy:*) ;; *) r=bad ;; esac
case "$(lib guard_decision 'not json')" in busy:*) ;; *) r=bad ;; esac
check guard_tolerates_string_bools_and_garbage $r

# ---------------------------------------------------------------- update_once
world; echo "sha256:old" > "$FAKE_DIR/registry_id"
r=ok; rc=$(run_update)
[ "$rc" = 10 ] || r=bad
[ "$(calls_matching ' pull ghcr.io/wojtacz/midicontrol-sw-py:stable')" = 1 ] || r=bad
no_calls ' compose ' || r=bad; no_calls ' tag ' || r=bad; no_calls ' run ' || r=bad
[ "$(ustat .result)" = nothing_new ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; }
check update_nothing_new_returns_10_without_recreate $r

world; touch "$FAKE_DIR/pull_fail"
r=ok; rc=$(run_update)
[ "$rc" = 12 ] || r=bad
no_calls ' compose ' || r=bad; no_calls ' tag ' || r=bad; no_calls ' run ' || r=bad
[ "$(cat "$FAKE_DIR/container")" = "sha256:old|true|0" ] || r=bad
[ "$(ustat .result)" = pull_failed ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; }
check update_pull_failure_returns_12_and_does_not_touch_app $r

world; mkstatus playing false - true true > "$FAKE_DIR/status_before"
r=ok; rc=$(run_update)
[ "$rc" = 11 ] || r=bad
no_calls ' compose ' || r=bad; no_calls ' tag ' || r=bad; no_calls ' run ' || r=bad
[ "$(ustat .result)" = busy ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; }
check update_busy_returns_11_and_no_recreate $r

world; rm "$FAKE_DIR/status_before"
r=ok; rc=$(run_update)
[ "$rc" = 11 ] || r=bad
no_calls ' compose ' || r=bad; no_calls ' tag ' || r=bad
[ -s "$FAKE_DIR/curl_calls" ] || r=bad
grep -q 'http://127.0.0.1:50118/api/status' "$FAKE_DIR/curl_calls" || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; }
check update_unreachable_status_with_running_container_is_busy $r

# Extra: unreachable and the container is not running at all -> proceed.
world; rm "$FAKE_DIR/status_before"; echo "sha256:old|false|0" > "$FAKE_DIR/container"
r=ok; rc=$(run_update)
[ "$rc" = 0 ] || r=bad
[ "$(calls_matching 'IMAGE_TAG=stable compose .* up -d midicontrol$')" = 1 ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; cat "$FAKE_DIR/log"; }
check update_stopped_container_proceeds $r

# Extra: no app container at all (first install) -> proceed without retagging.
world; rm "$FAKE_DIR/status_before" "$FAKE_DIR/container"
r=ok; rc=$(run_update)
[ "$rc" = 0 ] || r=bad
no_calls ' tag ' || r=bad
[ "$(calls_matching 'IMAGE_TAG=stable compose .* up -d midicontrol$')" = 1 ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; cat "$FAKE_DIR/log"; }
check update_no_container_proceeds $r

world; rm "$FAKE_DIR/status_after"   # new image never answers
r=ok; rc=$(run_update)
[ "$rc" = 13 ] || r=bad
[ "$(calls_matching ' tag sha256:old ghcr.io/wojtacz/midicontrol-sw-py:previous$')" = 1 ] || r=bad
[ "$(calls_matching 'IMAGE_TAG=previous compose .* up -d midicontrol$')" = 1 ] || r=bad
no_calls ' run ' || r=bad; no_calls 'up -d updater' || r=bad
[ "$(cat "$FAKE_DIR/container")" = "sha256:old|true|0" ] || r=bad
[ "$(ustat .result)" = rolled_back ] || r=bad
[ "$(ustat .failed_digest)" = sha256:new ] || r=bad
[ -n "$(ustat .time)" ] && [ "$(ustat .time)" != null ] || r=bad
# Night 2 and 3: same bad digest in the registry -> not retried.
for night in 2 3; do
    : > "$FAKE_DIR/calls"
    rc=$(run_update)
    [ "$rc" = 10 ] || { r=bad; echo "  night $night rc=$rc"; }
    no_calls ' compose ' || r=bad; no_calls ' tag ' || r=bad
    [ "$(ustat .failed_digest)" = sha256:new ] || r=bad
done
# A newer digest appears -> updated, failed digest cleared.
: > "$FAKE_DIR/calls"; echo "sha256:newer" > "$FAKE_DIR/registry_id"
mkstatus idle false - false true > "$FAKE_DIR/status_after"
rc=$(run_update)
[ "$rc" = 0 ] || { r=bad; echo "  newer rc=$rc"; }
[ "$(calls_matching 'IMAGE_TAG=stable compose .* up -d midicontrol$')" = 1 ] || r=bad
[ "$(ustat .result)" = updated ] || r=bad
[ "$(ustat .failed_digest)" = null ] || r=bad
[ "$r" = ok ] || { cat "$FAKE_DIR/calls"; cat "$FAKE_DIR/log"; }
check update_bad_image_rolls_back $r

# Extra: the new container does not stay up -> rollback.
world; echo false > "$FAKE_DIR/new_running"
r=ok; rc=$(run_update)
[ "$rc" = 13 ] || r=bad
[ "$(calls_matching 'IMAGE_TAG=previous compose .* up -d midicontrol$')" = 1 ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; }
check update_container_crash_rolls_back $r

world; echo 2 > "$FAKE_DIR/new_restarts"
r=ok; rc=$(run_update)
[ "$rc" = 13 ] || r=bad
check update_restart_loop_rolls_back $r

world; mkstatus idle false - null false > "$FAKE_DIR/status_after"   # base never comes back
r=ok; rc=$(run_update)
[ "$rc" = 13 ] || r=bad
[ "$(calls_matching 'IMAGE_TAG=previous compose .* up -d midicontrol$')" = 1 ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/calls"; cat "$FAKE_DIR/log"; }
check update_requires_base_reconnect_if_it_was_connected $r

# Extra: an unplugged base (not connected before) never blocks the update.
world; mkstatus idle false - null false > "$FAKE_DIR/status_before"
mkstatus idle false - null false > "$FAKE_DIR/status_after"
r=ok; rc=$(run_update)
[ "$rc" = 0 ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; cat "$FAKE_DIR/log"; }
check update_unplugged_base_does_not_block $r

world
r=ok; rc=$(run_update)
[ "$rc" = 0 ] || r=bad
# Order: pull, retag the running image :previous, recreate the app, then self-update last.
order=$(grep -n -e ' pull ' -e ' tag ' -e 'up -d midicontrol$' -e ' run ' "$FAKE_DIR/calls" | cut -d: -f1 | tr '\n' ' ')
[ "$(echo "$order" | wc -w)" = 4 ] || r=bad
[ "$order" = "$(echo "$order" | tr ' ' '\n' | grep . | sort -n | tr '\n' ' ')" ] || r=bad
[ "$(calls_matching ' tag sha256:old ghcr.io/wojtacz/midicontrol-sw-py:previous$')" = 1 ] || r=bad
[ "$(calls_matching "IMAGE_TAG=stable compose -p midicontrol --project-directory $MC_PREFIX -f $MC_PREFIX/compose.yaml up -d midicontrol$")" = 1 ] || r=bad
no_calls 'IMAGE_TAG=previous' || r=bad
selfup=$(grep ' run ' "$FAKE_DIR/calls")
case "$selfup" in *"--entrypoint docker"*"/var/run/docker.sock:/var/run/docker.sock"*"$MC_PREFIX:$MC_PREFIX"*"ghcr.io/wojtacz/midicontrol-sw-py:stable compose -p midicontrol --project-directory $MC_PREFIX -f $MC_PREFIX/compose.yaml up -d updater") ;; *) r=bad ;; esac
[ "$(tail -n 1 "$FAKE_DIR/calls")" = "$selfup" ] || r=bad
[ "$(jq -r .result "$FAKE_DIR/status_at_selfupdate" 2>/dev/null)" = updated ] || r=bad
[ "$(ustat .digest)" = sha256:new ] || r=bad
[ "$(cat "$FAKE_DIR/container")" = "sha256:new|true|0" ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc order=$order"; cat "$FAKE_DIR/calls"; cat "$FAKE_DIR/log"; }
check update_success_retags_previous_and_recreates $r

# ---------------------------------------------------------------- updater.sh
world
r=ok
out=$(sh "$here/../updater/updater.sh" --once 2>&1); rc=$?
[ "$rc" = 0 ] || r=bad
echo "$out" | tail -n 1 | grep -qx 'result: updated (0)' || r=bad
[ "$(calls_matching 'up -d midicontrol$')" = 1 ] || r=bad
[ "$r" = ok ] || { echo "  rc=$rc"; echo "$out"; }
check updater_once_runs_update_and_prints_result $r

world; mkstatus playing false - true true > "$FAKE_DIR/status_before"
r=ok
out=$(sh "$here/../updater/updater.sh" --once 2>&1); rc=$?
[ "$rc" = 11 ] || r=bad
echo "$out" | tail -n 1 | grep -q '^result: busy (11)' || r=bad
no_calls ' compose ' || r=bad
check updater_once_still_applies_guard $r

# Disabled: never touches docker, only sleeps (the fake sleep ends the script).
world
# shellcheck disable=SC2016 # expanded by the generated script
printf '#!/bin/sh\necho "SLEEP $1" >> "%s/sleeps"\nkill "$PPID"\n' "$FAKE_DIR" > "$FAKE_DIR/sleep"
chmod +x "$FAKE_DIR/sleep"
r=ok
# The extra sh -c swallows the shell's "Terminated" notice.
UPDATE_ENABLED=0 MC_SLEEP="$FAKE_DIR/sleep" sh -c 'sh "$1" >/dev/null 2>&1; :' sh "$here/../updater/updater.sh" 2>/dev/null
[ -s "$FAKE_DIR/calls" ] && r=bad
grep -qx 'SLEEP 86400' "$FAKE_DIR/sleeps" 2>/dev/null || r=bad
check updater_disabled_only_sleeps $r

# ---------------------------------------------------------------- loop
# run_loop with a fake clock: MC_SLEEP advances it, update_once is replaced by a
# recorder returning $LOOP_RC. Stops at MC_LOOP_UNTIL.
# shellcheck disable=SC2034,SC2329 # variables and overrides are used by run_loop
loop() { # loop START_TIME UNTIL_TIME WINDOW RC
    (
        # shellcheck source=docker/updater/lib.sh
        . "$MC_LIB_DIR/lib.sh"
        CLOCK=$(prague "$1"); MC_LOOP_UNTIL=$(prague "$2"); UPDATE_WINDOW=$3; LOOP_RC=$4
        now_epoch() { echo "$CLOCK"; }
        fake_sleep() {
            [ "$1" -ge 1 ] || echo "BADSLEEP $1"
            CLOCK=$((CLOCK + $1))
        }
        update_once() { echo "ATTEMPT $(date -d "@$CLOCK" '+%F %H:%M')"; return "$LOOP_RC"; }
        MC_SLEEP=fake_sleep
        run_loop
    ) 2>&1 | grep -e '^ATTEMPT' -e '^BADSLEEP'
}
r=ok; out=$(loop '2026-10-24 12:00' '2026-10-27 12:00' 02:00-04:00 10)
[ "$out" = "ATTEMPT 2026-10-25 02:00
ATTEMPT 2026-10-26 02:00
ATTEMPT 2026-10-27 02:00" ] || { r=bad; echo "$out"; }
check loop_one_attempt_per_night_on_fall_back $r

r=ok; out=$(loop '2026-03-28 12:00' '2026-03-30 12:00' 02:00-04:00 10)
[ "$out" = "ATTEMPT 2026-03-29 03:00
ATTEMPT 2026-03-30 02:00" ] || { r=bad; echo "$out"; }
check loop_one_attempt_per_night_on_spring_forward $r

# Busy all night: every 15 min inside the 2 h window, nothing outside it.
r=ok; out=$(loop '2026-06-10 12:00' '2026-06-11 12:00' 02:00-04:00 11)
[ "$(echo "$out" | grep -c ATTEMPT)" = 8 ] || r=bad
[ "$(echo "$out" | head -n 1)" = "ATTEMPT 2026-06-11 02:00" ] || r=bad
[ "$(echo "$out" | tail -n 1)" = "ATTEMPT 2026-06-11 03:45" ] || r=bad
echo "$out" | grep -q BADSLEEP && r=bad
[ "$r" = ok ] || echo "$out"
check loop_busy_retries_every_15_min_inside_window $r

r=ok; out=$(loop '2026-06-10 12:00' '2026-06-11 12:00' 23:00-01:00 12)
[ "$(echo "$out" | grep -c ATTEMPT)" = 8 ] || r=bad
[ "$(echo "$out" | head -n 1)" = "ATTEMPT 2026-06-10 23:00" ] || r=bad
[ "$(echo "$out" | tail -n 1)" = "ATTEMPT 2026-06-11 00:45" ] || r=bad
[ "$r" = ok ] || echo "$out"
check loop_window_crossing_midnight_retries $r

# Started inside the window (container restart at 02:30): attempts right away.
r=ok; out=$(loop '2026-06-11 02:30' '2026-06-11 12:00' 02:00-04:00 0)
[ "$out" = "ATTEMPT 2026-06-11 02:30" ] || { r=bad; echo "$out"; }
check loop_started_inside_window_attempts_once $r

exit $fail
