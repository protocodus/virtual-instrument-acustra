#!/usr/bin/env python3
"""Note evenness of piezo-blend variants, built offline by superposition.

Main(m, s, k) = Main(piezoMix 0) + s * m * Piezo(t - k), mono-summed as
Tools/MeasureNoteEvenness.py sums it (mean of the two channels; the piezo term
is the same on both). m = 0.2, s = +1 (shipped) or -1, k a delay in samples
(positive: the piezo later). The metric is the tool's own: note_metrics()'s
sustained level, neighbourhood_deviation(), RMS of dev, deepest hole and
highest peak, over E2-C6.

Writes evenness.json (every construction, every variant on the grid) and
evenness.txt (the tables).
"""
import json
import sys
from pathlib import Path

import numpy as np

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402

SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
MIX = 0.2
NOTES = range(40, 85)
# Located mid-band hole (Hz), Docs/mid-band-dip-2026-10-12.md table.
HOLES = {("parlor", "spruce"): 741, ("parlor", "mahogany"): 714, ("parlor", "maple"): 751,
         ("auditorium", "spruce"): 632, ("auditorium", "mahogany"): 609, ("auditorium", "maple"): 646,
         ("dreadnought", "spruce"): 563, ("dreadnought", "mahogany"): 543, ("dreadnought", "maple"): 575,
         ("jumbo", "spruce"): 516, ("jumbo", "mahogany"): 494, ("jumbo", "maple"): 524}
NAMED = {("parlor", "spruce"): ("F#5",), ("jumbo", "spruce"): ("C5",)}
GRID_MS = [round(-1.0 + 0.125 * i, 3) for i in range(17)]          # requested grid
FINE = list(range(-48, 49))                                           # every sample, +-1 ms
S0, S1 = round(ev.SUSTAIN[0] * ev.RATE), round(ev.SUSTAIN[1] * ev.RATE)


def load(path):
    return np.fromfile(path, dtype="<f4").reshape(-1, 2).astype(np.float64)


def shifted(signal, k):
    """signal delayed by k samples (k < 0 advances), zero-filled."""
    out = np.zeros_like(signal)
    if k > 0:
        out[k:] = signal[:-k]
    elif k < 0:
        out[:k] = signal[-k:]
    else:
        out[:] = signal
    return out


def nearest_note(hz):
    midi = int(round(69 + 12 * np.log2(hz / 440.0)))
    return ev.note_name(midi)


def summary(levels, names):
    dev = ev.neighbourhood_deviation(np.asarray(levels))
    return {"rms_deviation_db": float(np.sqrt(np.mean(dev ** 2))),
            "deepest_hole_note": names[int(dev.argmin())], "deepest_hole_db": float(dev.min()),
            "highest_peak_note": names[int(dev.argmax())], "highest_peak_db": float(dev.max()),
            "dev": {n: float(d) for n, d in zip(names, dev)},
            "level": {n: float(l) for n, l in zip(names, levels)}}


