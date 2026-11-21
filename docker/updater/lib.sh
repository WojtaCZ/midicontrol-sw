# shellcheck shell=sh
# MIDIControl night-window updater: functions (POSIX sh, GNU date, jq).
# Sourced by midicontrol-updater (docker/updater/updater.sh) and by
# docker/tests/test_updater.sh. Installed as /usr/local/lib/midicontrol/lib.sh.
#
# Environment:
#   IMAGE, IMAGE_TAG          image the app runs (default ghcr.io/wojtacz/midicontrol-sw:stable)
#   UPDATE_WINDOW             "HH:MM-HH:MM" local time (default 02:00-04:00; may cross midnight)
#   MC_PREFIX                 compose directory (default /opt/midicontrol)
#   MC_DATA_DIR               data directory; the result goes to $MC_DATA_DIR/diag/update-status.json
#   MC_STATUS_URL             player status endpoint (default http://127.0.0.1:50118/api/status)
#   MC_GATE_SECONDS, MC_GATE_POLL   health gate length and poll interval (60 s, 5 s)
#   MC_RETRY_SECONDS          retry interval inside the window (900 s)
#   MC_SLEEP                  sleep command or function name (default sleep)
#
# update_once return codes.
UPD_OK=0
UPD_NOTHING_NEW=10
UPD_BUSY=11
UPD_PULL_FAILED=12
UPD_ROLLED_BACK=13

MC_APP=midicontrol
MC_PROJECT=midicontrol
: "${MC_SLEEP:=sleep}"

log() { printf '%s updater: %s\n' "$(date '+%F %T')" "$*"; }

# Current wall time. The only place the clock is read for scheduling; re-read on
# every loop iteration, never cached across a sleep.
now_epoch() { date +%s; }

result_name() {
    case "$1" in
        0) echo updated ;;
        10) echo nothing_new ;;
        11) echo busy ;;
        12) echo pull_failed ;;
        13) # rollback impossible (no previous container) is recorded as "failed"
            if [ "$(jq -r '.result // ""' "$(status_file)" 2>/dev/null)" = failed ]; then echo failed; else echo rolled_back; fi ;;
        *) echo "error" ;;
    esac
}

# ------------------------------------------------------------------ window

