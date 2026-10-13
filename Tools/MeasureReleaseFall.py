#!/usr/bin/env python3
"""Measure how far a fretted note's pitch falls as the hand stops it.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string, and
per-string pitch contours tracked on those channels (JAMS ``pitch_contour``,
5.8 ms hop). A note counts when it lasts at least 250 ms and its string is
not played again within 300 ms of its annotated end, so the end is the
note's own release rather than the next note's attack.

For each such note the pitch is read against the median of its middle
(from 80 ms after the onset to 100 ms before the end), in cents, and the
channel's level against the median 10 ms RMS over the 300 ms to 150 ms
before the end (its sustain), in dB. Over the last 200 ms of the note each
10 ms frame gives a pair (level drop, pitch change). The report gives the
median pitch change in bins of level drop, for fretted notes and, as the
control, open strings, which have no fingertip to lift; and the median
pitch change over the last 30 ms the tracker follows.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureReleaseFall.py --annotation-zip annotation.zip \\
        --audio-dir /path/to/hex_cln_wavs [--json report.json]
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

import numpy as np
import soundfile as sf

sys.path.insert(0, str(Path(__file__).resolve().parent))
from MeasureStrums import load_track  # noqa: E402

OPEN = [40, 45, 50, 55, 59, 64]  # GuitarSet is in standard tuning
FRAME = 0.010
BINS = [(0, 3), (3, 6), (6, 10), (10, 15), (15, 20), (20, 25), (25, 30), (30, 40)]


def contours(path):
    with open(path) as f:
        jam = json.load(f)
    out = {}
    for annotation in jam["annotations"]:
        if annotation["namespace"] != "pitch_contour":
            continue
        data = annotation["data"]
        times = np.array(data["time"], dtype=float)
        values = np.array([v["frequency"] if isinstance(v, dict) else v
                           for v in data["value"]], dtype=float)
        out[int(annotation["annotation_metadata"]["data_source"])] = (times, values)
    return out


def frame_db(channel, rate, start, end):
    a, b = int(start * rate), int(end * rate)
    if a < 0 or b > len(channel) or b <= a:
        return None
    x = channel[a:b]
    return 10.0 * np.log10(float(np.mean(x * x)) + 1e-24)


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

    pairs = {"fretted": [], "open": []}
    last30 = {"fretted": [], "open": []}
    for stem in stems:
        strings_data, audio, rate = load_track(stem, args.extract_dir, args.audio_dir)
        tracks = contours(args.extract_dir / f"{stem}.jams")
        for string, notes in strings_data:
            if string not in tracks:
                continue
            times, freq = tracks[string]
            onsets = sorted(n["time"] for n in notes)
            for note in notes:
                start, end = note["time"], note["time"] + note["duration"]
                later = [o for o in onsets if o > start + 0.01]
                if note["duration"] < 0.25 or (later and later[0] - end < 0.3):
                    continue
                middle = (times > start + 0.08) & (times < end - 0.10) & (freq > 20.0)
                if middle.sum() < 5:
                    continue
                reference = float(np.median(freq[middle]))
                sustain = [frame_db(audio[:, string], rate, t, t + FRAME)
                           for t in np.arange(end - 0.30, end - 0.15, FRAME)]
                sustain = [v for v in sustain if v is not None]
                if not sustain:
                    continue
                level = float(np.median(sustain))
                kind = "open" if int(round(note["value"])) == OPEN[string] else "fretted"
                for t in np.arange(end - 0.20, end, FRAME):
                    inside = (times >= t) & (times < t + FRAME) & (freq > 20.0)
                    db = frame_db(audio[:, string], rate, t, t + FRAME)
                    if inside.any() and db is not None:
                        cents = 1200.0 * np.log2(float(np.median(freq[inside])) / reference)
                        pairs[kind].append((level - db, cents))
                tail = (times >= end - 0.03) & (times < end) & (freq > 20.0)
                if tail.any():
                    last30[kind].append(1200.0 * np.log2(float(np.median(freq[tail])) / reference))

    report = dict(takes=len(stems), kinds={})
    print(f"{len(stems)} takes")
    for kind in ("fretted", "open"):
        p = np.array(pairs[kind])
        rows = []
        for lo, hi in BINS:
            sel = p[(p[:, 0] >= lo) & (p[:, 0] < hi)]
            if len(sel) >= 20:
                rows.append(dict(drop_db=[lo, hi], frames=int(len(sel)),
                                 median_cents=float(np.median(sel[:, 1])),
                                 quartiles_cents=[float(np.percentile(sel[:, 1], 25)),
                                                  float(np.percentile(sel[:, 1], 75))]))
        tail = np.array(last30[kind])
        report["kinds"][kind] = dict(notes=int(len(tail)),
                                     last_30_ms_cents=dict(median=float(np.median(tail)),
                                                           quartiles=[float(np.percentile(tail, 25)),
                                                                      float(np.percentile(tail, 75))]),
                                     by_level_drop=rows)
        print(f"\n{kind}: {len(tail)} notes; last 30 ms tracked, median {np.median(tail):+.1f} cents")
        for row in rows:
            lo, hi = row["drop_db"]
            print(f"  level {lo:2d}-{hi:2d} dB under the sustain: {row['median_cents']:+6.1f} cents"
                  f" (quartiles {row['quartiles_cents'][0]:+.1f} to {row['quartiles_cents'][1]:+.1f}),"
                  f" {row['frames']} frames")
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