def construction(shape, wood):
    base = STUDY / "sweeps" / f"{shape}-{wood}"
    names = [ev.note_name(m) for m in NOTES]
    mic = {m: load(base / "mic" / f"note-{m}.f32").mean(axis=1) for m in NOTES}
    piezo = {m: load(base / "piezo" / f"note-{m}.f32")[:, 0] for m in NOTES}
    # Sustain-window statistics for every shift: E[M^2], E[M P_k], E[P_k^2].
    mm = np.array([np.mean(mic[m][S0:S1] ** 2) for m in NOTES])
    mp = np.array([[np.mean(mic[m][S0:S1] * piezo[m][S0 - k:S1 - k]) for k in FINE] for m in NOTES])
    pp = np.array([[np.mean(piezo[m][S0 - k:S1 - k] ** 2) for k in FINE] for m in NOTES])
    grid = {}
    for sign in (+1, -1):
        for column, k in enumerate(FINE):
            power = mm + 2 * sign * MIX * mp[:, column] + MIX ** 2 * pp[:, column]
            levels = [ev.db(p) for p in power]
            grid[(sign, k)] = summary(levels, names)
    # Full tool metrics for the headline variants (and a check that the fast
    # path is the tool's rms_dbfs).
    def full(sign, k):
        rows = [ev.note_metrics(mic[m] + sign * MIX * shifted(piezo[m], k), m) for m in NOTES]
        return rows
    v0_rows = [ev.note_metrics(mic[m], m) for m in NOTES]
    out = {"V0": summary([r["rms_dbfs"] for r in v0_rows], names)}
    out["V0"]["fundamental_share_db"] = {r["name"]: r["fundamental_share_db"] for r in v0_rows}
    for label, sign in (("V1", +1), ("V2", -1)):
        rows = full(sign, 0)
        fast = grid[(sign, 0)]["level"]
        assert max(abs(r["rms_dbfs"] - fast[r["name"]]) for r in rows) < 1e-9
        out[label] = summary([r["rms_dbfs"] for r in rows], names)
        out[label]["fundamental_share_db"] = {r["name"]: r["fundamental_share_db"] for r in rows}
    # The requested grid: -1.0..+1.0 ms in 0.125 ms = 6 samples at 48 kHz.
    requested = {}
    for sign in (+1, -1):
        for ms in GRID_MS:
            k = int(round(ms * ev.RATE / 1000))
            requested[f"{'+' if sign > 0 else '-'}{ms:+.3f}ms"] = grid[(sign, k)]
    best_label = min(requested, key=lambda key: requested[key]["rms_deviation_db"])
    best_fine = min(grid, key=lambda key: grid[key]["rms_deviation_db"])
    out["V3_grid"] = {key: {k2: v for k2, v in value.items() if k2 in (
        "rms_deviation_db", "deepest_hole_note", "deepest_hole_db",
        "highest_peak_note", "highest_peak_db")} for key, value in requested.items()}
    out["V3_best_label"] = best_label
    out["V3_best"] = requested[best_label]
    out["fine_best"] = {"sign": best_fine[0], "samples": best_fine[1],
                        **{k2: v for k2, v in grid[best_fine].items() if k2 != "level" and k2 != "dev"}}
    out["fine_rms"] = {f"{s:+d},{k}": grid[(s, k)]["rms_deviation_db"] for (s, k) in grid}
    # Per-note correlation of microphones and piezo in the sustain window
    # (k = 0), and the piezo term's level against the microphones'.
    out["corr"] = {n: float(mp[i, FINE.index(0)] / np.sqrt(mm[i] * pp[i, FINE.index(0)]))
                   for i, n in enumerate(names)}
    out["piezo_over_mic_db"] = {n: float(ev.db(MIX ** 2 * pp[i, FINE.index(0)]) - ev.db(mm[i]))
                                for i, n in enumerate(names)}
    hole_note = nearest_note(HOLES[(shape, wood)])
    out["hole_hz"] = HOLES[(shape, wood)]
    out["hole_note"] = hole_note
    return out


