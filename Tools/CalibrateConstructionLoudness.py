#!/usr/bin/env python3
"""Level every construction and Picking to one loudness, and check headroom.

Every construction (Model x Shape x Wood) and every Picking
is rendered through AcustraPerformanceRenderer (the shipping engine) at the
default controls and Output, playing one fixed, seeded phrase set: strums
over a chord progression, single notes low and high on the neck and held
chords, at velocities from soft to hard. Each render is measured as ITU-R
BS.1770-4 integrated loudness (Tools/MeasureMaterialLoudness.py's meter),
on the stereo microphones, the mono microphone and the piezo.

The target is the default construction - the Original model, Dreadnought,
Spruce, Finger - as it plays on the stereo
microphones, so the default patch keeps its loudness. For every other cell
the tool writes the gain that brings its stereo microphones to that target,
and for the mono microphone and the piezo the factor, relative to that gain,
that brings each of them to the same target: the three captures stay level
with each other on every construction. The data header it writes,
Source/DSP/ConstructionLoudnessData.h, holds those gains keyed by the four
settings (2 x 4 x 3 x 3 cells); AcustraEngine applies them where it applies
the strings' output reference, through the same smoothing. A gain changes
only the level: each construction keeps its tone.

It also plays the hardest case - velocity 127 with the Pick at Touch 1 and
Pluck Position 0: an open E major downstroke with two strings repicked into
it, all six strings on one sample, and a fast down-up pair - on every
construction and reports the peak before the output's safety limiter (the
limiter is inverted exactly; it is linear below its knee at -1 dBFS). The
aim is that peak at least 1 dB under the knee, but loudness comes first: a
Pick cell whose hardest case would come nearer sits under the target by
what it needs, at most 0.9 LU (--max-headroom-cut), inside the tolerance.
With the loudness fixed, a construction's peak is its own crest factor
(Docs/decisions.md).
With the gains built in, `--check` requires every cell within +-1 LU of the
target on each capture, and every Pick cell whose hardest case is short of
the 1 dB to have given up the level the tolerance allows; the report lists
the peaks.

  python3 Tools/CalibrateConstructionLoudness.py \\
      --renderer ./build-dsp/AcustraPerformanceRenderer --write-header
  python3 Tools/CalibrateConstructionLoudness.py --renderer ... --check

A change that moves one model's level rewrites only that model's cells,
keeping every other cell's built gain (the target stays the default cell):

  python3 Tools/CalibrateConstructionLoudness.py --renderer ... \\
      --json m.json --write-header --models bellido1978
  python3 Tools/CalibrateConstructionLoudness.py --self-test

The gains already built in (read from the header) are divided out of each
measurement, so writing the header again from a build that has them gives the
same gains. Renders go to a temporary directory and are removed as soon as
they are measured; nothing is downloaded and no audio is kept. NumPy and SciPy
are required.
"""

from __future__ import annotations

import argparse
import itertools
import json
import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import MeasureMaterialLoudness as meter  # noqa: E402

RATE = meter.RATE
HEADER = (Path(__file__).resolve().parent.parent
          / "Source/DSP/ConstructionLoudnessData.h")
SEED = 20260929

# The engine's enum orders (AcustraEngine.h) and the renderer's spellings.
MODELS = ("original", "bellido1978")
SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
PICKINGS = ("finger", "pick", "thumb")
CAPTURES = ("stereo_mic", "mono_mic", "piezo")
DEFAULT = ("original", "dreadnought", "spruce", "finger")
CELLS = len(MODELS) * len(SHAPES) * len(WOODS) * len(PICKINGS)

LIMIT_KNEE = 0.89125094        # safetyLimit's threshold, -1 dBFS
LIMIT_HEADROOM = 1.0 - LIMIT_KNEE
TOLERANCE_LU = 1.0
HEADROOM_DB = 1.0


def index(model, shape, wood, picking) -> int:
    """A cell's place in the header's tables (ConstructionLoudnessData.h)."""
    value = MODELS.index(model)
    for names, name in ((SHAPES, shape), (WOODS, wood), (PICKINGS, picking)):
        value = value * len(names) + names.index(name)
    return value


def constructions():
    """Every construction."""
    yield from itertools.product(MODELS, SHAPES, WOODS)


