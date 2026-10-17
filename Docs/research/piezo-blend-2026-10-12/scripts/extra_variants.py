#!/usr/bin/env python3
"""Robustness and alternatives, by superposition, on three note sets and the strums.

Note sets (each note against its neighbours, MeasureNoteEvenness metric):
  sweep   E2-C6, Finger v96 (sweeps/)           - the development set
  strings each string frets 0-20, Finger v96 (strings/), each string its own neighbourhood
  pick    E2-C6, Pick v96 (pick-sweeps/)         - holdout excitation
Variants (m = Piezo Mix):
  V0 none; V1 +0.2P; V2 -0.2P; V3c +0.2P 0.75 ms earlier; V3d -0.2P 0.625 ms later;
  PS  power-sum bound: the level an uncorrelated piezo of the same power would give;
  m0.1+/- : +-0.1P;  HP1k+/-, HP2k+/- : +-0.2 x the piezo through a causal
  2nd-order Butterworth high-pass at 1 or 2 kHz.
Writes extra.json / extra.txt.
"""
import json
import sys
from pathlib import Path

import numpy as np
from scipy.signal import butter, lfilter, welch

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402
import MeasureMaterialLoudness as meter  # noqa: E402
sys.path.insert(0, str(STUDY / "scripts"))
from evenness_variants import SHAPES, WOODS, load, shifted  # noqa: E402
from phrases_and_transients import bands, NOMINAL  # noqa: E402

RATE = 48000
S0, S1 = round(ev.SUSTAIN[0] * RATE), round(ev.SUSTAIN[1] * RATE)
HP = {fc: butter(2, fc, btype="highpass", fs=RATE) for fc in (1000, 2000)}


def piezo_term(piezo, name):
    if name == "V1": return 0.2 * piezo
    if name == "V2": return -0.2 * piezo
    if name == "V3c": return 0.2 * shifted(piezo, -36)
    if name == "V3d": return -0.2 * shifted(piezo, 30)
    if name == "m0.1+": return 0.1 * piezo
    if name == "m0.1-": return -0.1 * piezo
    for fc in (1000, 2000):
        tag = f"HP{fc // 1000}k"
        if name in (tag + "+", tag + "-"):
            b, a = HP[fc]
            return (0.2 if name.endswith("+") else -0.2) * lfilter(b, a, piezo)
    raise KeyError(name)


VARIANTS = ["V0", "V1", "V2", "V3c", "V3d", "PS", "m0.1+", "m0.1-", "HP1k+", "HP1k-", "HP2k+", "HP2k-"]


def note_level(mic_mono, piezo, name):
    if name == "V0":
        return ev.db(np.mean(mic_mono[S0:S1] ** 2))
    if name == "PS":
        return ev.db(np.mean(mic_mono[S0:S1] ** 2) + 0.04 * np.mean(piezo[S0:S1] ** 2))
    x = mic_mono + piezo_term(piezo, name)
    return ev.db(np.mean(x[S0:S1] ** 2))


def sweep_set(folder, key):
    names = [ev.note_name(m) for m in range(40, 85)]
    levels = {v: [] for v in VARIANTS}
    for midi in range(40, 85):
        mic = load(STUDY / folder / key / "mic" / f"note-{midi}.f32").mean(axis=1)
        piezo = load(STUDY / folder / key / "piezo" / f"note-{midi}.f32")[:, 0]
        for v in VARIANTS:
            levels[v].append(note_level(mic, piezo, v))
    out = {}
    for v in VARIANTS:
        dev = ev.neighbourhood_deviation(np.array(levels[v]))
        out[v] = {"rms": float(np.sqrt(np.mean(dev ** 2))),
                  "hole": f"{names[int(dev.argmin())]} {dev.min():+.1f}",
                  "peak": f"{names[int(dev.argmax())]} {dev.max():+.1f}",
                  "dev": dict(zip(names, map(float, dev)))}
    return out


