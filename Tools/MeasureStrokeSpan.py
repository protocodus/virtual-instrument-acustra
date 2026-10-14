#!/usr/bin/env python3
"""Measure how a picked stroke's span grows with the strings it crosses.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string; its
rock, singer-songwriter and funk comping is strummed with a pick. A stroke
here is a run of two to six strings' annotated onsets inside a window no
longer than the shortest sixteenth note of the takes (Tools/MeasureStrums.py),
each attack detected again on its own channel by Tools/MeasureFingerChords.py's
rule. Its time a string is the least-squares slope of its attacks against
string number (for two strings, their distance in time over the strings
apart); its gaps are the spacings from its lowest to its highest string.

Reported:

- the median time a string by stroke size, pooled over the takes;
- within each take, the median time a string of its strokes of each size
  over the median of its four-string strokes, and the median of those
  ratios over the takes (a take counts for a size with five strokes of it
  and five of four strings): one player's own profile, which the pooled
  medians mix with the takes' different sizes and speeds;
- that profile for simulated strokes whose time a string is the take's
  four-string median times (gaps / 3)**exponent, each string moved by a
  uniform draw of --scatter-ms standard deviation plus --detector-ms of
  detector error and measured the same way, over a grid of exponents; the
  exponent whose profile is closest to the corpus's (squared log error
  weighted by takes), and its 90% interval from resampling the takes;
- the within-take least-squares slope of log time a string on log gaps,
  which each string's scatter biases toward zero.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureStrokeSpan.py --annotation-zip annotation.zip \\
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

PICKED = ("Rock", "SS", "Funk")
_STYLE_RE = re.compile(r"\d+_([A-Za-z]+)\d")
EXPONENTS = np.round(np.arange(-0.20, -0.5501, -0.01), 2)
SIZES = (2, 3, 5, 6)


def strokes(stem, ann_dir, audio_dir, window):
    """(strings, gaps, time a string in ms) of every stroke of 2-6 strings."""
    strings_data, audio, rate = load_track(stem, ann_dir, audio_dir)
    env = envelopes(audio, rate)
    events = sorted((n["time"], s) for s, notes in strings_data for n in notes)
    out, i = [], 0
    while i < len(events):
        group, seen, j = [events[i]], {events[i][1]}, i + 1
        while j < len(events) and events[j][0] - group[0][0] <= window:
            if events[j][1] not in seen:
                group.append(events[j])
                seen.add(events[j][1])
            j += 1
        if len(group) < 2:
            i += 1
            continue
        i = j
        found = []
        for onset, string in group:
            t = attack_time(env[string], rate, onset)
            if t is None:
                break
            found.append((string, t))
        if len(found) != len(group):
            continue
        s = np.array([x for x, _ in found], dtype=float)
        t = 1000.0 * np.array([y for _, y in found])
        gaps = s.max() - s.min()
        out.append((len(found), gaps, time_a_string(s, t, gaps)))
    return out


def time_a_string(s, t, gaps):
    if len(s) == 2:
        return abs(t[1] - t[0]) / gaps
    design = np.vstack([np.ones_like(s), s]).T
    return abs(np.linalg.lstsq(design, t, rcond=None)[0][1])


def within_take_slope(rows):
    """rows: (take, strings, gaps, tau). Slope of log tau on log gaps, demeaned by take."""
    x_all, y_all = [], []
    for take in sorted(set(r[0] for r in rows)):
        sel = [r for r in rows if r[0] == take and r[3] > 0.3]
        if len(sel) < 20:
            continue
        x = np.log10([r[2] for r in sel])
        y = np.log10([r[3] for r in sel])
        x_all.append(x - x.mean())
        y_all.append(y - y.mean())
    x, y = np.concatenate(x_all), np.concatenate(y_all)
    return float(np.dot(x, y) / np.dot(x, x))


def take_ratios(rows):
    """Each take's median time a string by size over its four-string median."""
    ratios = {}
    for take in sorted(set(r[0] for r in rows)):
        own = [r for r in rows if r[0] == take]
        four = [r[3] for r in own if r[1] == 4]
        if len(four) < 5:
            continue
        for n in SIZES:
            sized = [r[3] for r in own if r[1] == n]
            if len(sized) >= 5:
                ratios[(take, n)] = float(np.median(sized) / np.median(four))
    return ratios