# The phrase set: (string 1-6, MIDI note, onset s, release s, velocity) rows.
def phrase_set():
    rng = np.random.default_rng(SEED)
    rows, time = [], 0.1
    open_strings = meter.OPEN_STRINGS
    # Strums over a progression, down and up, soft to hard.
    progression = ("E", "Am", "C", "G", "D", "Am", "E", "G") * 4
    velocities = rng.permutation(np.linspace(32, 124, len(progression)))
    for stroke, name in enumerate(progression):
        chord = meter.CHORDS[name]
        strings = [string for string in range(6) if chord[string] is not None]
        if stroke % 2:  # an upstroke catches the treble strings
            strings = strings[::-1][:4]
        spacing = float(rng.uniform(0.006, 0.014))
        for rank, string in enumerate(strings):
            rows.append((string + 1, chord[string], time + spacing * rank,
                         time + 0.44, int(round(velocities[stroke]))))
        time += 0.45
    time += 0.6
    # Single notes, low on the neck then high, each velocity ramp shuffled.
    for strings, frets in (((1, 2, 3), (0, 8)), ((4, 5, 6), (5, 18))):
        velocities = rng.permutation(np.linspace(24, 127, 12))
        for note_index in range(12):
            string = strings[int(rng.integers(0, 3))]
            fret = int(rng.integers(frets[0], frets[1]))
            rows.append((string, open_strings[string - 1] + fret, time,
                         time + 0.55, int(round(velocities[note_index]))))
            time += 0.6
        time += 0.4
    # Held chords: soft, hard and medium.
    for name, velocity, hold in (("E", 40, 4.0), ("G", 118, 4.0), ("D", 80, 3.0)):
        chord = meter.CHORDS[name]
        for rank, string in enumerate(s for s in range(6) if chord[s] is not None):
            rows.append((string + 1, chord[string], time + 0.011 * rank,
                         time + hold, velocity))
        time += hold + 0.3
    return rows, time + 1.2


def hardest_case():
    """Velocity 127 throughout; rendered with the Pick at Touch 1 and Pluck
    Position 0."""
    chord = meter.CHORDS["E"]
    rows = [(string + 1, chord[string], 0.1 + 0.009 * string, 2.9, 127)
            for string in range(6)]
    # Two strings repicked into the ringing chord: the top at fret 20, the
    # bottom open.
    rows += [(6, 84, 0.8, 2.9, 127), (1, 40, 0.8, 2.9, 127)]
    rows = [(s, n, on, 0.79 if (s in (1, 6) and on < 0.5) else off, v)
            for s, n, on, off, v in rows]
    # All six on one sample.
    rows += [(string + 1, chord[string], 3.2, 5.9, 127) for string in range(6)]
    # A fast downstroke and the upstroke 0.2 s after it.
    for string in range(6):
        rows.append((string + 1, chord[string], 6.2 + 0.004 * string, 6.39, 127))
    for rank, string in enumerate(range(5, -1, -1)):
        rows.append((string + 1, chord[string], 6.4 + 0.004 * rank, 8.9, 127))
    return rows, 9.5


def unlimited(samples: np.ndarray) -> np.ndarray:
    """Invert AcustraEngine's safetyLimit (exactly linear below its knee)."""
    magnitude = np.abs(samples.astype(np.float64))
    over = magnitude > LIMIT_KNEE
    excess = magnitude[over] - LIMIT_KNEE
    magnitude[over] = LIMIT_KNEE + excess / np.maximum(
        1.0 - excess / LIMIT_HEADROOM, 1.0e-12)
    return magnitude


def built_gains(path: Path = HEADER) -> dict[str, np.ndarray]:
    """The tables the engine was last built with, or unity if there is none."""
    tables = {name: np.ones(CELLS) for name in ("mic", "mono", "piezo")}
    if not path.exists():
        return tables
    text = path.read_text()
    for name, symbol in (("mic", "constructionMicReference"),
                         ("mono", "constructionMonoTrim"),
                         ("piezo", "constructionPiezoTrim")):
        match = re.search(symbol + r"\s*\{\{(.*?)\}\};", text, re.S)
        if not match:
            raise SystemExit(f"{symbol} not found in {path}")
        values = [float(value.rstrip("f"))
                  for value in re.findall(r"[0-9.eE+-]+f", match.group(1))]
        if len(values) != CELLS:
            raise SystemExit(f"{symbol} has {len(values)} values, not {CELLS}")
        tables[name] = np.array(values)
    return tables


