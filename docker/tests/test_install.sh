#!/bin/sh
# Tests for install.sh: fake docker/systemctl/ss on PATH, MC_PREFIX in a temp dir,
# templates fetched from the local docker/ directory over file:// (real curl).
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
fail=0
check() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# Only the fakes we need go on PATH (the fake curl of the updater tests must not).
mkdir "$tmp/bin"
for f in docker systemctl ss; do ln -s "$here/fakes/$f" "$tmp/bin/$f"; done
PATH="$tmp/bin:$PATH"
export PATH
unset SUDO_USER

n=0
# world: fresh prefix, bindir and fake state; sets FAKE_DIR and the MC_* variables.
world() {
    n=$((n + 1))
    w="$tmp/w$n"
    mkdir -p "$w/prefix" "$w/bin" "$w/fake"
    FAKE_DIR="$w/fake"
    MC_PREFIX="$w/prefix/midicontrol"
    MC_BINDIR="$w/bin"
    MC_RAW_URL="file://$root/docker"
    MC_WAIT_SECONDS=0
    export FAKE_DIR MC_PREFIX MC_BINDIR MC_RAW_URL MC_WAIT_SECONDS
}
run() { sh "$root/install.sh" "$@" > "$w/out" 2>&1; }
has() { grep -q -- "$1" "$2"; }

# fails_without_docker
world
MC_DOCKER=/nonexistent/docker run; rc=$?
if [ "$rc" = 1 ] && has "docs.docker.com" "$w/out" && [ ! -e "$MC_PREFIX/compose.yaml" ]; then
    check fails_without_docker ok; else check fails_without_docker bad; fi

