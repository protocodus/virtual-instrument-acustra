#!/usr/bin/env python3
"""Remaining renders, reduced on the fly (stats per file in stats/parts), raw deleted."""
import json, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
OUT = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad/piezo-verify-methodology")
sys.path.insert(0, str(OUT / "scripts"))
import render as R  # noqa
import reduce as RD  # noqa

done_stats = {cat: set(json.loads((OUT / "stats" / f"{cat}.json").read_text()).keys()) for cat in ("sweeps", "strings", "chords")}
PARTS = OUT / "stats" / "parts"


def reduce_one(cat, o):
    rel = o.relative_to(OUT / cat)
    a, b = RD.WIN[cat]
    m = RD.load(o); q = RD.load(RD.piezo_for(cat, o))[:, 0]
    mono = m.mean(axis=1); seg = mono[a:b]; L, Rr = m[a:b, 0], m[a:b, 1]
    rec = {"mm": float(np.mean(seg * seg)), "LL": float(np.mean(L * L)), "RR": float(np.mean(Rr * Rr)), "mp": [], "pp": [], "Lp": [], "Rp": []}
    for k in RD.LAGS:
        ps = q[a - k:b - k]
        rec["mp"].append(float(np.mean(seg * ps))); rec["pp"].append(float(np.mean(ps * ps)))
        rec["Lp"].append(float(np.mean(L * ps))); rec["Rp"].append(float(np.mean(Rr * ps)))
    part = PARTS / cat / (str(rel).replace("/", "__") + ".json")
    part.parent.mkdir(parents=True, exist_ok=True)
    part.write_text(json.dumps({str(rel): rec}))
    o.unlink()


def run(job):
    p, o, opts = job
    cat = o.relative_to(OUT).parts[0]
    rel = str(o.relative_to(OUT / cat))
    if cat in done_stats and rel in done_stats[cat]:
        return
    part = PARTS / cat / (rel.replace("/", "__") + ".json")
    if part.exists():
        return
    if not o.exists():
        tmp = o.with_name(o.name + ".part")
        o.parent.mkdir(parents=True, exist_ok=True)
        if tmp.exists(): tmp.unlink()
        r = subprocess.run([str(R.RENDERER), str(p), str(tmp), *opts], capture_output=True, text=True)
        if r.returncode != 0:
            print("FAIL", o, r.stderr[-200:], flush=True); return
        tmp.rename(o)
    if cat in ("sweeps", "strings") or (cat == "chords" and o.parent.name != "piezo"):
        reduce_one(cat, o)


if __name__ == "__main__":
    jobs = list(R.jobs())
    chord_piezo = [j for j in jobs if j[1].relative_to(OUT).parts[0] == "chords" and j[1].parent.name == "piezo"]
    rest = [j for j in jobs if j not in chord_piezo and j[1].relative_to(OUT).parts[0] != "phrases"]
    phr = [j for j in jobs if j[1].relative_to(OUT).parts[0] == "phrases" and not j[1].exists()]
    order = {"chords": 0, "strings": 1, "sweeps": 2}
    rest.sort(key=lambda j: order[j[1].relative_to(OUT).parts[0]])
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(run, chord_piezo))
        print("chord piezo done", flush=True)
        list(pool.map(run, rest))
        print("rest done", flush=True)
        # phrases only those missing from stats/strums.json
        have = set(json.loads((OUT / "stats" / "strums.json").read_text()).keys())
        phr = [j for j in phr if f"{j[1].name.split('-strums-')[0]}|{j[1].name.split('-strums-')[1][:-4]}" not in have]
        list(pool.map(lambda j: subprocess.run([str(R.RENDERER), str(j[0]), str(j[1]), *j[2]], capture_output=True), phr))
    print("all done", flush=True)