def render(renderer: str, work: Path, performance: str, construction, picking,
           capture, extra=()) -> np.ndarray:
    model, shape, wood = construction
    tag = "-".join((performance, *construction, picking, capture))
    output = work / f"{tag}.f32"
    subprocess.run(
        [renderer, str(work / f"{performance}.txt"), str(output), capture,
         picking, "--body-shape", shape, "--body-material", wood,
         "--guitar-model", model, *extra],
        check=True)
    audio = np.fromfile(output, dtype="<f4").reshape(-1, 2)
    output.unlink()
    return audio


def measure(renderer: str, jobs: int, hardest_pickings=("pick",)) -> dict:
    """Loudness per cell and capture, and the hardest case's peak before the
    limiter, both as rendered (with whatever gains are built in)."""
    with tempfile.TemporaryDirectory(prefix="acustra-construction-") as scratch:
        work = Path(scratch)
        meter.write_performance(*phrase_set(), work / "phrases.txt")
        meter.write_performance(*hardest_case(), work / "hardest.txt")

        def loudness(job):
            construction, picking, capture = job
            audio = render(renderer, work, "phrases", construction, picking, capture)
            return job, meter.integrated_loudness(audio)

        def peak(job):
            construction, picking, capture = job
            audio = render(renderer, work, "hardest", construction, picking, capture,
                           ("--touch", "1", "--pluck-position", "0"))
            return job, float(20.0 * np.log10(np.max(unlimited(audio))))

        loud_jobs = [(c, p, k) for c in constructions() for p in PICKINGS
                     for k in CAPTURES]
        peak_jobs = [(c, p, k) for c in constructions() for p in hardest_pickings
                     for k in CAPTURES]
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            lufs = dict(pool.map(loudness, loud_jobs))
            peaks = dict(pool.map(peak, peak_jobs))
    return {"lufs": lufs, "peaks": peaks}


def raw_levels(measured: dict, built: dict[str, np.ndarray]) -> dict:
    """Each measurement with the built-in gains divided out, in dB."""
    def gain_db(construction, picking, capture):
        cell = index(*construction, picking)
        gain = built["mic"][cell]
        if capture == "mono_mic":
            gain *= built["mono"][cell]
        elif capture == "piezo":
            gain *= built["piezo"][cell]
        return 20.0 * np.log10(gain)

    return {kind: {key: value - gain_db(*key) for key, value in table.items()}
            for kind, table in measured.items()}


def gains(raw: dict, max_cut_db: float = 0.9) -> dict[str, np.ndarray]:
    """The three tables that bring every cell and capture to the target.

    Where the hardest case (measured with the Pick) would come closer than
    HEADROOM_DB to the limiter's knee at the target, that cell and capture
    sit under the target by as much as it needs, but by no more than
    max_cut_db, which stays inside the loudness tolerance: parity comes
    first, and headroom is taken from the tolerance where it can be."""
    lufs, peaks = raw["lufs"], raw.get("peaks", {})
    target = lufs[(DEFAULT[:3], DEFAULT[3], "stereo_mic")]
    # Aimed a hair under the line, so rounding does not leave a cell on it.
    ceiling = 20.0 * np.log10(LIMIT_KNEE) - HEADROOM_DB - 0.02
    tables = {name: np.ones(CELLS) for name in ("mic", "mono", "piezo")}
    for construction in constructions():
        for picking in PICKINGS:
            cell = index(*construction, picking)
            level = {}
            for capture in CAPTURES:
                gain = target - lufs[(construction, picking, capture)]
                peak = peaks.get((construction, picking, capture))
                if picking == "pick" and peak is not None:
                    gain -= float(np.clip(peak + gain - ceiling, 0.0, max_cut_db))
                level[capture] = gain
            mic = 10.0 ** (level["stereo_mic"] / 20.0)
            if (*construction, picking) == DEFAULT:
                # Exactly 1 on both microphone captures: the default patch
                # renders unchanged. Its piezo is brought to the target like
                # every other cell's.
                tables["piezo"][cell] = 10.0 ** (level["piezo"] / 20.0)
                continue
            tables["mic"][cell] = mic
            for name, capture in (("mono", "mono_mic"), ("piezo", "piezo")):
                tables[name][cell] = 10.0 ** (level[capture] / 20.0) / mic
    return tables


def to_float32(value: float) -> float:
    return float(np.float32(value))


def float_literal(value: float) -> str:
    """A C++ float literal that reads back as the same float32."""
    text = f"{to_float32(value):.9g}"
    if not any(mark in text for mark in ".eE"):
        text += ".0"
    return text + "f"


