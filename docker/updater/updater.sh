#!/bin/sh
# MIDIControl night-window updater (installed as /usr/local/bin/midicontrol-updater).
#   midicontrol-updater          loop: update inside UPDATE_WINDOW (compose "updater" service)
#   midicontrol-updater --once   update now (the idle guard still applies), print the result
# Functions live in lib.sh; MC_LIB_DIR overrides its directory (tests run from the repo).
# shellcheck source=docker/updater/lib.sh
. "${MC_LIB_DIR:-/usr/local/lib/midicontrol}/lib.sh"

# PID 1 in the container: end promptly on TERM/INT (docker stop) and take the
# sleeping child along. Sleeping is "sleep & wait" so the trap runs at once.
sleep_pid=""
interruptible_sleep() {
    sleep "$1" &
    sleep_pid=$!
    wait "$sleep_pid"
    sleep_pid=""
}
stop_now() {
    [ -z "$sleep_pid" ] || kill "$sleep_pid" 2>/dev/null
    exit 0
}
trap stop_now TERM INT
[ "$MC_SLEEP" != sleep ] || MC_SLEEP=interruptible_sleep

usage() {
    echo "usage: midicontrol-updater [--once]"
    echo "  --once  update now (skipped while the organ is busy) and print the result"
}

case "${1:-}" in
    --once)
        update_once
        rc=$?
        echo "result: $(result_name "$rc") ($rc)"
        exit "$rc"
        ;;
    -h | --help) usage; exit 0 ;;
    "") ;;
    *) usage >&2; exit 2 ;;
esac

if [ "${UPDATE_ENABLED:-1}" != 1 ]; then
    log "UPDATE_ENABLED=${UPDATE_ENABLED}: automatic updates are off (update now: midicontrol-updater --once)"
    while :; do "$MC_SLEEP" 86400; done
fi
run_loop