# fresh_install_writes_files
world
run; rc=$?
ok=bad
if [ "$rc" = 0 ] && [ -f "$MC_PREFIX/compose.yaml" ] \
   && cmp -s "$MC_PREFIX/compose.yaml" "$root/docker/compose.template.yaml" \
   && [ "$(cat "$MC_PREFIX/.env")" = "IMAGE_TAG=stable
UPDATE_WINDOW=02:00-04:00
UPDATE_ENABLED=1" ] \
   && [ -d "$MC_PREFIX/data/songs" ] && [ -d "$MC_PREFIX/data/recordings" ] \
   && [ -d "$MC_PREFIX/data/diag" ] && [ -d "$MC_PREFIX/data/firmware" ] \
   && [ -x "$MC_BINDIR/midicontrol" ] \
   && [ ! -e "$MC_PREFIX/data/config.conf" ]; then
    pull=$(grep -n ' compose pull' "$FAKE_DIR/calls" | head -1 | cut -d: -f1)
    up=$(grep -n ' compose up -d' "$FAKE_DIR/calls" | head -1 | cut -d: -f1)
    [ -n "$pull" ] && [ -n "$up" ] && [ "$pull" -lt "$up" ] && ok=ok
fi
check fresh_install_writes_files "$ok"
ok=bad
if has "http://.*:50118/" "$w/out" && has "midicontrol-updater --once" "$w/out" \
   && has "install.sh | sh -s -- --uninstall" "$w/out" && has "midicontrol" "$w/out"; then ok=ok; fi
check prints_url_and_commands "$ok"

# rerun_keeps_customised_env_and_config (Review Focus 4)
world
run
printf 'IMAGE_TAG=edge\nUPDATE_WINDOW=05:00-06:00\nUPDATE_ENABLED=1\n# MARKER-ENV\n' > "$MC_PREFIX/.env"
printf '[X]\n# MARKER-CONF\n' > "$MC_PREFIX/data/config.conf"
cp "$MC_PREFIX/.env" "$w/env.before"; cp "$MC_PREFIX/data/config.conf" "$w/conf.before"
echo "# stale" > "$MC_PREFIX/compose.yaml"
run --tag foo; rc=$?
if [ "$rc" = 0 ] && cmp -s "$w/env.before" "$MC_PREFIX/.env" \
   && cmp -s "$w/conf.before" "$MC_PREFIX/data/config.conf" \
   && cmp -s "$MC_PREFIX/compose.yaml" "$root/docker/compose.template.yaml" \
   && has "ignored" "$w/out" && has "IMAGE_TAG" "$w/out"; then
    check rerun_keeps_customised_env_and_config ok; else check rerun_keeps_customised_env_and_config bad; fi

# old_systemd_unit_is_stopped (Review Focus 4): the unit holds port 50118 until the
# fake systemctl "stops" it, so a pass also proves stop happens before the port check.
world
touch "$FAKE_DIR/unit_active"
echo "LISTEN 0 5 0.0.0.0:50118 0.0.0.0:*" > "$FAKE_DIR/ss_out"
run; rc=$?
if [ "$rc" = 0 ] && has "^--user stop midicontrol.service" "$FAKE_DIR/systemctl_calls" \
   && has "^--user disable midicontrol.service" "$FAKE_DIR/systemctl_calls" \
   && has "systemd" "$w/out"; then
    check old_systemd_unit_is_stopped ok; else check old_systemd_unit_is_stopped bad; fi

# port_in_use_fails: something else (no unit, not our container) listens on 50118
world
echo "LISTEN 0 5 0.0.0.0:50118 0.0.0.0:*" > "$FAKE_DIR/ss_out"
run; rc=$?
if [ "$rc" = 1 ] && has "50118" "$w/out" && [ ! -e "$MC_PREFIX/compose.yaml" ]; then
    check port_in_use_fails ok; else check port_in_use_fails bad; fi

# port held by our own container is fine
world
echo "LISTEN 0 5 0.0.0.0:50118 0.0.0.0:*" > "$FAKE_DIR/ss_out"
echo midicontrol > "$FAKE_DIR/ps_out"
run; rc=$?
if [ "$rc" = 0 ]; then check port_held_by_own_container_ok ok; else check port_held_by_own_container_ok bad; fi

# no_updates_flag_sets_enabled_0
world
run --no-updates
if has '^UPDATE_ENABLED=0$' "$MC_PREFIX/.env" && has '^IMAGE_TAG=stable$' "$MC_PREFIX/.env"; then
    check no_updates_flag_sets_enabled_0 ok; else check no_updates_flag_sets_enabled_0 bad; fi

# tag_flag_sets_image_tag
world
run --tag edge
if has '^IMAGE_TAG=edge$' "$MC_PREFIX/.env"; then check tag_flag_sets_image_tag ok; else check tag_flag_sets_image_tag bad; fi

# wrapper content: -t only on a TTY
world
run
ok=bad
if has 'exec -it midicontrol' "$MC_BINDIR/midicontrol" && has 'midicontrol "\$@"' "$MC_BINDIR/midicontrol"; then
    : > "$FAKE_DIR/calls"
    sh "$MC_BINDIR/midicontrol" --cmd x < /dev/null > /dev/null 2>&1
    if grep -q 'exec -i midicontrol midicontrol --cmd x' "$FAKE_DIR/calls"; then ok=ok; fi
fi
check wrapper_runs_docker_exec_without_tty "$ok"

# uninstall_keeps_data_without_purge
world
run
echo keep > "$MC_PREFIX/data/songs/a.mid"
: > "$FAKE_DIR/calls"
run --uninstall; rc=$?
if [ "$rc" = 0 ] && [ ! -e "$MC_PREFIX/compose.yaml" ] && [ ! -e "$MC_PREFIX/.env" ] \
   && [ ! -e "$MC_BINDIR/midicontrol" ] && [ -f "$MC_PREFIX/data/songs/a.mid" ] \
   && has ' compose down' "$FAKE_DIR/calls"; then
    check uninstall_keeps_data_without_purge ok; else check uninstall_keeps_data_without_purge bad; fi
run; run --uninstall --purge
if [ ! -e "$MC_PREFIX/data" ]; then check uninstall_purge_removes_data ok; else check uninstall_purge_removes_data bad; fi

exit "$fail"
