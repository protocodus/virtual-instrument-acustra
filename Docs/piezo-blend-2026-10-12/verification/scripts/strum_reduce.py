#!/usr/bin/env python3
"""Strummed phrase (E-Am-C-G-D-E-Am-E, Finger, 32 strokes) per condition: for V0..V3d at m=0.2,
third-octave bands 80 Hz-8 kHz and 1/12-octave bands 125 Hz-7.5 kHz (Welch nperseg 16384, mean of
channel PSDs) and BS.1770-4 integrated loudness. Includes the study's Room 0.5 stereo renders
(read only). New mic renders are deleted once reduced."""
import json, sys
from pathlib import Path
import numpy as np
from scipy.signal import welch
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"; STUDY = SP / "piezo-study"
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureMaterialLoudness as meter  # noqa
VARIANTS = {"V0": (0, 0), "V1": (+1, 0), "V2": (-1, 0), "V3c": (+1, -36), "V3d": (-1, +30)}
C3 = [1000 * 2 ** (n / 3) for n in range(-11, 10)]
C12 = [1000 * 2 ** (n / 12) for n in range(-36, 72)]


def shifted(x, k):
    o = np.zeros_like(x)
    if k > 0: o[k:] = x[:-k]
    elif k < 0: o[:k] = x[-k:]
    else: o[:] = x
    return o


def bands(f, psd, centres, frac):
    return [float(10 * np.log10(psd[(f >= c * 2 ** (-frac / 2)) & (f < c * 2 ** (frac / 2))].sum() + 1e-30)) for c in centres]


store = OUT / "stats" / "strums.json"
data = json.loads(store.read_text()) if store.exists() else {}
items = [(STUDY / "phrases" / f"{c}-strums-mic.f32", c, "mic_r05", False) for c in
         [f"{a}-{b}" for a in ("parlor", "auditorium", "dreadnought", "jumbo") for b in ("spruce", "mahogany", "maple")]]
items += [(p, p.name.split("-strums-")[0], p.name.split("-strums-")[1][:-4], True) for p in sorted((OUT / "phrases").glob("*.f32"))]
for path, cons, cond, delete in items:
    key = f"{cons}|{cond}"
    if key in data:
        if delete: path.unlink()
        continue
    a = np.fromfile(path, dtype="<f4")
    if a.size != 2 * 844800:
        print("bad", path); path.unlink(); continue
    mic = a.reshape(-1, 2).astype(np.float64)
    pz = np.fromfile(STUDY / "phrases" / f"{cons}-strums-piezo.f32", dtype="<f4").reshape(-1, 2)[:, 0].astype(np.float64)
    rec = {}
    for v, (s, k) in VARIANTS.items():
        x = mic + (s * 0.2 * shifted(pz, k))[:, None]
        f, pl = welch(x[:, 0], fs=48000, nperseg=16384); _, pr = welch(x[:, 1], fs=48000, nperseg=16384)
        psd = 0.5 * (pl + pr)
        rec[v] = {"b3": bands(f, psd, C3, 1 / 3), "b12": bands(f, psd, C12, 1 / 12),
                  "lufs": float(meter.integrated_loudness(x.astype(np.float32)))}
    data[key] = rec
    store.write_text(json.dumps(data))
    if delete: path.unlink()
print(len(data), "phrase records")
