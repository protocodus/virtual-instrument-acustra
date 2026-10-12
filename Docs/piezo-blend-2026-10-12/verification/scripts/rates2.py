#!/usr/bin/env python3
"""E2-C6 Finger v96 Room 0.5 stereo sweeps at 44.1 and 96 kHz for six constructions; mic and piezo
rendered, reduced to sustain-window statistics at the lags 0, +0.625 ms and -0.75 ms (rounded to samples)
and deleted. The 48 kHz column uses the study's renders."""
import json, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"; R = SP / "piezo-study" / "bin" / "AcustraPerformanceRenderer"
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as ev  # noqa
CONS = ("parlor-spruce", "jumbo-spruce", "dreadnought-spruce", "auditorium-mahogany", "parlor-maple", "jumbo-maple")
VAR = {"V0": (0, 0.0), "V1": (1, 0.0), "V2": (-1, 0.0), "V3c": (1, -0.75), "V3d": (-1, 0.625)}
store = OUT / "stats" / "rates.json"


def score(path, s, midi, rate):
    path.write_text(f"ACUSTRA_PERFORMANCE_V1 {rate} {round(2.0*rate)}\n0 {s} {midi} 96 0\n{round(1.2*rate)} {s} {midi} 0 0\n")


def one(job):
    rate, cons, midi = job
    sh, w = cons.split("-"); s = ev.lowest_fret_string(midi)
    tmp = OUT / "rates_tmp"; tmp.mkdir(exist_ok=True)
    p = tmp / f"r{rate}-{midi}.events"
    if not p.exists(): score(p, s, midi, rate)
    sig = {}
    for name, cap in (("mic", "stereo_mic"), ("piezo", "piezo")):
        o = tmp / f"{rate}-{cons}-{midi}-{name}.f32"
        if o.exists(): o.unlink()
        subprocess.run([str(R), str(p), str(o), cap, "finger", "--body-shape", sh, "--body-material", w, "--room", "0.5", "--piezo-mix", "0"], check=True, capture_output=True)
        a = np.fromfile(o, dtype="<f4").reshape(-1, 2).astype(np.float64); o.unlink()
        assert len(a) == round(2.0 * rate)
        sig[name] = a
    m = sig["mic"].mean(axis=1); q = sig["piezo"][:, 0]
    a0, b0 = round(0.08 * rate), round(1.15 * rate)
    seg = m[a0:b0]; rec = {"mm": float(np.mean(seg * seg))}
    for v, (sg, ms) in VAR.items():
        k = round(ms * rate / 1000); ps = q[a0 - k:b0 - k]
        rec[v] = [float(np.mean(seg * ps)), float(np.mean(ps * ps)), k]
    return f"{rate}|{cons}|{midi}", rec

jobs = [(rate, c, n) for rate in (44100, 96000) for c in CONS for n in range(40, 85)]
data = json.loads(store.read_text()) if store.exists() else {}
jobs = [j for j in jobs if f"{j[0]}|{j[1]}|{j[2]}" not in data]
with ThreadPoolExecutor(max_workers=4) as pool:
    for key, rec in pool.map(one, jobs):
        data[key] = rec
store.write_text(json.dumps(data))
print("rates done", len(data))
