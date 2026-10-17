#!/usr/bin/env python3
"""How loud the Guitar presets play, and the loudness meter the calibrations share.

Renders the plug-in's factory constructions (the Guitar presets in
Source/PluginEditor.cpp) playing typical material: single notes up the neck at
three velocities, alternating strums, a Travis-style arpeggio and two ringing
chords, each with Finger and with Pick, at the default Output and the stereo
microphones. Every render goes through AcustraPerformanceRenderer (the
shipping engine) and is measured as ITU-R BS.1770-4 integrated loudness
(K-weighted, 400 ms blocks, absolute and relative gates) and as plain RMS.
The report gives each preset's median and the grid's.

The meter, the performances and the event writer here are also what
Tools/CalibrateConstructionLoudness.py and Tools/CalibratePiezo.py measure
with. (The name is historical: the tool first compared nylon strings with
steel, and the instrument is steel-strung only since 2026-09-29.)

  python3 Tools/MeasureMaterialLoudness.py \\
      --renderer ./build-dsp/AcustraPerformanceRenderer [--json OUT.json]
  python3 Tools/MeasureMaterialLoudness.py --self-test

Renders are written to a temporary directory and removed as soon as they are
measured; nothing is downloaded and no audio is kept. NumPy and SciPy are
required.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.signal import lfilter

RATE = 48000
# Standard tuning; the renderer's channel 1 is the low E string.
OPEN_STRINGS = (40, 45, 50, 55, 59, 64)


def k_weight(signal: np.ndarray) -> np.ndarray:
    """BS.1770-4's K-weighting at 48 kHz, with the published coefficients."""
    shelf_b = (1.53512485958697, -2.69169618940638, 1.19839281085285)
    shelf_a = (1.0, -1.69065929318241, 0.73248077421585)
    high_pass_b = (1.0, -2.0, 1.0)
    high_pass_a = (1.0, -1.99004745483398, 0.99007225036621)
    return lfilter(high_pass_b, high_pass_a,
                   lfilter(shelf_b, shelf_a, signal, axis=0), axis=0)


def integrated_loudness(stereo: np.ndarray) -> float:
    """BS.1770-4 integrated loudness in LUFS of a (frames, 2) array."""
    weighted = k_weight(stereo.astype(np.float64))
    block, hop = int(0.4 * RATE), int(0.1 * RATE)
    count = 1 + (len(weighted) - block) // hop
    if count < 1:
        return float("-inf")
    power = np.array([
        np.mean(weighted[index * hop:index * hop + block] ** 2, axis=0).sum()
        for index in range(count)
    ])
    loudness = -0.691 + 10.0 * np.log10(np.maximum(power, 1.0e-30))
    absolute = power[loudness > -70.0]
    if absolute.size == 0:
        return float("-inf")
    relative = -0.691 + 10.0 * np.log10(absolute.mean()) - 10.0
    gated = power[(loudness > -70.0) & (loudness > relative)]
    return float(-0.691 + 10.0 * np.log10(gated.mean()))


def rms_db(stereo: np.ndarray) -> float:
    return float(10.0 * np.log10(np.mean(stereo.astype(np.float64) ** 2)))


# A performance is (string 1-6, MIDI note, onset s, release s, velocity) rows
# and a length in seconds.
CHORDS = {  # the note on each string, low E first, or None where not played
    "E": (40, 47, 52, 56, 59, 64), "Am": (None, 45, 52, 57, 60, 64),
    "G": (43, 47, 50, 55, 59, 67), "C": (None, 48, 52, 55, 60, 64),
    "D": (None, None, 50, 57, 62, 66),
}


def single_notes(velocity: int):
    notes = ((1, 40), (2, 47), (3, 52), (4, 57), (5, 61), (6, 64),
             (6, 69), (5, 64), (4, 60), (3, 55), (2, 50), (1, 43))
    rows = [(string, note, 0.1 + 0.7 * index, 0.75 + 0.7 * index, velocity)
            for index, (string, note) in enumerate(notes)]
    return rows, 0.1 + 0.7 * len(notes) + 1.0


def strums():
    rows, time, upstroke = [], 0.1, False
    for name in ("E", "Am", "C", "G", "D", "E", "Am", "E"):
        chord = CHORDS[name]
        for _ in range(4):
            strings = [string for string in range(6) if chord[string] is not None]
            if upstroke:  # an upstroke catches the treble strings
                strings = strings[::-1][:4]
            velocity = 82 if upstroke else 100
            for rank, string in enumerate(strings):
                rows.append((string + 1, chord[string], time + 0.009 * rank,
                             time + 0.49, velocity))
            time += 0.5
            upstroke = not upstroke
    return rows, time + 1.5


def arpeggio():
    rows, time = [], 0.1
    pattern = (0, 3, 1, 4, 2, 5, 3, 4)
    for name in ("C", "G", "Am", "E", "C", "G"):
        chord = CHORDS[name]
        strings = [string for string in range(6) if chord[string] is not None]
        for index, step in enumerate(pattern):
            string = strings[min(step, len(strings) - 1)]
            rows.append((string + 1, chord[string], time, time + 0.9,
                         78 if index % 2 else 92))
            time += 0.19
    return rows, time + 1.5


def ringing_chords():
    rows = [(string + 1, CHORDS["E"][string], 0.1 + 0.012 * string, 3.9, 105)
            for string in range(6)]
    rows += [(string + 1, CHORDS["G"][string], 4.2 + 0.012 * string, 8.0, 95)
             for string in range(6)]
    return rows, 8.5


