#!/usr/bin/env python3
"""Measure note-to-note evenness and low-frequency balance of single notes.

Renders a chromatic sweep (default E2-E5) through AcustraPerformanceRenderer,
one fresh engine per note, each note on the string that reaches it at the
lowest fret, held 1.2 s at one velocity, and reports for every note:

  rms    sustained level, mono sum, 0.08-1.15 s after the attack (dBFS)
  dev    that level against the mean of up to three neighbours each side (dB)
  att    attack level, 5-80 ms (dBFS)
  bass, lowmid, pres, treb
         share of the sustained power in 70-180, 180-650, 650-2000 and
         2000-6000 Hz (dB re the 20 Hz-10 kHz total)
  fund   share of the sustained power at the fundamental (+/-1.5%)
  H2/H1  second partial against the fundamental (dB)
  decay  0.95-1.15 s against 0.10-0.30 s (dB)

and the RMS of dev over the sweep with its extreme notes. A prominent
positive dev on a low note near the body's air mode, or a deep negative one
under a near-field antiresonance, is where a listener hears a resonance or
a hole; this measures where they are and how large, not whether they are
heard as unnatural (a real flat-top's own sweep deviates by about 2 dB RMS,
README Known gaps). Signal metrics establish no listener preference.

Any renderer option may follow the output directory, including --calibration
FILE (Tools/CalibrationFile.h's "name value" lines) to try a calibration
change without rebuilding. Renders already in the output directory are
reused, so keep one directory per variant.

  python3 Tools/MeasureNoteEvenness.py build-dsp/AcustraPerformanceRenderer \
      out/default --capture stereo_mic --technique finger
  python3 Tools/MeasureNoteEvenness.py build-dsp/AcustraPerformanceRenderer \
      out/jumbo --body-shape jumbo
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from scipy.signal import welch

RATE = 48000
OPEN_NOTES = (40, 45, 50, 55, 59, 64)
NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
HOLD_SECONDS = 1.2
NOTE_SECONDS = 2.0
SUSTAIN = (0.08, 1.15)
ATTACK = (0.005, 0.08)
EARLY = (0.10, 0.30)
LATE = (0.95, 1.15)
BANDS = {"bass": (70, 180), "lowmid": (180, 650), "pres": (650, 2000),
         "treb": (2000, 6000)}


def note_name(midi: int) -> str:
    return NAMES[midi % 12] + str(midi // 12 - 1)


def db(power: float) -> float:
    return float(10 * np.log10(max(float(power), 1e-30)))


def lowest_fret_string(midi: int) -> int:
    """1-based string whose open note is the highest at or below the note."""
    return max(index for index, open_note in enumerate(OPEN_NOTES)
               if open_note <= midi) + 1


def write_score(path: Path, midi: int, velocity: int) -> None:
    string = lowest_fret_string(midi)
    path.write_text(
        f"ACUSTRA_PERFORMANCE_V1 {RATE} {round(NOTE_SECONDS * RATE)}\n"
        f"0 {string} {midi} {velocity} 0\n"
        f"{round(HOLD_SECONDS * RATE)} {string} {midi} 0 0\n")


def segment(signal: np.ndarray, window: tuple[float, float]) -> np.ndarray:
    return signal[round(window[0] * RATE):round(window[1] * RATE)]


def note_metrics(mono: np.ndarray, midi: int) -> dict:
    """Sustained-level and balance metrics of one rendered note (mono sum)."""
    f0 = 440.0 * 2 ** ((midi - 69) / 12)
    sustain = segment(mono, SUSTAIN)
    length = min(16384, len(sustain))
    frequencies, density = welch(sustain, fs=RATE, nperseg=length,
                                 noverlap=length // 2, window="hann",
                                 detrend=False, scaling="density")
    step = frequencies[1] - frequencies[0]

    def band(low: float, high: float) -> float:
        return float(density[(frequencies >= low) & (frequencies < high)].sum()
                     * step)

    total = band(20, 10000)

    def partial(index: int) -> float:
        centre = f0 * index
        width = max(step, centre * 0.015)
        return band(centre - width, centre + width)

    result = {"midi": midi, "name": note_name(midi), "f0_hz": f0,
              "rms_dbfs": db(np.mean(sustain * sustain)),
              "attack_dbfs": db(np.mean(segment(mono, ATTACK) ** 2)),
              "peak_dbfs": db(np.max(np.abs(mono)) ** 2),
              "fundamental_share_db": db(partial(1) / max(total, 1e-30)),
              "h2_over_h1_db": db(partial(2) / max(partial(1), 1e-30)),
              "decay_db": db(np.mean(segment(mono, LATE) ** 2))
              - db(np.mean(segment(mono, EARLY) ** 2))}
    for name, bounds in BANDS.items():
        result[f"{name}_share_db"] = db(band(*bounds) / max(total, 1e-30))
    return result


def neighbourhood_deviation(levels: np.ndarray, reach: int = 3) -> np.ndarray:
    """Each level against the mean of up to `reach` neighbours each side."""
    deviation = np.zeros(len(levels))
    for index in range(len(levels)):
        neighbours = [other for other in range(max(0, index - reach),
                                               min(len(levels), index + reach + 1))
                      if other != index]
        deviation[index] = levels[index] - levels[neighbours].mean()
    return deviation


def render(renderer: str, score: Path, raw: Path, options: list[str]) -> np.ndarray:
    if not raw.exists():
        subprocess.run([renderer, str(score), str(raw), *options], check=True,
                       capture_output=True, text=True)
    audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2).astype(np.float64)
    if len(audio) != round(NOTE_SECONDS * RATE) or not np.isfinite(audio).all():
        raise ValueError(f"non-finite or incorrectly sized render: {raw}")
    return audio.mean(axis=1)


def sweep(renderer: str, output: Path, options: list[str], first: int,
          last: int, velocity: int) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    rows = []
    for midi in range(first, last + 1):
        score = output / f"note-{midi}.events"
        write_score(score, midi, velocity)
        mono = render(renderer, score, output / f"note-{midi}.f32", options)
        rows.append(note_metrics(mono, midi))
    deviation = neighbourhood_deviation(np.array([row["rms_dbfs"] for row in rows]))
    for row, value in zip(rows, deviation):
        row["neighbourhood_deviation_db"] = float(value)
    worst = int(np.argmax(np.abs(deviation)))
    return {"renderer": renderer, "options": options, "velocity": velocity,
            "rms_deviation_db": float(np.sqrt(np.mean(deviation ** 2))),
            "worst_note": rows[worst]["name"],
            "worst_deviation_db": float(deviation[worst]),
            "deepest_hole_db": float(deviation.min()),
            "deepest_hole_note": rows[int(deviation.argmin())]["name"],
            "highest_peak_db": float(deviation.max()),
            "highest_peak_note": rows[int(deviation.argmax())]["name"],
            "notes": rows}


def print_report(report: dict) -> None:
    print(f"{'note':>5} {'f0':>6} {'rms':>6} {'dev':>5} {'att':>6} {'bass':>6} "
          f"{'lomid':>6} {'pres':>6} {'treb':>6} {'fund':>6} {'H2/H1':>6} {'decay':>6}")
    for row in report["notes"]:
        print(f"{row['name']:>5} {row['f0_hz']:6.1f} {row['rms_dbfs']:6.1f} "
              f"{row['neighbourhood_deviation_db']:5.1f} {row['attack_dbfs']:6.1f} "
              f"{row['bass_share_db']:6.1f} {row['lowmid_share_db']:6.1f} "
              f"{row['pres_share_db']:6.1f} {row['treb_share_db']:6.1f} "
              f"{row['fundamental_share_db']:6.1f} {row['h2_over_h1_db']:6.1f} "
              f"{row['decay_db']:6.1f}")
    print(f"rms deviation {report['rms_deviation_db']:.2f} dB; worst "
          f"{report['worst_note']} {report['worst_deviation_db']:+.1f} dB; deepest hole "
          f"{report['deepest_hole_note']} {report['deepest_hole_db']:+.1f} dB; highest peak "
          f"{report['highest_peak_note']} {report['highest_peak_db']:+.1f} dB")


def self_test() -> int:
    """The metrics on synthesised notes whose answers are known."""
    failures = 0

    def expect(condition: bool, message: str) -> None:
        nonlocal failures
        if not condition:
            failures += 1
            print("FAIL:", message)

    time = np.arange(round(NOTE_SECONDS * RATE)) / RATE
    # A2 at -20 dBFS RMS with a second partial 6 dB under it, decaying 10 dB
    # over the sustain window's late-against-early comparison.
    f0 = 110.0
    envelope = 10 ** (-10 / 20 * (time - 0.2) / 0.85)
    note = (np.sqrt(2) * 0.1 * np.sin(2 * np.pi * f0 * time)
            + np.sqrt(2) * 0.05 * np.sin(2 * np.pi * 2 * f0 * time)) * envelope
    metrics = note_metrics(note, 45)
    expect(abs(metrics["h2_over_h1_db"] + 6.0) < 0.3,
           f"H2/H1 read {metrics['h2_over_h1_db']:.2f} dB for a -6 dB partial")
    expect(abs(metrics["decay_db"] + 10.0) < 0.6,
           f"decay read {metrics['decay_db']:.2f} dB for a 10 dB fall")
    expect(abs(metrics["bass_share_db"] - db(1 / 1.25)) < 0.4,
           f"bass share read {metrics['bass_share_db']:.2f} dB for 80% in band")
    expect(metrics["fundamental_share_db"] < 0.0
           and metrics["fundamental_share_db"] > -1.5,
           f"fundamental share read {metrics['fundamental_share_db']:.2f} dB for 80%")
    expect(lowest_fret_string(40) == 1 and lowest_fret_string(44) == 1
           and lowest_fret_string(45) == 2 and lowest_fret_string(64) == 6
           and lowest_fret_string(76) == 6,
           "the lowest-fret string choice is wrong")
    levels = np.array([0.0, 0.0, 0.0, 6.0, 0.0, 0.0, 0.0])
    deviation = neighbourhood_deviation(levels)
    expect(abs(deviation[3] - 6.0) < 1e-9 and abs(deviation[0] + 2.0) < 1e-9,
           "the neighbourhood deviation is not the level over its neighbours' mean")
    if failures:
        print(f"{failures} MeasureNoteEvenness self-test failure(s)")
        return 1
    print("MeasureNoteEvenness self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true",
                        help="check the metrics on synthesised notes and exit")
    parser.add_argument("renderer", nargs="?", help="AcustraPerformanceRenderer")
    parser.add_argument("output", nargs="?", type=Path,
                        help="directory for the renders and summary.json")
    parser.add_argument("--capture", default="stereo_mic",
                        choices=("stereo_mic", "mono_mic", "piezo"))
    parser.add_argument("--technique", default="finger",
                        choices=("finger", "pick", "thumb"))
    parser.add_argument("--first", type=int, default=40, help="first MIDI note")
    parser.add_argument("--last", type=int, default=76, help="last MIDI note")
    parser.add_argument("--velocity", type=int, default=96)
    arguments, renderer_options = parser.parse_known_args()
    if arguments.self_test:
        return self_test()
    if arguments.renderer is None or arguments.output is None:
        parser.error("renderer and output are required unless --self-test")
    if not 40 <= arguments.first <= arguments.last <= 84:
        parser.error("the sweep must lie within MIDI 40-84")
    options = [arguments.capture, arguments.technique, *renderer_options]
    report = sweep(arguments.renderer, arguments.output, options,
                   arguments.first, arguments.last, arguments.velocity)
    (arguments.output / "summary.json").write_text(json.dumps(report, indent=1) + "\n")
    print_report(report)
    return 0


if __name__ == "__main__":
    sys.exit(main())