def profile(ratios, takes=None):
    """The median ratio by size over the takes (with repeats, for resampling)."""
    takes = takes if takes is not None else sorted(set(t for t, _ in ratios))
    out = {}
    for n in SIZES:
        values = [ratios[(t, n)] for t in takes if (t, n) in ratios]
        out[n] = (float(np.median(values)), len(values))
    return out


def closest(corpus, simulated):
    """The exponent whose simulated profile is nearest the corpus's."""
    def error(sim):
        weight = sum(k for _, k in corpus.values())
        return sum(k * np.log(sim[n][0] / v) ** 2 for n, (v, k) in corpus.items()) / weight
    errors = {beta: error(sim) for beta, sim in simulated.items()}
    best = min(errors, key=errors.get)
    return best, errors


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--annotation-zip", type=Path, default=None)
    parser.add_argument("--audio-dir", type=Path, required=True)
    parser.add_argument("--extract-dir", type=Path, default=Path("ann"))
    parser.add_argument("--scatter-ms", type=float, default=4.35)
    parser.add_argument("--detector-ms", type=float, default=1.27)
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
    rows = []   # (take, strings, gaps, tau)
    for stem in stems:
        if _STYLE_RE.match(stem).group(1) not in PICKED:
            continue
        for n, gaps, tau in strokes(stem, args.extract_dir, args.audio_dir, window):
            rows.append((stem, n, gaps, tau))

    by_size = {}
    for n in range(2, 7):
        tau = np.array([r[3] for r in rows if r[1] == n])
        by_size[str(n)] = dict(strokes=int(len(tau)), median_ms=float(np.median(tau)),
                               quartiles_ms=[float(np.percentile(tau, 25)),
                                             float(np.percentile(tau, 75))])

    ratios = take_ratios(rows)
    corpus = profile(ratios)
    half_width = args.scatter_ms * np.sqrt(3.0)
    takes = sorted(set(r[0] for r in rows))
    simulated = {}
    for beta in EXPONENTS:
        sim_rng = np.random.default_rng(5)
        sim = []
        for take in takes:
            own = [r for r in rows if r[0] == take]
            four = [r[3] for r in own if r[1] == 4] or [r[3] for r in own]
            base = np.median(four) / 3.0 ** beta
            for _, n, g, _ in own:
                position = np.round(np.linspace(0.0, g, n))
                t = (base * g ** beta * position + sim_rng.uniform(-half_width, half_width, n)
                     + sim_rng.normal(0.0, args.detector_ms, n))
                sim.append((take, n, g, time_a_string(position, t, g)))
        simulated[float(beta)] = profile(take_ratios(sim))
    best, errors = closest(corpus, simulated)
    boot_rng = np.random.default_rng(7)
    ratio_takes = sorted(set(t for t, _ in ratios))
    boot = [closest(profile(ratios, list(boot_rng.choice(ratio_takes, len(ratio_takes)))), simulated)[0]
            for _ in range(500)]

    report = dict(takes=len(stems), window_ms=window * 1000.0, strokes=len(rows),
                  time_a_string_by_strings=by_size,
                  within_take_over_four_strings={str(n): dict(median=v, takes=k)
                                                 for n, (v, k) in corpus.items()},
                  exponent_of_time_a_string=dict(
                      best=best, interval_90=[float(np.percentile(boot, 5)),
                                              float(np.percentile(boot, 95))],
                      profile_rms_log_error={f"{b:+.2f}": float(np.sqrt(e)) for b, e in errors.items()},
                      simulated_profile_at_best={str(n): v for n, (v, _) in simulated[best].items()}),
                  within_take_log_slope=within_take_slope(rows))
    print(f"{len(stems)} takes, chord window {window * 1000.0:.1f} ms, {len(rows)} picked strokes")
    print("time a string, median by strokes' strings: "
          + ", ".join(f"{n}: {e['median_ms']:.1f} ms ({e['strokes']})" for n, e in by_size.items()))
    print("within takes, over the take's four-string median: "
          + ", ".join(f"{n}: {v:.2f} ({k} takes)" for n, (v, k) in corpus.items()))
    print(f"closest simulated exponent of time a string on gaps: {best:+.2f}"
          f" (90% {np.percentile(boot, 5):+.2f} to {np.percentile(boot, 95):+.2f}), whose profile is "
          + ", ".join(f"{n}: {v:.2f}" for n, (v, _) in simulated[best].items()))
    print(f"within takes, least-squares slope of log time a string on log gaps: "
          f"{report['within_take_log_slope']:+.3f} (biased toward zero)")
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