def header_text(tables: dict[str, np.ndarray]) -> str:
    def table(name: str, symbol: str, comment: str) -> str:
        lines = [comment, f"inline constexpr std::array<float, {CELLS}> {symbol} {{{{"]
        values = tables[name]
        for model, shape in itertools.product(MODELS, SHAPES):
            start = index(model, shape, WOODS[0], PICKINGS[0])
            chunk = ", ".join(float_literal(v)
                              for v in values[start:start + len(WOODS) * len(PICKINGS)])
            lines.append(f"    // {model} {shape}: "
                         f"{', '.join(WOODS)} x {', '.join(PICKINGS)}")
            lines.append(f"    {chunk},")
        lines[-1] = lines[-1].rstrip(",")
        lines.append("}};")
        return "\n".join(lines)

    return "\n".join([
        "// Generated by Tools/CalibrateConstructionLoudness.py; do not edit by hand.",
        "// Output level references that bring every construction and Picking to",
        "// the default construction's integrated loudness (Original model,",
        "// Dreadnought, Spruce, Finger, on the stereo microphones), each capture on",
        "// its own (Docs/decisions.md, 2026-09-29, \"Every construction as loud as",
        "// the default\"). A cell is",
        "//   ((model * 4 + shape) * 3 + wood) * 3 + picking",
        "// in the enums' order in AcustraEngine.h.",
        "#pragma once",
        "",
        "#include <array>",
        "",
        "namespace acustra::detail",
        "{",
        table("mic", "constructionMicReference",
              "// The factor on the output reference (outputReferenceFor)."),
        table("mono", "constructionMonoTrim",
              "// The mono microphone's factor over constructionMicReference."),
        table("piezo", "constructionPiezoTrim",
              "// The piezo's factor over constructionMicReference, on its own trim\n"
              "// (PiezoDesign::trim)."),
        "} // namespace acustra::detail",
        "",
    ])


def label(construction, picking) -> str:
    model, shape, wood = construction
    return f"{model} {shape} {wood} {picking}"


def report(levels: dict, target: float) -> dict:
    """The spread about the target per capture and per group, the extremes,
    and the hardest case's peaks."""
    summary = {"target_lufs": float(target), "captures": {}, "groups": {}}
    for capture in CAPTURES:
        rows = {key: value - target for key, value in levels["lufs"].items()
                if key[2] == capture}
        low = min(rows, key=rows.get)
        high = max(rows, key=rows.get)
        summary["captures"][capture] = {
            "min_lu": float(rows[low]), "max_lu": float(rows[high]),
            "median_lu": float(np.median(list(rows.values()))),
            "quietest": label(*low[:2]), "loudest": label(*high[:2]),
            "within_tolerance": int(sum(abs(v) <= TOLERANCE_LU for v in rows.values())),
            "cells": len(rows)}
    for model in MODELS:
        for picking in PICKINGS:
            values = [value - target for (c, p, k), value in levels["lufs"].items()
                      if c[0] == model and p == picking and k == "stereo_mic"]
            summary["groups"][f"{model} {picking}"] = {
                "min_lu": float(min(values)), "median_lu": float(np.median(values)),
                "max_lu": float(max(values))}
    knee_db = 20.0 * np.log10(LIMIT_KNEE)
    for capture, picking in itertools.product(CAPTURES, PICKINGS):
        rows = {key: value for key, value in levels["peaks"].items()
                if key[1] == picking and key[2] == capture}
        if not rows:
            continue
        high = max(rows, key=rows.get)
        over = sorted((key for key, value in rows.items()
                       if value > knee_db - HEADROOM_DB), key=rows.get, reverse=True)
        summary.setdefault("hardest", {})[f"{capture} {picking}"] = {
            "peak_dbfs": float(rows[high]), "construction": label(*high[:2]),
            "median_peak_dbfs": float(np.median(list(rows.values()))),
            "headroom_to_knee_db": float(knee_db - rows[high]),
            "over_knee": int(sum(v > knee_db for v in rows.values())),
            "under_1db": len(rows) - len(over), "cells": len(rows),
            "short_of_1db": [{"construction": label(*key[:2]),
                              "peak_dbfs": float(rows[key]),
                              "lu": float(levels["lufs"][key] - target)}
                             for key in over]}
    return summary


