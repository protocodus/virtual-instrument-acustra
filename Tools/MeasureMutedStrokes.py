#!/usr/bin/env python3
"""Measure what a strum does to a string inside its span that the chord leaves out.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string.
Strokes are the onset clusters of Tools/MeasureStrums.py: three or more
strings inside a window no longer than the shortest sixteenth note of the
takes, in string order (a downstroke low to high, an upstroke high to low).
A string strictly between a stroke's first and last strings with no onset
of its own in the stroke is one the chord leaves out. The pick crosses it at
a time interpolated, by string number, between the attacks of the strings on
either side of it. Every attack is re-detected on its own channel by
Tools/MeasureFingerChords.py's rule, so annotation lag does not move it.

For each such crossed string, on its own channel (the coil gain cancels
from every ratio here):

- the rise: mean square over the 25 ms from the crossing against the 30 ms
  ending 10 ms before it;
- the strike: the 40 ms peak after the crossing against the median 40 ms
  peak of the same string's own annotated notes in the same take;
- the fall: from that peak, the time until a 5 ms RMS envelope first sits
  20 dB under it, within 400 ms.

As a control for bleed from the other strings, the same rise is read on
strings outside the stroke (beyond its first or last string) at the time
the stroke would reach them if it went on at its own pace. Such a string can
also be struck by a pick that carries on past the chord; the report gives
its distribution separately for the first string beyond each end.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureMutedStrokes.py --annotation-zip annotation.zip \\
        --audio-dir /path/to/hex_cln_wavs [--json report.json]
"""

from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from MeasureStrums import cluster, load_track, sixteenth_note_window  # noqa: E402
from MeasureFingerChords import attack_time, envelopes  # noqa: E402

RISE_AFTER = 0.025      # s from the crossing
RISE_BEFORE = 0.030     # s, ending RISE_GAP before it
RISE_GAP = 0.010
PEAK_WINDOW = 0.040     # s, as MeasureStrums.LEVEL_WINDOW
FALL_WINDOW = 0.005     # s RMS frames for the fall
FALL_LIMIT = 0.400      # s
STRINGS = 6
STRING_NAMES = ["E2", "A2", "D3", "G3", "B3", "E4"]


def mean_square(x):
    return float(np.mean(x * x)) if len(x) else 0.0


def rise_db(channel, rate, t):
    a = int(t * rate)
    b = a + int(RISE_AFTER * rate)
    c = a - int(RISE_GAP * rate)
    d = c - int(RISE_BEFORE * rate)
    if d < 0 or b > len(channel):
        return None
    after, before = mean_square(channel[a:b]), mean_square(channel[d:c])
    if after <= 0.0 or before <= 0.0:
        return None
    return 10.0 * np.log10(after / before)


def peak(channel, rate, t):
    a = max(0, int(t * rate))
    b = min(len(channel), a + int(PEAK_WINDOW * rate))
    return float(np.max(np.abs(channel[a:b]))) if b > a else 0.0


def fall_ms(channel, rate, t):
    frame = max(1, int(FALL_WINDOW * rate))
    a = max(0, int(t * rate))
    b = min(len(channel), a + int(FALL_LIMIT * rate))
    frames = (b - a) // frame
    if frames < 4:
        return None
    rms = np.sqrt(np.mean(channel[a:a + frames * frame].reshape(frames, frame) ** 2, axis=1))
    top = int(np.argmax(rms[:max(1, int(PEAK_WINDOW / FALL_WINDOW))]))
    below = np.nonzero(rms[top:] < rms[top] * 0.1)[0]
    return None if len(below) == 0 else 1000.0 * below[0] * frame / rate


