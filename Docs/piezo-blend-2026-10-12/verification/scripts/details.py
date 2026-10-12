"""Best-V3 holes/peaks, located-hole notes, level changes and fundamental M/P phase (render-and-discard)."""
import json, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as mne
sys.path.insert(0, str(Path(__file__).parent))
from render import ALL
import stream  # reuses its one() render-and-discard worker
from analyse import shift
RATE = 48000
f = json.load(open(Path(sys.argv[1]) / "analysis-sweep.json"))

def lev(x): s = mne.segment(x, mne.SUSTAIN); return mne.db(np.mean(s * s))

def fundamental(m, p, midi):
    """Complex ratio M/P at the fundamental, sustain window, Hann, peak bin near f0."""
    f0 = 440 * 2 ** ((midi - 69) / 12)
    sm, sp = mne.segment(m, mne.SUSTAIN), mne.segment(p, mne.SUSTAIN)
    w = np.hanning(len(sm)); N = 1 << 18
    M = np.fft.rfft(sm * w, N); P = np.fft.rfft(sp * w, N); fr = np.fft.rfftfreq(N, 1 / RATE)
    band = (fr > f0 * 0.97) & (fr < f0 * 1.03)
    i = np.flatnonzero(band)[np.argmax(np.abs(P[band]) + np.abs(M[band]))]
    r = M[i] / (0.2 * P[i])
    return {"f_hz": float(fr[i]), "M_over_0.2P_db": float(20 * np.log10(abs(r))), "phase_deg": float(np.degrees(np.angle(r))),
            "fund_change_V1_db": float(20 * np.log10(abs(M[i] + 0.2 * P[i]) / abs(M[i])))}

out = {}
hole_note = {"parlor-spruce": "F#5", "parlor-mahogany": "F5", "parlor-maple": "F#5", "auditorium-spruce": "D#5",
             "auditorium-mahogany": "D#5", "auditorium-maple": "E5", "dreadnought-spruce": "C#5", "dreadnought-mahogany": "C#5",
             "dreadnought-maple": "D5", "jumbo-spruce": "C5", "jumbo-mahogany": "B4", "jumbo-maple": "C5"}
for c in ALL:
    with ThreadPoolExecutor(4) as pool:
        rows = list(pool.map(stream.one, [(c, "sweep", mne.lowest_fret_string(m), m) for m in range(40, 85)]))
    M = [r["mic"].astype(np.float64) for r in rows]; P = [r["pie"].astype(np.float64) for r in rows]
    names = [mne.note_name(m) for m in range(40, 85)]
    best = f[c]["best"][0]; sign = 1 if best.startswith("+") else -1; d = float(best.split()[1][:-2])
    k = int(round(d * 48))
    L = [lev(m + sign * 0.2 * shift(p, k)) for m, p in zip(M, P)]
    dv = mne.neighbourhood_deviation(np.array(L))
    hn = hole_note[c]
    o = {"best": best, "rms": float(np.sqrt(np.mean(dv ** 2))), "hole": (names[int(dv.argmin())], float(dv.min())),
         "peak": (names[int(dv.argmax())], float(dv.max())), "hole_note": hn, "hole_note_dev": float(dv[names.index(hn)])}
    for v in ("V0", "V1", "V2", "V3c", "V3d"):
        o[f"{v}_hole_note_dev"] = f[c][v]["dev"][hn]
    o["level_change_V1_minus_V0"] = {n: float(lev(m + 0.2 * p) - lev(m)) for n, m, p in zip(names, M, P) if n in ("F#5", "C5", "A#4", "B4", "C#5", "D#5")}
    o["level_change_V3d_minus_V0"] = {n: float(lev(m - 0.2 * shift(p, 30)) - lev(m)) for n, m, p in zip(names, M, P) if n in ("F#5", "C5", "B4", "C#5", "D#5", "E5")}
    if c in ("parlor-spruce", "jumbo-spruce"):
        o["fund"] = {n: fundamental(m, p, 40 + i) for i, (n, m, p) in enumerate(zip(names, M, P)) if n in ("D#5", "E5", "F5", "F#5", "G5", "A#4", "B4", "C5")}
    out[c] = o
    print(c, json.dumps({k: o[k] for k in ("best", "rms", "hole", "peak", "hole_note", "hole_note_dev")}), flush=True)
# per-string named cases
extra = {}
for c, s, midi in (("parlor-spruce", 5, 78), ("parlor-spruce", 6, 78), ("jumbo-spruce", 4, 72), ("jumbo-spruce", 5, 72), ("jumbo-spruce", 6, 72), ("dreadnought-spruce", 4, 73)):
    r = stream.one((c, "strings", s, midi)); m = r["mic"].astype(np.float64); p = r["pie"].astype(np.float64)
    extra[f"{c} s{s} {mne.note_name(midi)}"] = {"V1-V0": lev(m + 0.2 * p) - lev(m), "V2-V0": lev(m - 0.2 * p) - lev(m),
                                                 "V3d-V0": lev(m - 0.2 * shift(p, 30)) - lev(m), "fund": fundamental(m, p, midi)}
out["per_string"] = extra
(Path(sys.argv[1]) / "details.json").write_text(json.dumps(out, indent=1))
for k, v in extra.items(): print(k, {a: (round(b, 1) if isinstance(b, float) else {x: round(y, 1) for x, y in b.items()}) for a, b in v.items()})
for c in ("parlor-spruce", "jumbo-spruce"):
    print(c, {n: {x: round(y, 1) for x, y in v.items()} for n, v in out[c]["fund"].items()})
    print(c, "V1-V0", {n: round(v, 1) for n, v in out[c]["level_change_V1_minus_V0"].items()}, "V3d-V0", {n: round(v, 1) for n, v in out[c]["level_change_V3d_minus_V0"].items()})
