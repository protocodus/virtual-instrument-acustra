#!/usr/bin/env python3
"""Level against pitch in an isolated-note corpus, beside the model's.

Protocol. For every row of a corpus in the shared corpus row schema
(Tools/BenchmarkOpenCorpora.py; Tools/PrepareEastmanCorpus.py builds the CC0
Eastman E1D chromatic walks, one guitar, one microphone, one take per
technique), the target's channels are summed and two levels are read from
the note's own onset (20 ms into the target): the RMS of the first 100 ms
(attack) and of 0.3-0.8 s (sustain), in dBFS. Round robins of one pitch are
reduced to their median. The trend is the least-squares slope of level
against pitch in dB per octave over MIDI 40-76 (every pitch the six strings
reach below the 12th fret of the high E) and over 40-84.

The same two slopes are read from a Tools/MeasureFretboardSweep.py report
when one is given (--sweep REPORT.json): per preset and velocity, each
pitch's level is the median over the strings that can play it, since the
recordings do not say which string the player chose. Nothing is level
matched: the recordings' absolute level is the player's effort and the
microphone's distance, so only the slopes are compared, and a player who
leans into the high notes is part of the recorded slope.

  python3 Tools/MeasureCorpusLevels.py out/eastman/rows.json \\
      --sweep out/sweep/report.json
"""
from __future__ import annotations

import argparse
import json
import statistics
from pathlib import Path

import numpy as np

ONSET_SECONDS = 0.02
ATTACK_SECONDS = 0.1
SUSTAIN_SECONDS = (0.3, 0.8)
RANGES = ((40, 76), (40, 84))


def db(window: np.ndarray) -> float:
    return float(20.0 * np.log10(np.sqrt(np.mean(window * window)) + 1e-12))


def slope_db_per_octave(midi: list[int], levels: list[float]) -> float:
    design = np.vstack([np.asarray(midi, float) / 12.0, np.ones(len(midi))]).T
    return float(np.linalg.lstsq(design, np.asarray(levels, float), rcond=None)[0][0])


def corpus_levels(rows_path: Path) -> dict[str, dict[int, tuple[float, float]]]:
    corpus = json.loads(rows_path.read_text(encoding="utf-8"))
    taken: dict[str, dict[int, list[tuple[float, float]]]] = {}
    for row in corpus["rows"]:
        target = row["target"]
        rate = int(target["sample_rate"])
        audio = np.fromfile(rows_path.parent / target["path"], dtype="<f4")
        mono = audio.reshape(-1, int(target["channels"])).mean(axis=1).astype(np.float64)
        onset = int(ONSET_SECONDS * rate)
        sustain = mono[onset + int(SUSTAIN_SECONDS[0] * rate):onset + int(SUSTAIN_SECONDS[1] * rate)]
        if len(sustain) < int(0.8 * (SUSTAIN_SECONDS[1] - SUSTAIN_SECONDS[0]) * rate):
            continue
        attack = mono[onset:onset + int(ATTACK_SECONDS * rate)]
        taken.setdefault(row["picking"] or "unknown", {}).setdefault(
            int(row["midi"]), []).append((db(attack), db(sustain)))
    return {picking: {midi: (statistics.median(a for a, _ in takes),
                             statistics.median(s for _, s in takes))
                      for midi, takes in by_midi.items()}
            for picking, by_midi in taken.items()}


def report_slopes(label: str, levels: dict[int, tuple[float, float]]) -> None:
    for low, high in RANGES:
        midi = sorted(m for m in levels if low <= m <= high)
        if len(midi) < 6:
            continue
        print(f"  {label}: attack {slope_db_per_octave(midi, [levels[m][0] for m in midi]):+.1f}"
              f"  sustain {slope_db_per_octave(midi, [levels[m][1] for m in midi]):+.1f}"
              f" dB/oct over MIDI {low}-{high} ({len(midi)} pitches)")


def sweep_levels(report: Path) -> dict[str, dict[int, tuple[float, float]]]:
    out: dict[str, dict[int, tuple[float, float]]] = {}
    for preset in json.loads(report.read_text(encoding="utf-8")):
        for velocity in preset["velocities"]:
            by_midi: dict[int, list[tuple[float, float]]] = {}
            for note in preset["notes"]:
                if note["velocity"] == velocity:
                    by_midi.setdefault(note["midi"], []).append(
                        (note["attack_dbfs"], note["sustain_dbfs"]))
            out[f"{preset['preset']} v{velocity}"] = {
                midi: (statistics.median(a for a, _ in notes),
                       statistics.median(s for _, s in notes))
                for midi, notes in by_midi.items()}
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rows", type=Path, nargs="+", metavar="CORPUS_ROWS_JSON")
    parser.add_argument("--sweep", type=Path, metavar="REPORT_JSON")
    parser.add_argument("--table", action="store_true",
                        help="print every pitch's attack and sustain level")
    arguments = parser.parse_args()
    for rows in arguments.rows:
        print(f"== {rows}")
        for picking, levels in sorted(corpus_levels(rows).items()):
            if arguments.table:
                for midi in sorted(levels):
                    print(f"  {picking} {midi:3d} {levels[midi][0]:6.1f} {levels[midi][1]:6.1f}")
            report_slopes(picking, levels)
    if arguments.sweep:
        print(f"== {arguments.sweep}")
        for label, levels in sweep_levels(arguments.sweep).items():
            report_slopes(label, levels)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
