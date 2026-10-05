#!/usr/bin/env python3
"""Fit and check the two models' reciprocal microphone brightness tilt.

Each model moves a quarter of the measured brightness gap toward the other.
The model's modal residues receive one smooth logarithmic tilt, leaving its
resonance frequencies, each modal residue's phase, bridge coupling and decay
poles intact. The phase of the summed microphone response can still change.
This tool uses matched stock ``variant=0`` WAVs from ``manifest.csv``:
Original is model 0 and Bellido 1978 is model 1. Stereo and mono microphone
captures contribute equally; piezo is reported separately and never fitted.
The manifest pairs geometry, picking style, capture and MIDI note, so model
level, preset geometry and the open/fretted assignment cannot bias pairing.

Brightness is 10 log10(power 2-10 kHz / power 60 Hz-2 kHz), measured from
the sum of the channels' periodograms. Each matched note has equal weight.
The fitting window is 3-500 ms, spanning attack and early sustain. Separate
attack, sustain and late windows show any change in decay balance.

The fitted correction is +/- k dB/octave around 1 kHz, clamped at 60 Hz
and 10 kHz. An analytical output-EQ fit supplies the initial coefficient;
``--after`` measures actual modal-residue renders, which are not exactly an
output EQ where complex modes cancel. No audio or header is written.

    python3 Tools/FitModelConvergence.py --before DIR [--after DIR] \\
        [--tilt-db-per-octave 0.4375] [--json report.json]
    python3 Tools/FitModelConvergence.py --self-test
"""

from __future__ import annotations

import argparse
import csv
import json
import math
from dataclasses import dataclass
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.optimize import brentq
from scipy.signal import periodogram


LOW_HZ, SPLIT_HZ, HIGH_HZ = 60.0, 2000.0, 10000.0
PIVOT_HZ = 1000.0
WINDOWS = {
    "attack": (0.003, 0.08),
    "sustain": (0.1, 0.5),
    "late": (0.8, 1.2),
    "whole": (0.003, 0.5),
}
CAPTURES = ("stereo", "mono")
MODELS = ("0", "1")


@dataclass
class Spectrum:
    frequency: np.ndarray
    power: np.ndarray
    rms_dbfs: float


def brightness(frequency: np.ndarray, power: np.ndarray) -> float:
    """Gain-independent band ratio; reject silence or a missing band."""
    low = float(power[(frequency >= LOW_HZ) & (frequency < SPLIT_HZ)].sum())
    high = float(power[(frequency >= SPLIT_HZ) & (frequency < HIGH_HZ)].sum())
    if not (low > 0.0 and high > 0.0):
        raise ValueError("cannot measure brightness of silence or a missing band")
    return 10.0 * math.log10(high / low)


def tilt_db(frequency: np.ndarray, coefficient: float) -> np.ndarray:
    return coefficient * np.log2(
        np.clip(frequency, LOW_HZ, HIGH_HZ) / PIVOT_HZ)


def tilted_brightness(spectrum: Spectrum, coefficient: float,
                      model: str) -> float:
    sign = 1.0 if model == "0" else -1.0
    power = spectrum.power * np.power(
        10.0, sign * tilt_db(spectrum.frequency, coefficient) / 10.0)
    return brightness(spectrum.frequency, power)


