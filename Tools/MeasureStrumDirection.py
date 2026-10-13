#!/usr/bin/env python3
"""Measure how a strum's stroke direction changes each string's attack.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string, with
uncalibrated per-coil sensitivity. A string's level is therefore never
compared with another string's. Each string is compared with itself on the
same channel of the same take: its attacks in upstrokes against its attacks
in downstrokes. The coil gain cancels from that ratio.

Strokes are the onset clusters of Tools/MeasureStrums.py (three or more
strings inside a window no longer than the shortest sixteenth note of the
takes used). A stroke whose strings sound low to high is a downstroke.

GuitarSet's annotated onsets trail the attacks they mark. Averaged over
every clustered string onset, the hex channels' first-difference energy
starts rising 16 ms before the annotation and peaks 4 ms before it, so every
window here starts ONSET_LEAD before the annotated time.

Comping strings ring on from the previous stroke, so a string's peak after
its onset partly measures what it already carried. The main attack measure
is the energy the onset added: the 40 ms mean square from the attack's start
less the 20 ms mean square just before it. A pluck's transient is
broadband, while what a string still carries is mostly its lower partials,
so the same difference on the first difference of the signal (+6 dB per
octave) over 20 ms is reported as a second measure; the plain peak over the
40 ms is the third, as MeasureStrums.py measures it. Strokes that added
nothing are counted, not dropped.

Per take and string, the median attack over its downstrokes and over its
upstrokes give one ratio in dB; takes with fewer than three strokes in
either direction for that string are left out. The ratios are summarised
over takes (median and a take-resampled 90% interval). Players accent beats
and their up strokes mostly fall between them, so a take's ratios also carry
an offset common to every string. The stroke's own tilt across the strings
is therefore summarised by a least-squares line through each take's ratios
against string number (three or more strings): its slope, in dB per string,
has no offset in it. The high E string's ratio is also subtracted per take
as a cruder check of the same thing.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureStrumDirection.py --annotation-zip annotation.zip \\
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
from MeasureStrums import cluster, load_track, sixteenth_note_window  # noqa: E402

ONSET_LEAD = 0.016       # s: the measured lead of the attack over its annotation
ATTACK_WINDOW = 0.040    # s from the attack's start, as MeasureStrums.LEVEL_WINDOW
BEFORE_WINDOW = 0.020    # s before it: what the string already carried
TRANSIENT_WINDOW = 0.020 # s each side of the attack's start, first difference
MIN_STROKES = 3
STRINGS = 6
STRING_NAMES = ["E2", "A2", "D3", "G3", "B3", "E4"]
BOOTSTRAP = 2000


def attack_measures(audio, rate, onset, string):
    channel = audio[:, string]
    start = int(round((onset - ONSET_LEAD) * rate))
    if start < 1:
        return None
    after = channel[start:start + int(ATTACK_WINDOW * rate)]
    before = channel[max(0, start - int(BEFORE_WINDOW * rate)):start]
    if len(after) == 0 or len(before) == 0:
        return None
    peak = float(np.max(np.abs(after)))
    added = float(np.mean(after ** 2) - np.mean(before ** 2))
    span = int(TRANSIENT_WINDOW * rate)
    slope = np.diff(channel[max(0, start - span - 1):start + span])
    split = len(slope) - span
    transient = (float(np.mean(slope[split:] ** 2) - np.mean(slope[:split] ** 2))
                 if 0 < split < len(slope) else 0.0)
    return peak, added, transient


def stroke_records(stem, ann_dir, audio_dir, window):
    strings_data, audio, rate = load_track(stem, ann_dir, audio_dir)
    records = []
    for group in cluster(strings_data, use_offset=False, window=window):
        onsets = [t for t, _ in group]
        order = [s for _, s in group]
        span = onsets[-1] - onsets[0]
        if span <= 0.0:
            continue
        direction = "down" if order[-1] > order[0] else "up"
        attacks = {}
        for onset, string in group:
            measured = attack_measures(audio, rate, onset, string)
            if measured is not None:
                attacks[string] = measured
        records.append(dict(direction=direction, order=order, onsets=onsets,
                            span=span, attacks=attacks))
    return records


def db_power(value):
    return 10.0 * np.log10(max(value, 1.0e-20))


def per_take_ratios(records):
    """Up-minus-down median attack per string for one take, in dB."""
    ratios = {"added": {}, "peak": {}, "transient": {}}
    counts = {}
    for string in range(STRINGS):
        down = [r["attacks"][string] for r in records
                if r["direction"] == "down" and string in r["attacks"]]
        up = [r["attacks"][string] for r in records
              if r["direction"] == "up" and string in r["attacks"]]
        counts[string] = (len(down), len(up))
        if len(down) < MIN_STROKES or len(up) < MIN_STROKES:
            continue
        for measure, index in (("added", 1), ("transient", 2)):
            down_value = np.median([max(m[index], 0.0) for m in down])
            up_value = np.median([max(m[index], 0.0) for m in up])
            if down_value > 0.0:
                ratios[measure][string] = db_power(up_value) - db_power(down_value)
        ratios["peak"][string] = (20.0 * np.log10(np.median([m[0] for m in up]))
                                  - 20.0 * np.log10(np.median([m[0] for m in down])))
    return ratios, counts


def inclusion(records):
    """Fraction of up and of down strokes that reach each string."""
    result = {}
    for direction in ("down", "up"):
        strokes = [r for r in records if r["direction"] == direction]
        result[direction] = [
            (sum(1 for r in strokes if s in r["order"]) / len(strokes)) if strokes else None
            for s in range(STRINGS)]
    return result


def summarise(values, rng):
    values = np.asarray(values, dtype=float)
    if len(values) == 0:
        return None
    resampled = np.median(rng.choice(values, size=(BOOTSTRAP, len(values)), replace=True), axis=1)
    return dict(n=int(len(values)), median=float(np.median(values)),
                low=float(np.percentile(resampled, 5)),
                high=float(np.percentile(resampled, 95)))


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
    rng = np.random.default_rng(20261013)
    measures = ("added", "transient", "peak")
    take_ratios = {m: {s: [] for s in range(STRINGS)} for m in measures}
    relative = {m: {s: [] for s in range(STRINGS)} for m in measures}
    reach = {"down": {s: [] for s in range(STRINGS)}, "up": {s: [] for s in range(STRINGS)}}
    slopes = {m: [] for m in measures}
    offsets = {m: [] for m in measures}
    speed_ratio = []
    stroke_totals = {"down": 0, "up": 0}
    for stem in stems:
        records = stroke_records(stem, args.extract_dir, args.audio_dir, window)
        for r in records:
            stroke_totals[r["direction"]] += 1
        ratios, _ = per_take_ratios(records)
        for measure in measures:
            for string, value in ratios[measure].items():
                take_ratios[measure][string].append(value)
            if len(ratios[measure]) >= 3:
                strings = np.array(sorted(ratios[measure]), dtype=float)
                values = np.array([ratios[measure][int(k)] for k in strings])
                centred = strings - strings.mean()
                slope = float(np.dot(centred, values - values.mean())
                              / np.dot(centred, centred))
                slopes[measure].append(slope)
                # The same line's value mid-neck, between the D and G strings.
                offsets[measure].append(float(values.mean()
                                              + slope * (2.5 - strings.mean())))
            reference = ratios[measure].get(STRINGS - 1)
            if reference is not None:
                for string, value in ratios[measure].items():
                    relative[measure][string].append(value - reference)
        fractions = inclusion(records)
        for direction in ("down", "up"):
            for s in range(STRINGS):
                if fractions[direction][s] is not None:
                    reach[direction][s].append(fractions[direction][s])
        # Traversal speed per direction, strings crossed per second, as the
        # take's own median: a ratio of medians keeps tempo out of it.
        speeds = {d: [(len(r["order"]) - 1) / r["span"] for r in records
                      if r["direction"] == d] for d in ("down", "up")}
        if len(speeds["down"]) >= MIN_STROKES and len(speeds["up"]) >= MIN_STROKES:
            speed_ratio.append(float(np.median(speeds["up"]) / np.median(speeds["down"])))

    report = dict(
        takes=len(stems), window_ms=window * 1000.0, strokes=stroke_totals,
        strings=STRING_NAMES,
        up_minus_down_db={m: [summarise(take_ratios[m][s], rng) for s in range(STRINGS)]
                          for m in take_ratios},
        relative_to_high_e_db={m: [summarise(relative[m][s], rng) for s in range(STRINGS)]
                               for m in relative},
        reach_fraction={d: [summarise(reach[d][s], rng) for s in range(STRINGS)]
                        for d in reach},
        tilt_db_per_string={m: summarise(slopes[m], rng) for m in measures},
        offset_mid_neck_db={m: summarise(offsets[m], rng) for m in measures},
        up_over_down_speed=summarise(speed_ratio, rng))

    print(f"takes {report['takes']}, window {report['window_ms']:.1f} ms, "
          f"strokes down {stroke_totals['down']} up {stroke_totals['up']}")
    for measure in measures:
        print(f"\nup minus down, {measure} attack (dB): median [90% take-resampled], takes")
        for s in range(STRINGS):
            a = report["up_minus_down_db"][measure][s]
            b = report["relative_to_high_e_db"][measure][s]
            if a is None:
                continue
            line = (f"  {STRING_NAMES[s]}: {a['median']:+6.2f} [{a['low']:+6.2f}, {a['high']:+6.2f}] n={a['n']}")
            if b is not None:
                line += (f"   relative to E4: {b['median']:+6.2f} "
                         f"[{b['low']:+6.2f}, {b['high']:+6.2f}] n={b['n']}")
            print(line)
    print("\nup-minus-down tilt across the strings, dB per string toward the high E")
    for measure in measures:
        t = report["tilt_db_per_string"][measure]
        if t is not None:
            print(f"  {measure:>9}: {t['median']:+.3f} [{t['low']:+.3f}, {t['high']:+.3f}] n={t['n']}")
    print("\nthe same lines' up-minus-down value mid-neck (between D and G), dB")
    for measure in measures:
        t = report["offset_mid_neck_db"][measure]
        if t is not None:
            print(f"  {measure:>9}: {t['median']:+.3f} [{t['low']:+.3f}, {t['high']:+.3f}] n={t['n']}")
    print("\nfraction of strokes reaching each string (median over takes)")
    for d in ("down", "up"):
        print(f"  {d:>4}: " + "  ".join(
            f"{STRING_NAMES[s]} {report['reach_fraction'][d][s]['median']:.2f}"
            for s in range(STRINGS) if report["reach_fraction"][d][s] is not None))
    if report["up_over_down_speed"]:
        v = report["up_over_down_speed"]
        print(f"\nup/down traversal speed: {v['median']:.3f} [{v['low']:.3f}, {v['high']:.3f}] n={v['n']}")
    if args.json:
        args.json.write_text(json.dumps(report, indent=1) + "\n")


if __name__ == "__main__":
    main()