def main():
    results = {}
    for shape in SHAPES:
        for wood in WOODS:
            results[f"{shape}-{wood}"] = construction(shape, wood)
    # One setting for all constructions on the requested grid.
    labels = list(next(iter(results.values()))["V3_grid"].keys())
    mean_rms = {label: float(np.mean([r["V3_grid"][label]["rms_deviation_db"] for r in results.values()]))
                for label in labels}
    max_rms = {label: float(np.max([r["V3_grid"][label]["rms_deviation_db"] for r in results.values()]))
               for label in labels}
    fine_keys = list(next(iter(results.values()))["fine_rms"].keys())
    fine_mean = {key: float(np.mean([r["fine_rms"][key] for r in results.values()])) for key in fine_keys}
    common = {"mean_rms_by_setting": mean_rms, "max_rms_by_setting": max_rms,
              "best_common_by_mean": min(mean_rms, key=mean_rms.get),
              "best_common_by_max": min(max_rms, key=max_rms.get),
              "fine_best_common_by_mean": min(fine_mean, key=fine_mean.get),
              "fine_mean_rms": fine_mean}
    (STUDY / "evenness.json").write_text(json.dumps({"constructions": results, "common": common}, indent=1) + "\n")

    lines = []
    p = lines.append
    p("RMS deviation (dB), E2-C6, Finger, stereo mic, Room 0.5, velocity 96; MeasureNoteEvenness metric")
    p("V0 mics only; V1 +0.2 piezo (shipped); V2 -0.2 piezo; V3best = best of the +-1 ms / 0.125 ms grid x polarity")
    p("label: polarity then delay (positive = piezo later)")
    p("")
    head = f"{'construction':22} {'V0':>5} {'V1':>5} {'V2':>5} {'V3best':>6} {'setting':>11}  {'V0 hole':>12} {'V1 hole':>12} {'V2 hole':>12} {'V3 hole':>12}  {'V0 peak':>11} {'V1 peak':>11} {'V2 peak':>11} {'V3 peak':>11}"
    p(head)
    for key, r in results.items():
        def h(v): return f"{v['deepest_hole_note']} {v['deepest_hole_db']:+.1f}"
        def k(v): return f"{v['highest_peak_note']} {v['highest_peak_db']:+.1f}"
        p(f"{key:22} {r['V0']['rms_deviation_db']:5.2f} {r['V1']['rms_deviation_db']:5.2f} "
          f"{r['V2']['rms_deviation_db']:5.2f} {r['V3_best']['rms_deviation_db']:6.2f} {r['V3_best_label']:>11}  "
          f"{h(r['V0']):>12} {h(r['V1']):>12} {h(r['V2']):>12} {h(r['V3_best']):>12}  "
          f"{k(r['V0']):>11} {k(r['V1']):>11} {k(r['V2']):>11} {k(r['V3_best']):>11}")
    mean = {v: np.mean([r[v]['rms_deviation_db'] for r in results.values()]) for v in ("V0", "V1", "V2", "V3_best")}
    p(f"{'mean of 12':22} {mean['V0']:5.2f} {mean['V1']:5.2f} {mean['V2']:5.2f} {mean['V3_best']:6.2f}")
    p("")
    p("Hole-adjacent and named notes: dev (dB) V0 / V1 / V2 / V3best; level change vs V0 (dB) V1 / V2;"
      " sustain-window correlation mic.piezo; 0.2*piezo over mic (dB)")
    for key, r in results.items():
        shape, wood = key.split("-")
        names = [r["hole_note"]] + [n for n in NAMED.get((shape, wood), ()) if n != r["hole_note"]]
        idx = [ev.note_name(m) for m in NOTES]
        hi = idx.index(r["hole_note"])
        names += [n for n in idx[max(0, hi - 2):hi + 3] if n not in names]
        for n in sorted(set(names), key=idx.index):
            mark = "*" if n == r["hole_note"] else (" named" if n in NAMED.get((shape, wood), ()) else "")
            p(f"  {key:22} {n:>4}{mark:6} dev {r['V0']['dev'][n]:+6.1f} {r['V1']['dev'][n]:+6.1f} "
              f"{r['V2']['dev'][n]:+6.1f} {r['V3_best']['dev'][n]:+6.1f}   dlevel {r['V1']['level'][n]-r['V0']['level'][n]:+5.1f} "
              f"{r['V2']['level'][n]-r['V0']['level'][n]:+5.1f}   corr {r['corr'][n]:+.2f}   pz/mic {r['piezo_over_mic_db'][n]:+5.1f}")
    p("")
    p("One setting for all 12 (requested grid): best by mean RMS deviation "
      f"{common['best_common_by_mean']} (mean {mean_rms[common['best_common_by_mean']]:.3f}, max {max_rms[common['best_common_by_mean']]:.3f}); "
      f"best by worst-construction RMS {common['best_common_by_max']} (mean {mean_rms[common['best_common_by_max']]:.3f}, max {max_rms[common['best_common_by_max']]:.3f}); "
      f"every-sample grid best by mean {common['fine_best_common_by_mean']} samples ({fine_mean[common['fine_best_common_by_mean']]:.3f})")
    p("")
    p("Mean / max RMS deviation over the 12 constructions per setting (requested grid):")
    for label in labels:
        p(f"  {label:>11}  mean {mean_rms[label]:.3f}  max {max_rms[label]:.3f}")
    p("")
    p("Per construction, RMS deviation over the requested grid (rows: setting):")
    p(f"  {'setting':>11} " + " ".join(f"{key[:11]:>11}" for key in results))
    for label in labels:
        p(f"  {label:>11} " + " ".join(f"{r['V3_grid'][label]['rms_deviation_db']:11.2f}" for r in results.values()))
    p("")
    p("Every-sample search (+-48 samples), per construction best: " + "; ".join(
        f"{key} {r['fine_best']['sign']:+d}/{r['fine_best']['samples']:+d} smp {r['fine_best']['rms_deviation_db']:.2f}"
        for key, r in results.items()))
    (STUDY / "evenness.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
