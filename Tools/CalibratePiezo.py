#!/usr/bin/env python3
"""Calibrate the under-saddle piezo chain's sensitivity and its level match.

Two numbers in AcustraEngine::PiezoDesign come from here (Docs/decisions.md,
2026-09-29):

1. sensitivity, S, in volts per engine force unit. The engine's displacement
   waves have no physical unit, so S is anchored to a physical level instead:
   the reference hard strum - the default construction, steel, default Touch,
   Finger, velocity 127, an open E major downstroke across all six strings at
   48 kHz - must peak at 1.0 V where it enters the preamp, the level Zollner
   gives for a piezo played loudly (M. Zollner, Physics of the Electric
   Guitar, 2005, ch.6 section 6.7). The renderer's `--observe piezo_voltage`
   gives that voltage with the S the engine was built with, and the voltage
   is linear in S, so one render sets it.

   The tool also predicts the same peak from physics - a saddle force of
   tension times string slope, at Zollner's 0.2 V/N for a bridge piezo - and
   stops if the two disagree by more than a factor of two. That needs the
   engine's displacement unit in metres: the fitted steelDisplacementScaleMetres
   the attack-pitch glide and the axial drive already read it with
   (FittedPhysicalData.h, "known to within a factor"). The check bounds S; it
   does not set it.

2. steelTrim and nylonTrim, the piezo's level match to the stereo
   microphones. Every construction the loudness tool renders
   (Tools/MeasureMaterialLoudness.py: the plug-in's six Guitar presets, each
   strung with steel and with nylon, Finger and Pick, single notes at three
   velocities, strums, an arpeggio and ringing chords) is rendered with
   Capture on the stereo microphones and on the piezo, and measured as
   ITU-R BS.1770-4 integrated loudness. A material's trim is the one that
   makes the median of (microphones - piezo) zero. Loudness rather than RMS:
   the two sensors' spectra differ by up to 15 dB per third octave (Zollner
   Figs 6.24/6.25), and plain RMS would weigh the microphones' bass.

  python3 Tools/CalibratePiezo.py --renderer ./build-dsp/AcustraPerformanceRenderer
  python3 Tools/CalibratePiezo.py --renderer ... --sensitivity-only
  python3 Tools/CalibratePiezo.py --self-test

The report prints the values to write into PiezoDesign and, with the values
already built in, how far each is from its target. Renders go to a temporary
directory and are removed once measured; nothing is downloaded and no audio is
kept. NumPy and SciPy are required.
"""

from __future__ import annotations

import argparse
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
import MeasureMaterialLoudness as loudness  # noqa: E402

RATE = loudness.RATE
ENGINE_HEADER = Path(__file__).resolve().parent.parent / "Source/DSP/AcustraEngine.h"
FITTED_HEADER = Path(__file__).resolve().parent.parent / "Source/DSP/FittedPhysicalData.h"
TARGET_PEAK_VOLTS = 1.0            # Zollner ch.6 section 6.7
PIEZO_VOLTS_PER_NEWTON = 0.2       # Zollner ch.6, a bridge piezo's sensitivity
STEEL_HARD_AMPLITUDE = 0.24        # initialisePluck: steel's amplitude at v = 1
STEEL_D_TENSION = 133.892          # N, the D string (steelTensionNewtons)
# initialisePluck's Finger distance at the default Pluck Position 0.28 with
# steel's pluckDistanceScale of 1.8, as a share of the 648 mm scale.
STEEL_FINGER_DISTANCE = (0.045 + 0.135 * 0.28) * 1.8 * 0.648
# One engine force unit is Z times one displacement unit per 48 kHz sample
# (FixedDerivative differences over the 48 kHz reference period).
REFERENCE_RATE = 48000.0


def built_design() -> dict:
    """The PiezoDesign values the engine was last built with."""
    text = ENGINE_HEADER.read_text()
    values = {}
    for name in ("sensitivity", "steelTrim", "nylonTrim"):
        match = re.search(rf"static constexpr float {name} = ([0-9.eE+-]+)f;", text)
        if not match:
            raise SystemExit(f"PiezoDesign::{name} not found in {ENGINE_HEADER}")
        values[name] = float(match.group(1))
    return values


