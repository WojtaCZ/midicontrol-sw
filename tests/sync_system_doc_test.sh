#!/usr/bin/env bash
# P1-25: tools/sync-system-doc.sh --check on a fake MIDICONTROL_ROOT (temp dir; never the real repos).
# Covers: top-level copies, the lib/protocol + lib/bootloader submodule copies (checked, never written),
# and other git worktrees of a repo (reported, not counted as drift).
set -uo pipefail
script="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/tools/sync-system-doc.sh"
root=$(mktemp -d); trap 'rm -rf "$root"' EXIT
fail=0
expect() { if eval "$1"; then echo "ok: $2"; else echo "FAIL: $2"; fail=1; fi; }
mk() { mkdir -p "$(dirname "$1")"; printf '%s\n' "$2" > "$1"; }
mk "$root/midicontrolpy-sw/docs/SYSTEM.md" "system v1"
mk "$root/midicontrol-fw/docs/HARDWARE.md" "base hw v1"
mk "$root/midicontroller-fw/docs/HARDWARE.md" "remote hw v1"
mkdir -p "$root/midicontrol-fw/lib/protocol/docs" "$root/midicontroller-fw/lib/bootloader/docs"
git -C "$root/midicontrolpy-sw" init -q && git -C "$root/midicontrolpy-sw" add -A &&
  git -C "$root/midicontrolpy-sw" -c user.name=t -c user.email=t@t commit -qm init
git -C "$root/midicontrolpy-sw" worktree add -q "$root/midicontrolpy-sw-docker" -b docker 2>/dev/null

# 1. copy mode writes the top-level copies, never a submodule copy
out=$(MIDICONTROL_ROOT="$root" bash "$script" 2>&1)
expect '[[ -f "$root/midicontrol-fw/docs/SYSTEM.md" ]]' "top-level copy written"
expect '[[ ! -f "$root/midicontrol-fw/lib/protocol/docs/SYSTEM.md" ]]' "submodule copy not written"
expect 'grep -q "lib/protocol/docs/SYSTEM.md" <<<"$out"' "copy mode names the stale submodule copy"

# 2. --check: top-level clean, submodule copies missing/old -> drift, exit 1
out=$(MIDICONTROL_ROOT="$root" bash "$script" --check 2>&1); rc=$?
expect '[[ $rc -eq 1 ]]' "check exits 1 on submodule drift (rc=$rc)"
expect 'grep -q "midicontrol-fw/lib/protocol/docs/SYSTEM.md" <<<"$out"' "check names the base protocol submodule copy"
expect 'grep -q "midicontroller-fw/lib/bootloader/docs/HARDWARE-base.md" <<<"$out"' "check names the remote bootloader submodule copy"
expect '! grep -q "DRIFT: midicontrol-fw/docs/SYSTEM.md" <<<"$out"' "top-level copy is clean"

# 3. submodule copies brought up to date -> only the worktree note remains, exit 0
for d in "$root/midicontrol-fw/lib/protocol/docs" "$root/midicontroller-fw/lib/bootloader/docs"; do
  cp "$root/midicontrol-fw/docs/SYSTEM.md" "$d/SYSTEM.md"
  cp "$root/midicontrol-fw/docs/HARDWARE-remote.md" "$d/HARDWARE-remote.md"
  cp "$root/midicontroller-fw/docs/HARDWARE-base.md" "$d/HARDWARE-base.md"
done
mk "$root/midicontrolpy-sw-docker/docs/SYSTEM.md" "system v0 on the docker branch"
out=$(MIDICONTROL_ROOT="$root" bash "$script" --check 2>&1); rc=$?
expect '[[ $rc -eq 0 ]]' "check exits 0 when only a worktree differs (rc=$rc)"
expect 'grep -q "midicontrolpy-sw-docker" <<<"$out"' "check reports the other worktree"
echo "$out" | sed 's/^/  | /'
exit $fail
