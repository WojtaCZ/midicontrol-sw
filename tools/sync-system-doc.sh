#!/usr/bin/env bash
# Copy the shared system documents from their main copies into every MIDIcontrol repo.
#   docs/SYSTEM.md            main copy: midicontrol-sw/docs/SYSTEM.md
#   docs/HARDWARE-base.md     main copy: midicontrol-fw/docs/HARDWARE.md
#   docs/HARDWARE-remote.md   main copy: midicontroller-fw/docs/HARDWARE.md
# The repos are expected side by side in one parent directory (override with MIDICONTROL_ROOT).
# Usage: tools/sync-system-doc.sh          copy, overwriting the copies
#        tools/sync-system-doc.sh --check  only report copies that differ (exit 1 on drift)
# The firmware repos also carry copies inside their lib/protocol and lib/bootloader submodules. This script never
# writes into a submodule: a stale submodule copy is reported (and counts as drift in --check); update it in the
# submodule's own repo and bump the pointer. Other git worktrees of a repo (e.g. a feature branch checked out next
# to it) are reported as a NOTE only: they follow their branch and do not count as drift.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${MIDICONTROL_ROOT:-$(dirname "$here")}"
check=0; [[ "${1:-}" == "--check" ]] && check=1

# source-repo:source-path  target-name
docs=(
  "midicontrol-sw:docs/SYSTEM.md SYSTEM.md"
  "midicontrol-fw:docs/HARDWARE.md HARDWARE-base.md"
  "midicontroller-fw:docs/HARDWARE.md HARDWARE-remote.md"
)
repos=(midicontrol-sw midicontrol-fw midicontroller-fw midicontrol-bootloader midicontrol-protocol midicontrol-hw midicontroller-hw)
fw_repos=(midicontrol-fw midicontroller-fw)
submodules=(lib/protocol lib/bootloader)

# Other worktrees of a repo (absolute paths, one per line), the repo's own checkout excluded.
other_worktrees() {
  local self; self=$(cd "$root/$1" && pwd -P)
  git -C "$root/$1" worktree list --porcelain 2>/dev/null | sed -n 's/^worktree //p' |
    while IFS= read -r wt; do [[ "$(cd "$wt" 2>/dev/null && pwd -P)" == "$self" ]] || echo "$wt"; done || true
}
show() { [[ "$1" == "$root"/* ]] && echo "${1#"$root"/}" || echo "$1"; }

drift=0
for entry in "${docs[@]}"; do
  src_spec=${entry% *}; name=${entry#* }
  src_repo=${src_spec%%:*}; src_path=${src_spec#*:}
  src="$root/$src_repo/$src_path"
  [[ -f "$src" ]] || { echo "missing main copy: $src" >&2; exit 2; }
  expected=$(mktemp)
  { echo "<!-- COPY of $src_repo/$src_path. Do not edit here: edit the main copy, then run midicontrol-sw/tools/sync-system-doc.sh -->"; echo; cat "$src"; } > "$expected"
  for repo in "${repos[@]}"; do
    [[ "$repo" == "$src_repo" ]] && continue
    [[ -d "$root/$repo" ]] || { echo "skip (not present): $repo"; continue; }
    dst="$root/$repo/docs/$name"
    tmp=$(mktemp); cp "$expected" "$tmp"
    if cmp -s "$tmp" "$dst" 2>/dev/null; then rm -f "$tmp"; continue; fi
    if (( check )); then echo "DRIFT: $repo/docs/$name"; drift=1; rm -f "$tmp"
    else mkdir -p "$(dirname "$dst")"; mv "$tmp" "$dst"; echo "updated: $repo/docs/$name"; fi
  done
  # Copies inside the firmware repos' submodules: checked, never written.
  for repo in "${fw_repos[@]}"; do
    for sub in "${submodules[@]}"; do
      [[ -d "$root/$repo/$sub" ]] || continue
      dst="$root/$repo/$sub/docs/$name"
      cmp -s "$expected" "$dst" 2>/dev/null && continue
      echo "STALE SUBMODULE COPY: $repo/$sub/docs/$name (not written here: update it in the submodule's repo, then bump the pointer in $repo)"
      (( check )) && drift=1
    done
  done
  # Other worktrees: reported, not counted.
  for repo in "${repos[@]}"; do
    [[ -d "$root/$repo" ]] || continue
    while IFS= read -r wt; do
      [[ -n "$wt" ]] || continue
      if [[ "$repo" == "$src_repo" ]]; then want="$src"; f="$wt/$src_path"; else want="$expected"; f="$wt/docs/$name"; fi
      cmp -s "$want" "$f" 2>/dev/null && continue
      echo "NOTE (other worktree, not counted): $(show "$f") differs (it follows its own branch)"
    done < <(other_worktrees "$repo")
  done
  rm -f "$expected"
done
exit $drift