def load(root: Path) -> dict[tuple, Spectrum]:
    """Read only paired stock captures, preserving manifest case identity."""
    spectra: dict[tuple, Spectrum] = {}
    with (root / "manifest.csv").open(newline="") as file:
        manifest = list(csv.DictReader(file))
    for row in manifest:
        if row["variant"] != "0" or row["model"] not in MODELS:
            continue
        if row["capture"] not in (*CAPTURES, "piezo"):
            continue
        audio_path = (root / row["file"]).resolve()
        if not audio_path.is_relative_to(root.resolve()):
            raise ValueError("manifest WAV must be inside its render directory")
        rate, audio = wavfile.read(audio_path)
        if np.issubdtype(audio.dtype, np.integer):
            limits = np.iinfo(audio.dtype)
            if audio.dtype.kind == "u":
                midpoint = (limits.max + 1) / 2.0
                audio = (audio.astype(np.float64) - midpoint) / midpoint
            else:
                audio = audio.astype(np.float64) / max(abs(limits.min), limits.max)
        else:
            audio = audio.astype(np.float64)
        if audio.ndim == 1:
            audio = audio[:, None]
        if not np.isfinite(audio).all():
            raise ValueError(f"non-finite audio in {audio_path}")
        case = (row["geometry"], row["style"], row["capture"], row["note"])
        for name, (start, end) in WINDOWS.items():
            lo, hi = int(start * rate), int(end * rate)
            if hi > len(audio):
                raise ValueError(f"{audio_path} does not cover the {name} window")
            samples = audio[lo:hi]
            frequency, power = periodogram(samples, rate, axis=0)
            power = power.sum(axis=1)
            level = float(np.mean(samples * samples))
            key = (row["model"], *case, name)
            if key in spectra:
                raise ValueError(f"duplicate manifest case {key}")
            spectra[key] = Spectrum(frequency, power, 10.0 * math.log10(
                max(level, 1e-30)))
    if not spectra:
        raise ValueError("manifest has no stock model renders")
    for key in spectra:
        other = ("1" if key[0] == "0" else "0", *key[1:])
        if other not in spectra:
            raise ValueError(f"missing matched model render for {key}")
    for capture in CAPTURES:
        if not any(key[3] == capture for key in spectra):
            raise ValueError(f"manifest has no {capture} microphone captures")
    for key in spectra:
        if key[3] in CAPTURES:
            other_capture = "mono" if key[3] == "stereo" else "stereo"
            paired = (*key[:3], other_capture, *key[4:])
            if paired not in spectra:
                raise ValueError(f"missing matched microphone capture for {key}")
    return spectra


def score(spectra: dict[tuple, Spectrum], period: str, capture: str,
          coefficient: float | None = None) -> dict:
    selected = [key for key in spectra
                if key[0] == "0" and key[-1] == period
                and (key[3] == capture or (capture == "microphones"
                                          and key[3] in CAPTURES))]
    levels, values = [[], []], [[], []]
    for key in sorted(selected):
        for index, model in enumerate(MODELS):
            spectrum = spectra[(model, *key[1:])]
            values[index].append(brightness(spectrum.frequency, spectrum.power)
                                 if coefficient is None else
                                 tilted_brightness(spectrum, coefficient, model))
            levels[index].append(spectrum.rms_dbfs)
    if not selected:
        raise ValueError(f"no cases for {capture}/{period}")
    result = {
        "period": period,
        "capture": capture,
        "paired_notes": len(selected),
        "original_brightness_db": float(np.mean(values[0])),
        "bellido1978_brightness_db": float(np.mean(values[1])),
        "original_rms_dbfs": float(np.mean(levels[0])),
        "bellido1978_rms_dbfs": float(np.mean(levels[1])),
    }
    result["model_gap_db"] = (result["bellido1978_brightness_db"]
                              - result["original_brightness_db"])
    return result


def fit(spectra: dict[tuple, Spectrum], fraction: float) -> float:
    old = score(spectra, "whole", "microphones")["model_gap_db"]
    if old <= 0.0:
        raise ValueError("this tilt convention requires 78 brighter than Original")
    target = old * (1.0 - 2.0 * fraction)
    objective = lambda coefficient: (
        score(spectra, "whole", "microphones", coefficient)["model_gap_db"]
        - target)
    return float(brentq(objective, 0.0, 8.0))


def movement(before: dict, after: dict) -> dict:
    gap = before["model_gap_db"]
    original = after["original_brightness_db"] - before["original_brightness_db"]
    bellido = after["bellido1978_brightness_db"] - before["bellido1978_brightness_db"]
    return {
        "period": before["period"], "capture": before["capture"],
        "paired_notes": before["paired_notes"],
        "gap_before_db": gap, "gap_after_db": after["model_gap_db"],
        "original_shift_db": original, "bellido1978_shift_db": bellido,
        "original_movement_fraction": original / gap if gap else None,
        "bellido1978_movement_fraction": -bellido / gap if gap else None,
        "gap_closure_fraction": ((gap - after["model_gap_db"]) / gap
                                 if gap else None),
        "original_level_change_db": (after["original_rms_dbfs"]
                                     - before["original_rms_dbfs"]),
        "bellido1978_level_change_db": (after["bellido1978_rms_dbfs"]
                                       - before["bellido1978_rms_dbfs"]),
    }


