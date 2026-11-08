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
# The template with the default prefix replaced by this world's MC_PREFIX.
expected_compose() { sed "s|/opt/midicontrol|$MC_PREFIX|g" "$root/docker/compose.template.yaml" > "$w/expected.yaml"; echo "$w/expected.yaml"; }

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
   && cmp -s "$MC_PREFIX/compose.yaml" "$(expected_compose)" \
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

# custom_prefix_is_honoured_everywhere (final review I1): bind mounts and the updater's
# MC_PREFIX/MC_DATA_DIR all follow MC_PREFIX, no hard-coded default path is left.
world
run; rc=$?
c="$MC_PREFIX/compose.yaml"
ok=bad
if [ "$rc" = 0 ] && ! grep -q '/opt/midicontrol' "$c" \
   && has "^      - $MC_PREFIX/data:/data\$" "$c" \
   && has "^      - $MC_PREFIX:$MC_PREFIX\$" "$c" \
   && has "^      MC_PREFIX: $MC_PREFIX\$" "$c" \
   && has "^      MC_DATA_DIR: $MC_PREFIX/data\$" "$c" \
   && has "^    working_dir: $MC_PREFIX\$" "$c"; then ok=ok; fi
[ "$ok" = ok ] || cat "$c"
check custom_prefix_is_honoured_everywhere "$ok"

# prefix characters that could break the YAML or the sed expression are refused
bad=ok
for p in "$tmp/a b" "$tmp/a|b" "$tmp/a&b" "$tmp/a:b"; do
    world
    MC_PREFIX=$p run; rc=$?
    [ "$rc" = 1 ] && has "MC_PREFIX" "$w/out" && [ ! -e "$p/compose.yaml" ] || { bad=bad; echo "  prefix '$p' rc=$rc"; }
done
check prefix_with_unsafe_characters_refused "$bad"

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
   && cmp -s "$MC_PREFIX/compose.yaml" "$(expected_compose)" \
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

# --no-updates on an existing .env: file untouched, note printed
world
run
printf 'IMAGE_TAG=stable\nUPDATE_WINDOW=02:00-04:00\nUPDATE_ENABLED=1\n# M\n' > "$MC_PREFIX/.env"
cp "$MC_PREFIX/.env" "$w/env.before"
run --no-updates
if cmp -s "$w/env.before" "$MC_PREFIX/.env" && has "ignored" "$w/out" && has "UPDATE_ENABLED" "$w/out"; then
    check no_updates_on_existing_env_untouched ok; else check no_updates_on_existing_env_untouched bad; fi

# bad tags and a missing value are rejected before anything is written
world
bad=ok
# shellcheck disable=SC2016 # '$(x)' must stay literal
for t in 'a b' '.x' '-x' 'a;b' '$(x)'; do
    run --tag "$t"; rc=$?
    { [ "$rc" != 0 ] && [ ! -e "$MC_PREFIX/.env" ]; } || bad=bad
done
if run --tag --no-updates; then bad=bad; fi
if run --tag; then bad=bad; fi
check bad_tag_rejected "$bad"
world
run --tag v1.2.3-rc_1
if has '^IMAGE_TAG=v1.2.3-rc_1$' "$MC_PREFIX/.env"; then check good_tag_accepted ok; else check good_tag_accepted bad; fi

# unsafe prefixes never reach rm -rf
world
bad=ok
mkdir -p "$w/cwd/relative/dir/data"
for p in / relative/dir // /tmp/../x; do
    ( cd "$w/cwd" && MC_PREFIX=$p sh "$root/install.sh" --uninstall --purge > "$w/out" 2>&1 ); rc=$?
    { [ "$rc" != 0 ] && [ -d "$w/cwd/relative/dir/data" ]; } || bad=bad
done
check unsafe_prefix_refused "$bad"

