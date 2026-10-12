#!/usr/bin/env python3
"""Chord sweeps (E-shape and A-shape barre, frets 0-12, 8 ms strum spread, v90) and the
strummed phrase under Room 0 / 0.5 / 1 and Capture Mono, by superposition."""
import sys
from pathlib import Path
import numpy as np
from scipy.signal import welch
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"; STUDY = SP / "piezo-study"
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
sys.path.insert(0, str(OUT / "scripts"))
from analyse import CONS, VARIANTS, load, levels, rmsdev, stats  # noqa

W = (round(0.10 * 48000), round(1.50 * 48000))
CONDS = ("mic_r05", "mic_r0", "mic_r1", "mono_r05")
print("Chord sweeps: RMS neighbourhood dev of chord sustained level (0.1-1.5 s), E and A barre shapes frets 0-12, m=0.2")
allres = {}
for cond in CONDS:
    tab = {v: [] for v in VARIANTS}; holes = {v: [] for v in VARIANTS}
    for cons in CONS:
        groups = {v: [] for v in VARIANTS}
        for kind in ("E", "A"):
            mics = [OUT / "chords" / cons / cond / f"{kind}{f}.f32" for f in range(13)]
            pzs = [OUT / "chords" / cons / "piezo" / f"{kind}{f}.f32" for f in range(13)]
            st = stats(mics, pzs, W)
            for v, (s, k) in VARIANTS.items():
                groups[v].append(levels(st, s, 0.2, k))
        for v in VARIANTS:
            r, d = rmsdev(groups[v]); tab[v].append(r); holes[v].append(d.min())
    allres[cond] = tab
    line = f"  {cond:9s} mean " + "  ".join(f"{v} {np.mean(tab[v]):.2f}" for v in VARIANTS)
    for v in ("V2", "V3c", "V3d"):
        d = np.array(tab[v]) - np.array(tab["V1"])
        line += f" | {v}-V1 b/w {(d < -0.05).sum()}/{(d > 0.05).sum()} worst {d.max():+.2f}"
    print(line)
print("  per construction (mic_r05): V0 V1 V2 V3c V3d")
for i, c in enumerate(CONS):
    print(f"    {c:22s} " + " ".join(f"{allres['mic_r05'][v][i]:5.2f}" for v in VARIANTS) + "   mono: " + " ".join(f"{allres['mono_r05'][v][i]:5.2f}" for v in VARIANTS))

# Strummed phrase: third-octave and 1/12-octave spectra
RATE = 48000
C3 = [1000 * 2 ** (n / 3) for n in range(-11, 10)]
NOM = [80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000]
C12 = [1000 * 2 ** (n / 12) for n in range(-36, 72)]  # 125 Hz .. 7.5 kHz


def shifted(x, k):
    o = np.zeros_like(x)
    if k > 0: o[k:] = x[:-k]
    elif k < 0: o[:k] = x[-k:]
    else: o[:] = x
    return o


def spec(stereo):
    f, pl = welch(stereo[:, 0], fs=RATE, nperseg=16384)
    _, pr = welch(stereo[:, 1], fs=RATE, nperseg=16384)
    return f, 0.5 * (pl + pr)


def bands(f, psd, centres, frac):
    return np.array([10 * np.log10(psd[(f >= c * 2 ** (-frac / 2)) & (f < c * 2 ** (frac / 2))].sum() + 1e-30) for c in centres])

print("\nStrummed phrase (E-Am-C-G-D-E-Am-E, Finger): third-octave V minus V1 (dB) at 80/100/125/160 Hz and 500 Hz, mean over shapes; and fine ripple")
print("ripple = std over 125 Hz-7.5 kHz of the 1/12-octave level difference against V0 after removing its 1-octave running mean (comb structure)")
for cond in CONDS:
    rip = {v: [] for v in VARIANTS}; low = {v: [] for v in VARIANTS}
    for cons in CONS:
        micp = (STUDY / "phrases" / f"{cons}-strums-mic.f32") if cond == "mic_r05" else (OUT / "phrases" / f"{cons}-strums-{cond}.f32")
        mic = load(micp); pz = load(STUDY / "phrases" / f"{cons}-strums-piezo.f32")[:, 0]
        ref = None
        for v, (s, k) in VARIANTS.items():
            x = mic + (s * 0.2 * shifted(pz, k))[:, None]
            f, p = spec(x)
            b3 = bands(f, p, C3, 1 / 3); b12 = bands(f, p, C12, 1 / 12)
            if v == "V0": ref = b12; b3_0 = b3
            if v == "V1": b3_1 = b3
            d = b12 - ref
            trend = np.convolve(d, np.ones(13) / 13, mode="same")
            rip[v].append(float(np.std((d - trend)[6:-6])))
            low[v].append(b3)
        for v in VARIANTS:
            low[v][-1] = low[v][-1] - b3_1
    print(f"  {cond}: ripple mean (max) " + "  ".join(f"{v} {np.mean(rip[v]):.2f} ({np.max(rip[v]):.2f})" for v in VARIANTS))
    for v in ("V0", "V2", "V3c", "V3d"):
        arr = np.array(low[v])  # cons x bands
        idx = [NOM.index(x) for x in (80, 100, 125, 160, 250, 500)]
        print(f"     {v}-V1 by shape (P/A/D/J) @80,100,125,160,250,500 Hz: " + " | ".join(
            ",".join(f"{arr[3*i:3*i+3, j].mean():+.1f}" for j in idx) for i in range(4)))
