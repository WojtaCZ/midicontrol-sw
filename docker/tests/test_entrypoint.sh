#!/bin/sh
# Tests for docker/entrypoint.sh; runs on the host with MC_DATA_DIR/MC_BIN overrides.
here=$(cd "$(dirname "$0")" && pwd)
ep="$here/../entrypoint.sh"
cfg="$here/../config.container.conf"
fail=0
check() { if [ "$2" = ok ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi; }
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

# Stub binary: prints its arguments and the umask it runs under.
stub="$tmp/stub.sh"
printf '#!/bin/sh\necho "ARGS:$*"\necho "UMASK:$(umask)"\n' > "$stub"
chmod +x "$stub"

# seeds_config_when_missing
d="$tmp/a"; mkdir -p "$d"
MC_DATA_DIR="$d" MC_SEED_CONFIG="$cfg" MC_BIN="$stub" sh "$ep" >/dev/null
r=ok
[ -f "$d/config.conf" ] || r=bad
grep -q 'PlayFolder = /data/songs' "$d/config.conf" 2>/dev/null || r=bad
grep -q 'RecordFolder = /data/recordings' "$d/config.conf" 2>/dev/null || r=bad
grep -q 'Dir = /data/diag' "$d/config.conf" 2>/dev/null || r=bad
for s in songs recordings diag firmware; do [ -d "$d/$s" ] || r=bad; done
check seeds_config_when_missing $r

# keeps_existing_config
d="$tmp/b"; mkdir -p "$d"; echo "# marker" > "$d/config.conf"
MC_DATA_DIR="$d" MC_SEED_CONFIG="$cfg" MC_BIN="$stub" sh "$ep" >/dev/null
r=ok; [ "$(cat "$d/config.conf")" = "# marker" ] || r=bad
check keeps_existing_config $r

# passes_through_other_command
d="$tmp/c"; mkdir -p "$d"
out=$(MC_DATA_DIR="$d" MC_SEED_CONFIG="$cfg" sh "$ep" echo hi 2>&1)
r=ok; [ "$out" = hi ] || r=bad
check passes_through_other_command $r

# default_command_and_umask
d="$tmp/d"; mkdir -p "$d"
out=$(MC_DATA_DIR="$d" MC_SEED_CONFIG="$cfg" MC_BIN="$stub" sh "$ep")
r=ok
echo "$out" | grep -qx "ARGS:--headless --exit-on-stall --config $d/config.conf" || r=bad
echo "$out" | grep -qx "UMASK:0002" || r=bad
[ "$r" = ok ] || echo "$out"
check default_command_and_umask $r

exit $fail
