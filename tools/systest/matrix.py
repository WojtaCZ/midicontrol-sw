#!/usr/bin/env python3
"""List @pytest.mark.req IDs per test; --check fails if a marker names an ID absent from audit/test-matrix.md.

HARNESS-* IDs are harness self-checks and are ignored by --check."""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MATRIX = Path(sys.argv[sys.argv.index("--matrix") + 1]) if "--matrix" in sys.argv else HERE.parents[1] / "audit" / "test-matrix.md"
if not MATRIX.exists():
    MATRIX = Path("/workspaces/MIDICONTROL-b81bba4a/audit/test-matrix.md")


def markers():
    rows = []
    for f in sorted(HERE.glob("test_*.py")):
        pending = []
        for line in f.read_text().splitlines():
            m = re.match(r'\s*@pytest\.mark\.req\("([^"]+)"\)', line)
            if m:
                pending.append(m.group(1))
                continue
            d = re.match(r"\s*def (test_\w+)", line)
            if d:
                rows += [(i, f"{f.name}::{d.group(1)}") for i in pending]
                pending = []
    return rows


def main():
    rows = markers()
    known = set(re.findall(r"\b(SYS-[A-Z]+-\d+)\b", MATRIX.read_text()))
    if "--check" in sys.argv:
        missing = sorted({i for i, _ in rows if not i.startswith("HARNESS-") and i not in known})
        for i in missing:
            print(f"unknown requirement ID in marker: {i}")
        print(f"{len(rows)} markers, {len(missing)} unknown")
        return 1 if missing else 0
    for i, t in rows:
        print(f"{i}\t{t}")
    return 0


sys.exit(main())
