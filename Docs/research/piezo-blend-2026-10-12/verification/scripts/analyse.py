#!/usr/bin/env python3
"""Superposition and evenness metrics, independently of the study's scripts.

analyse.py ROOT SET [constructions...]
"""
import json, sys
from pathlib import Path
import numpy as np

sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as mne  # noqa: E402
sys.path.insert(0, str(Path(__file__).parent))
from render import notes_for, ALL  # noqa: E402

RATE = mne.RATE
MS = RATE / 1000.0

def load(path):
    a = np.fromfile(path, dtype="<f4").reshape(-1, 2)
    assert len(a) == round(mne.NOTE_SECONDS * RATE) and np.isfinite(a).all(), path
    return a

def shift(x, samples):
    """samples > 0: x delayed (later); < 0: x advanced (earlier). Zero fill."""
    y = np.zeros_like(x)
    if samples > 0:
        y[samples:] = x[:-samples]
    elif samples < 0:
        y[:samples] = x[-samples:]
    else:
        y[:] = x
    return y

def level(mono, midi):
    return mne.note_metrics(mono, midi)["rms_dbfs"]

def summarise(names, levels, groups):
    """groups: list of index lists, each its own neighbourhood (strings) or one group (sweep)."""
    dev = np.zeros(len(levels))
    for g in groups:
        dev[g] = mne.neighbourhood_deviation(np.asarray(levels)[g])
    i_min, i_max = int(dev.argmin()), int(dev.argmax())
    return {"rms": float(np.sqrt(np.mean(dev ** 2))), "hole": (names[i_min], float(dev[i_min])),
            "peak": (names[i_max], float(dev[i_max])), "dev": dict(zip(names, map(float, dev)))}

def main():
    root = Path(sys.argv[1]); set_name = sys.argv[2]
    cons = sys.argv[3:] or ALL
    notes = notes_for(set_name)
    if set_name == "strings":
        names = [f"s{s}:{mne.note_name(m)}" for s, m in notes]
        groups = [[i for i, (s, _) in enumerate(notes) if s == k] for k in range(1, 7)]
    else:
        names = [mne.note_name(m) for _, m in notes]
        groups = [list(range(len(notes)))]
    delays_ms = [round(-1.0 + 0.125 * k, 3) for k in range(17)]
    results = {}
    for c in cons:
        sup = {"max_abs_diff_f64": 0.0, "max_abs_diff_f32": 0.0, "max_rel": 0.0, "peak_default": 0.0,
               "piezo_lr_identical": True, "max_abs_default_minus_mic": 0.0}
        mic_m, pie_m, def_m, midis = [], [], [], []
        for string, midi in notes:
            base = root / set_name / c
            mic = load(base / "mic" / f"s{string}-n{midi}.f32")
            pie = load(base / "piezo" / f"s{string}-n{midi}.f32")
            dft = load(base / "default" / f"s{string}-n{midi}.f32")
            pred64 = mic.astype(np.float64) + 0.2 * pie.astype(np.float64)
            pred32 = (mic + np.float32(0.2) * pie).astype(np.float32)
            d64 = np.abs(dft.astype(np.float64) - pred64).max()
            d32 = np.abs(dft - pred32).max()
            pk = float(np.abs(dft).max())
            sup["max_abs_diff_f64"] = max(sup["max_abs_diff_f64"], float(d64))
            sup["max_abs_diff_f32"] = max(sup["max_abs_diff_f32"], float(d32))
            sup["max_rel"] = max(sup["max_rel"], float(d64 / max(pk, 1e-30)))
            sup["peak_default"] = max(sup["peak_default"], pk)
            sup["max_abs_default_minus_mic"] = max(sup["max_abs_default_minus_mic"], float(np.abs(dft - mic).max()))
            if not np.array_equal(pie[:, 0].view(np.uint32), pie[:, 1].view(np.uint32)):
                sup["piezo_lr_identical"] = False
            mic_m.append(mic.astype(np.float64).mean(axis=1))
            pie_m.append(pie.astype(np.float64).mean(axis=1))
            def_m.append(dft.astype(np.float64).mean(axis=1))
            midis.append(midi)
        def variant(sign, d_ms, mix=0.2):
            k = int(round(d_ms * MS))
            assert abs(k - d_ms * MS) < 1e-9
            return [level(m + sign * mix * shift(p, k), midi) for m, p, midi in zip(mic_m, pie_m, midis)]
        lv = {"V0": [level(m, midi) for m, midi in zip(mic_m, midis)],
              "V1_engine": [level(m, midi) for m, midi in zip(def_m, midis)],
              "V1": variant(+1, 0.0), "V2": variant(-1, 0.0),
              "V3c": variant(+1, -0.75), "V3d": variant(-1, 0.625)}
        from scipy.signal import butter, lfilter
        for fc in (1000,):
            b, a = butter(2, fc, btype="highpass", fs=RATE)
            hp = [lfilter(b, a, p) for p in pie_m]
            for sign, tag in ((+1, "+"), (-1, "-")):
                lv[f"HP1k{tag}"] = [level(m + sign * 0.2 * h, midi) for m, h, midi in zip(mic_m, hp, midis)]
        lv["m0.1+"] = variant(+1, 0.0, 0.1)
        lv["m0.1-"] = variant(-1, 0.0, 0.1)
        # power-sum bound: an uncorrelated piezo of the same power
        ps = []
        for m, p, midi in zip(mic_m, pie_m, midis):
            sm, sp = mne.segment(m, mne.SUSTAIN), mne.segment(p, mne.SUSTAIN)
            ps.append(mne.db(np.mean(sm * sm) + 0.04 * np.mean(sp * sp)))
        lv["PS"] = ps
        out = {k: summarise(names, v, groups) for k, v in lv.items()}
        grid = {}
        for sign in (+1, -1):
            for d in delays_ms:
                grid[f"{'+' if sign > 0 else '-'}pol {d:+.3f}ms"] = summarise(names, variant(sign, d), groups)["rms"]
        best = min(grid, key=grid.get)
        out["grid"] = grid
        out["best"] = (best, grid[best])
        out["superposition"] = sup
        out["levels"] = lv
        results[c] = out
        print(c, f"sup f64 {sup['max_abs_diff_f64']:.2e} f32 {sup['max_abs_diff_f32']:.2e} rel {sup['max_rel']:.2e} "
              f"peak {sup['peak_default']:.3f} pieLR {sup['piezo_lr_identical']} | "
              + " ".join(f"{k} {out[k]['rms']:.2f}" for k in lv) + f" | best {best} {grid[best]:.2f}", flush=True)
    (root / f"analysis-{set_name}.json").write_text(json.dumps(results, indent=1))

if __name__ == "__main__":
    main()
