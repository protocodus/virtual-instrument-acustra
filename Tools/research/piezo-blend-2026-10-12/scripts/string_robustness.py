#!/usr/bin/env python3
"""Per-string evenness (frets 0-20, each string its own neighbourhood) of the
variants, by superposition, with the MeasureNoteEvenness sustained level and
neighbourhood deviation. Also the level change each variant makes at the
hole notes when they are played on other strings.

Writes strings.json and strings.txt.
"""
import json
import sys
from pathlib import Path

import numpy as np

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402
sys.path.insert(0, str(STUDY / "scripts"))
from evenness_variants import SHAPES, WOODS, MIX, load, shifted  # noqa: E402

S0, S1 = round(ev.SUSTAIN[0] * ev.RATE), round(ev.SUSTAIN[1] * ev.RATE)
COMMON = {"V0": None, "V1": (+1, 0), "V2": (-1, 0), "+adv0.750": (+1, -36),
          "-adv0.250": (-1, -12), "+adv0.625": (+1, -30), "-del0.625": (-1, +30)}


def level(mic, piezo, setting):
    if setting is None:
        x = mic
    else:
        sign, k = setting
        x = mic + sign * MIX * shifted(piezo, k)
    return ev.db(np.mean(x[S0:S1] ** 2))


def main():
    evenness = json.loads((STUDY / "evenness.json").read_text())["constructions"]
    results, lines = {}, []
    p = lines.append
    p("Per-string evenness: RMS of dev over all 6 strings x frets 0-20 (each note against up to 3 fret-neighbours on its own string)")
    p("own-V3 = the construction's best E2-C6 grid setting applied here; +adv0.750 = + polarity, piezo 0.75 ms earlier, etc.")
    p("")
    labels = list(COMMON) + ["own-V3"]
    p(f"{'construction':22} " + " ".join(f"{l:>10}" for l in labels) + "   per-string RMS V0 | V1 | V2 (s1..s6)")
    bitcheck = 0
    for shape in SHAPES:
        for wood in WOODS:
            key = f"{shape}-{wood}"
            best = evenness[key]["V3_best_label"]  # e.g. "+-0.750ms"
            own = (+1 if best[0] == "+" else -1, int(round(float(best[1:-2]) * 48)))
            settings = dict(COMMON, **{"own-V3": own})
            base = STUDY / "strings" / key
            per = {label: {} for label in settings}
            for string, open_note in enumerate(ev.OPEN_NOTES, start=1):
                mics, piezos = [], []
                for fret in range(21):
                    midi = open_note + fret
                    mic = load(base / "mic" / f"s{string}-{midi}.f32").mean(axis=1)
                    piezo = load(base / "piezo" / f"s{string}-{midi}.f32")[:, 0]
                    if midi >= 64 and string == 6 and midi <= 84:
                        ref = load(STUDY / "sweeps" / key / "mic" / f"note-{midi}.f32").mean(axis=1)
                        bitcheck += int(not np.array_equal(ref, mic))
                    mics.append(mic)
                    piezos.append(piezo)
                for label, setting in settings.items():
                    levels = np.array([level(m, pz, setting) for m, pz in zip(mics, piezos)])
                    per[label][string] = {"levels": levels.tolist(),
                                          "dev": ev.neighbourhood_deviation(levels).tolist()}
            row = {}
            for label in settings:
                allDev = np.concatenate([per[label][s]["dev"] for s in range(1, 7)])
                worst = int(np.argmin(allDev))
                s_w, f_w = divmod(worst, 21)
                row[label] = {"rms": float(np.sqrt(np.mean(allDev ** 2))),
                              "per_string_rms": [float(np.sqrt(np.mean(np.square(per[label][s]["dev"])))) for s in range(1, 7)],
                              "deepest": f"s{s_w+1} {ev.note_name(ev.OPEN_NOTES[s_w]+f_w)} {allDev[worst]:+.1f}"}
            results[key] = {"rows": row, "own_setting": best,
                            "per": {label: {s: per[label][s] for s in per[label]} for label in per}}
            p(f"{key:22} " + " ".join(f"{row[l]['rms']:10.2f}" for l in labels)
              + "   " + " ".join(f"{v:.1f}" for v in row['V0']['per_string_rms'])
              + " | " + " ".join(f"{v:.1f}" for v in row['V1']['per_string_rms'])
              + " | " + " ".join(f"{v:.1f}" for v in row['V2']['per_string_rms']))
    means = {l: np.mean([results[k]["rows"][l]["rms"] for k in results]) for l in labels}
    p(f"{'mean of 12':22} " + " ".join(f"{means[l]:10.2f}" for l in labels))
    p("")
    p("Deepest per-string hole (string note dev) V0 | V1 | V2 | own-V3:")
    for key, r in results.items():
        p(f"  {key:22} " + " | ".join(r["rows"][l]["deepest"] for l in ("V0", "V1", "V2", "own-V3")))
    p("")
    p("The named hole pitches on every string that reaches them (level change vs V0, dB: V1 / V2; dev V0 / V1 / V2):")
    for key, pitch in (("parlor-spruce", 78), ("jumbo-spruce", 72), ("parlor-maple", 78), ("auditorium-spruce", 75),
                       ("dreadnought-spruce", 73), ("jumbo-maple", 72)):
        for string, open_note in enumerate(ev.OPEN_NOTES, start=1):
            fret = pitch - open_note
            if 0 <= fret <= 20:
                per = results[key]["per"]
                d = {l: per[l][string] for l in ("V0", "V1", "V2")}
                p(f"  {key:22} {ev.note_name(pitch):>4} s{string} fret {fret:2d}: dlevel {d['V1']['levels'][fret]-d['V0']['levels'][fret]:+5.1f} "
                  f"{d['V2']['levels'][fret]-d['V0']['levels'][fret]:+5.1f}; dev {d['V0']['dev'][fret]:+5.1f} {d['V1']['dev'][fret]:+5.1f} {d['V2']['dev'][fret]:+5.1f}")
    p("")
    p(f"bit-identity check of string-6 renders against the E2-C6 sweep: {bitcheck} mismatches")
    (STUDY / "strings.json").write_text(json.dumps({k: {"rows": v["rows"], "own_setting": v["own_setting"]} for k, v in results.items()}, indent=1) + "\n")
    (STUDY / "strings.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
