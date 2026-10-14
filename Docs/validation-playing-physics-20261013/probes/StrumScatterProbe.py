#!/usr/bin/env python3
"""Tools/MeasureStrumScatter.py's measures on StrumScatterProbe's output.

Every pluck's attack is detected again on its string's force by
Tools/MeasureFingerChords.py's rule, searched about the scheduled pluck plus
5 ms (where an annotation would put it). For strokes of four or more
strings: the residual about each stroke's own least-squares line, for the
scheduled plucks and for the detected attacks, and the share of strokes
taking 4 to 12 ms a string whose detected attacks sound in string order.
Usage: StrumScatterProbe.py forces.f32 plucks.txt"""
import collections
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "Tools"))
from MeasureFingerChords import attack_time, envelopes  # noqa: E402

RATE = 44100.0
force = np.fromfile(sys.argv[1], dtype=np.float32).reshape(-1, 6).astype(float)
env = envelopes(force, RATE)
strokes = collections.defaultdict(list)
for line in open(sys.argv[2]):
    k, s, n = map(int, line.split())
    strokes[k].append((s, n / RATE))


def fit(found):
    s = np.array([x for x, _ in found], float)
    t = np.array([y for _, y in found]) * 1000.0
    design = np.vstack([np.ones_like(s), s]).T
    c = np.linalg.lstsq(design, t, rcond=None)[0]
    return abs(c[1]), (t - design @ c) * np.sqrt(len(s) / (len(s) - 2))


def ordered(found):
    steps = np.diff([s for s, _ in sorted(found, key=lambda x: x[1])])
    return bool(np.all(steps > 0) or np.all(steps < 0))


def robust_sd(r):
    return 1.4826 * np.median(np.abs(np.asarray(r) - np.median(r)))


scheduled, detected, slopes = [], [], []
order = collections.defaultdict(list)
for members in strokes.values():
    if len(members) < 4:
        continue
    found = []
    for s, t in members:
        attack = attack_time(env[s], RATE, t + 0.005)
        if attack is None:
            break
        found.append((s, attack))
    if len(found) != len(members):
        continue
    scheduled.extend(fit(members)[1])
    slope, residual = fit(found)
    detected.extend(residual)
    slopes.append(slope)
    if 4.0 <= slope < 12.0:
        order[len(found)].append(ordered(found))
print(f"{len(strokes)} strokes: scheduled plucks about their own lines, robust SD {robust_sd(scheduled):.2f} ms;"
      f" detected, robust SD {robust_sd(detected):.2f} ms, SD {np.std(detected):.2f} ms;"
      f" median time a string {np.median(slopes):.1f} ms; in string order at 4-12 ms a string: "
      + ", ".join(f"{n} strings {100 * np.mean(v):.0f}% ({len(v)})" for n, v in sorted(order.items())))
