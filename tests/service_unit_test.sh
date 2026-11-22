#!/usr/bin/env bash
# B2: a stop must wait for a running firmware update (remote measured 152 s): the unit has TimeoutStopSec >= 180 and the
# compose file stop_grace_period >= 180 s.
set -uo pipefail
unit="$1"; compose="$2"; fail=0
t=$(sed -n 's/^TimeoutStopSec=\([0-9]*\).*/\1/p' "$unit")
if [[ -n "$t" && "$t" -ge 180 ]]; then echo "ok: unit TimeoutStopSec=$t"; else echo "FAIL: unit TimeoutStopSec='$t'"; fail=1; fi
if [[ ! -f "$compose" ]]; then echo "skip: $compose not in this tree (docker build context)"; exit $fail; fi
g=$(sed -n 's/^ *stop_grace_period: *\([0-9]*\)s.*/\1/p' "$compose" | head -1)
if [[ -n "$g" && "$g" -ge 180 ]]; then echo "ok: compose stop_grace_period=${g}s"; else echo "FAIL: compose stop_grace_period='$g'"; fail=1; fi
exit $fail
