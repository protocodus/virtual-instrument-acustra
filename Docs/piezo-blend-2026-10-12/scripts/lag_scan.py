#!/usr/bin/env python3
"""Is there a physical alignment? Every lag from -3 to +3 ms (1 sample), both
polarities, on the three note sets (E2-C6 Finger, per-string Finger, E2-C6
Pick), MeasureNoteEvenness sustained level and neighbourhood deviation, by
superposition; and the cross-correlation of microphones and piezo over +-5 ms
(strums and the Finger sweep). Writes lag_scan.txt / lag_scan.json.
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.signal import correlate

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402
sys.path.insert(0, str(STUDY / "scripts"))
from evenness_variants import SHAPES, WOODS, load  # noqa: E402

S0, S1 = round(ev.SUSTAIN[0] * ev.RATE), round(ev.SUSTAIN[1] * ev.RATE)
R = 144
LAGS = np.arange(-R, R + 1)


def stats(mic, piezo):
    """E[M^2], E[M P(n-k)], E[P(n-k)^2] over the sustain window for every k."""
    m = mic[S0:S1]
    seg = piezo[S0 - R:S1 + R]
    # correlate(seg, m, 'valid')[j] = sum m[n] seg[n + j]; seg[n + j] = P[S0 - R + n + j] = P[n' - k] with k = R - j
    c = correlate(seg, m, mode="valid", method="fft")[::-1] / len(m)
    c2 = np.cumsum(np.concatenate([[0.0], seg ** 2]))
    n = len(m)
    pp = np.array([(c2[j + n] - c2[j]) / n for j in range(2 * R + 1)])[::-1]
    return float(np.mean(m * m)), c, pp


def note_sets(key):
    sweep = [(STUDY / "sweeps" / key / "mic" / f"note-{m}.f32", STUDY / "sweeps" / key / "piezo" / f"note-{m}.f32") for m in range(40, 85)]
    pick = [(STUDY / "pick-sweeps" / key / "mic" / f"note-{m}.f32", STUDY / "pick-sweeps" / key / "piezo" / f"note-{m}.f32") for m in range(40, 85)]
    strings = [[(STUDY / "strings" / key / "mic" / f"s{s}-{o + f}.f32", STUDY / "strings" / key / "piezo" / f"s{s}-{o + f}.f32")
                for f in range(21)] for s, o in enumerate(ev.OPEN_NOTES, start=1)]
    return {"sweep": [sweep], "pick": [pick], "strings": strings}


def rms_curves(groups):
    """RMS deviation for each (sign, lag); groups are neighbourhoods (lists of notes)."""
    allstats = []
    for group in groups:
        allstats.append([stats(load(mp).mean(axis=1), load(pp)[:, 0]) for mp, pp in group])
    out = {}
    for sign in (+1, -1):
        rms = []
        for column in range(len(LAGS)):
            devs = []
            for group in allstats:
                levels = np.array([ev.db(mm + 2 * sign * 0.2 * mp[column] + 0.04 * pp[column]) for mm, mp, pp in group])
                devs += list(ev.neighbourhood_deviation(levels))
            rms.append(float(np.sqrt(np.mean(np.square(devs)))))
        out[sign] = rms
    # power-sum bound and V0
    devs_ps, devs_0 = [], []
    for group in allstats:
        devs_ps += list(ev.neighbourhood_deviation(np.array([ev.db(mm + 0.04 * pp[R]) for mm, mp, pp in group])))
        devs_0 += list(ev.neighbourhood_deviation(np.array([ev.db(mm) for mm, mp, pp in group])))
    out["PS"] = float(np.sqrt(np.mean(np.square(devs_ps))))
    out["V0"] = float(np.sqrt(np.mean(np.square(devs_0))))
    return out


def xcorr_peaks(M, P, reach=240):
    norm = np.sqrt(np.sum(M ** 2) * np.sum(P ** 2))
    c = correlate(M, P, mode="full", method="fft")  # c[i] = sum M[n + i - (len(P)-1)] P[n] -> lag k = i - (len(P)-1): M[n+k] ~ P[n] => piezo delayed by k
    mid = len(P) - 1
    seg = c[mid - reach: mid + reach + 1] / norm
    lags = np.arange(-reach, reach + 1)
    return lags, seg


def main():
    result = {}
    lines = []
    p = lines.append
    for shape in SHAPES:
        for wood in WOODS:
            key = f"{shape}-{wood}"
            sets = note_sets(key)
            result[key] = {name: rms_curves(groups) for name, groups in sets.items()}
    p("Mean RMS deviation over the 12 constructions against the piezo lag (positive = piezo later), both polarities;")
    p("V0 (no piezo), V1 (+, 0), V2 (-, 0) and the power-sum bound PS for reference.")
    for name in ("sweep", "strings", "pick"):
        mean_plus = np.mean([result[k][name][1] for k in result], axis=0)
        mean_minus = np.mean([result[k][name][-1] for k in result], axis=0)
        ps = np.mean([result[k][name]["PS"] for k in result])
        v0 = np.mean([result[k][name]["V0"] for k in result])
        p(f"\n[{name}] V0 {v0:.3f}  V1 {mean_plus[R]:.3f}  V2 {mean_minus[R]:.3f}  PS {ps:.3f}")
        p(f"  best +: {mean_plus.min():.3f} at {LAGS[mean_plus.argmin()]/48:+.3f} ms; best -: {mean_minus.min():.3f} at {LAGS[mean_minus.argmin()]/48:+.3f} ms")
        p("   lag ms    +pol    -pol")
        for i in range(0, len(LAGS), 6):
            p(f"  {LAGS[i]/48:+6.3f}  {mean_plus[i]:6.3f}  {mean_minus[i]:6.3f}")
        p("  per-construction best (sign, ms, rms):  " + "; ".join(
            f"{k} {'+' if min(result[k][name][1]) <= min(result[k][name][-1]) else '-'}"
            f"{LAGS[int(np.argmin(result[k][name][1] if min(result[k][name][1]) <= min(result[k][name][-1]) else result[k][name][-1]))]/48:+.2f} "
            f"{min(min(result[k][name][1]), min(result[k][name][-1])):.2f}" for k in result))
    p("\nCross-correlation of mono microphones and piezo over +-5 ms (k > 0: piezo delayed): r(0), max r (lag), min r (lag)")
    for source in ("sweep", "strums"):
        p(f"-- {source}")
        for shape in SHAPES:
            for wood in WOODS:
                key = f"{shape}-{wood}"
                if source == "sweep":
                    M = np.concatenate([load(STUDY / "sweeps" / key / "mic" / f"note-{m}.f32").mean(axis=1) for m in range(40, 85)])
                    P = np.concatenate([load(STUDY / "sweeps" / key / "piezo" / f"note-{m}.f32")[:, 0] for m in range(40, 85)])
                else:
                    M = load(STUDY / "phrases" / f"{key}-strums-mic.f32").mean(axis=1)
                    P = load(STUDY / "phrases" / f"{key}-strums-piezo.f32")[:, 0]
                lags, r = xcorr_peaks(M, P)
                p(f"  {key:22} r(0) {r[240]:+.3f}  max {r.max():+.3f} at {lags[r.argmax()]/48:+.3f} ms  min {r.min():+.3f} at {lags[r.argmin()]/48:+.3f} ms")
    (STUDY / "lag_scan.json").write_text(json.dumps({k: {n: {str(s): v for s, v in d.items()} for n, d in r.items()} for k, r in result.items()}) + "\n")
    (STUDY / "lag_scan.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
