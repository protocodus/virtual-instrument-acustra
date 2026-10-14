#!/usr/bin/env python3
"""Measure how a finger-plucked chord's strings are timed against a picked one's.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) are played in five styles. Its bossa nova comping
is played with the thumb and fingers; rock, singer-songwriter and funk
comping is strummed with a pick. Chords are the onset clusters of
Tools/MeasureStrums.py: three or more strings whose annotated onsets fall
inside a window no longer than the shortest sixteenth note of the takes.

The annotated onsets come from a detector whose lag can differ from string to
string, so every chord member's attack is detected again here by one rule on
its own hex channel: the channel's first difference, squared and smoothed
over 1 ms, is searched from 40 ms before to 15 ms after the annotation; the
attack is where it last rises through a quarter of the way from the floor
before the window to its peak, walking back from the peak. A member whose
channel shows no attack four times over its floor leaves its chord out. The
same rule runs on every string and style, so a lag it has in common does not
move the comparison.

Reported per style group and chord size: the share of chords whose strings
sound in string order (low to high, a downstroke; high to low, an upstroke)
or in no order; the spread from first to last attack; and, for the
finger-plucked chords, each string's median lag behind the chord's highest
string by its rank from the top, with the robust spread of what is left
about that pattern (1.4826 times the median absolute deviation).

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureFingerChords.py --annotation-zip annotation.zip \\
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

SEARCH_BEFORE = 0.040   # s before the annotated onset
SEARCH_AFTER = 0.015    # s after it
FLOOR_WINDOW = 0.030    # s before the search: what the channel already carried
THRESHOLD = 0.25        # of the way from the floor to the peak
MIN_RISE = 4.0          # the peak over the floor for an attack to count
GROUPS = {"finger": ("BN",), "pick": ("Rock", "SS", "Funk"), "jazz": ("Jazz",)}
_STYLE_RE = re.compile(r"\d+_([A-Za-z]+)\d")


def envelopes(audio, rate):
    width = max(1, int(0.001 * rate))
    kernel = np.ones(width) / width
    return [np.convolve(np.diff(audio[:, s], prepend=audio[0, s]) ** 2, kernel, mode="same")
            for s in range(audio.shape[1])]


def attack_time(envelope, rate, onset):
    lo = max(0, int((onset - SEARCH_BEFORE) * rate))
    hi = min(len(envelope), int((onset + SEARCH_AFTER) * rate))
    if hi - lo < 8:
        return None
    segment = envelope[lo:hi]
    k = int(np.argmax(segment))
    peak = float(segment[k])
    before = envelope[max(0, lo - int(FLOOR_WINDOW * rate)):lo]
    floor = float(np.median(before)) if len(before) else 0.0
    if not (peak > 0.0 and peak > MIN_RISE * floor):
        return None
    level = floor + THRESHOLD * (peak - floor)
    j = k
    while j > 0 and segment[j - 1] >= level:
        j -= 1
    return (lo + j) / rate


def chords(stem, ann_dir, audio_dir, window):
    strings_data, audio, rate = load_track(stem, ann_dir, audio_dir)
    env = envelopes(audio, rate)
    out = []
    for group in cluster(strings_data, use_offset=False, window=window):
        found = []
        for onset, string in group:
            t = attack_time(env[string], rate, onset)
            if t is None:
                break
            found.append((string, t))
        if len(found) == len(group):
            out.append(found)
    return out


def order_of(found):
    by_time = [s for s, _ in sorted(found, key=lambda x: x[1])]
    if all(b > a for a, b in zip(by_time, by_time[1:])):
        return "down"
    if all(b < a for a, b in zip(by_time, by_time[1:])):
        return "up"
    return "none"


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
    found_by_group = {g: [] for g in GROUPS}
    for stem in stems:
        style = _STYLE_RE.match(stem).group(1)
        group = next((g for g, styles in GROUPS.items() if style in styles), None)
        if group is not None:
            found_by_group[group].extend(chords(stem, args.extract_dir, args.audio_dir, window))

    report = dict(takes=len(stems), window_ms=window * 1000.0, groups={})
    for group, found_list in found_by_group.items():
        entry = dict(chords=len(found_list), order_share={}, sizes={})
        orders = [order_of(f) for f in found_list]
        for kind in ("down", "up", "none"):
            entry["order_share"][kind] = orders.count(kind) / max(1, len(orders))
        for size in (3, 4, 5, 6):
            members = [f for f in found_list if len(f) == size]
            if not members:
                continue
            spreads = [1000.0 * (max(t for _, t in f) - min(t for _, t in f)) for f in members]
            lags = np.array([[1000.0 * (t - sorted(f, key=lambda x: -x[0])[0][1])
                              for _, t in sorted(f, key=lambda x: -x[0])] for f in members])
            pattern = np.median(lags, axis=0)
            residual = lags - pattern
            entry["sizes"][size] = dict(
                chords=len(members),
                spread_median_ms=float(np.median(spreads)),
                spread_quartiles_ms=[float(np.percentile(spreads, 25)),
                                     float(np.percentile(spreads, 75))],
                lag_behind_highest_by_rank_from_top_ms=[float(x) for x in pattern],
                highest_first_share=float(np.mean(np.argmin(lags, axis=1) == 0)),
                residual_robust_sd_ms=float(1.4826 * np.median(
                    np.abs(residual - np.median(residual)))))
        report["groups"][group] = entry

    print(f"{len(stems)} takes, chord window {window * 1000.0:.1f} ms")
    for group, entry in report["groups"].items():
        shares = ", ".join(f"{k} {100 * v:.0f}%" for k, v in entry["order_share"].items())
        print(f"\n{group}: {entry['chords']} chords; string order {shares}")
        for size, s in entry["sizes"].items():
            pattern = " ".join(f"{x:+5.2f}" for x in s["lag_behind_highest_by_rank_from_top_ms"])
            print(f"  {size} strings, {s['chords']} chords: spread median {s['spread_median_ms']:.1f} ms"
                  f" (quartiles {s['spread_quartiles_ms'][0]:.1f}-{s['spread_quartiles_ms'][1]:.1f});"
                  f" lag behind the highest string by rank from top [{pattern}] ms;"
                  f" residual robust SD {s['residual_robust_sd_ms']:.2f} ms")
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
