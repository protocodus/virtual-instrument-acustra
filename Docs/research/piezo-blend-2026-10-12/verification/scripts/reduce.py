#!/usr/bin/env python3
"""Reduce renders to the sufficient statistics of the superposition model, then delete them
(the shared /tmp filled up). For each mic render with its piezo partner, over the metric window:
E[M^2], E[M P(t-k)], E[P(t-k)^2] for k in LAGS (samples at 48 kHz, k>0 piezo later); M = mono sum.
Stats go to stats/<category>.json (merged), raw .f32 deleted after a successful reduction."""
import json, sys
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"; STUDY = SP / "piezo-study"
LAGS = sorted(set(range(-144, 145, 3)) | {-36, 30, 0})
WIN = {"sweeps": (round(0.08 * 48000), round(1.15 * 48000)), "strings": (round(0.08 * 48000), round(1.15 * 48000)),
       "chords": (round(0.10 * 48000), round(1.50 * 48000))}


def load(p):
    a = np.fromfile(p, dtype="<f4")
    if a.size != 2 * 96000:
        raise ValueError(f"bad size {p} {a.size}")
    return a.reshape(-1, 2).astype(np.float64)


def piezo_for(cat, p):
    rel = p.relative_to(OUT / cat)  # cons/cond/name
    cons, cond, name = rel.parts
    if cat == "sweeps":
        return STUDY / "sweeps" / cons / "piezo" / name
    if cat == "strings":
        return STUDY / "strings" / cons / "piezo" / name
    return OUT / "chords" / cons / "piezo" / name


def main(cat, delete_piezo_cons=()):
    store = OUT / "stats" / f"{cat}.json"; store.parent.mkdir(exist_ok=True)
    data = json.loads(store.read_text()) if store.exists() else {}
    a, b = WIN[cat]
    n = 0
    for p in sorted((OUT / cat).rglob("*.f32")):
        rel = p.relative_to(OUT / cat)
        if rel.parts[1] == "piezo":
            continue
        pz = piezo_for(cat, p)
        if not pz.exists():
            continue
        try:
            m = load(p); q = load(pz)[:, 0]
        except ValueError as e:
            print(e); p.unlink(); continue
        mono = m.mean(axis=1); seg = mono[a:b]
        L, R = m[a:b, 0], m[a:b, 1]
        rec = {"mm": float(np.mean(seg * seg)), "LL": float(np.mean(L * L)), "RR": float(np.mean(R * R)),
               "mp": [], "pp": [], "Lp": [], "Rp": []}
        for k in LAGS:
            ps = q[a - k:b - k]
            rec["mp"].append(float(np.mean(seg * ps))); rec["pp"].append(float(np.mean(ps * ps)))
            rec["Lp"].append(float(np.mean(L * ps))); rec["Rp"].append(float(np.mean(R * ps)))
        data[str(rel)] = rec
        n += 1
        if n % 200 == 0:
            store.write_text(json.dumps(data))
        p.unlink()
    store.write_text(json.dumps(data))
    print(cat, "reduced", n, "total", len(data))

if __name__ == "__main__":
    main(sys.argv[1])
