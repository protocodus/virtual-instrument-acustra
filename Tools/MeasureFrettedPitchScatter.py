#!/usr/bin/env python3
"""Measure how much a fretted note's pitch differs from one note to the next.

GuitarSet (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018, Zenodo 3371780,
CC BY 4.0) tracks a pitch contour per string on its hex pickup (JAMS
``pitch_contour``) beside its ``note_midi`` notes. A note counts when it
lasts at least 300 ms and holds still: the 10th to 90th percentile of its
pitch over the window below spans under 15 cents, which leaves out bends,
vibrato and slides. Its pitch is the median contour frequency from 80 ms
after the onset to 100 ms before the end, in cents from the nearest
equal-tempered note; the fret is that note less the string's open note.

Notes are grouped by take, string and fret, so each string's tuning and the
compensation at each fret drop out and only the note-to-note scatter stays.
Each note's deviation from its group's mean, scaled by sqrt(n / (n - 1)),
gives a robust sigma (1.4826 times the median absolute deviation), with a
95% interval from 200 bootstrap resamples of the groups. Open strings are
the control: no finger presses them, so what they scatter by is everything
but the fretting hand (tracker, tuning drift, pluck).

Requires numpy. Usage:

    python3 Tools/MeasureFrettedPitchScatter.py --annotation-dir annotation/
"""

from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path

import numpy as np

OPEN = [40, 45, 50, 55, 59, 64]  # GuitarSet is in standard tuning


def notes_by_group(directory):
    groups = collections.defaultdict(list)
    for path in sorted(directory.glob("*.jams")):
        with open(path) as f:
            jam = json.load(f)
        contours, notes = {}, {}
        for annotation in jam["annotations"]:
            source = annotation["annotation_metadata"]["data_source"]
            if annotation["namespace"] == "pitch_contour":
                data = annotation["data"]
                values = [v["frequency"] if isinstance(v, dict) else v for v in data["value"]]
                contours[int(source)] = (np.array(data["time"], float), np.array(values, float))
            elif annotation["namespace"] == "note_midi":
                notes[int(source)] = [(n["time"], n["duration"]) for n in annotation["data"]]
        for string, string_notes in notes.items():
            if string not in contours:
                continue
            times, frequencies = contours[string]
            for onset, duration in string_notes:
                if duration < 0.3:
                    continue
                window = (times > onset + 0.08) & (times < onset + duration - 0.1) & (frequencies > 0)
                if window.sum() < 10:
                    continue
                cents = 1200.0 * np.log2(frequencies[window] / 440.0)
                if np.percentile(cents, 90) - np.percentile(cents, 10) > 15.0:
                    continue
                frequency = float(np.median(frequencies[window]))
                midi = int(round(69 + 12 * np.log2(frequency / 440.0)))
                fret = midi - OPEN[string]
                if 0 <= fret <= 20:
                    groups[(path.name, string, fret)].append(
                        1200.0 * np.log2(frequency / (440.0 * 2.0 ** ((midi - 69) / 12.0))))
    return groups


def robust_sigma(groups, keys):
    deviations = []
    for key in keys:
        g = np.array(groups[key])
        deviations.extend((g - g.mean()) / np.sqrt(1.0 - 1.0 / len(g)))
    return 1.4826 * float(np.median(np.abs(deviations))), len(deviations)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--annotation-dir", type=Path, required=True)
    args = parser.parse_args()
    groups = notes_by_group(args.annotation_dir)
    rng = np.random.default_rng(1)
    classes = [("open", lambda f: f == 0), ("frets 1-4", lambda f: 1 <= f <= 4),
               ("frets 5-9", lambda f: 5 <= f <= 9), ("frets 10+", lambda f: f >= 10),
               ("fretted", lambda f: f >= 1)]
    sigmas = {}
    for name, test in classes:
        keys = [k for k, g in groups.items() if test(k[2]) and len(g) >= 2]
        sigma, count = robust_sigma(groups, keys)
        boot = [robust_sigma(groups, [keys[i] for i in rng.integers(0, len(keys), len(keys))])[0]
                for _ in range(200)]
        sigmas[name] = sigma
        print(f"{name:10s} {count:6d} notes {len(keys):5d} groups  sigma {sigma:.2f} cents"
              f"  95% [{np.percentile(boot, 2.5):.2f}, {np.percentile(boot, 97.5):.2f}]")
    excess = np.sqrt(max(sigmas["fretted"] ** 2 - sigmas["open"] ** 2, 0.0))
    print(f"fretting finger's own sigma {excess:.2f} cents,"
          f" uniform half-width {excess * np.sqrt(3.0):.2f}")


if __name__ == "__main__":
    main()
