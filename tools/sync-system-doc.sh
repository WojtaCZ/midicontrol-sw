#!/usr/bin/env bash
# Copy the shared system documents from their main copies into every MIDIcontrol repo.
#   docs/SYSTEM.md            main copy: midicontrolpy-sw/docs/SYSTEM.md
#   docs/HARDWARE-base.md     main copy: midicontrol-fw/docs/HARDWARE.md
#   docs/HARDWARE-remote.md   main copy: midicontroller-fw/docs/HARDWARE.md
# The repos are expected side by side in one parent directory (override with MIDICONTROL_ROOT).
# Usage: tools/sync-system-doc.sh          copy, overwriting the copies
#        tools/sync-system-doc.sh --check  only report copies that differ (exit 1 on drift)
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
root="${MIDICONTROL_ROOT:-$(dirname "$here")}"
check=0; [[ "${1:-}" == "--check" ]] && check=1

# source-repo:source-path  target-name
docs=(
  "midicontrolpy-sw:docs/SYSTEM.md SYSTEM.md"
  "midicontrol-fw:docs/HARDWARE.md HARDWARE-base.md"
  "midicontroller-fw:docs/HARDWARE.md HARDWARE-remote.md"
)
repos=(midicontrolpy-sw midicontrol-fw midicontroller-fw midicontrol-bootloader midicontrol-protocol midicontrol-hw midicontroller-hw)

drift=0
for entry in "${docs[@]}"; do
  src_spec=${entry% *}; name=${entry#* }
  src_repo=${src_spec%%:*}; src_path=${src_spec#*:}
  src="$root/$src_repo/$src_path"
  [[ -f "$src" ]] || { echo "missing main copy: $src" >&2; exit 2; }
  for repo in "${repos[@]}"; do
    [[ "$repo" == "$src_repo" ]] && continue
    [[ -d "$root/$repo" ]] || { echo "skip (not present): $repo"; continue; }
    dst="$root/$repo/docs/$name"
    tmp=$(mktemp)
    { echo "<!-- COPY of $src_repo/$src_path. Do not edit here: edit the main copy, then run midicontrolpy-sw/tools/sync-system-doc.sh -->"; echo; cat "$src"; } > "$tmp"
    if cmp -s "$tmp" "$dst" 2>/dev/null; then rm -f "$tmp"; continue; fi
    if (( check )); then echo "DRIFT: $repo/docs/$name"; drift=1; rm -f "$tmp"
    else mkdir -p "$(dirname "$dst")"; mv "$tmp" "$dst"; echo "updated: $repo/docs/$name"; fi
  done
done
exit $drift