def print_report(summary: dict, title: str) -> None:
    print(f"== {title}: target {summary['target_lufs']:.2f} LUFS")
    for capture, row in summary["captures"].items():
        print(f"  {capture:<10} {row['min_lu']:+6.2f} .. {row['max_lu']:+6.2f} LU"
              f" (median {row['median_lu']:+5.2f}; {row['within_tolerance']}/{row['cells']}"
              f" within +-{TOLERANCE_LU:g}); quietest {row['quietest']},"
              f" loudest {row['loudest']}")
    print("  stereo mic by model and picking (min / median / max LU):")
    for group, row in summary["groups"].items():
        print(f"    {group:<26} {row['min_lu']:+6.2f} {row['median_lu']:+6.2f}"
              f" {row['max_lu']:+6.2f}")
    for name, row in summary.get("hardest", {}).items():
        print(f"  hardest case, {name:<17} peak {row['peak_dbfs']:+6.2f} dBFS before the"
              f" limiter ({row['construction']}), median {row['median_peak_dbfs']:+6.2f};"
              f" {row['over_knee']} over the knee, {row['under_1db']}/{row['cells']}"
              " at least 1 dB under it")


def serialise(levels: dict) -> dict:
    return {kind: [{"construction": list(key[0]), "picking": key[1],
                    "capture": key[2], "value": float(value)}
                   for key, value in table.items()]
            for kind, table in levels.items()}


def deserialise(tables: dict) -> dict:
    return {kind: {(tuple(row["construction"]), row["picking"], row["capture"]):
                   row["value"] for row in rows}
            for kind, rows in tables.items()}


