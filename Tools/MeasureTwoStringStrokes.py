#!/usr/bin/env python3
"""Measure how far apart a guitarist's two strings of a two-note group sound.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string. Its
rock, singer-songwriter and funk comping is played with a pick, its bossa
nova with the thumb and fingers. A two-string group is two strings' annotated
onsets inside a window no longer than the shortest sixteenth note of the
takes (Tools/MeasureStrums.py) with no other onset, on any string, in the
120 ms before the first or after the second. Both attacks are detected again
on their own channels by Tools/MeasureFingerChords.py's rule.

For each style group and distance between the strings (1, 2, 3 or more):
how far apart the two attacks are, the share within 2 ms of each other, and
the shares with the lower string first and the higher string first by more
than 2 ms. For context, groups of exactly two, three and four strings chosen
the same way give the time a string between their outer strings.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureTwoStringStrokes.py --annotation-zip annotation.zip \\
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
from MeasureStrums import load_track, sixteenth_note_window  # noqa: E402
from MeasureFingerChords import attack_time, envelopes  # noqa: E402

GROUPS = {"pick": ("Rock", "SS", "Funk"), "finger": ("BN",), "jazz": ("Jazz",)}
_STYLE_RE = re.compile(r"\d+_([A-Za-z]+)\d")
ISOLATION = 0.120   # s with no other onset before and after the group
TOGETHER = 2.0      # ms


def groups_of(events, times, window):
    """Each run of onsets on distinct strings inside the window, isolated."""
    i = 0
    while i < len(events):
        group, seen, j = [events[i]], {events[i][1]}, i + 1
        while j < len(events) and events[j][0] - group[0][0] <= window:
            if events[j][1] not in seen:
                group.append(events[j])
                seen.add(events[j][1])
            j += 1
        if len(group) >= 2:
            first, last = group[0][0], group[-1][0]
            isolated = not np.any(((times < first) & (times > first - ISOLATION))
                                  | ((times > last) & (times < last + ISOLATION)))
            if isolated:
                yield group
            i = j
        else:
            i += 1


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
    gaps = {g: {d: [] for d in (1, 2, 3)} for g in GROUPS}   # t(higher) - t(lower), ms
    per_string = {g: {k: [] for k in (2, 3, 4)} for g in GROUPS}
    for stem in stems:
        style = _STYLE_RE.match(stem).group(1)
        group_name = next((g for g, styles in GROUPS.items() if style in styles), None)
        if group_name is None:
            continue
        strings_data, audio, rate = load_track(stem, args.extract_dir, args.audio_dir)
        env = envelopes(audio, rate)
        events = sorted((n["time"], s) for s, notes in strings_data for n in notes)
        times = np.array([t for t, _ in events])
        for group in groups_of(events, times, window):
            if len(group) > 4:
                continue
            found = []
            for onset, string in group:
                t = attack_time(env[string], rate, onset)
                if t is None:
                    break
                found.append((string, t))
            if len(found) != len(group):
                continue
            found.sort()
            low, high = found[0], found[-1]
            distance = high[0] - low[0]
            per_string[group_name][len(found)].append(1000.0 * abs(high[1] - low[1]) / distance)
            if len(found) == 2:
                gaps[group_name][min(distance, 3)].append(1000.0 * (high[1] - low[1]))

    report = dict(takes=len(stems), window_ms=window * 1000.0, groups={})
    print(f"{len(stems)} takes, chord window {window * 1000.0:.1f} ms")
    for g in GROUPS:
        entry = dict(two_strings={}, time_a_string_ms={})
        for d, values in gaps[g].items():
            v = np.array(values)
            if len(v) == 0:
                continue
            entry["two_strings"][f"{d}{'+' if d == 3 else ''}"] = dict(
                groups=int(len(v)),
                apart_ms=dict(median=float(np.median(np.abs(v))),
                              quartiles=[float(np.percentile(np.abs(v), 25)),
                                         float(np.percentile(np.abs(v), 75))]),
                together_share=float(np.mean(np.abs(v) < TOGETHER)),
                lower_first_share=float(np.mean(v > TOGETHER)),
                higher_first_share=float(np.mean(v < -TOGETHER)))
        for k, values in per_string[g].items():
            v = np.array(values)
            if len(v):
                entry["time_a_string_ms"][str(k)] = dict(
                    groups=int(len(v)), median=float(np.median(v)),
                    quartiles=[float(np.percentile(v, 25)), float(np.percentile(v, 75))])
        report["groups"][g] = entry
        print(f"{g}:")
        for d, e in entry["two_strings"].items():
            print(f"  two strings {d} apart: {e['groups']} groups, {e['apart_ms']['median']:.1f} ms apart"
                  f" (quartiles {e['apart_ms']['quartiles'][0]:.1f}-{e['apart_ms']['quartiles'][1]:.1f}),"
                  f" within {TOGETHER:.0f} ms {100 * e['together_share']:.0f}%,"
                  f" lower first {100 * e['lower_first_share']:.0f}%, higher first {100 * e['higher_first_share']:.0f}%")
        print("  time a string between the outer strings: "
              + ", ".join(f"{k} strings {e['median']:.1f} ms ({e['groups']})"
                          for k, e in entry["time_a_string_ms"].items()))
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
