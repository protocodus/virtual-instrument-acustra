#!/usr/bin/env python3
"""Attack of V3d vs V1 (and V2): sample peak and first-5 ms energy from the common onset,
study's Pick v120 notes and Finger sweep notes, all 12 constructions (Room 0.5)."""
import sys
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
sys.path.insert(0, str(SP / "piezo-verify-methodology" / "scripts"))
import analyse as A  # noqa


def shifted(x, k):
    o = np.zeros_like(x)
    if k > 0: o[k:] = x[:-k]
    elif k < 0: o[:k] = x[-k:]
    else: o[:] = x
    return o

rows = []
for c in A.CONS:
    items = [(A.STUDY / "picks" / f"{c}-{m}-mic.f32", A.STUDY / "picks" / f"{c}-{m}-piezo.f32", f"pick{m}") for m in (40, 55, 60, 64, 66)]
    items += [(A.STUDY / "sweeps" / c / "mic" / f"note-{m}.f32", A.STUDY / "sweeps" / c / "piezo" / f"note-{m}.f32", f"fing{m}") for m in (40, 45, 50, 55, 59, 64, 72, 78)]
    for mp, pp, lab in items:
        mic = A.load(mp); pz = A.load(pp)[:, 0]
        v1 = mic + 0.2 * pz[:, None]
        thr = 1e-3 * np.abs(v1).max()
        on = int(min(np.argmax(np.abs(mic).max(axis=1) > thr), np.argmax(np.abs(0.2 * pz) > thr)))
        out = {}
        for v, (s, k) in A.VARIANTS.items():
            x = mic + (s * 0.2 * shifted(pz, k))[:, None]
            seg = x[on:on + 240]
            out[v] = (20 * np.log10(np.abs(x).max()), 10 * np.log10(np.mean(seg ** 2)))
        rows.append((c, lab, out))
for v in ("V2", "V3c", "V3d"):
    dp = np.array([r[2][v][0] - r[2]["V1"][0] for r in rows]); d5 = np.array([r[2][v][1] - r[2]["V1"][1] for r in rows])
    d5v0 = np.array([r[2][v][1] - r[2]["V0"][1] for r in rows])
    print(f"{v}-V1 over {len(rows)} notes: peak mean {dp.mean():+.2f} range {dp.min():+.2f}..{dp.max():+.2f} dB; first 5 ms mean {d5.mean():+.2f} range {d5.min():+.2f}..{d5.max():+.2f} dB; first 5 ms vs V0 mean {d5v0.mean():+.2f}")
d5 = np.array([r[2]["V1"][1] - r[2]["V0"][1] for r in rows]); print(f"V1-V0 first 5 ms mean {d5.mean():+.2f} range {d5.min():+.2f}..{d5.max():+.2f}")
