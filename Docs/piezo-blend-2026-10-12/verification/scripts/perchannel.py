#!/usr/bin/env python3
"""Mono-sum metric vs each stereo channel alone (Room 0.5 study sweeps, m=0.2)."""
import sys
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(SP / "piezo-verify-methodology" / "scripts"))
import analyse as A  # noqa
S0, S1 = A.S0, A.S1
out = {ch: {v: [] for v in A.VARIANTS} for ch in ("L", "R", "mono", "LRmeanpow")}
names = [ev.note_name(n) for n in range(40, 85)]
named = {}
for c in A.CONS:
    mics = [A.load(A.STUDY / "sweeps" / c / "mic" / f"note-{n}.f32") for n in range(40, 85)]
    pzs = [A.load(A.STUDY / "sweeps" / c / "piezo" / f"note-{n}.f32")[:, 0] for n in range(40, 85)]
    for v, (s, k) in A.VARIANTS.items():
        lv = {"L": [], "R": [], "mono": [], "LRmeanpow": []}
        for m, p in zip(mics, pzs):
            ps = p[S0 - k:S1 - k]
            L = m[S0:S1, 0] + s * 0.2 * ps; R = m[S0:S1, 1] + s * 0.2 * ps
            lv["L"].append(10 * np.log10(np.mean(L * L))); lv["R"].append(10 * np.log10(np.mean(R * R)))
            M = 0.5 * (L + R); lv["mono"].append(10 * np.log10(np.mean(M * M)))
            lv["LRmeanpow"].append(10 * np.log10(0.5 * np.mean(L * L) + 0.5 * np.mean(R * R)))
        for ch in lv:
            d = ev.neighbourhood_deviation(np.array(lv[ch]))
            out[ch][v].append(float(np.sqrt(np.mean(d ** 2))))
            if c in ("parlor-spruce", "jumbo-spruce"):
                n = "F#5" if c == "parlor-spruce" else "C5"
                named[(c, ch, v)] = d[names.index(n)]
for ch in out:
    line = f"{ch:10s} " + " ".join(f"{v} {np.mean(out[ch][v]):.3f}" for v in A.VARIANTS)
    for v in ("V2", "V3c", "V3d"):
        d = np.array(out[ch][v]) - np.array(out[ch]["V1"])
        line += f" | {v}-V1 {(d < -0.05).sum()}/{(d > 0.05).sum()}"
    print(line)
for c in ("parlor-spruce", "jumbo-spruce"):
    for ch in ("L", "R", "mono", "LRmeanpow"):
        print(f"  {c} {ch}: " + " ".join(f"{v} {named[(c, ch, v)]:+.1f}" for v in A.VARIANTS))
