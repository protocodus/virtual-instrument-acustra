#!/usr/bin/env python3
"""Per-string chromatic sweeps (frets 0-20 on each of the six strings) for the
robustness check: the E2-C6 sweep plays every note at or above E4 on the top
string, so it samples one string's bridge drive in the mid band.

Same score format, hold, length and velocity as Tools/MeasureNoteEvenness.py
(write_score with the string fixed). Renders strings/<shape>-<wood>/<mic|piezo>/s<string>-<midi>.f32.
"""
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
RENDERER = STUDY / "bin" / "AcustraPerformanceRenderer"
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa: E402

SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
VARIANTS = {"mic": "stereo_mic", "piezo": "piezo"}


def score(path, string, midi, velocity=96):
    path.write_text(
        f"ACUSTRA_PERFORMANCE_V1 {ev.RATE} {round(ev.NOTE_SECONDS * ev.RATE)}\n"
        f"0 {string} {midi} {velocity} 0\n"
        f"{round(ev.HOLD_SECONDS * ev.RATE)} {string} {midi} 0 0\n")


def jobs():
    scores = STUDY / "strings" / "scores"
    scores.mkdir(parents=True, exist_ok=True)
    for string, open_note in enumerate(ev.OPEN_NOTES, start=1):
        for fret in range(0, 21):
            midi = open_note + fret
            path = scores / f"s{string}-{midi}.events"
            if not path.exists():
                score(path, string, midi)
            for shape in SHAPES:
                for wood in WOODS:
                    for name, capture in VARIANTS.items():
                        out = STUDY / "strings" / f"{shape}-{wood}" / name / f"s{string}-{midi}.f32"
                        out.parent.mkdir(parents=True, exist_ok=True)
                        if out.exists():
                            continue
                        yield [str(RENDERER), str(path), str(out), capture, "finger",
                               "--body-shape", shape, "--body-material", wood,
                               "--room", "0.5", "--piezo-mix", "0"]


def main():
    todo = list(jobs())
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda c: subprocess.run(c, check=True, capture_output=True), todo))
    print(f"{len(todo)} renders")


if __name__ == "__main__":
    main()
