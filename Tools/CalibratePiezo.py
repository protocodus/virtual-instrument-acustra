#!/usr/bin/env python3
"""Check the under-saddle piezo chain's headroom and calibrate its level match.

The chain is the documented circuit in AcustraEngine::PiezoDesign
(Docs/decisions.md, 2026-09-29, "Accurate piezo chain"): its sensitivity is
physical - Zollner's 0.2 V/N for a bridge piezo on the engine's force in
newtons (0.0061 m per displacement unit, FittedPhysicalData.h) - so nothing
here sets it. This tool

1. checks it: the hottest reference strum - an open E major downstroke
   across all six strings at velocity 127 with the Pick, the playing that
   drives the saddle hardest, at 48 kHz, over the six Guitar presets strung
   with each material and Touch at 0, 0.58 and 1 - must leave the element
   at 0.7-2.5 V open-circuit (Zollner gives 1-2 V for a piezo played loudly,
   M. Zollner, Physics of the Electric Guitar, 2005, ch.6 section 6.7; the
   margin allows for the Ovation element standing in for a strip). It
   reports that peak, the jack's, and the headroom left to U1A's input range
   and U1B's output swing (the renderer's `--observe piezo_levels` and
   `piezo_stages`). A one-pluck physical estimate - tension times string
   slope at 0.2 V/N - is printed beside it.

2. calibrates steelTrim and nylonTrim, the piezo's level match to the stereo
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
  python3 Tools/CalibratePiezo.py --renderer ... --headroom-only
  python3 Tools/CalibratePiezo.py --self-test

The report prints the trims to write into PiezoDesign and, with the values
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
OPEN_CIRCUIT_RANGE = (0.7, 2.5)    # V; Zollner ch.6 section 6.7 gives 1-2 V
PIEZO_VOLTS_PER_NEWTON = 0.2       # Zollner ch.6, a bridge piezo's sensitivity
STEEL_HARD_AMPLITUDE = 0.24        # initialisePluck: steel's amplitude at v = 1
STEEL_D_TENSION = 133.892          # N, the D string (steelTensionNewtons)
# initialisePluck's Finger distance at the default Pluck Position 0.28 with
# steel's pluckDistanceScale of 1.8, as a share of the 648 mm scale.
STEEL_FINGER_DISTANCE = (0.045 + 0.135 * 0.28) * 1.8 * 0.648


def built_design() -> dict:
    """The PiezoDesign values the engine was last built with."""
    text = ENGINE_HEADER.read_text()
    values = {}
    for name, kind in (("steelTrim", "float"), ("nylonTrim", "float"),
                       ("railHigh", "double"), ("railLow", "double"),
                       ("commonModeLimit", "double")):
        match = re.search(rf"static constexpr {kind} {name} = ([0-9.eE+-]+)f?;", text)
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


ANCHOR_TOUCHES = (0.0, 0.58, 1.0)


def anchor_grid():
    """Every construction, string material and Touch the anchor strum is
    played with, all with the Pick at velocity 127."""
    return [(preset, material, touch) for preset in loudness.PRESETS
            for material in ("steel", "nylon") for touch in ANCHOR_TOUCHES]


def strum_observation(renderer: str, work: Path, picking: str, preset: str,
                      material: str, touch: float, observation: str) -> np.ndarray:
    """One reference strum's (frames, 2) observation, in volts."""
    shape, wood, _strings, bridge, model = loudness.PRESETS[preset]
    rows, seconds = reference_strum()
    tag = f"{preset}-{material}-{picking}-{touch}-{observation}".replace(" ", "_")
    events = work / f"{tag}.txt"
    output = work / f"{tag}.f32"
    loudness.write_performance(rows, seconds, events)
    subprocess.run([renderer, str(events), str(output), "stereo_mic", picking,
                    bridge, "--string-material", material, "--body-shape", shape,
                    "--body-material", wood, "--guitar-model", model,
                    "--touch", f"{touch}", "--observe", observation],
                   check=True)
    values = np.fromfile(output, dtype="<f4").reshape(-1, 2).astype(np.float64)
    output.unlink()
    events.unlink()
    return values


def headroom_db(stages: np.ndarray, design: dict) -> dict:
    """dB left to U1A's input range and to U1B's output swing (negative:
    past it) at the strum's worst sample."""
    buffer_input, drive = stages[:, 0], stages[:, 1]
    u1a = 20.0 * np.log10(design["commonModeLimit"] / max(np.max(np.abs(buffer_input)), 1e-30))
    high = design["railHigh"] / max(np.max(drive), 1e-30)
    low = design["railLow"] / min(np.min(drive), -1e-30)
    return {"u1a_db": float(u1a), "u1b_db": float(20.0 * np.log10(min(high, low)))}


