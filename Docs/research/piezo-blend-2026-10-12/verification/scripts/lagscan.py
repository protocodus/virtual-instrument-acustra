#!/usr/bin/env python3
"""Mean sweep RMS dev against lag, both polarities, per condition (m=0.2), and the
optimism check: per-string RMS dev over notes NOT in the E2-C6 sweep only."""
import sys
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(OUT / "scripts"))
import analyse as A  # noqa
A.LAGS = list(range(-144, 145, 3))
A.LAGS = sorted(set(A.LAGS) | {-36, 30, 0})
for cond in ("mic_r05", "mic_r0", "mic_r1", "mono_r05", "mono_r0"):
    sts = [A.stats(*A.sweep_paths(c, cond)) for c in A.CONS]
    v0 = np.mean([A.rmsdev([A.levels(st, 0, 0.2, 0)])[0] for st in sts])
    res = {}
    for s in (+1, -1):
        for k in A.LAGS:
            res[(s, k)] = np.mean([A.rmsdev([A.levels(st, s, 0.2, k)])[0] for st in sts])
    bp = min((k for k in A.LAGS), key=lambda k: res[(1, k)]); bm = min((k for k in A.LAGS), key=lambda k: res[(-1, k)])
    print(f"[{cond}] V0 {v0:.3f} V1 {res[(1,0)]:.3f} V2 {res[(-1,0)]:.3f} V3c {res[(1,-36)]:.3f} V3d {res[(-1,30)]:.3f} | best + {res[(1,bp)]:.3f} at {bp/48:+.3f} ms; best - {res[(-1,bm)]:.3f} at {bm/48:+.3f} ms")
    print("   -pol lag(ms):rms " + " ".join(f"{k/48:+.2f}:{res[(-1,k)]:.2f}" for k in A.LAGS if -48 <= k <= 72 and k % 6 == 0))
# optimism: per-string, notes outside the sweep's (string, midi) set
sweep_set = {(ev.lowest_fret_string(m), m) for m in range(40, 85)}
print("\nPer-string RMS dev restricted to the 81 (string, fret) notes not rendered in the E2-C6 sweep (Room 0.5 stereo):")
A.LAGS = [0, -36, 30]
tab = {v: [] for v in A.VARIANTS}
for c in A.CONS:
    devs = {v: [] for v in A.VARIANTS}
    for s in range(1, 7):
        st = A.stats(*A.string_paths(c, "mic_r05", s))
        notes = [ev.OPEN_NOTES[s - 1] + f for f in range(21)]
        keep = np.array([(s, n) not in sweep_set for n in notes])
        for v, (sg, k) in A.VARIANTS.items():
            d = ev.neighbourhood_deviation(A.levels(st, sg, 0.2, k))
            devs[v].append(d[keep])
    for v in A.VARIANTS:
        tab[v].append(float(np.sqrt(np.mean(np.concatenate(devs[v]) ** 2))))
print("  mean " + "  ".join(f"{v} {np.mean(tab[v]):.3f}" for v in A.VARIANTS))
for v in ("V2", "V3c", "V3d"):
    d = np.array(tab[v]) - np.array(tab["V1"])
    print(f"  {v}-V1 better/worse {(d < -0.05).sum()}/{(d > 0.05).sum()}, worst {d.max():+.2f} ({A.CONS[int(d.argmax())]})")
