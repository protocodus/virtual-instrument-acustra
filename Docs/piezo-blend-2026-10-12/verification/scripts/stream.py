#!/usr/bin/env python3
"""Render-and-discard holdout sets (disk is short): stream.py OUTJSON SET constructions..."""
import json, os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import numpy as np
from scipy.signal import butter, lfilter
sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as mne  # noqa: E402
sys.path.insert(0, str(Path(__file__).parent))
from render import RENDERER, notes_for, settings, score_text, ALL  # noqa: E402
from analyse import shift, summarise  # noqa: E402

RATE = mne.RATE; MS = RATE / 1000.0
TMP = Path(sys.argv[1]).parent / "tmp"; TMP.mkdir(exist_ok=True)

def level(mono, midi):
    s = mne.segment(mono, mne.SUSTAIN)
    return mne.db(np.mean(s * s))  # == note_metrics(...)["rms_dbfs"]

def one(job):
    c, set_name, string, midi = job
    velocity, room, technique = settings(set_name)
    shape, wood = c.split("-")
    common = ["--body-shape", shape, "--body-material", wood, "--room", room]
    out = {}
    with tempfile.TemporaryDirectory(dir=TMP) as d:
        ev = Path(d) / "n.events"; ev.write_text(score_text(string, midi, velocity))
        for v, opts in (("mic", ["stereo_mic", technique, *common, "--piezo-mix", "0"]),
                        ("piezo", ["piezo", technique, *common]),
                        ("default", ["stereo_mic", technique, *common, "--piezo-mix", "0.2"])):
            raw = Path(d) / f"{v}.f32"
            r = subprocess.run([RENDERER, str(ev), str(raw), *opts], capture_output=True, text=True)
            if r.returncode: raise RuntimeError(r.stderr)
            a = np.fromfile(raw, dtype="<f4").reshape(-1, 2); raw.unlink()
            assert len(a) == 96000 and np.isfinite(a).all()
            out[v] = a
    mic, pie, dft = out["mic"], out["piezo"], out["default"]
    pred = mic.astype(np.float64) + 0.2 * pie.astype(np.float64)
    return {"diff": float(np.abs(dft - pred).max()), "peak": float(np.abs(dft).max()),
            "peak_mic": float(np.abs(mic).max()),
            "lr": bool(np.array_equal(pie[:, 0].view(np.uint32), pie[:, 1].view(np.uint32))),
            "mic": mic.astype(np.float64).mean(axis=1).astype(np.float32),
            "pie": pie.astype(np.float64).mean(axis=1).astype(np.float32),
            "dft": dft.astype(np.float64).mean(axis=1).astype(np.float32)}

def main():
    outjson = Path(sys.argv[1]); set_name = sys.argv[2]; cons = sys.argv[3:] or ALL
    notes = notes_for(set_name)
    if set_name == "strings":
        names = [f"s{s}:{mne.note_name(m)}" for s, m in notes]
        groups = [[i for i, (s, _) in enumerate(notes) if s == k] for k in range(1, 7)]
    else:
        names = [mne.note_name(m) for _, m in notes]; groups = [list(range(len(notes)))]
    b, a = butter(2, 1000, btype="highpass", fs=RATE)
    results = json.loads(outjson.read_text()) if outjson.exists() else {}
    for c in cons:
        if c in results: continue
        with ThreadPoolExecutor(4) as pool:
            rows = list(pool.map(one, [(c, set_name, s, m) for s, m in notes]))
        midis = [m for _, m in notes]
        M = [r["mic"].astype(np.float64) for r in rows]; P = [r["pie"].astype(np.float64) for r in rows]
        D = [r["dft"].astype(np.float64) for r in rows]
        def var(sign, d_ms, mix=0.2):
            k = int(round(d_ms * MS))
            return [level(m + sign * mix * shift(p, k), mi) for m, p, mi in zip(M, P, midis)]
        lv = {"V0": [level(m, mi) for m, mi in zip(M, midis)],
              "V1_engine": [level(x, mi) for x, mi in zip(D, midis)],
              "V1": var(1, 0), "V2": var(-1, 0), "V3c": var(1, -0.75), "V3d": var(-1, 0.625),
              "HP1k-": [level(m - 0.2 * lfilter(b, a, p), mi) for m, p, mi in zip(M, P, midis)],
              "PS": [mne.db(np.mean(mne.segment(m, mne.SUSTAIN) ** 2) + 0.04 * np.mean(mne.segment(p, mne.SUSTAIN) ** 2)) for m, p in zip(M, P)]}
        res = {k: summarise(names, v, groups) for k, v in lv.items()}
        grid = {}
        for sign in (1, -1):
            for kk in range(17):
                d = round(-1 + 0.125 * kk, 3)
                grid[f"{'+' if sign > 0 else '-'}pol {d:+.3f}ms"] = summarise(names, var(sign, d), groups)["rms"]
        res["grid"] = grid
        res["sup"] = {"max_diff": max(r["diff"] for r in rows), "max_rel": max(r["diff"] / r["peak"] for r in rows),
                      "peak": max(r["peak"] for r in rows), "peak_mic": max(r["peak_mic"] for r in rows),
                      "lr": all(r["lr"] for r in rows)}
        results[c] = res
        outjson.write_text(json.dumps(results))
        print(c, f"sup {res['sup']['max_diff']:.2e} peak {res['sup']['peak']:.3f} lr {res['sup']['lr']} |",
              " ".join(f"{k} {res[k]['rms']:.2f}" for k in lv), flush=True)

if __name__ == "__main__":
    main()