# daemon down / no compose plugin / unsupported arch: one line, exit 1, nothing written
world
touch "$FAKE_DIR/info_fail"
run; rc=$?
if [ "$rc" = 1 ] && has "daemon" "$w/out" && [ ! -e "$MC_PREFIX/compose.yaml" ]; then
    check fails_when_daemon_down ok; else check fails_when_daemon_down bad; fi
world
touch "$FAKE_DIR/compose_missing"
run; rc=$?
if [ "$rc" = 1 ] && has "Compose" "$w/out" && [ ! -e "$MC_PREFIX/compose.yaml" ]; then
    check fails_without_compose_plugin ok; else check fails_without_compose_plugin bad; fi
world
mkdir "$w/fb"
realuname=$(command -v uname)
# shellcheck disable=SC2016 # script text, expanded when the fake runs
printf '#!/bin/sh\n[ "$1" = -m ] && { echo armv7l; exit 0; }\nexec %s "$@"\n' "$realuname" > "$w/fb/uname"
chmod +x "$w/fb/uname"
PATH="$w/fb:$PATH" run; rc=$?
if [ "$rc" = 1 ] && has "armv7l" "$w/out" && [ ! -e "$MC_PREFIX/compose.yaml" ]; then
    check fails_on_unsupported_arch ok; else check fails_on_unsupported_arch bad; fi

# status page never answers: an honest message, no "is running" claim (exit stays 0:
# the containers were started, only the check timed out)
world
mkdir "$w/fb"
realcurl=$(command -v curl)
cat > "$w/fb/curl" <<CURLEOF
#!/bin/sh
case "\$*" in
    *localhost:50118/api/status*) exit 7 ;;
esac
exec $realcurl "\$@"
CURLEOF
chmod +x "$w/fb/curl"
MC_WAIT_SECONDS=2 PATH="$w/fb:$PATH" run; rc=$?
if [ "$rc" = 0 ] && ! has "MIDIControl is running" "$w/out" && has "did not answer within 2 s" "$w/out" \
   && has "docker logs midicontrol" "$w/out" && has "midicontrol-updater --once" "$w/out"; then
    check status_timeout_is_reported_honestly ok; else cat "$w/out"; check status_timeout_is_reported_honestly bad; fi
# MC_WAIT_SECONDS=0 (not waiting) also makes no running claim
world
run
if ! has "MIDIControl is running" "$w/out" && has "not waiting" "$w/out"; then
    check no_wait_makes_no_running_claim ok; else check no_wait_makes_no_running_claim bad; fi

# version comes from the software node of a realistic multi-node /api/status
world
mkdir "$w/fb"
realcurl=$(command -v curl)
cat > "$w/fb/curl" <<CURLEOF
#!/bin/sh
case "\$*" in
    *localhost:50118/api/status*) [ -f "$FAKE_DIR/status_json" ] && { cat "$FAKE_DIR/status_json"; exit 0; }; exit 7 ;;
esac
exec $realcurl "\$@"
CURLEOF
chmod +x "$w/fb/curl"
printf '%s\n' '{"schema":2,"now_ms":1,"nodes":{"base":{"dot":"green","fields":{"fw_version":"1.2.0","version":"BASE-9.9"},"counters":{},"events":[],"last_fault":null,"unseen_faults":0},"controller":{"dot":"grey","fields":{"version":"CTRL-8.8"},"counters":{},"events":[],"last_fault":null,"unseen_faults":0},"software":{"dot":"green","fields":{"activity":"idle","version":"abc1234"},"counters":{"rx":5},"events":[{"t":1,"level":"info","text":"\"version\":\"EVIL\""}],"last_fault":null,"unseen_faults":0}},"problems":[],"songs":[]}' > "$FAKE_DIR/status_json"
MC_WAIT_SECONDS=3 PATH="$w/fb:$PATH" run; rc=$?
if [ "$rc" = 0 ] && has "MIDIControl is running" "$w/out" && has "Version: *abc1234$" "$w/out"; then
    check version_from_software_node ok; else check version_from_software_node bad; fi

exit "$fail"