def displacement_metres_per_unit() -> float:
    match = re.search(r"float steelDisplacementScaleMetres \{ ([0-9.eE+-]+)f \};",
                      FITTED_HEADER.read_text())
    if not match:
        raise SystemExit(f"steelDisplacementScaleMetres not found in {FITTED_HEADER}")
    return float(match.group(1))


def reference_strum():
    """An open E major downstroke at velocity 127, low E first, 9 ms apart."""
    chord = loudness.CHORDS["E"]
    rows = [(string + 1, chord[string], 0.1 + 0.009 * string, 2.9, 127)
            for string in range(6)]
    return rows, 3.0


def voltage_peaks(renderer: str, work: Path) -> dict:
    rows, seconds = reference_strum()
    events = work / "reference-strum.txt"
    output = work / "reference-strum.f32"
    loudness.write_performance(rows, seconds, events)
    subprocess.run([renderer, str(events), str(output), "stereo_mic", "finger",
                    "--string-material", "steel", "--observe", "piezo_voltage"],
                   check=True)
    volts = np.fromfile(output, dtype="<f4").reshape(-1, 2)[:, 0].astype(np.float64)
    output.unlink()
    return {"positive": float(volts.max()), "negative": float(volts.min()),
            "peak": float(np.max(np.abs(volts)))}


def physical_peak_volts() -> float:
    """What one hard pluck's saddle force predicts at 0.2 V/N, in volts.

    Tension T times the string's slope at the saddle, A / d for a release
    amplitude A at distance d, is the force the string held on the saddle
    before it was let go, and the step the piezo sees at release.
    """
    amplitude = STEEL_HARD_AMPLITUDE * displacement_metres_per_unit()
    force = STEEL_D_TENSION * amplitude / STEEL_FINGER_DISTANCE
    return PIEZO_VOLTS_PER_NEWTON * force


def level_rows(renderer: str, jobs: int) -> list[dict]:
    with tempfile.TemporaryDirectory(prefix="acustra-piezo-") as scratch:
        work = Path(scratch)
        for name, (rows, seconds) in loudness.PERFORMANCES.items():
            loudness.write_performance(rows, seconds, work / f"{name}.txt")

        def render(job):
            preset, material, picking, performance = job
            shape, wood, _strings, bridge, model = loudness.PRESETS[preset]
            levels = {}
            for capture in ("stereo_mic", "piezo"):
                output = work / f"{preset}-{material}-{picking}-{performance}-{capture}.f32"
                subprocess.run(
                    [renderer, str(work / f"{performance}.txt"), str(output),
                     capture, picking, bridge,
                     "--string-material", material, "--body-shape", shape,
                     "--body-material", wood, "--guitar-model", model],
                    check=True)
                audio = np.fromfile(output, dtype="<f4").reshape(-1, 2)
                output.unlink()
                levels[capture] = {
                    "lufs": loudness.integrated_loudness(audio),
                    "peak_dbfs": float(20.0 * np.log10(np.max(np.abs(audio)))),
                }
            return {"preset": preset, "material": material, "picking": picking,
                    "performance": performance,
                    "mic_lufs": levels["stereo_mic"]["lufs"],
                    "piezo_lufs": levels["piezo"]["lufs"],
                    "piezo_peak_dbfs": levels["piezo"]["peak_dbfs"],
                    "mic_minus_piezo": levels["stereo_mic"]["lufs"]
                                       - levels["piezo"]["lufs"]}

        grid = [(preset, material, picking, performance)
                for preset in loudness.PRESETS for material in ("steel", "nylon")
                for picking in ("finger", "pick")
                for performance in loudness.PERFORMANCES]
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            return list(pool.map(render, grid))


def summarise_levels(rows: list[dict], design: dict) -> dict:
    summary = {}
    for material, key in (("steel", "steelTrim"), ("nylon", "nylonTrim")):
        deltas = np.array([row["mic_minus_piezo"] for row in rows
                           if row["material"] == material])
        median = float(np.median(deltas))
        summary[material] = {
            "median_mic_minus_piezo_lu": median,
            "min_lu": float(deltas.min()), "max_lu": float(deltas.max()),
            "within_2_lu_of_median": float(np.mean(np.abs(deltas - median) <= 2.0)),
            "built_trim": design[key],
            "calibrated_trim": design[key] * 10.0 ** (median / 20.0),
        }
    summary["loudest_piezo_peak_dbfs"] = max(row["piezo_peak_dbfs"] for row in rows)
    return summary