def self_test() -> None:
    assert CELLS == 72
    assert index(*DEFAULT) == index("original", "dreadnought", "spruce", "finger")
    seen = {index(*cell) for cell in itertools.product(
        MODELS, SHAPES, WOODS, PICKINGS)}
    assert seen == set(range(CELLS))
    assert len(list(constructions())) == 24
    # The phrase set is fixed: the same rows every time, playable, under the
    # renderer's minute, and spanning soft to hard.
    first, second = phrase_set(), phrase_set()
    assert first == second
    rows, seconds = first
    assert seconds < 60.0
    velocities = [row[4] for row in rows]
    assert min(velocities) <= 40 and max(velocities) >= 120
    with tempfile.TemporaryDirectory() as scratch:
        meter.write_performance(rows, seconds, Path(scratch) / "p.txt")
        meter.write_performance(*hardest_case(), Path(scratch) / "h.txt")
    assert all(row[4] == 127 for row in hardest_case()[0])
    # The limiter inverts exactly (AcustraEngine.cpp safetyLimit).
    raw = np.array([0.1, 0.8, 0.9, 1.2, 2.0, 5.0])
    limited = np.where(raw <= LIMIT_KNEE, raw, LIMIT_KNEE + (raw - LIMIT_KNEE)
                       / (1.0 + (raw - LIMIT_KNEE) / LIMIT_HEADROOM))
    assert np.allclose(unlimited(limited.astype(np.float32)), raw, rtol=2e-4)
    # The header round-trips and the default cell is exactly 1.
    tables = {name: np.linspace(0.5, 2.0, CELLS) for name in ("mic", "mono", "piezo")}
    for name in tables:
        tables[name][index(*DEFAULT)] = 1.0
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / "h.h"
        path.write_text(header_text(tables))
        read = built_gains(path)
    for name in tables:
        assert np.allclose(read[name], tables[name], rtol=1e-7)
        assert read[name][index(*DEFAULT)] == 1.0
    fake = {"lufs": {}, "peaks": {}}
    for construction in constructions():
        for picking in PICKINGS:
            for offset, capture in enumerate(CAPTURES):
                fake["lufs"][(construction, picking, capture)] = (
                    -30.0 + 0.5 * offset + 0.1 * PICKINGS.index(picking)
                    + (3.0 if construction[0] == "bellido1978" else 0.0))
    made = gains(fake)
    assert made["mic"][index(*DEFAULT)] == 1.0
    assert made["mono"][index(*DEFAULT)] == 1.0
    assert abs(20 * np.log10(made["piezo"][index(*DEFAULT)]) + 1.0) < 1e-9
    # A Pick cell whose hardest case would reach the knee gives up headroom
    # from the tolerance, and no more than that.
    # At the target this Pick cell's stereo microphones would peak 0.4 dB
    # too near the knee, its mono microphone far too near it.
    pick = ("bellido1978", "jumbo", "maple")
    fake["peaks"] = {(pick, "pick", "stereo_mic"): -1.62 + 3.1,
                     (pick, "pick", "mono_mic"): 10.0}
    capped = gains(fake)
    cell = index(*pick, "pick")
    assert abs(20 * np.log10(capped["mic"][cell]) - (-3.1 - 0.4)) < 1e-6
    assert abs(20 * np.log10(capped["mono"][cell]) - (-1.0)) < 1e-6
    # The piezo had no peak to keep under the knee: its level is unchanged.
    assert abs(capped["piezo"][cell] * capped["mic"][cell]
               - made["piezo"][cell] * made["mic"][cell]) < 1e-12
    bellido = index("bellido1978", "parlor", "maple", "thumb")
    assert abs(20 * np.log10(made["mic"][bellido]) + 3.2) < 1e-9
    assert abs(20 * np.log10(made["mono"][bellido]) + 0.5) < 1e-9
    assert HEADER.exists(), HEADER
    built = built_gains()
    assert built["mic"][index(*DEFAULT)] == 1.0
    assert built["mono"][index(*DEFAULT)] == 1.0
    print("CalibrateConstructionLoudness self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--renderer", help="AcustraPerformanceRenderer")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--json", type=Path, help="write every measurement here")
    parser.add_argument("--write-header", action="store_true",
                        help=f"write the gains to {HEADER.name}")
    parser.add_argument("--check", action="store_true",
                        help="fail unless the built gains hold every cell within"
                             f" +-{TOLERANCE_LU:g} LU and {HEADROOM_DB:g} dB under the knee")
    parser.add_argument("--measurements", type=Path,
                        help="reuse a --json file's measurements instead of rendering"
                             " (they carry the gains built when they were made)")
    parser.add_argument("--max-headroom-cut", type=float, default=0.9,
                        help="dB a Pick cell may sit under the target to keep its"
                             " hardest case 1 dB under the knee (default 0.9, inside"
                             " the tolerance; a large value puts headroom first)")
    parser.add_argument("--all-pickings", action="store_true",
                        help="also play the hardest case with Finger and Thumb")
    parser.add_argument("--models", nargs="+", choices=MODELS, default=MODELS,
                        help="with --write-header, rewrite only these models' cells"
                             " and keep the other cells' built gains (a change that"
                             " moved one model's level; the target is the default"
                             " cell's either way)")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    if arguments.measurements:
        saved = json.loads(arguments.measurements.read_text())
        measured = deserialise(saved["as_rendered"])
        raw = deserialise(saved["without_gains"])
    else:
        if not arguments.renderer:
            parser.error("--renderer or --measurements is required")
        measured = measure(arguments.renderer, max(1, arguments.jobs),
                           PICKINGS if arguments.all_pickings else ("pick",))
        raw = raw_levels(measured, built_gains())
    target = measured["lufs"][(DEFAULT[:3], DEFAULT[3], "stereo_mic")]
    as_built = report(measured, target)
    if arguments.json:
        arguments.json.write_text(json.dumps({
            "as_rendered": serialise(measured), "without_gains": serialise(raw),
            "summary": as_built}, indent=1) + "\n")
    print_report(as_built, "as rendered")
    print_report(report(raw, target), "without the construction gains")
    if arguments.write_header:
        tables = gains(raw, arguments.max_headroom_cut)
        built = built_gains()
        for model in set(MODELS) - set(arguments.models):
            for cell in itertools.product([model], SHAPES, WOODS, PICKINGS):
                for name in tables:
                    tables[name][index(*cell)] = built[name][index(*cell)]
        HEADER.write_text(header_text(tables))
        print(f"wrote {HEADER}")
    if arguments.check:
        failures = [f"{capture} spans {row['min_lu']:+.2f}..{row['max_lu']:+.2f} LU"
                    for capture, row in as_built["captures"].items()
                    if row["within_tolerance"] != row["cells"]]
        # With the Pick, a cell short of the headroom must already have given
        # up all the level the tolerance allows it (gains): otherwise the
        # built table is out of date.
        failures += [f"hardest case on {name}: {cell['construction']} peaks"
                     f" {cell['peak_dbfs']:+.2f} dBFS at {cell['lu']:+.2f} LU"
                     for name, row in as_built.get("hardest", {}).items()
                     if name.endswith(" pick")
                     for cell in row["short_of_1db"]
                     if cell["lu"] > -arguments.max_headroom_cut + 0.05]
        if failures:
            print("FAILED: " + "; ".join(failures))
            return 1
        print(f"every construction and Picking within +-{TOLERANCE_LU:g} LU; every Pick"
              " cell short of the headroom has given up the level the tolerance allows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
