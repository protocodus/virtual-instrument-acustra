#!/usr/bin/env python3
"""Sweep evenness at 44.1 and 96 kHz (Room 0.5, Finger v96, m=0.2), lags in ms rounded to samples."""
import sys
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
CONS = ("parlor-spruce", "jumbo-spruce", "dreadnought-spruce", "auditorium-mahogany", "parlor-maple", "jumbo-maple")
names = [ev.note_name(n) for n in range(40, 85)]
VAR = {"V0": (0, 0.0), "V1": (1, 0.0), "V2": (-1, 0.0), "V3c": (1, -0.75), "V3d": (-1, 0.625)}


def load(p):
    return np.fromfile(p, dtype="<f4").reshape(-1, 2).astype(np.float64)


def lev(mic, pz, s, ms, rate):
    a, b = round(0.08 * rate), round(1.15 * rate); k = round(ms * rate / 1000)
    m = mic[a:b]
    if s == 0: return 10 * np.log10(np.mean(m * m))
    x = m + s * 0.2 * pz[a - k:b - k]
    return 10 * np.log10(np.mean(x * x))

for rate in (48000, 44100, 96000):
    tab = {v: [] for v in VAR}; nm = {}
    for c in CONS:
        if rate == 48000:
            base = SP / "piezo-study" / "sweeps" / c; md, pd = base / "mic", base / "piezo"
        else:
            base = OUT / "rates" / str(rate) / c; md, pd = base / "mic", base / "piezo"
        mics = [load(md / f"note-{n}.f32") for n in range(40, 85)]
        assert all(len(m) == round(2.0 * rate) for m in mics), (rate, c)
        mics = [m.mean(axis=1) for m in mics]
        pzs = [load(pd / f"note-{n}.f32")[:, 0] for n in range(40, 85)]
        for v, (s, ms) in VAR.items():
            d = ev.neighbourhood_deviation(np.array([lev(m, p, s, ms, rate) for m, p in zip(mics, pzs)]))
            tab[v].append(float(np.sqrt(np.mean(d ** 2))))
            if c == "parlor-spruce": nm[v] = d[names.index("F#5")]
    print(f"{rate}: " + "  ".join(f"{v} {np.mean(tab[v]):.3f}" for v in VAR) + "   | per cons V1/V3d: " +
          " ".join(f"{c.split('-')[0][0]}{c.split('-')[1][:2]} {tab['V1'][i]:.2f}/{tab['V3d'][i]:.2f}" for i, c in enumerate(CONS))
          + "  | Parlor Spruce F#5 " + " ".join(f"{v} {nm[v]:+.1f}" for v in VAR))