def strings_set(key):
    out = {}
    devs = {v: [] for v in VARIANTS}
    for string, open_note in enumerate(ev.OPEN_NOTES, start=1):
        levels = {v: [] for v in VARIANTS}
        for fret in range(21):
            midi = open_note + fret
            mic = load(STUDY / "strings" / key / "mic" / f"s{string}-{midi}.f32").mean(axis=1)
            piezo = load(STUDY / "strings" / key / "piezo" / f"s{string}-{midi}.f32")[:, 0]
            for v in VARIANTS:
                levels[v].append(note_level(mic, piezo, v))
        for v in VARIANTS:
            devs[v] += list(ev.neighbourhood_deviation(np.array(levels[v])))
    for v in VARIANTS:
        d = np.array(devs[v])
        out[v] = {"rms": float(np.sqrt(np.mean(d ** 2))), "deepest": float(d.min())}
    return out


def strum_set(key):
    mic = load(STUDY / "phrases" / f"{key}-strums-mic.f32")
    piezo = load(STUDY / "phrases" / f"{key}-strums-piezo.f32")[:, 0]
    out = {}
    for v in VARIANTS:
        if v == "PS":
            continue
        stereo = mic if v == "V0" else mic + piezo_term(piezo, v)[:, None]
        out[v] = {"lufs": meter.integrated_loudness(stereo), "bands": bands(stereo).tolist()}
    return out


def transient_set(key):
    rows = []
    for folder, midis, tag in (("sweeps", (40, 45, 50, 55, 59, 64, 72, 78), "finger"), ("picks", (40, 55, 60, 64, 66), "pick")):
        for midi in midis:
            if folder == "sweeps":
                mic = load(STUDY / "sweeps" / key / "mic" / f"note-{midi}.f32")
                piezo = load(STUDY / "sweeps" / key / "piezo" / f"note-{midi}.f32")[:, 0]
            else:
                mic = load(STUDY / "picks" / f"{key}-{midi}-mic.f32")
                piezo = load(STUDY / "picks" / f"{key}-{midi}-piezo.f32")[:, 0]
            v1 = mic + 0.2 * piezo[:, None]
            env = np.maximum(np.abs(mic).max(axis=1), 0.2 * np.abs(piezo))
            onset = int(np.argmax(env > 1e-3 * np.max(np.abs(v1))))
            row = {"note": f"{tag} {ev.note_name(midi)}"}
            for v in ("V0", "V1", "V2", "V3c", "HP1k+", "HP2k+", "HP2k-"):
                s = mic if v == "V0" else mic + piezo_term(piezo, v)[:, None]
                row[v] = (float(20 * np.log10(np.abs(s).max())),
                          float(10 * np.log10(np.mean(s[onset:onset + 240] ** 2))))
            rows.append(row)
    return rows