PERFORMANCES = {
    "notes-soft": single_notes(50), "notes-medium": single_notes(90),
    "notes-hard": single_notes(120), "strums": strums(),
    "arpeggio": arpeggio(), "ringing-chords": ringing_chords(),
}

# The plug-in's Guitar presets (constructionPresets): shape, wood.
PRESETS = {
    "Dreadnought": ("dreadnought", "spruce"),
    "Auditorium": ("auditorium", "spruce"),
    "Parlor": ("parlor", "spruce"),
}


def render_command(renderer: str, events: Path, output: Path, capture: str,
                   picking: str, preset: str, *extra: str) -> list[str]:
    """An AcustraPerformanceRenderer command line for one preset."""
    shape, wood = PRESETS[preset]
    return [renderer, str(events), str(output), capture, picking,
            "--body-shape", shape, "--body-material", wood, *extra]


def write_performance(rows, seconds: float, path: Path) -> None:
    events = []
    for string, note, onset, release, velocity in rows:
        fret = note - OPEN_STRINGS[string - 1]
        if not 0 <= fret <= 20:
            raise ValueError(f"string {string} cannot play MIDI {note}")
        events.append((round(onset * RATE), 1, string, note, velocity))
        events.append((round(release * RATE), 0, string, note, 0))
    events.sort(key=lambda event: (event[0], event[1]))  # releases first
    with path.open("w") as output:
        output.write(f"ACUSTRA_PERFORMANCE_V1 {RATE} {round(seconds * RATE)}\n")
        for frame, _, string, note, velocity in events:
            output.write(f"{frame} {string} {note} {velocity} 0\n")


def measure(renderer: str, jobs: int) -> list[dict]:
    with tempfile.TemporaryDirectory(prefix="acustra-loudness-") as scratch:
        work = Path(scratch)
        for name, (rows, seconds) in PERFORMANCES.items():
            write_performance(rows, seconds, work / f"{name}.txt")

        def render(job):
            preset, picking, performance = job
            output = work / f"{preset}-{picking}-{performance}.f32".replace(" ", "_")
            subprocess.run(render_command(renderer, work / f"{performance}.txt", output,
                                          "stereo_mic", picking, preset),
                           check=True)
            audio = np.fromfile(output, dtype="<f4").reshape(-1, 2)
            output.unlink()
            return {
                "preset": preset, "picking": picking, "performance": performance,
                "lufs": integrated_loudness(audio), "rms_db": rms_db(audio),
                "peak_dbfs": float(20.0 * np.log10(np.max(np.abs(audio)))),
            }

        grid = [(preset, picking, performance) for preset in PRESETS
                for picking in ("finger", "pick") for performance in PERFORMANCES]
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            return list(pool.map(render, grid))


def summarise(rows: list[dict]) -> dict:
    summary = {}
    for key in ("lufs", "rms_db"):
        summary[key] = {"grid": float(np.median([row[key] for row in rows]))}
        for preset in PRESETS:
            summary[key][preset] = float(np.median(
                [row[key] for row in rows if row["preset"] == preset]))
    summary["loudest_peak_dbfs"] = max(row["peak_dbfs"] for row in rows)
    return summary


def self_test() -> None:
    time = np.arange(5 * RATE) / RATE
    tone = np.zeros((time.size, 2))
    tone[:, 0] = np.sin(2.0 * np.pi * 997.0 * time)
    # BS.1770-4: a 0 dBFS 997 Hz sine in one channel reads -3.01 LKFS.
    assert abs(integrated_loudness(tone) + 3.01) < 0.01, integrated_loudness(tone)
    assert abs(integrated_loudness(0.1 * tone) + 23.01) < 0.01
    for name, (rows, seconds) in PERFORMANCES.items():
        with tempfile.TemporaryDirectory() as scratch:
            write_performance(rows, seconds, Path(scratch) / "p.txt")
    fake = [{"preset": preset, "lufs": level, "rms_db": level - 3.0, "peak_dbfs": -1.0}
            for preset in PRESETS for level in (-20.0, -18.0, -10.0)]
    summary = summarise(fake)
    assert summary["lufs"]["grid"] == -18.0 and summary["rms_db"]["Parlor"] == -21.0
    command = render_command("r", Path("e.txt"), Path("o.f32"), "stereo_mic", "pick",
                             "Auditorium", "--touch", "1")
    assert command == ["r", "e.txt", "o.f32", "stereo_mic", "pick", "--body-shape",
                       "auditorium", "--body-material", "spruce", "--touch", "1"]
    print("MeasureMaterialLoudness self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--renderer", help="AcustraPerformanceRenderer")
    parser.add_argument("--json", type=Path, help="write every row here")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    if not arguments.renderer:
        parser.error("--renderer is required")
    rows = measure(arguments.renderer, max(1, arguments.jobs))
    summary = summarise(rows)
    if arguments.json:
        arguments.json.write_text(json.dumps(
            {"summary": summary, "rows": rows}, indent=1) + "\n")
    lufs, rms = summary["lufs"], summary["rms_db"]
    print(f"{len(rows)} renders, loudest peak {summary['loudest_peak_dbfs']:.2f} dBFS")
    print(f"{'median':<16} {'LUFS':>8}  {'RMS dB':>8}")
    for scope in ("grid", *PRESETS):
        print(f"{scope:<16} {lufs[scope]:8.2f}  {rms[scope]:8.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