def self_test() -> None:
    # Two exact bins separated by five octaves, with a known 6 dB model gap.
    frequency = np.array([125.0, 4000.0])
    a = Spectrum(frequency, np.array([1.0, 1.0]), 0.0)
    b = Spectrum(frequency, np.array([1.0, 10.0 ** 0.6]), 0.0)
    spectra = {}
    for capture in CAPTURES:
        for model, spectrum in zip(MODELS, (a, b)):
            spectra[(model, "0", "0", capture, "59", "whole")] = spectrum
    coefficient = fit(spectra, 0.25)
    assert abs(coefficient - 0.3) < 1e-12
    old = score(spectra, "whole", "microphones")
    changed = score(spectra, "whole", "microphones", coefficient)
    result = movement(old, changed)
    assert abs(result["original_movement_fraction"] - 0.25) < 1e-12
    assert abs(result["bellido1978_movement_fraction"] - 0.25) < 1e-12
    assert abs(result["gap_closure_fraction"] - 0.5) < 1e-12
    assert abs(brightness(frequency, b.power * 0.004)
               - brightness(frequency, b.power)) < 1e-12
    extremes = tilt_db(np.array([0.0, 60.0, 1000.0, 10000.0, 24000.0]), coefficient)
    assert extremes[0] == extremes[1] and extremes[3] == extremes[4]
    assert extremes[2] == 0.0
    assert np.isfinite(extremes).all()
    print("self-test passed: known reciprocal movement, gain invariance and clamped tilt")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--before", type=Path)
    parser.add_argument("--after", type=Path)
    parser.add_argument("--fraction", type=float, default=0.25)
    parser.add_argument("--tilt-db-per-octave", type=float, default=0.4375,
                        help="correction used by the after-render engine")
    parser.add_argument("--json", "--json-report", dest="json", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.before is None:
        parser.error("--before is required unless --self-test is used")
    if not 0.0 < args.fraction <= 0.5:
        parser.error("--fraction must be above zero and at most 0.5")
    if not math.isfinite(args.tilt_db_per_octave) or args.tilt_db_per_octave <= 0:
        parser.error("--tilt-db-per-octave must be finite and positive")
    before = load(args.before)
    fitted = fit(before, args.fraction)
    captures = (*CAPTURES, "microphones")
    if any(key[3] == "piezo" for key in before):
        captures = (*captures, "piezo")
    baseline = [score(before, window, capture)
                for window in WINDOWS for capture in captures]
    predicted = [movement(row, score(before, row["period"], row["capture"], fitted))
                 for row in baseline if row["capture"] != "piezo"]
    report = {
        "before": str(args.before.resolve()),
        "fraction_per_model": args.fraction,
        "fit_window_seconds": list(WINDOWS["whole"]),
        "brightness_bands_hz": [[LOW_HZ, SPLIT_HZ], [SPLIT_HZ, HIGH_HZ]],
        "pivot_hz": PIVOT_HZ,
        "coefficient_db_per_octave": fitted,
        "fitted_full_gap_tilt_db_per_octave": fitted / args.fraction,
        "method": "paired mean note brightness, both microphone captures; analytical output-EQ seed",
        "baseline": baseline,
        "predicted_output_eq": predicted,
        "caveat": "modal-residue scaling must be checked with after renders; captures and notes vary",
    }
    if args.after is not None:
        after = load(args.after)
        if set(before) != set(after):
            raise ValueError("before and after manifests do not have identical stock cases")
        actual = [movement(row, score(after, row["period"], row["capture"]))
                  for row in baseline]
        report["after"] = str(args.after.resolve())
        report["applied_correction_db_per_octave"] = args.tilt_db_per_octave
        report["actual_modal_movement"] = actual
        combined = next(row for row in actual
                        if row["period"] == "whole" and row["capture"] == "microphones")
        closure = combined["gap_closure_fraction"]
        report["suggested_correction_db_per_octave_linear_update"] = (
            args.tilt_db_per_octave * (2.0 * args.fraction) / closure
            if closure is not None and closure > 0 else None)
    if args.json is not None:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"Fitted correction: +/- {fitted:.6f} dB/octave; "
          f"full-gap coefficient {fitted / args.fraction:.6f}")
    for row in report.get("actual_modal_movement", predicted):
        if row["period"] != "whole":
            continue
        print(f"{row['capture']}: gap {row['gap_before_db']:.3f} -> "
              f"{row['gap_after_db']:.3f} dB; Original "
              f"{row['original_shift_db']:+.3f}, 78 "
              f"{row['bellido1978_shift_db']:+.3f} dB")


if __name__ == "__main__":
    main()