def main():
    res = {}
    for shape in SHAPES:
        for wood in WOODS:
            key = f"{shape}-{wood}"
            res[key] = {"sweep": sweep_set("sweeps", key), "pick": sweep_set("pick-sweeps", key),
                        "strings": strings_set(key), "strum": strum_set(key), "transients": transient_set(key)}
    lines = []
    p = lines.append
    for setname, title in (("sweep", "E2-C6 Finger v96 (development set)"), ("strings", "per string, frets 0-20, Finger v96"),
                           ("pick", "E2-C6 Pick v96 (holdout excitation)")):
        p(f"RMS deviation (dB), {title}")
        p(f"  {'':22} " + " ".join(f"{v:>6}" for v in VARIANTS))
        for key, r in res.items():
            p(f"  {key:22} " + " ".join(f"{r[setname][v]['rms']:6.2f}" for v in VARIANTS))
        means = [np.mean([r[setname][v]["rms"] for r in res.values()]) for v in VARIANTS]
        p(f"  {'mean':22} " + " ".join(f"{m:6.2f}" for m in means))
        better = [sum(r[setname][v]["rms"] < r[setname]["V1"]["rms"] - 0.05 for r in res.values()) for v in VARIANTS]
        worse = [sum(r[setname][v]["rms"] > r[setname]["V1"]["rms"] + 0.05 for r in res.values()) for v in VARIANTS]
        p(f"  {'#better/#worse vs V1':22} " + " ".join(f"{b:>3}/{w:<2}" for b, w in zip(better, worse)) + "  (by more than 0.05 dB)")
        p("")
    p("Named notes, dev (dB) on the E2-C6 Finger sweep / Pick sweep:")
    for key, note in (("parlor-spruce", "F#5"), ("jumbo-spruce", "C5")):
        p(f"  {key} {note}: " + "  ".join(f"{v} {res[key]['sweep'][v]['dev'][note]:+.1f}/{res[key]['pick'][v]['dev'][note]:+.1f}" for v in VARIANTS))
    p("")
    p("Deepest hole and highest peak, E2-C6 Finger sweep:")
    for key, r in res.items():
        p(f"  {key:22} " + " | ".join(f"{v}: {r['sweep'][v]['hole']}, {r['sweep'][v]['peak']}" for v in ("V0", "V1", "V2", "V3c", "HP1k+", "HP2k+", "m0.1+")))
    p("")
    p("Strums: integrated loudness minus V0 (LU):")
    sv = [v for v in VARIANTS if v != "PS"]
    p(f"  {'':22} " + " ".join(f"{v:>6}" for v in sv[1:]))
    for key, r in res.items():
        p(f"  {key:22} " + " ".join(f"{r['strum'][v]['lufs'] - r['strum']['V0']['lufs']:+6.2f}" for v in sv[1:]))
    p("")
    for v in ("V1", "V2", "V3c", "m0.1+", "HP1k+", "HP2k+"):
        p(f"Strums: third-octave band level, {v} minus V0 (dB):")
        p(f"  {'':22} " + " ".join(f"{n:>5}" for n in NOMINAL))
        for key, r in res.items():
            d = np.array(r["strum"][v]["bands"]) - np.array(r["strum"]["V0"]["bands"])
            p(f"  {key:22} " + " ".join(f"{x:+5.1f}" for x in d))
        p("")
    p("Strums: largest band cut and boost against V0, 80 Hz-8 kHz third octaves (dB), and mean change 80-160 Hz:")
    p(f"  {'':22} " + " ".join(f"{v:>18}" for v in sv[1:]))
    for key, r in res.items():
        cells = []
        for v in sv[1:]:
            d = np.array(r["strum"][v]["bands"]) - np.array(r["strum"]["V0"]["bands"])
            cells.append(f"{d.min():+5.1f}/{d.max():+4.1f}/{d[:4].mean():+4.1f}")
        p(f"  {key:22} " + " ".join(f"{c:>18}" for c in cells))
    p("")
    p("Transients, peak dBFS / first-5 ms dBFS, Parlor and Jumbo Spruce:")
    for key in ("parlor-spruce", "jumbo-spruce"):
        for row in res[key]["transients"]:
            p(f"  {key:14} {row['note']:11} " + "  ".join(f"{v} {row[v][0]:6.1f}/{row[v][1]:6.1f}" for v in ("V0", "V1", "V2", "V3c", "HP1k+", "HP2k+")))
    p("")
    p("Transients, all 12: mean change against V1 over the 13 notes, peak dB / first-5 ms dB:")
    for key, r in res.items():
        cells = []
        for v in ("V0", "V2", "V3c", "HP1k+", "HP2k+", "HP2k-"):
            dp = np.mean([row[v][0] - row["V1"][0] for row in r["transients"]])
            de = np.mean([row[v][1] - row["V1"][1] for row in r["transients"]])
            cells.append(f"{v} {dp:+5.2f}/{de:+5.2f}")
        p(f"  {key:22} " + "  ".join(cells))
    (STUDY / "extra.json").write_text(json.dumps(res, indent=1) + "\n")
    (STUDY / "extra.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