def take_records(stem, ann_dir, audio_dir, window):
    strings_data, audio, rate = load_track(stem, ann_dir, audio_dir)
    env = envelopes(audio, rate)
    played_peaks = {}
    for string, notes in strings_data:
        peaks = [peak(audio[:, string], rate, n["time"]) for n in notes]
        peaks = [p for p in peaks if p > 0.0]
        if peaks:
            played_peaks[string] = float(np.median(peaks))
    crossed, outside = [], []
    for group in cluster(strings_data, use_offset=False, window=window):
        found = []
        for onset, string in group:
            t = attack_time(env[string], rate, onset)
            if t is None:
                break
            found.append((string, t))
        if len(found) != len(group):
            continue
        by_time = sorted(found, key=lambda x: x[1])
        order = [s for s, _ in by_time]
        if all(b > a for a, b in zip(order, order[1:])):
            direction = "down"
        elif all(b < a for a, b in zip(order, order[1:])):
            direction = "up"
        else:
            continue
        by_string = sorted(found)
        times = dict(found)
        present = set(times)
        low, high = by_string[0][0], by_string[-1][0]
        span = by_time[-1][1] - by_time[0][1]
        per_string = span / max(1, abs(order[-1] - order[0]))
        for s in range(low + 1, high):
            if s in present:
                continue
            below = max(k for k in present if k < s)
            above = min(k for k in present if k > s)
            t = times[below] + (times[above] - times[below]) * (s - below) / (above - below)
            record = dict(string=s, direction=direction, rise_db=rise_db(audio[:, s], rate, t))
            if s in played_peaks:
                p = peak(audio[:, s], rate, t)
                if p > 0.0:
                    record["strike_db"] = 20.0 * np.log10(p / played_peaks[s])
            record["fall_ms"] = fall_ms(audio[:, s], rate, t)
            crossed.append(record)
        # The first string beyond each end, at the stroke's own pace.
        last, first = by_time[-1], by_time[0]
        step = 1 if direction == "down" else -1
        for end, s, t, label in ((last, last[0] + step, last[1] + per_string, "after"),
                                 (first, first[0] - step, first[1] - per_string, "before")):
            if 0 <= s < STRINGS and s not in present:
                if t > 0.0:
                    side = "treble" if s > end[0] else "bass"
                    record = dict(string=s, direction=direction, end=label, side=side,
                                  rise_db=rise_db(audio[:, s], rate, t))
                    if s in played_peaks:
                        p = peak(audio[:, s], rate, t)
                        if p > 0.0:
                            record["strike_db"] = 20.0 * np.log10(p / played_peaks[s])
                    outside.append(record)
    return crossed, outside


def quantiles(values):
    v = np.array([x for x in values if x is not None], dtype=float)
    if len(v) == 0:
        return None
    return dict(n=int(len(v)), median=float(np.median(v)),
                quartiles=[float(np.percentile(v, 25)), float(np.percentile(v, 75))])


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
    crossed, outside = [], []
    for stem in stems:
        c, o = take_records(stem, args.extract_dir, args.audio_dir, window)
        crossed.extend(c)
        outside.extend(o)

    report = dict(takes=len(stems), window_ms=window * 1000.0,
                  crossed=dict(all=dict(rise_db=quantiles(r["rise_db"] for r in crossed),
                                        strike_db=quantiles(r.get("strike_db") for r in crossed),
                                        fall_20db_ms=quantiles(r["fall_ms"] for r in crossed)),
                               by_string={STRING_NAMES[s]: dict(
                                   strike_db=quantiles(r.get("strike_db") for r in crossed if r["string"] == s),
                                   fall_20db_ms=quantiles(r["fall_ms"] for r in crossed if r["string"] == s))
                                   for s in range(STRINGS)
                                   if any(r["string"] == s for r in crossed)}),
                  outside={f"{e}-{side}": dict(
                      rise_db=quantiles(r["rise_db"] for r in outside if r["end"] == e and r["side"] == side),
                      strike_db=quantiles(r.get("strike_db") for r in outside if r["end"] == e and r["side"] == side))
                      for e in ("before", "after") for side in ("bass", "treble")})

    def show(label, q):
        if q is None:
            return f"{label}: none"
        return (f"{label}: median {q['median']:+.1f} (quartiles {q['quartiles'][0]:+.1f}"
                f" to {q['quartiles'][1]:+.1f}), n {q['n']}")

    print(f"{len(stems)} takes, chord window {window * 1000.0:.1f} ms")
    print("Strings a stroke crosses that its chord leaves out:")
    for key, label in (("rise_db", "rise over the string's own floor, dB"),
                       ("strike_db", "strike against its own played attacks, dB"),
                       ("fall_20db_ms", "20 dB fall, ms")):
        print("  " + show(label, report["crossed"]["all"][key]))
    for name, entry in report["crossed"]["by_string"].items():
        print(f"    {name}: " + show("strike dB", entry["strike_db"]) + "; " + show("20 dB fall ms", entry["fall_20db_ms"]))
    print("The first string beyond either end of the stroke:")
    for key, entry in report["outside"].items():
        print(f"  {key}: " + show("rise dB", entry["rise_db"]) + "; " + show("strike dB", entry["strike_db"]))
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