def self_test() -> None:
    rows, seconds = reference_strum()
    assert len(rows) == 6 and all(row[4] == 127 for row in rows)
    assert [row[1] for row in rows] == [40, 47, 52, 56, 59, 64]
    with tempfile.TemporaryDirectory() as scratch:
        loudness.write_performance(rows, seconds, Path(scratch) / "p.txt")
    predicted = physical_peak_volts()
    assert 0.05 < predicted < 2.0, predicted
    design = built_design()
    assert design["sensitivity"] > 0.0
    fake = [{"material": m, "mic_minus_piezo": d, "piezo_peak_dbfs": -6.0}
            for m, d in (("steel", 1.0), ("steel", 2.0), ("steel", 3.0),
                         ("nylon", -2.0), ("nylon", -2.0), ("nylon", 4.0))]
    summary = summarise_levels(fake, {"sensitivity": 1.0, "steelTrim": 1.0,
                                      "nylonTrim": 2.0})
    assert abs(summary["steel"]["calibrated_trim"] - 10 ** (2.0 / 20)) < 1e-12
    assert abs(summary["nylon"]["calibrated_trim"] - 2.0 * 10 ** (-2.0 / 20)) < 1e-12
    print("CalibratePiezo self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--renderer", help="AcustraPerformanceRenderer")
    parser.add_argument("--json", type=Path, help="write the rows and summary here")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--sensitivity-only", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    if not arguments.renderer:
        parser.error("--renderer is required")
    design = built_design()
    with tempfile.TemporaryDirectory(prefix="acustra-piezo-") as scratch:
        peaks = voltage_peaks(arguments.renderer, Path(scratch))
    anchored = design["sensitivity"] * TARGET_PEAK_VOLTS / peaks["peak"]
    predicted = physical_peak_volts()
    # The same strum through a physically scaled element: an engine force
    # unit is Z (N s/m) times one displacement unit per 48 kHz sample.
    newtons_per_unit = REFERENCE_RATE * displacement_metres_per_unit()
    physical = PIEZO_VOLTS_PER_NEWTON * newtons_per_unit
    ratio = anchored / physical
    report = {"built": design, "reference_strum_volts": peaks,
              "sensitivity_for_1V": anchored,
              "physical_sensitivity": physical, "anchor_over_physical": ratio,
              "physical_peak_volts": predicted}
    print(f"built sensitivity {design['sensitivity']:.6g} V/unit: reference strum "
          f"peaks {peaks['positive']:+.4f} / {peaks['negative']:+.4f} V")
    print(f"sensitivity for a 1 V peak: {anchored:.6g} V/unit")
    print(f"physical check: {physical:.6g} V/unit at {PIEZO_VOLTS_PER_NEWTON} V/N "
          f"and {displacement_metres_per_unit() * 1e3:.2f} mm per displacement unit "
          f"(anchor / physical = {ratio:.3f}); one hard pluck's tension x slope "
          f"is a {predicted:.2f} V step")
    if not 0.5 <= ratio <= 2.0:
        print("STOP: the 1 V anchor and the physical estimate disagree by more "
              "than 2x; decide which to trust before calibrating further.")
        if arguments.json:
            arguments.json.write_text(json.dumps(report, indent=1) + "\n")
        return 1
    if not arguments.sensitivity_only:
        rows = level_rows(arguments.renderer, max(1, arguments.jobs))
        summary = summarise_levels(rows, design)
        report["levels"] = summary
        report["rows"] = rows
        for material in ("steel", "nylon"):
            entry = summary[material]
            print(f"{material}: microphones - piezo median "
                  f"{entry['median_mic_minus_piezo_lu']:+.2f} LU "
                  f"(range {entry['min_lu']:+.2f} to {entry['max_lu']:+.2f}, "
                  f"{100 * entry['within_2_lu_of_median']:.0f}% within 2 LU of it); "
                  f"trim {entry['built_trim']:.6g} -> {entry['calibrated_trim']:.6g}")
        print(f"{len(rows)} pairs, loudest piezo peak "
              f"{summary['loudest_piezo_peak_dbfs']:.2f} dBFS")
    if arguments.json:
        arguments.json.write_text(json.dumps(report, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
