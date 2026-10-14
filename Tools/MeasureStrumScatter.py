#!/usr/bin/env python3
"""Measure how far a strum's strings sound from the stroke's own sweep.

GuitarSet's comping takes (Xi, Bittner, Pauwels, Ye, Bello, ISMIR 2018,
Zenodo 3371780, CC BY 4.0) carry a hex pickup, one channel per string. Its
rock, singer-songwriter and funk comping is strummed with a pick. Strokes are
the onset clusters of Tools/MeasureStrums.py with four or more strings, and
every member's attack is detected again on its own channel by
Tools/MeasureFingerChords.py's rule.

Each stroke's attacks are fitted by least squares against string number: the
line is the stroke's own sweep, whatever its speed and direction, and what is
left is each string's own moment about it (scaled by sqrt(n / (n - 2)) for the
two fitted values). Reported, for the picked strokes and for the jazz takes
as a cross-check:

- the residual's robust standard deviation (1.4826 times the median absolute
  deviation), its standard deviation and quantiles of its size, by stroke
  size and direction;
- within each take, the robust spread of its slower half of strokes over its
  faster half, against how much slower they are: a scatter that is a place on
  the sweep would grow with the stroke's time, one that is a time would not;
- the share of strokes of four, five and six strings whose attacks sound in
  string order, for strokes taking 4 to 12 ms a string;
- that share again from each such stroke's own size and fitted time a string,
  with every string moved by a uniform draw of --scatter-ms standard
  deviation plus a Gaussian detector error of --detector-ms (the same rule
  scatters 1.27 ms on the engine's own strokes, which have none:
  Docs/validation-playing-physics-20261013/probes), 20 draws a stroke.

Requires numpy and soundfile. Usage:

    python3 Tools/MeasureStrumScatter.py --annotation-zip annotation.zip \\
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

GROUPS = {"pick": ("Rock", "SS", "Funk"), "jazz": ("Jazz",)}
_STYLE_RE = re.compile(r"\d+_([A-Za-z]+)\d")
ORDER_RANGE_MS = (4.0, 12.0)


def robust_sd(values):
    v = np.asarray(values, dtype=float)
    return float(1.4826 * np.median(np.abs(v - np.median(v)))) if len(v) else float("nan")


def strokes(stem, ann_dir, audio_dir, window):
    """Each stroke of four or more strings: (strings, attack times in ms)."""
    strings_data, audio, rate = load_track(stem, ann_dir, audio_dir)
    env = envelopes(audio, rate)
    out = []
    for group in cluster(strings_data, use_offset=False, window=window):
        if len(group) < 4:
            continue
        found = []
        for onset, string in group:
            t = attack_time(env[string], rate, onset)
            if t is None:
                break
            found.append((string, t))
        if len(found) == len(group):
            s = np.array([x for x, _ in found], dtype=float)
            out.append((s, 1000.0 * np.array([t for _, t in found])))
    return out


def fit(s, t):
    design = np.vstack([np.ones_like(s), s]).T
    coefficients = np.linalg.lstsq(design, t, rcond=None)[0]
    residual = (t - design @ coefficients) * np.sqrt(len(s) / (len(s) - 2))
    return float(coefficients[1]), residual


def in_order(s, t):
    order = s[np.argsort(t, kind="stable")]
    steps = np.diff(order)
    return bool(np.all(steps > 0) or np.all(steps < 0))


def summary(residuals):
    r = np.asarray(residuals, dtype=float)
    return dict(n=int(len(r)), robust_sd_ms=robust_sd(r), sd_ms=float(np.std(r)),
                abs_quantiles_ms={str(q): float(np.percentile(np.abs(r), q))
                                  for q in (50, 75, 90, 95, 99)})


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
    report = dict(takes=len(stems), window_ms=window * 1000.0,
                  scatter_ms=args.scatter_ms, detector_ms=args.detector_ms, groups={})
    rng = np.random.default_rng(11)
    for group, styles in GROUPS.items():
        rows = []  # (stem, size, per-string ms, residuals, in order)
        for stem in stems:
            if _STYLE_RE.match(stem).group(1) not in styles:
                continue
            for s, t in strokes(stem, args.extract_dir, args.audio_dir, window):
                slope, residual = fit(s, t)
                rows.append((stem, len(s), slope, residual, in_order(s, t)))
        if not rows:
            continue
        everything = np.concatenate([r[3] for r in rows])
        entry = dict(strokes=len(rows), residual=summary(everything),
                     by_size={str(n): summary(np.concatenate([r[3] for r in rows if r[1] == n]))
                              for n in (4, 5, 6) if any(r[1] == n for r in rows)},
                     by_direction={d: summary(np.concatenate([r[3] for r in rows
                                                              if (r[2] > 0) == (d == "down")]))
                                   for d in ("down", "up")})

        # Within a take: the slower half of its strokes against the faster.
        ratios = []
        for stem in sorted(set(r[0] for r in rows)):
            take = [r for r in rows if r[0] == stem and 3.0 <= abs(r[2]) < 15.0]
            if sum(len(r[3]) for r in take) < 60:
                continue
            middle = np.median([abs(r[2]) for r in take])
            fast = [r for r in take if abs(r[2]) < middle]
            slow = [r for r in take if abs(r[2]) >= middle]
            if not fast or not slow:
                continue
            ratios.append((robust_sd(np.concatenate([r[3] for r in slow]))
                           / robust_sd(np.concatenate([r[3] for r in fast])),
                           np.median([abs(r[2]) for r in slow]) / np.median([abs(r[2]) for r in fast])))
        if ratios:
            ratios = np.array(ratios)
            entry["slower_over_faster"] = dict(
                takes=int(len(ratios)),
                scatter_ratio_median=float(np.median(ratios[:, 0])),
                scatter_ratio_quartiles=[float(np.percentile(ratios[:, 0], 25)),
                                         float(np.percentile(ratios[:, 0], 75))],
                time_per_string_ratio_median=float(np.median(ratios[:, 1])))

        # String order, measured and simulated, in the engine's usual range.
        lo, hi = ORDER_RANGE_MS
        half_width = args.scatter_ms * np.sqrt(3.0)
        order = {}
        for n in (4, 5, 6):
            chosen = [r for r in rows if r[1] == n and lo <= abs(r[2]) < hi]
            if not chosen:
                continue
            simulated = 0.0
            for r in chosen:
                hits = 0
                for _ in range(20):
                    t = (abs(r[2]) * np.arange(n) + rng.uniform(-half_width, half_width, n)
                         + rng.normal(0.0, args.detector_ms, n))
                    hits += bool(np.all(np.diff(t) > 0))
                simulated += hits / 20.0
            order[str(n)] = dict(strokes=len(chosen),
                                 measured_share=float(np.mean([r[4] for r in chosen])),
                                 simulated_share=simulated / len(chosen))
        entry["in_string_order"] = dict(time_per_string_ms=[lo, hi], by_size=order)
        report["groups"][group] = entry

    print(f"{len(stems)} takes, chord window {window * 1000.0:.1f} ms")
    for group, entry in report["groups"].items():
        r = entry["residual"]
        q = r["abs_quantiles_ms"]
        print(f"{group}: {entry['strokes']} strokes; residual about each stroke's own line:"
              f" robust SD {r['robust_sd_ms']:.2f} ms, SD {r['sd_ms']:.2f} ms,"
              f" |r| median {q['50']:.1f}, 90% {q['90']:.1f}, 99% {q['99']:.1f} ms")
        for key in ("by_size", "by_direction"):
            print("  " + ", ".join(f"{k}: robust {v['robust_sd_ms']:.2f} ms (n {v['n']})"
                                   for k, v in entry[key].items()))
        if "slower_over_faster" in entry:
            s = entry["slower_over_faster"]
            print(f"  within {s['takes']} takes, the slower half of the strokes"
                  f" ({s['time_per_string_ratio_median']:.2f} times the time a string)"
                  f" scatters {s['scatter_ratio_median']:.2f} times the faster half"
                  f" (quartiles {s['scatter_ratio_quartiles'][0]:.2f}-{s['scatter_ratio_quartiles'][1]:.2f})")
        print(f"  in string order at {ORDER_RANGE_MS[0]:.0f}-{ORDER_RANGE_MS[1]:.0f} ms a string,"
              f" measured vs a {args.scatter_ms:.2f} ms uniform scatter and {args.detector_ms:.2f} ms detector: "
              + ", ".join(f"{n} strings {100 * v['measured_share']:.0f}% vs {100 * v['simulated_share']:.0f}%"
                          f" ({v['strokes']})"
                          for n, v in entry["in_string_order"]["by_size"].items()))
    if args.json is not None:
        args.json.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
