"""Piezo Mix level and sample-rate sensitivity of the -pol/0.625 ms variant (render-and-discard)."""
import json, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as mne
sys.path.insert(0, str(Path(__file__).parent))
from render import RENDERER, ALL
TMP = Path(sys.argv[1]).parent / "tmp"; TMP.mkdir(exist_ok=True)

def score(rate, string, midi, velocity=96):
    return (f"ACUSTRA_PERFORMANCE_V1 {rate} {round(mne.NOTE_SECONDS * rate)}\n"
            f"0 {string} {midi} {velocity} 0\n{round(mne.HOLD_SECONDS * rate)} {string} {midi} 0 0\n")

def one(job):
    c, rate, midi = job
    shape, wood = c.split("-"); s = mne.lowest_fret_string(midi)
    common = ["finger", "--body-shape", shape, "--body-material", wood, "--room", "0.5"]
    out = {}
    with tempfile.TemporaryDirectory(dir=TMP) as d:
        ev = Path(d) / "n.events"; ev.write_text(score(rate, s, midi))
        for v, opts in (("mic", ["stereo_mic", *common]), ("piezo", ["piezo", *common]),
                        ("default", ["stereo_mic", *common, "--piezo-mix", "0.2"])):
            raw = Path(d) / f"{v}.f32"
            subprocess.run([RENDERER, str(ev), str(raw), *opts], check=True, capture_output=True)
            out[v] = np.fromfile(raw, dtype="<f4").reshape(-1, 2).astype(np.float64); raw.unlink()
    diff = float(np.abs(out["default"] - out["mic"] - 0.2 * out["piezo"]).max())
    return out["mic"].mean(axis=1), out["piezo"].mean(axis=1), diff

def frac_delay(x, samples):
    """Delay by a real number of samples (linear-phase FFT shift, zero padded)."""
    n = len(x); N = 1 << int(np.ceil(np.log2(2 * n)))
    X = np.fft.rfft(x, N); f = np.fft.rfftfreq(N)
    return np.fft.irfft(X * np.exp(-2j * np.pi * f * samples), N)[:n]

def ishift(x, k):
    y = np.zeros_like(x)
    if k > 0: y[k:] = x[:-k]
    elif k < 0: y[:k] = x[-k:]
    else: y[:] = x
    return y

def rms_dev(levels):
    d = mne.neighbourhood_deviation(np.array(levels)); return float(np.sqrt(np.mean(d * d))), d

def main():
    outjson = Path(sys.argv[1]); rates = [int(r) for r in sys.argv[2].split(",")]; cons = sys.argv[3:] or ALL
    res = json.loads(outjson.read_text()) if outjson.exists() else {}
    midis = list(range(40, 85)); names = [mne.note_name(m) for m in midis]
    for rate in rates:
        for c in cons:
            key = f"{rate}/{c}"
            if key in res: continue
            with ThreadPoolExecutor(4) as pool:
                rows = list(pool.map(one, [(c, rate, m) for m in midis]))
            a, b = round(0.08 * rate), round(1.15 * rate)
            def lev(sig): return [10 * np.log10(max(np.mean(x[a:b] ** 2), 1e-30)) for x in sig]
            M = [r[0] for r in rows]; P = [r[1] for r in rows]
            d = 0.625e-3 * rate
            out = {"sup": max(r[2] for r in rows)}
            V = {"V0": lev(M)}
            for mix in (0.1, 0.2, 0.35, 0.5, 1.0):
                V[f"+{mix}"] = lev([m + mix * p for m, p in zip(M, P)])
                V[f"V3d{mix}"] = lev([m - mix * ishift(p, int(round(d))) for m, p in zip(M, P)])
            V["V3d0.2frac"] = lev([m - 0.2 * frac_delay(p, d) for m, p in zip(M, P)])
            V["V3c0.2"] = lev([m + 0.2 * ishift(p, -int(round(0.75e-3 * rate))) for m, p in zip(M, P)])
            V["V2"] = lev([m - 0.2 * p for m, p in zip(M, P)])
            for k, v in V.items():
                r, dv = rms_dev(v); i = int(dv.argmin())
                out[k] = {"rms": r, "hole": [names[i], float(dv[i])], "dev": dict(zip(names, map(float, dv)))}
            res[key] = out; outjson.write_text(json.dumps(res))
            print(key, f"sup {out['sup']:.1e} delay {d:.3f} smp |", " ".join(f"{k} {out[k]['rms']:.2f}" for k in V), flush=True)

if __name__ == "__main__":
    main()
