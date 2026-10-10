#!/usr/bin/env python3
"""Level every construction and Picking to one loudness, and check headroom.

Every construction (Shape x Wood) and every Picking
is rendered through AcustraPerformanceRenderer (the shipping engine) at the
default controls and Output, playing one fixed, seeded phrase set: strums
over a chord progression, single notes low and high on the neck and held
chords, at velocities from soft to hard. Each render is measured as ITU-R
BS.1770-4 integrated loudness (Tools/MeasureMaterialLoudness.py's meter),
on the stereo microphones, the mono microphone and the piezo.

The target is the default construction - Dreadnought, Spruce, Finger - as it
plays on the stereo microphones, so the default patch keeps its loudness. For
every other cell the tool writes the gain that brings its stereo microphones to
that target, and for the mono microphone and the piezo the factor, relative to
that gain, that brings each of them to the same target: the three captures
stay level with each other on every construction. The data header it writes,
Source/DSP/ConstructionLoudnessData.h, holds those gains keyed by the three
settings (4 x 3 x 3 cells); AcustraEngine applies them where it applies the
strings' output reference, through the same smoothing. A gain changes only the
level: each construction keeps its tone.

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

A change that moves only some playing styles' level rewrites only those
styles' cells, keeping every other cell's built gain (the target stays the
default cell):

  python3 Tools/CalibrateConstructionLoudness.py --renderer ... \\
      --json m.json --write-header --pickings pick
  python3 Tools/CalibrateConstructionLoudness.py --self-test

A microphone-only update can keep the existing absolute pickup level with
`--write-header --preserve-piezo-level`. Its relative pickup trim compensates
the new microphone reference; float32 rounding can change the product by a
few ulps. This does not waive any loudness or headroom checks.

For a physical update, `--strum-measurements native.json` uses the output of
AcustraConstructionStrumLevels to keep all five existing Finger tuning guards
within 9 dB while staying within the phrase's 1 LU tolerance. It projects each
capture to the nearest feasible gain; it never changes the default reference.
Rebuild and rerender both protocols to verify the generated float32 tables.

The selection combines with the other write options:

  python3 Tools/CalibrateConstructionLoudness.py --measurements m.json \\
      --write-header --pickings pick --preserve-piezo-level

`--pickings` selects header writes only. Measurement and `--check` still cover
every construction, style and capture. `--all-pickings` independently adds
Finger and Thumb to the hardest-case rendering protocol.

The gains already built in (read from the header) are divided out of each
measurement, so writing the header again from a build that has them gives the
same gains. Renders go to a temporary directory and are removed as soon as
they are measured; nothing is downloaded and no audio is kept. NumPy and SciPy
are required.
"""

from __future__ import annotations

import argparse
import contextlib
import io
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
SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
PICKINGS = ("finger", "pick", "thumb")
CAPTURES = ("stereo_mic", "mono_mic", "piezo")
DEFAULT = ("dreadnought", "spruce", "finger")
CELLS = len(SHAPES) * len(WOODS) * len(PICKINGS)
STRUM_SCHEMA = "AcustraConstructionStrumLevelsV2"  # ConstructionStrumLevels.cpp
TUNINGS = ("standard", "drop_d", "dadgad", "open_g", "half_step_down")
STRUM_TOLERANCE_DB = 9.0       # ConstructionMatrixTests' existing guard
FIT_MARGIN_DB = 0.02          # leave room for float32 table rounding

LIMIT_KNEE = 0.89125094        # safetyLimit's threshold, -1 dBFS
LIMIT_HEADROOM = 1.0 - LIMIT_KNEE
TOLERANCE_LU = 1.0
HEADROOM_DB = 1.0


def index(shape, wood, picking) -> int:
    """A cell's place in the header's tables (ConstructionLoudnessData.h)."""
    value = SHAPES.index(shape)
    for names, name in ((WOODS, wood), (PICKINGS, picking)):
        value = value * len(names) + names.index(name)
    return value


