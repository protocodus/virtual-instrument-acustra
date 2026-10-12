#!/usr/bin/env python3
"""Does the variant move unevenness from sustained level into attack or timbre?
Full MeasureNoteEvenness.note_metrics on each variant's mono sum (Room 0.5 study renders),
neighbourhood deviation RMS of: rms_dbfs, attack_dbfs (5-80 ms), band shares, H2/H1.
"""
import sys
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(SP / "piezo-verify-methodology" / "scripts"))
from analyse import CONS, VARIANTS, STUDY, load  # noqa


def shifted(x, k):
    o = np.zeros_like(x)
    if k > 0: o[k:] = x[:-k]
    elif k < 0: o[:k] = x[-k:]
    else: o[:] = x
    return o

KEYS = ("rms_dbfs", "attack_dbfs", "peak_dbfs", "bass_share_db", "lowmid_share_db", "pres_share_db", "treb_share_db", "h2_over_h1_db")
acc = {v: {k: [] for k in KEYS} for v in VARIANTS}
for cons in CONS:
    mic = {n: load(STUDY / "sweeps" / cons / "mic" / f"note-{n}.f32").mean(axis=1) for n in range(40, 85)}
    pz = {n: load(STUDY / "sweeps" / cons / "piezo" / f"note-{n}.f32")[:, 0] for n in range(40, 85)}
    row = []
    for v, (s, k) in VARIANTS.items():
        rows = [ev.note_metrics(mic[n] + s * 0.2 * shifted(pz[n], k), n) for n in range(40, 85)]
        vals = []
        for key in KEYS:
            d = ev.neighbourhood_deviation(np.array([r[key] for r in rows]))
            r = float(np.sqrt(np.mean(d ** 2)))
            acc[v][key].append(r); vals.append(r)
        row.append(vals)
print("Mean over 12 constructions of neighbourhood-deviation RMS (dB), E2-C6 Finger v96 Room 0.5, m=0.2")
print(f"{'metric':18s} " + " ".join(f"{v:>6s}" for v in VARIANTS) + "   V3d better/worse than V1 (>0.05)  V2 b/w")
for key in KEYS:
    m = [np.mean(acc[v][key]) for v in VARIANTS]
    d3 = np.array(acc["V3d"][key]) - np.array(acc["V1"][key])
    d2 = np.array(acc["V2"][key]) - np.array(acc["V1"][key])
    print(f"{key:18s} " + " ".join(f"{x:6.2f}" for x in m) + f"   {int((d3 < -0.05).sum())}/{int((d3 > 0.05).sum())}  worst {d3.max():+.2f} ({CONS[int(d3.argmax())]})   {int((d2 < -0.05).sum())}/{int((d2 > 0.05).sum())}")