# "HH:MM" -> minutes since midnight; fails on anything else.
hhmm_to_min() {
    case "$1" in
        [0-2][0-9]:[0-5][0-9]) ;;
        *) return 1 ;;
    esac
    hm_h=${1%%:*}; hm_m=${1#*:}
    hm_h=${hm_h#0}; hm_m=${hm_m#0}
    [ "${hm_h:-0}" -le 23 ] || return 1
    echo $(( ${hm_h:-0} * 60 + ${hm_m:-0} ))
}

min_to_hhmm() { printf '%02d:%02d\n' $(($1 / 60)) $(($1 % 60)); }

# parse_window "HH:MM-HH:MM" -> prints "START_HHMM LEN_MIN". Invalid or empty
# windows fall back to 02:00-04:00 with a message on stderr.
parse_window() {
    if ! { pw_s=$(hhmm_to_min "${1%%-*}") && pw_e=$(hhmm_to_min "${1#*-}") && [ "$pw_s" != "$pw_e" ]; }; then
        echo "updater: invalid UPDATE_WINDOW '$1', using 02:00-04:00" >&2
        echo "02:00 120"
        return 0
    fi
    echo "$(min_to_hhmm "$pw_s") $(( (pw_e - pw_s + 1440) % 1440 ))"
}

# Local calendar date (YYYY-MM-DD) of EPOCH shifted by DAYS days. Steps from
# local noon, which exists on every day, so DST days do not skip or repeat a date.
local_date() { # EPOCH DAYS
    ld_d=$(date -d "@$1" +%F) || return 1
    ld_noon=$(date -d "$ld_d 12:00" +%s) || return 1
    date -d "@$((ld_noon + $2 * 86400))" +%F
}

# Epoch of the window start HH:MM on local date D. If HH:MM does not exist on
# that date (spring-forward gap), the start is the first instant after the gap.
# An ambiguous time (fall-back) resolves to one instant (GNU date's choice), so
# every date has exactly one window start.
window_start_on() { # DATE HHMM
    if date -d "$1 $2" +%s 2>/dev/null; then return 0; fi
    ws_m=$(hhmm_to_min "$2") || return 1
    for ws_k in 30 60 90 120 150 180; do
        [ "$ws_m" -ge "$ws_k" ] || break
        if ws_t=$(date -d "$1 $(min_to_hhmm $((ws_m - ws_k)))" +%s 2>/dev/null); then
            echo $((ws_t + ws_k * 60))
            return 0
        fi
    done
    return 1
}

# window_open START_HHMM LEN_MIN NOW_EPOCH: 0 when NOW is inside today's or
# yesterday's window [start, start + len).
window_open() {
    for wo_off in 0 -1; do
        wo_d=$(local_date "$3" "$wo_off") || return 1
        wo_s=$(window_start_on "$wo_d" "$1") || continue
        if [ "$3" -ge "$wo_s" ] && [ "$3" -lt $((wo_s + $2 * 60)) ]; then return 0; fi
    done
    return 1
}

# seconds_until_window START_HHMM NOW_EPOCH: seconds until the next window start
# strictly after NOW (today's if still ahead, else tomorrow's); always >= 1.
seconds_until_window() {
    su_s=""
    for su_off in 0 1 2; do
        su_d=$(local_date "$2" "$su_off") || break
        su_t=$(window_start_on "$su_d" "$1") || continue
        if [ "$su_t" -gt "$2" ]; then su_s=$((su_t - $2)); break; fi
    done
    [ -n "$su_s" ] || su_s=3600
    [ "$su_s" -ge 1 ] || su_s=1
    echo "$su_s"
}

# ------------------------------------------------------------------ guard

# guard_decision STATUS_JSON -> "proceed" or "busy:<reason>". Paths follow
# src/status_json.cpp: software fields playback/recording/fw_update_state, base
# fields organ_power (null = unknown) and connected. Booleans rendered as strings
# are accepted too.
guard_decision() {
    gd_out=$(printf '%s' "$1" | jq -r '
        def flag: if . == true or . == "true" then true
                  elif . == false or . == "false" then false
                  else null end;
        (.nodes.software.fields // {}) as $s
        | (.nodes.base.fields // {}) as $b
        | if ($s.playback // "") != "idle" then "busy:playback=\($s.playback // "unknown")"
          elif ($s.recording | flag) == true then "busy:recording"
          elif ($s.fw_update_state // "") == "running" then "busy:firmware update running"
          elif ($b.organ_power | flag) == false then "proceed"
          elif ($b.organ_power | flag) == true then "busy:organ on"
          elif ($b.connected | flag) == true then "busy:organ power unknown, base connected"
          else "proceed" end' 2>/dev/null) || gd_out=""
    case "$gd_out" in
        proceed | busy:*) echo "$gd_out" ;;
        *) echo "busy:status unreadable" ;;
    esac
}

# 0 when the status JSON says the base is connected.
base_connected() {
    [ "$(printf '%s' "$1" | jq -r '.nodes.base.fields.connected | tostring' 2>/dev/null)" = true ]
}

fetch_status() {
    curl -fs --max-time 5 "${MC_STATUS_URL:-http://127.0.0.1:50118/api/status}"
}

# ------------------------------------------------------------------ docker

# mc_compose TAG ARGS...: docker compose on the installed project with IMAGE_TAG=TAG.
mc_compose() {
    mcc_tag=$1; shift
    IMAGE_TAG=$mcc_tag docker compose -p "$MC_PROJECT" --project-directory "${MC_PREFIX:-/opt/midicontrol}" \
        -f "${MC_PREFIX:-/opt/midicontrol}/compose.yaml" "$@"
}

# "IMAGE_ID|RUNNING|RESTARTS" of the app container, empty if there is none.
app_state() {
    docker inspect --type container --format '{{.Image}}|{{.State.Running}}|{{.RestartCount}}' "$MC_APP" 2>/dev/null || true
}

status_file() { echo "${MC_DATA_DIR:-/opt/midicontrol/data}/diag/update-status.json"; }

# write_status RESULT [DIGEST] [DETAIL]: keep the last result. failed_digest is
# carried over from the previous file, set on rolled_back and cleared on updated,
# so a digest that failed is skipped until a different one is pulled.
write_status() {
    wr_f=$(status_file)
    mkdir -p "$(dirname "$wr_f")" || return 0
    wr_failed=$(jq -r '.failed_digest // ""' "$wr_f" 2>/dev/null) || wr_failed=""
    case "$1" in
        rolled_back) wr_failed=$2 ;;
        updated) wr_failed="" ;;
    esac
    if jq -n --arg result "$1" --arg digest "${2:-}" --arg detail "${3:-}" \
        --arg failed "$wr_failed" --arg time "$(date -Iseconds)" \
        --arg image "${IMAGE:-ghcr.io/wojtacz/midicontrol-sw}" --arg tag "${IMAGE_TAG:-stable}" \
        '{result: $result, time: $time, image: $image, tag: $tag,
          digest: (if $digest == "" then null else $digest end),
          failed_digest: (if $failed == "" then null else $failed end),
          detail: $detail}' > "$wr_f.tmp" 2>/dev/null; then
        mv "$wr_f.tmp" "$wr_f" || log "could not write $wr_f"
    else
        log "could not write $wr_f"
    fi
}

# health_gate BASE_WAS_CONNECTED(0/1): the app container stays up (running, no
# restarts) for MC_GATE_SECONDS, the status endpoint answers at the end, and if
# the base was connected before, it is connected again at some point.
health_gate() {
    hg_poll=${MC_GATE_POLL:-5}
    hg_n=$(( ${MC_GATE_SECONDS:-60} / hg_poll ))
    [ "$hg_n" -ge 1 ] || hg_n=1
    hg_base_ok=1; [ "$1" = 1 ] && hg_base_ok=0
    hg_answer=0; hg_i=0
    while [ "$hg_i" -lt "$hg_n" ]; do
        "$MC_SLEEP" "$hg_poll"
        hg_i=$((hg_i + 1))
        hg_state=$(app_state)
        hg_rest=${hg_state#*|}
        if [ "${hg_rest%%|*}" != true ] || [ "${hg_state##*|}" != 0 ]; then
            log "health gate: container not up (${hg_state:-missing})"
            return 1
        fi
        if hg_st=$(fetch_status) && [ -n "$hg_st" ]; then
            hg_answer=1
            base_connected "$hg_st" && hg_base_ok=1
        else
            hg_answer=0
        fi
    done
    if [ "$hg_answer" != 1 ]; then log "health gate: status endpoint does not answer"; return 1; fi
    if [ "$hg_base_ok" != 1 ]; then log "health gate: base was connected before and did not reconnect"; return 1; fi
    return 0
}

# Recreate the updater with the new image, last. Run from a one-shot helper
# container: a compose client running inside this container would be killed when
# compose stops it, possibly before the new updater is created (a container
# stopped through the API is not restarted by "restart: always").
self_update() {
    log "recreating the updater with $1"
    docker run -d --rm --entrypoint docker \
        -v /var/run/docker.sock:/var/run/docker.sock \
        -v "${MC_PREFIX:-/opt/midicontrol}:${MC_PREFIX:-/opt/midicontrol}" \
        -w "${MC_PREFIX:-/opt/midicontrol}" \
        -e IMAGE="${IMAGE:-ghcr.io/wojtacz/midicontrol-sw}" -e IMAGE_TAG="${IMAGE_TAG:-stable}" \
        "$1" compose -p "$MC_PROJECT" --project-directory "${MC_PREFIX:-/opt/midicontrol}" \
        -f "${MC_PREFIX:-/opt/midicontrol}/compose.yaml" up -d updater >/dev/null \
        || log "self-update helper failed to start (the updater keeps its current image)"
}

# ------------------------------------------------------------------ update

# Lock file serialising update attempts (the loop and "--once" run in different processes).
lock_file() { echo "${MC_DATA_DIR:-/opt/midicontrol/data}/diag/update.lock"; }

# update_once: one update attempt, run under an exclusive flock; a concurrent
# attempt returns 11 (busy) without touching anything.
update_once() {
    uol_f=$(lock_file)
    mkdir -p "$(dirname "$uol_f")" 2>/dev/null || uol_f=/tmp/midicontrol-update.lock
    (
        flock -n 9 || { log "another update attempt is already running; skipping"; exit "$UPD_BUSY"; }
        update_locked
    ) 9> "$uol_f"
}

# update_locked: one update attempt (spec 5.5 steps 2-5). Returns 0 updated,
# 10 nothing new, 11 busy, 12 pull failed, 13 rolled back.
update_locked() {
    uo_image=${IMAGE:-ghcr.io/wojtacz/midicontrol-sw}
    uo_tag=${IMAGE_TAG:-stable}
    uo_ref="$uo_image:$uo_tag"

    uo_state=$(app_state)
    uo_cur=${uo_state%%|*}
    uo_rest=${uo_state#*|}
    uo_running=${uo_rest%%|*}
    [ -n "$uo_state" ] || uo_running=false
    log "app image ${uo_cur:-none}, running=$uo_running"

    if ! uo_out=$(docker pull "$uo_ref" 2>&1); then
        log "pull $uo_ref failed: $(printf '%s\n' "$uo_out" | tail -n 1); app untouched"
        write_status pull_failed "" "$(printf '%s\n' "$uo_out" | tail -n 1)"
        return $UPD_PULL_FAILED
    fi
    uo_new=$(docker image inspect --format '{{.Id}}' "$uo_ref" 2>/dev/null)
    if [ -z "$uo_new" ]; then
        log "pulled $uo_ref but cannot read its id; app untouched"
        write_status pull_failed "" "no image id after pull"
        return $UPD_PULL_FAILED
    fi
    if [ "$uo_new" = "$uo_cur" ]; then
        log "nothing new ($uo_new)"
        write_status nothing_new "$uo_new"
        return $UPD_NOTHING_NEW
    fi
    uo_failed=$(jq -r '.failed_digest // ""' "$(status_file)" 2>/dev/null) || uo_failed=""
    if [ "$uo_new" = "$uo_failed" ]; then
        log "$uo_new failed the health gate before; waiting for a newer image"
        write_status nothing_new "$uo_new" "known bad digest"
        return $UPD_NOTHING_NEW
    fi

    uo_base_before=0
    if uo_status=$(fetch_status) && [ -n "$uo_status" ]; then
        uo_decision=$(guard_decision "$uo_status")
        if [ "$uo_decision" != proceed ]; then
            log "busy (${uo_decision#busy:}); not updating to $uo_new"
            write_status busy "$uo_new" "${uo_decision#busy:}"
            return $UPD_BUSY
        fi
        base_connected "$uo_status" && uo_base_before=1
    elif [ "$uo_running" = true ]; then
        log "busy (status endpoint unreachable while the app runs); not updating"
        write_status busy "$uo_new" "status unreachable"
        return $UPD_BUSY
    else
        log "app not running; proceeding without a status check"
    fi

    if [ -n "$uo_cur" ]; then
        if ! docker tag "$uo_cur" "$uo_image:previous"; then
            log "could not tag $uo_cur as :previous; app untouched"
            write_status pull_failed "$uo_new" "retag failed"
            return $UPD_PULL_FAILED
        fi
    fi
    log "updating app ${uo_cur:-none} -> $uo_new"
    if mc_compose "$uo_tag" up -d "$MC_APP" && health_gate "$uo_base_before"; then
        log "updated to $uo_new"
        write_status updated "$uo_new"
        self_update "$uo_ref"
        return $UPD_OK
    fi

    if [ -z "$uo_cur" ]; then
        # First install through the updater: there is no :previous to go back to.
        log "update to $uo_new failed and there was no app container before; leaving it as it is"
        write_status failed "$uo_new" "health gate failed, no previous container"
        return $UPD_ROLLED_BACK
    fi
    log "update to $uo_new failed; rolling back to :previous"
    mc_compose previous up -d "$MC_APP" || log "rollback compose call failed"
    write_status rolled_back "$uo_new" "health gate failed"
    return $UPD_ROLLED_BACK
}

# ------------------------------------------------------------------ loop

# run_loop: sleep until the window, then try update_once every MC_RETRY_SECONDS
# until it returns 0/10/13 or the window closes; then sleep until the next
# window. Every iteration sleeps >= 1 s. MC_LOOP_UNTIL (epoch) ends it (tests).
run_loop() {
    rl_w=$(parse_window "${UPDATE_WINDOW:-02:00-04:00}")
    rl_start=${rl_w% *}; rl_len=${rl_w#* }
    log "update window $rl_start for $rl_len min, image ${IMAGE:-ghcr.io/wojtacz/midicontrol-sw}:${IMAGE_TAG:-stable}"
    while :; do
        rl_now=$(now_epoch)
        if [ -n "${MC_LOOP_UNTIL:-}" ] && [ "$rl_now" -ge "$MC_LOOP_UNTIL" ]; then return 0; fi
        if window_open "$rl_start" "$rl_len" "$rl_now"; then
            update_once
            rl_rc=$?
            log "result: $(result_name "$rl_rc") ($rl_rc)"
            case "$rl_rc" in
                0 | 10 | 13) rl_wait=$(seconds_until_window "$rl_start" "$(now_epoch)") ;;
                *) rl_wait=${MC_RETRY_SECONDS:-900} ;;
            esac
        else
            rl_wait=$(seconds_until_window "$rl_start" "$rl_now")
        fi
        [ "$rl_wait" -ge 1 ] 2>/dev/null || rl_wait=1
        log "sleeping ${rl_wait}s"
        "$MC_SLEEP" "$rl_wait"
    done
}