def constructions():
    """Every construction: (shape, wood)."""
    yield from itertools.product(SHAPES, WOODS)


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
    shape, wood = construction
    tag = "-".join((performance, *construction, picking, capture))
    output = work / f"{tag}.f32"
    subprocess.run(
        [renderer, str(work / f"{performance}.txt"), str(output), capture,
         picking, "--body-shape", shape, "--body-material", wood, *extra],
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
    target = lufs[(DEFAULT[:2], DEFAULT[2], "stereo_mic")]
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


def retain_unselected_gains(tables: dict[str, np.ndarray],
                            built: dict[str, np.ndarray],
                            pickings=PICKINGS) -> None:
    """Keep all existing gains outside the selected styles' cells."""
    for cell in itertools.product(SHAPES, WOODS, PICKINGS):
        if cell[2] not in pickings:
            i = index(*cell)
            for name in tables:
                tables[name][i] = built[name][i]


def preserve_piezo_level(tables: dict[str, np.ndarray],
                         built: dict[str, np.ndarray],
                         pickings=PICKINGS) -> None:
    """Keep the previous mic*piezo gain on only the selected styles' cells.

    Use the float32 microphone references that the generated C++ will read,
    not the unrounded fit. The runtime also multiplies a common string
    reference, so this preserves level to float rounding, not sample bits.
    """
    for cell in itertools.product(SHAPES, WOODS, pickings):
        i = index(*cell)
        old_level = to_float32(built["mic"][i]) * to_float32(built["piezo"][i])
        tables["piezo"][i] = old_level / to_float32(tables["mic"][i])


def absolute_gain(tables, cell, capture):
    gain = tables["mic"][cell]
    if capture != "stereo_mic":
        gain *= tables["mono" if capture == "mono_mic" else "piezo"][cell]
    return float(gain)


def validate_strum_measurements(saved, built):
    """Reject incomplete, stale or limited native guard measurements."""
    if (saved.get("schema") != STRUM_SCHEMA
            or saved.get("rate") != 48000 or saved.get("frames") != 16800
            or saved.get("picking") != "finger"):
        raise ValueError("strum measurements must use the native 48 kHz Finger guard protocol")
    captured = saved.get("built_gains", {})
    for name in ("mic", "mono", "piezo"):
        values = np.asarray(captured.get(name, []), dtype=float)
        if (values.shape != (CELLS,) or not np.isfinite(values).all()
                or np.any(values <= 0)
                or not np.array_equal(values.astype(np.float32),
                                      built[name].astype(np.float32))):
            raise ValueError(f"strum measurements' built {name} gains differ from the current header")
    expected = set(itertools.product(constructions(), CAPTURES, TUNINGS))
    rows = {}
    references = {}
    for row in saved.get("rows", []):
        key = (tuple(row.get("construction", [])), row.get("capture"), row.get("tuning"))
        if key not in expected or key in rows:
            raise ValueError(f"invalid or duplicate strum measurement: {key}")
        values = [row.get(name) for name in
                  ("weighted_db", "reference_db", "relative_db", "peak", "piezo_peak")]
        if (row.get("finite") is not True
                or any(not isinstance(v, (float, int)) or not np.isfinite(v) for v in values)
                or min(values[3:]) <= 0):
            raise ValueError(f"non-finite or silent strum measurement: {key}")
        if row["peak"] >= LIMIT_KNEE or row["piezo_peak"] >= LIMIT_KNEE:
            raise ValueError(f"limited strum measurement cannot be projected linearly: {key}")
        if abs(row["weighted_db"] - row["reference_db"] - row["relative_db"]) > 1e-8:
            raise ValueError(f"inconsistent strum levels: {key}")
        reference_key = key[1:]
        if reference_key in references and abs(references[reference_key] - row["reference_db"]) > 1e-8:
            raise ValueError(f"inconsistent default strum reference: {reference_key}")
        references[reference_key] = row["reference_db"]
        rows[key] = row
    if set(rows) != expected:
        raise ValueError(f"strum measurements need all {len(expected)} construction/capture/tuning rows")
    for capture, tuning in itertools.product(CAPTURES, TUNINGS):
        row = rows[(DEFAULT[:2], capture, tuning)]
        if abs(row["relative_db"]) > 1e-8:
            raise ValueError("default strum must equal its capture/tuning reference")
    return rows


def constrain_strum_gains(tables, built, raw, saved, pickings):
    """Choose the nearest phrase fit satisfying every native tuning guard.

    Solve absolute capture gains first; encode mono/piezo as relative trims
    afterward. The existing default reference is immutable. These predictions
    require a fresh native rerender after rebuilding: BS.1770 gating and the
    limiter can make phrase measurements differ from simple scalar arithmetic.
    """
    rows = validate_strum_measurements(saved, built)
    for name in ("mic", "mono", "piezo"):
        if (tables[name].shape != (CELLS,) or not np.isfinite(tables[name]).all()
                or np.any(tables[name] <= 0)):
            raise ValueError(f"invalid fitted {name} gains")
    for construction, capture in itertools.product(constructions(), CAPTURES):
        value = raw["lufs"][(construction, "finger", capture)]
        if not np.isfinite(value):
            raise ValueError(f"non-finite phrase level: {construction} {capture}")
    target = raw["lufs"][(DEFAULT[:2], DEFAULT[2], "stereo_mic")]
    default_cell = index(*DEFAULT)
    for capture in CAPTURES:
        if abs(20 * np.log10(absolute_gain(tables, default_cell, capture)
                            / absolute_gain(built, default_cell, capture))) > 1e-6:
            raise ValueError("strum constraints require preserving the default reference on every capture")
    planned = {name: values.copy() for name, values in tables.items()}
    changes = []
    for construction in constructions():
        cell = index(*construction, "finger")
        selected = "finger" in pickings and cell != default_cell
        levels = {}
        for capture in CAPTURES:
            old_db = 20 * np.log10(absolute_gain(built, cell, capture))
            wanted_db = 20 * np.log10(absolute_gain(tables, cell, capture))
            exact_db = target - raw["lufs"][(construction, "finger", capture)]
            low = exact_db - TOLERANCE_LU + FIT_MARGIN_DB
            high = exact_db + TOLERANCE_LU - FIT_MARGIN_DB
            for tuning in TUNINGS:
                row = rows[(construction, capture, tuning)]
                low = max(low, old_db - STRUM_TOLERANCE_DB + FIT_MARGIN_DB - row["relative_db"])
                high = min(high, old_db + STRUM_TOLERANCE_DB - FIT_MARGIN_DB - row["relative_db"])
            if low > high:
                raise ValueError(f"no gain satisfies phrase and strum limits: {construction} {capture}")
            fitted_db = float(np.clip(wanted_db, low, high)) if selected else wanted_db
            if not selected and not low - 1e-6 <= fitted_db <= high + 1e-6:
                raise ValueError(f"unselected gain violates strum/phrase limits: {construction} {capture}")
            levels[capture] = 10 ** (fitted_db / 20)
            if abs(fitted_db - wanted_db) > 1e-6:
                changes.append({"construction": list(construction), "capture": capture,
                                "phrase_offset_lu": float(fitted_db - exact_db),
                                "gain_interval_db": [float(low), float(high)]})
        if selected:
            planned["mic"][cell] = levels["stereo_mic"]
            planned["mono"][cell] = levels["mono_mic"] / levels["stereo_mic"]
            planned["piezo"][cell] = levels["piezo"] / levels["stereo_mic"]
    # Commit only after every cell is feasible; failure leaves input untouched.
    for name in tables:
        tables[name][:] = planned[name]
    return {"protocol": saved["schema"], "rows": len(rows),
            "rounding_margin_db": FIT_MARGIN_DB, "adjusted_captures": changes,
            "requires_native_rerender": True}


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
        for shape in SHAPES:
            start = index(shape, WOODS[0], PICKINGS[0])
            chunk = ", ".join(float_literal(v)
                              for v in values[start:start + len(WOODS) * len(PICKINGS)])
            lines.append(f"    // original {shape}: "
                         f"{', '.join(WOODS)} x {', '.join(PICKINGS)}")
            lines.append(f"    {chunk},")
        lines[-1] = lines[-1].rstrip(",")
        lines.append("}};")
        return "\n".join(lines)

    return "\n".join([
        "// Generated by Tools/CalibrateConstructionLoudness.py; do not edit by hand.",
        "// Output level references that bring every construction and Picking to",
        "// the default construction's integrated loudness (Dreadnought, Spruce,",
        "// Finger, on the stereo microphones), each capture on its own",
        "// (Docs/decisions.md, 2026-09-29, \"Every construction as loud as the",
        "// default\"). A cell is",
        "//   (shape * 3 + wood) * 3 + picking",
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
    shape, wood = construction
    return f"{shape} {wood} {picking}"


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
    for picking in PICKINGS:
        values = [value - target for (c, p, k), value in levels["lufs"].items()
                  if p == picking and k == "stereo_mic"]
        summary["groups"][picking] = {
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
    print("  stereo mic by picking (min / median / max LU):")
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
    known = set(constructions())
    for rows in tables.values():
        for row in rows:
            if tuple(row["construction"]) not in known:
                raise SystemExit(f"unknown construction {row['construction']}: measurements"
                                 " made before the second guitar model was removed carry"
                                 " [model, shape, wood]; measure again")
    return {kind: {(tuple(row["construction"]), row["picking"], row["capture"]):
                   row["value"] for row in rows}
            for kind, rows in tables.items()}


def self_test() -> None:
    assert CELLS == 36
    # The header's documented layout, (shape * 3 + wood) * 3 + picking, which
    # is AcustraEngine.cpp's constructionLoudnessCell, for every cell.
    for shape, wood, picking in itertools.product(SHAPES, WOODS, PICKINGS):
        assert index(shape, wood, picking) == ((SHAPES.index(shape) * 3 + WOODS.index(wood)) * 3
                                               + PICKINGS.index(picking))
    assert index(*DEFAULT) == index("dreadnought", "spruce", "finger")
    seen = {index(*cell) for cell in itertools.product(SHAPES, WOODS, PICKINGS)}
    assert seen == set(range(CELLS))
    assert len(list(constructions())) == 12
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
    # Microphone-only calibration must not silently relevel the pickup or
    # touch unselected styles, even when the microphone gain changes widely.
    old = {name: values.copy() for name, values in tables.items()}
    updated = {name: values.copy() for name, values in tables.items()}
    updated["mic"] *= np.geomspace(0.2, 5.0, CELLS)
    chosen = ("finger", "thumb")
    selected = [index(*cell) for cell in itertools.product(SHAPES, WOODS, chosen)]
    unselected = [index(*cell) for cell in itertools.product(SHAPES, WOODS, ("pick",))]
    untouched = updated["piezo"][unselected].copy()
    preserve_piezo_level(updated, old, chosen)
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / "preserved.h"
        path.write_text(header_text(updated))
        generated = built_gains(path)
    assert np.array_equal(updated["piezo"][unselected], untouched)
    previous = old["mic"][selected].astype(np.float32).astype(float) \
        * old["piezo"][selected].astype(np.float32).astype(float)
    actual = generated["mic"][selected] * generated["piezo"][selected]
    assert np.allclose(previous, actual, rtol=1e-7, atol=0)
    # Write selectors restrict the written cells, rather than render fewer.
    # An isolated Pick update must preserve every Finger and Thumb entry.
    fitted = {name: old[name] * np.linspace(1.1, 1.9, CELLS)
              for name in old}
    for pickings in (PICKINGS, ("pick",), ("finger", "thumb"), ("thumb",)):
        scoped = {name: values.copy() for name, values in fitted.items()}
        retain_unselected_gains(scoped, old, pickings)
        for cell in itertools.product(SHAPES, WOODS, PICKINGS):
            i = index(*cell)
            expected = fitted if cell[2] in pickings else old
            for name in scoped:
                assert scoped[name][i] == expected[name][i], (cell, name)
    scoped = {name: values.copy() for name, values in fitted.items()}
    retain_unselected_gains(scoped, old, ("pick",))
    before = {name: values.copy() for name, values in scoped.items()}
    preserve_piezo_level(scoped, old, ("pick",))
    assert np.array_equal(scoped["mic"], before["mic"])
    assert np.array_equal(scoped["mono"], before["mono"])
    with tempfile.TemporaryDirectory() as scratch:
        path = Path(scratch) / "pick-scoped.h"
        path.write_text(header_text(scoped))
        generated = built_gains(path)
    for cell in itertools.product(SHAPES, WOODS, PICKINGS):
        i = index(*cell)
        if cell[2] == "pick":
            old_product = np.float32(np.float32(old["mic"][i]) * np.float32(old["piezo"][i]))
            new_product = np.float32(np.float32(generated["mic"][i])
                                     * np.float32(generated["piezo"][i]))
            assert abs(int(old_product.view(np.uint32))
                       - int(new_product.view(np.uint32))) <= 1, cell
        else:
            assert scoped["piezo"][i] == before["piezo"][i], cell
            for name in scoped:
                assert np.float32(generated[name][i]) == np.float32(old[name][i]), (cell, name)
    arguments = argument_parser().parse_args([])
    assert tuple(arguments.pickings) == PICKINGS
    selected = argument_parser().parse_args([
        "--pickings", "pick", "--write-header", "--preserve-piezo-level"])
    assert selected.pickings == ["pick"]
    assert selected.write_header and selected.preserve_piezo_level
    assert not selected.all_pickings
    # The guitar model is no longer a write selector.
    with contextlib.redirect_stderr(io.StringIO()):
        try:
            argument_parser().parse_args(["--models", "original"])
        except SystemExit:
            pass
        else:
            raise AssertionError("--models is still accepted")
    fake = {"lufs": {}, "peaks": {}}
    for construction in constructions():
        for picking in PICKINGS:
            for offset, capture in enumerate(CAPTURES):
                fake["lufs"][(construction, picking, capture)] = (
                    -30.0 + 0.5 * offset + 0.1 * PICKINGS.index(picking)
                    + (3.0 if construction[1] == "maple" else 0.0))
    made = gains(fake)
    assert made["mic"][index(*DEFAULT)] == 1.0
    assert made["mono"][index(*DEFAULT)] == 1.0
    assert abs(20 * np.log10(made["piezo"][index(*DEFAULT)]) + 1.0) < 1e-9
    # A Pick cell whose hardest case would reach the knee gives up headroom
    # from the tolerance, and no more than that.
    # At the target this Pick cell's stereo microphones would peak 0.4 dB
    # too near the knee, its mono microphone far too near it.
    pick = ("jumbo", "maple")
    fake["peaks"] = {(pick, "pick", "stereo_mic"): -1.62 + 3.1,
                     (pick, "pick", "mono_mic"): 10.0}
    capped = gains(fake)
    cell = index(*pick, "pick")
    assert abs(20 * np.log10(capped["mic"][cell]) - (-3.1 - 0.4)) < 1e-6
    assert abs(20 * np.log10(capped["mono"][cell]) - (-1.0)) < 1e-6
    # The piezo had no peak to keep under the knee: its level is unchanged.
    assert abs(capped["piezo"][cell] * capped["mic"][cell]
               - made["piezo"][cell] * made["mic"][cell]) < 1e-12
    maple = index("parlor", "maple", "thumb")
    assert abs(20 * np.log10(made["mic"][maple]) + 3.2) < 1e-9
    assert abs(20 * np.log10(made["mono"][maple]) + 0.5) < 1e-9
    # One quiet native tuning needs +0.82 dB, within the phrase tolerance.
    # Every other cell and capture stays at unity.
    unity = {name: np.ones(CELLS) for name in ("mic", "mono", "piezo")}
    flat = {"lufs": {(c, p, k): -30.0 for c in constructions()
                     for p in PICKINGS for k in CAPTURES}, "peaks": {}}
    native = {"schema": STRUM_SCHEMA, "rate": 48000,
              "frames": 16800, "picking": "finger",
              "built_gains": {name: values.tolist() for name, values in unity.items()},
              "rows": []}
    for c, capture, tuning in itertools.product(constructions(), CAPTURES, TUNINGS):
        relative = -9.8 if (c == pick and capture == "mono_mic" and tuning == "dadgad") else 0.0
        native["rows"].append({"construction": list(c), "capture": capture,
                               "tuning": tuning, "weighted_db": relative,
                               "reference_db": 0.0, "relative_db": relative,
                               "peak": 0.01, "piezo_peak": 0.01, "finite": True})
    projected = {name: values.copy() for name, values in unity.items()}
    fit_report = constrain_strum_gains(projected, unity, flat, native, PICKINGS)
    finger_cell = index(*pick, "finger")
    assert abs(20 * np.log10(projected["mono"][finger_cell]) - 0.82) < 1e-10
    assert np.array_equal(np.delete(projected["mono"], finger_cell), np.ones(CELLS - 1))
    assert np.array_equal(projected["mic"], unity["mic"])
    assert np.array_equal(projected["piezo"], unity["piezo"])
    assert len(fit_report["adjusted_captures"]) == 1
    # With Finger outside the written styles that cell may not move, so the
    # same evidence fails before any table write.
    unchanged = {name: values.copy() for name, values in unity.items()}
    try:
        constrain_strum_gains(unchanged, unity, flat, native, ("pick", "thumb"))
    except ValueError:
        pass
    else:
        raise AssertionError("an unselected Finger cell was moved")
    assert all(np.array_equal(unchanged[name], unity[name]) for name in unity)
    # Bad evidence and an empty feasible interval fail before any table write.
    failures = []
    duplicate = json.loads(json.dumps(native)); duplicate["rows"].append(duplicate["rows"][0])
    failures.append(duplicate)
    missing = json.loads(json.dumps(native)); missing["rows"].pop(); failures.append(missing)
    stale = json.loads(json.dumps(native)); stale["built_gains"]["mono"][0] = 2.0; failures.append(stale)
    nonfinite = json.loads(json.dumps(native)); nonfinite["rows"][0]["weighted_db"] = float("nan"); failures.append(nonfinite)
    limited = json.loads(json.dumps(native)); limited["rows"][0]["peak"] = LIMIT_KNEE; failures.append(limited)
    inconsistent = json.loads(json.dumps(native)); inconsistent["rows"][0]["relative_db"] = 1.0; failures.append(inconsistent)
    # Rows from before the guitar model was removed name [model, shape, wood].
    retired = json.loads(json.dumps(native))
    retired["schema"] = "AcustraConstructionStrumLevelsV1"
    failures.append(retired)
    legacy = json.loads(json.dumps(native)); legacy["rows"][0]["construction"].insert(0, "original")
    failures.append(legacy)
    impossible = json.loads(json.dumps(native))
    for row in impossible["rows"]:
        if row["relative_db"] == -9.8:
            row["relative_db"] = row["weighted_db"] = -10.1
    failures.append(impossible)
    for invalid in failures:
        unchanged = {name: values.copy() for name, values in unity.items()}
        try:
            constrain_strum_gains(unchanged, unity, flat, invalid, PICKINGS)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid native evidence accepted")
        assert all(np.array_equal(unchanged[name], unity[name]) for name in unity)
    # Measurements serialise and read back; a pre-removal construction does not.
    assert deserialise(serialise(fake)) == fake
    try:
        deserialise({"lufs": [{"construction": ["original", "dreadnought", "spruce"],
                               "picking": "finger", "capture": "stereo_mic",
                               "value": -30.0}]})
    except SystemExit:
        pass
    else:
        raise AssertionError("a [model, shape, wood] measurement was accepted")
    assert HEADER.exists(), HEADER
    built = built_gains()
    assert built["mic"][index(*DEFAULT)] == 1.0
    assert built["mono"][index(*DEFAULT)] == 1.0
    print("CalibrateConstructionLoudness self-test passed")


def argument_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--renderer", help="AcustraPerformanceRenderer")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--json", type=Path, help="write every measurement here")
    parser.add_argument("--write-header", action="store_true",
                        help=f"write the gains to {HEADER.name}")
    parser.add_argument("--preserve-piezo-level", action="store_true",
                        help="with --write-header, keep the selected styles' cells' existing"
                             " absolute pickup level during a microphone-only update")
    parser.add_argument("--check", action="store_true",
                        help="fail unless the built gains hold every cell within"
                             f" +-{TOLERANCE_LU:g} LU and {HEADROOM_DB:g} dB under the knee")
    parser.add_argument("--measurements", type=Path,
                        help="reuse a --json file's measurements instead of rendering"
                             " (they carry the gains built when they were made)")
    parser.add_argument("--strum-measurements", type=Path,
                        help="native AcustraConstructionStrumLevels JSON; constrain"
                             " header writes to phrase and five-tuning limits, or"
                             " verify every native strum with --check")
    parser.add_argument("--max-headroom-cut", type=float, default=0.9,
                        help="dB a Pick cell may sit under the target to keep its"
                             " hardest case 1 dB under the knee (default 0.9, inside"
                             " the tolerance; a large value puts headroom first)")
    parser.add_argument("--all-pickings", action="store_true",
                        help="also play the hardest case with Finger and Thumb")
    parser.add_argument("--pickings", nargs="+", choices=PICKINGS, default=PICKINGS,
                        help="with --write-header, rewrite only these styles' cells"
                             " and keep every other cell's built gains (a change that"
                             " moved one style's level; the target is the default"
                             " cell's either way; does not restrict measurements or"
                             " --check)")
    parser.add_argument("--self-test", action="store_true")
    return parser


def main() -> int:
    parser = argument_parser()
    arguments = parser.parse_args()
    if arguments.preserve_piezo_level and not arguments.write_header:
        parser.error("--preserve-piezo-level requires --write-header")
    if arguments.strum_measurements and not (arguments.write_header or arguments.check):
        parser.error("--strum-measurements requires --write-header or --check")
    if arguments.strum_measurements and arguments.preserve_piezo_level:
        parser.error("--strum-measurements and --preserve-piezo-level cannot both change the capture fit")
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
    target = measured["lufs"][(DEFAULT[:2], DEFAULT[2], "stereo_mic")]
    as_built = report(measured, target)
    saved_strums = None
    if arguments.strum_measurements:
        saved_strums = json.loads(arguments.strum_measurements.read_text())
        validate_strum_measurements(saved_strums, built_gains())
    output = {"as_rendered": serialise(measured), "without_gains": serialise(raw),
              "summary": as_built}
    print_report(as_built, "as rendered")
    print_report(report(raw, target), "without the construction gains")
    if arguments.write_header:
        tables = gains(raw, arguments.max_headroom_cut)
        built = built_gains()
        retain_unselected_gains(tables, built, arguments.pickings)
        if arguments.preserve_piezo_level:
            preserve_piezo_level(tables, built, arguments.pickings)
        if saved_strums is not None:
            output["strum_constraints"] = constrain_strum_gains(
                tables, built, raw, saved_strums, arguments.pickings)
        HEADER.write_text(header_text(tables))
        print(f"wrote {HEADER}")
    if arguments.json:
        arguments.json.write_text(json.dumps(output, indent=1, allow_nan=False) + "\n")
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
        if saved_strums is not None:
            failures += [f"native strum {row['construction']} {row['capture']}"
                         f" {row['tuning']} is {row['relative_db']:+.2f} dB"
                         for row in saved_strums["rows"]
                         if abs(row["relative_db"]) > STRUM_TOLERANCE_DB]
        if failures:
            print("FAILED: " + "; ".join(failures))
            return 1
        print(f"every construction and Picking within +-{TOLERANCE_LU:g} LU; every Pick"
              " cell short of the headroom has given up the level the tolerance allows")
    return 0


if __name__ == "__main__":
    sys.exit(main())
