#!/usr/bin/env python3
"""Evaluate criteria.toml against a run.

usage: criteria.py <rundir> [--toml criteria.toml] [--require play,reset,...]   reads <rundir>/latency.jsonl and/or <rundir>/summary.json,
                                                      writes <rundir>/criteria.md + criteria.json, exit 1 on a hard fail
       criteria.py --selftest                         offline check of the evaluator
The toml's sha256 is recorded in the result so a report can name exactly which criteria were applied.
"""
import hashlib, json, os, statistics, sys, tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
OPS = {"<": lambda a, b: a < b, "<=": lambda a, b: a <= b, "==": lambda a, b: a == b, ">=": lambda a, b: a >= b,
       ">": lambda a, b: a > b}


def stat(vals, how):
    v = sorted(x for x in vals if isinstance(x, (int, float)) and not isinstance(x, bool))
    if how == "n":
        return len(v)
    if not v:
        return None
    if how == "max":
        return v[-1]
    if how == "min":
        return v[0]
    if how == "median":
        return statistics.median(v)
    if how == "p95":
        return v[min(len(v) - 1, int(round(0.95 * (len(v) - 1))))]
    if how == "value":
        return v[0]
    raise ValueError(how)


def evaluate(crit, parts, required=()):
    """parts: {part: [records]} (burn-in summary = one record). -> list of result dicts."""
    out = []
    for c in crit:
        recs = parts.get(c["part"])
        if recs is None:
            continue  # part not in this run
        vals = [r.get(c["metric"]) for r in recs]
        # a trial that timed out (metric None/absent) must not silently drop out of max/p95
        missing = 0 if c["stat"] == "n" else sum(
            1 for x in vals if not isinstance(x, (int, float)) or isinstance(x, bool))
        val = stat(vals, c["stat"])
        ok = None if val is None else OPS[c["op"]](val, c["limit"])
        res = "NO DATA" if ok is None else ("PASS" if ok else "FAIL")
        if missing and res == "PASS":
            res = "FAIL"
        out.append({"id": c["id"], "metric": f'{c["part"]}.{c["metric"]}', "stat": c["stat"], "value": val,
                    "missing": missing, "op": c["op"], "limit": c["limit"], "hard": c.get("hard", False),
                    "result": res, "src": c["src"]})
    for part in sorted(parts):  # implicit hard criterion per part: no trial ended with an error
        errs = [r.get("i") for r in parts[part] if "err" in r]
        out.append({"id": f"ERR-{part}", "metric": f"{part}.err", "stat": "n", "value": len(errs), "missing": 0,
                    "op": "==", "limit": 0, "hard": True, "result": "FAIL" if errs else "PASS",
                    "src": f"implicit: trials with err {errs}"[:200]})
    for part in required:
        if part not in parts:
            out.append({"id": f"REQ-{part}", "metric": part, "stat": "n", "value": 0, "missing": 0, "op": ">",
                        "limit": 0, "hard": True, "result": "NO DATA", "src": "required part missing from the run"})
    return out


def load(rundir):
    parts = {}
    p = os.path.join(rundir, "latency.jsonl")
    if os.path.exists(p):
        for line in open(p):
            r = json.loads(line)
            parts.setdefault(r["part"], []).append(r)
    p = os.path.join(rundir, "summary.json")
    if os.path.exists(p):
        parts["burnin"] = [json.load(open(p))]
    return parts


def run(rundir, toml, required=()):
    raw = open(toml, "rb").read()
    crit = tomllib.loads(raw.decode())["c"]
    res = evaluate(crit, load(rundir), required)
    sha = hashlib.sha256(raw).hexdigest()
    hard_fail = [r for r in res if r["hard"] and r["result"] in ("FAIL", "NO DATA")]
    json.dump({"criteria_sha256": sha, "results": res, "hard_fail": len(hard_fail)},
              open(os.path.join(rundir, "criteria.json"), "w"), indent=1)
    lines = [f"# Criteria ({os.path.basename(toml)} sha256 {sha[:16]})\n",
             "| id | metric | stat | value | missing | limit | hard | result | source |", "|---|---|---|---|---|---|---|---|---|"]
    for r in res:
        lines.append(f'| {r["id"]} | {r["metric"]} | {r["stat"]} | {r["value"]} | {r["missing"]} | {r["op"]} {r["limit"]} | '
                     f'{"yes" if r["hard"] else "no"} | {r["result"]} | {r["src"]} |')
    lines.append(f"\nHard failures: {len(hard_fail)}")
    open(os.path.join(rundir, "criteria.md"), "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 1 if hard_fail else 0


def selftest():
    crit = tomllib.loads(open(os.path.join(HERE, "criteria.toml")).read())["c"]
    ids = [c["id"] for c in crit]
    assert len(ids) == len(set(ids)), "duplicate ids"
    for c in crit:
        assert c["op"] in OPS and c["stat"] in ("n", "max", "min", "median", "p95", "value"), c
    good = {"play": [{"key_to_play_song_ms": 80 + i, "warmup_ms": 3002, "playing_to_first_note_ms": 2,
                      "playing_to_base_screen_ms": 20, "playing_to_remote_frame_ms": 130, "key_to_stopped_ms": 90,
                      "stopped_to_base_main_ms": 1520, "notes_on_after_stop": 0, "fwd_err": 0} for i in range(30)]}
    r = {x["id"]: x["result"] for x in evaluate(crit, good)}
    assert all(v == "PASS" for v in r.values()), r
    assert "BURN-1" not in r, "parts absent from the run are skipped"
    assert r["ERR-play"] == "PASS"
    r = {x["id"]: x["result"] for x in evaluate(crit, good, ("play", "reset"))}
    assert r["REQ-reset"] == "NO DATA" and "REQ-play" not in r, r
    tmo = {"play": good["play"][:29] + [dict(good["play"][0], key_to_play_song_ms=None, err="x")]}
    r = {x["id"]: x["result"] for x in evaluate(crit, tmo)}
    assert r["LAT-1"] == "FAIL" and r["ERR-play"] == "FAIL", r
    bad = {"play": [dict(good["play"][0], notes_on_after_stop=1, key_to_play_song_ms=1600)]}
    r = {x["id"]: x["result"] for x in evaluate(crit, bad)}
    assert r["LAT-10"] == "FAIL" and r["LAT-1"] == "FAIL" and r["LAT-16"] == "FAIL", r
    r = {x["id"]: x["result"] for x in evaluate(crit, {"play": [{"warmup_ms": None}]})}
    assert r["LAT-3"] == "NO DATA"
    assert stat([True, 1, 2], "max") == 2, "bools are not numbers"
    assert stat(list(range(21)), "p95") == 19
    print("criteria selftest OK:", len(crit), "criteria")


if __name__ == "__main__":
    if sys.argv[1:] == ["--selftest"]:
        selftest(); sys.exit(0)
    a = sys.argv[1:]
    toml = os.path.join(HERE, "criteria.toml")
    if "--toml" in a:
        i = a.index("--toml"); toml = a[i + 1]; del a[i:i + 2]
    req = ()
    if "--require" in a:
        i = a.index("--require"); req = tuple(a[i + 1].split(",")); del a[i:i + 2]
    if len(a) != 1:
        print(__doc__); sys.exit(2)
    sys.exit(run(a[0], toml, req))
