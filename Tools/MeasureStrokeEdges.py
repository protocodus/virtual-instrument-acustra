#!/usr/bin/env python3
"""Measure how hard a pick strikes each string across one stroke.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string, with
uncalibrated per-coil sensitivity; its rock, singer-songwriter and funk
comping is strummed with a pick. Strokes are the onset clusters of
Tools/MeasureStrums.py with four or more strings, each attack detected again
on its own channel by Tools/MeasureFingerChords.py's rule. A stroke's
direction is the sign of its attacks' least-squares slope against string
number; the string it enters on is its lowest on a downstroke and its
highest on an upstroke, the string it leaves on the other end.

Each attack is measured as Tools/MeasureStrumDirection.py measures it, by
the energy it added, the plain 40 ms peak and the transient, from the
re-detected attack. A level is that string's own value less its median in
the take, so the coil's gain cancels; it is then compared only with the
other strings of the same stroke (the levels are demeaned within each
stroke), so the stroke's own force, and an accent or a lighter return
stroke, cancel too. What is left is fitted by least squares as an entry
string's and an exit string's difference from the strings inside the
stroke, plus a tilt across string number for downstrokes and one for
upstrokes. The same is fitted with the tilts alone, as the 1.4.0 upstroke
model has it. 90% intervals come from resampling the takes.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureStrokeEdges.py --annotation-zip annotation.zip \\
        --audio-dir /path/to/hex_cln_wavs [--json report.json]
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from MeasureStrums import cluster, load_track, sixteenth_note_window  # noqa: E402
from MeasureFingerChords import attack_time, envelopes  # noqa: E402
from MeasureStrumDirection import ONSET_LEAD, attack_measures  # noqa: E402

PICKED = ("Rock", "SS", "Funk")
_STYLE_RE = re.compile(r"\d+_([A-Za-z]+)\d")
MEASURES = ("added", "peak", "transient")
TERMS = ("entry", "exit", "downstroke_tilt_per_string", "upstroke_tilt_per_string")
RESAMPLES = 300


def stroke_rows(stem, ann_dir, audio_dir, window):
    """Per attack: (string, upstroke, entry, exit, added dB, peak dB, transient dB)."""
    strings_data, audio, rate = load_track(stem, ann_dir, audio_dir)
    env = envelopes(audio, rate)
    strokes = []
    for group in cluster(strings_data, use_offset=False, window=window):
        if len(group) < 4:
            continue
        found = []
        for onset, string in group:
            t = attack_time(env[string], rate, onset)
            if t is None:
                break
            found.append((string, t))
        if len(found) != len(group):
            continue
        s = np.array([x for x, _ in found], dtype=float)
        t = np.array([y for _, y in found])
        slope = np.linalg.lstsq(np.vstack([np.ones_like(s), s]).T, t, rcond=None)[0][1]
        if slope == 0.0:
            continue
        up = slope < 0.0
        low, high = int(s.min()), int(s.max())
        entry, exit_ = (high, low) if up else (low, high)
        rows = []
        for string, attack in found:
            measured = attack_measures(audio, rate, attack + ONSET_LEAD, string)
            if measured is None:
                continue
            peak, added, transient = measured
            if added <= 0.0 or transient <= 0.0 or peak <= 0.0:
                continue
            rows.append((string, up, string == entry, string == exit_,
                         10.0 * np.log10(added), 20.0 * np.log10(peak),
                         10.0 * np.log10(transient)))
        if len(rows) >= 3:
            strokes.append(rows)
    return strokes


def design(rows, joint):
    k = np.array([r[0] for r in rows], dtype=float) - 2.5
    up = np.array([r[1] for r in rows], dtype=float)
    columns = [(1.0 - up) * k, up * k]
    if joint:
        columns = [np.array([r[2] for r in rows], dtype=float),
                   np.array([r[3] for r in rows], dtype=float)] + columns
    return np.vstack(columns).T


def prepare(takes):
    """Each take's strokes, levels less the take's per-string medians, then
    every column demeaned within its stroke."""
    blocks = []
    for strokes in takes:
        medians = {}
        for column in range(4, 7):
            for string in range(6):
                values = [r[column] for stroke in strokes for r in stroke if r[0] == string]
                if values:
                    medians[(column, string)] = float(np.median(values))
        x_joint, x_tilt, y = [], [], []
        for stroke in strokes:
            levels = np.array([[r[c] - medians[(c, r[0])] for c in range(4, 7)] for r in stroke])
            xj, xt = design(stroke, True), design(stroke, False)
            x_joint.append(xj - xj.mean(axis=0))
            x_tilt.append(xt - xt.mean(axis=0))
            y.append(levels - levels.mean(axis=0))
        if y:
            blocks.append((np.vstack(x_joint), np.vstack(x_tilt), np.vstack(y)))
    return blocks


def fit(blocks):
    xj = np.vstack([b[0] for b in blocks])
    xt = np.vstack([b[1] for b in blocks])
    y = np.vstack([b[2] for b in blocks])
    return (np.linalg.lstsq(xj, y, rcond=None)[0], np.linalg.lstsq(xt, y, rcond=None)[0])


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--annotation-zip", type=Path, default=None)
    parser.add_argument("--audio-dir", type=Path, required=True)
    parser.add_argument("--extract-dir", type=Path, default=Path("ann"))
    parser.add_argument("--json", type=Path, default=None)
    args = parser.parse_args()

    stems = sorted(p.name[:-len("_hex_cln.wav")]
                   for p in args.audio_dir.glob("*_comp_hex_cln.wav"))
    if not stems:
        raise SystemExit("no *_comp_hex_cln.wav files found")
    args.extract_dir.mkdir(parents=True, exist_ok=True)
    missing = [s for s in stems if not (args.extract_dir / f"{s}.jams").exists()]
    if missing:
        if args.annotation_zip is None:
            raise SystemExit(f"--annotation-zip required to fetch: {missing[:3]}...")
        with zipfile.ZipFile(args.annotation_zip) as zf:
            for stem in missing:
                zf.extract(f"{stem}.jams", args.extract_dir)

    window = sixteenth_note_window(stems)
    takes = [stroke_rows(stem, args.extract_dir, args.audio_dir, window)
             for stem in stems if _STYLE_RE.match(stem).group(1) in PICKED]
    takes = [t for t in takes if t]
    blocks = prepare(takes)
    joint, tilt = fit(blocks)
    rng = np.random.default_rng(2)
    boot_joint, boot_tilt = [], []
    for _ in range(RESAMPLES):
        sample = [blocks[i] for i in rng.integers(0, len(blocks), len(blocks))]
        j, t = fit(sample)
        boot_joint.append(j)
        boot_tilt.append(t)
    boot_joint, boot_tilt = np.array(boot_joint), np.array(boot_tilt)

    strokes = sum(len(t) for t in takes)
    report = dict(takes=len(takes), strokes=strokes, window_ms=window * 1000.0,
                  joint={}, tilts_alone={})
    print(f"{len(takes)} picked takes, {strokes} strokes of four or more strings")
    for m, measure in enumerate(MEASURES):
        report["joint"][measure] = {
            term: dict(db=float(joint[i, m]),
                       interval_90=[float(np.percentile(boot_joint[:, i, m], 5)),
                                    float(np.percentile(boot_joint[:, i, m], 95))])
            for i, term in enumerate(TERMS)}
        report["tilts_alone"][measure] = {
            term: dict(db=float(tilt[i, m]),
                       interval_90=[float(np.percentile(boot_tilt[:, i, m], 5)),
                                    float(np.percentile(boot_tilt[:, i, m], 95))])
            for i, term in enumerate(TERMS[2:])}
        e = report["joint"][measure]
        print(f"{measure:9s} joint: " + ", ".join(
            f"{term} {e[term]['db']:+.2f} ({e[term]['interval_90'][0]:+.2f} to {e[term]['interval_90'][1]:+.2f})"
            for term in TERMS))
        a = report["tilts_alone"][measure]
        print(f"{'':9s} tilts alone: down {a[TERMS[2]]['db']:+.2f}, up {a[TERMS[3]]['db']:+.2f} dB a string,"
              f" up minus down {a[TERMS[3]]['db'] - a[TERMS[2]]['db']:+.2f}")
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