def strum_peaks(renderer: str, work: Path, jobs: int, design: dict) -> dict:
    """The anchor grid's open-circuit and jack peaks and headroom, the
    hottest of them, and a Finger strum for comparison."""
    def peak(job):
        preset, material, touch, picking = job
        levels = strum_observation(renderer, work, picking, preset, material, touch, "piezo_levels")
        stages = strum_observation(renderer, work, picking, preset, material, touch, "piezo_stages")
        return {"preset": preset, "material": material, "touch": touch, "picking": picking,
                "open_circuit": float(np.max(np.abs(levels[:, 0]))),
                "jack": float(np.max(np.abs(levels[:, 1]))), **headroom_db(stages, design)}
    grid = [job + ("pick",) for job in anchor_grid()]
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        rows = list(pool.map(peak, grid + [("Dreadnought", "steel", 0.58, "finger")]))
    return {"hottest": max(rows[:-1], key=lambda row: row["open_circuit"]),
            "least_headroom": min(rows[:-1], key=lambda row: min(row["u1a_db"], row["u1b_db"])),
            "grid": rows[:-1], "finger_reference": rows[-1]}


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
    grid = anchor_grid()
    assert len(grid) == len(loudness.PRESETS) * 2 * len(ANCHOR_TOUCHES)
    assert {material for _, material, _ in grid} == {"steel", "nylon"}
    assert [row[1] for row in rows] == [40, 47, 52, 56, 59, 64]
    with tempfile.TemporaryDirectory() as scratch:
        loudness.write_performance(rows, seconds, Path(scratch) / "p.txt")
    predicted = physical_peak_volts()
    assert 0.05 < predicted < 2.0, predicted
    design = built_design()
    assert design["railHigh"] > 0.0 > design["railLow"] and design["commonModeLimit"] > 0.0
    stages = np.array([[1.0, 1.63125], [-2.5, -3.9125]])
    room = headroom_db(stages, design)
    assert abs(room["u1a_db"]) < 1e-6 and abs(room["u1b_db"]) < 1e-3, room
    fake = [{"material": m, "mic_minus_piezo": d, "piezo_peak_dbfs": -6.0}
            for m, d in (("steel", 1.0), ("steel", 2.0), ("steel", 3.0),
                         ("nylon", -2.0), ("nylon", -2.0), ("nylon", 4.0))]
    summary = summarise_levels(fake, {"steelTrim": 1.0, "nylonTrim": 2.0})
    assert abs(summary["steel"]["calibrated_trim"] - 10 ** (2.0 / 20)) < 1e-12
    assert abs(summary["nylon"]["calibrated_trim"] - 2.0 * 10 ** (-2.0 / 20)) < 1e-12
    print("CalibratePiezo self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--renderer", help="AcustraPerformanceRenderer")
    parser.add_argument("--json", type=Path, help="write the rows and summary here")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--headroom-only", "--sensitivity-only", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    if not arguments.renderer:
        parser.error("--renderer is required")
    design = built_design()
    with tempfile.TemporaryDirectory(prefix="acustra-piezo-") as scratch:
        peaks = strum_peaks(arguments.renderer, Path(scratch), max(1, arguments.jobs), design)
    hottest, least = peaks["hottest"], peaks["least_headroom"]
    finger = peaks["finger_reference"]
    predicted = physical_peak_volts()
    report = {"built": design, "reference_strums": peaks, "physical_peak_volts": predicted}
    print(f"hottest Pick strum ({hottest['preset']}, {hottest['material']}, Touch "
          f"{hottest['touch']}): {hottest['open_circuit']:.3f} V open-circuit, "
          f"{hottest['jack']:.3f} V at the jack; headroom {hottest['u1a_db']:+.2f} dB to "
          f"U1A's input range, {hottest['u1b_db']:+.2f} dB to U1B's swing")
    print(f"least headroom ({least['preset']}, {least['material']}, Touch {least['touch']}): "
          f"U1A {least['u1a_db']:+.2f} dB, U1B {least['u1b_db']:+.2f} dB")
    print(f"the Finger strum (Dreadnought, steel): {finger['open_circuit']:.3f} V open-circuit")
    print(f"physical check: one hard pluck's tension x slope at "
          f"{PIEZO_VOLTS_PER_NEWTON} V/N is a {predicted:.2f} V step")
    low, high = OPEN_CIRCUIT_RANGE
    if not low <= hottest["open_circuit"] <= high:
        print(f"STOP: the hottest strum's open-circuit peak is outside {low}-{high} V; "
              "the force scale or the element's sensitivity needs a look.")
        if arguments.json:
            arguments.json.write_text(json.dumps(report, indent=1) + "\n")
        return 1
    if not arguments.headroom_only:
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
