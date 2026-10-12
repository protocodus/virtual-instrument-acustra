#!/usr/bin/env python3
"""Render everything the piezo-polarity study needs, with the frozen renderer.

For each of the 12 constructions (Shape x Wood), Finger, Room 0.5:
  sweeps/<shape>-<wood>/{mic,piezo,default}/  E2-C6 single notes via
      Tools/MeasureNoteEvenness.py (--last 84), velocity 96:
      mic     = stereo_mic, --piezo-mix 0      (V0, the microphones alone)
      piezo   = piezo capture, --piezo-mix 0   (the piezo term alone)
      default = stereo_mic, --piezo-mix 0.2    (the shipped default, V1 as the engine renders it)
  phrases/<shape>-<wood>-<phrase>-<variant>.f32 for the strummed phrase
      (Tools/MeasureMaterialLoudness.py strums(): E-Am-C-G-D-E-Am-E, 32
      strokes) and the calibration phrase set
      (Tools/CalibrateConstructionLoudness.py phrase_set()), same three variants.
  picks/<shape>-<wood>-<note>-<variant>.f32: single Pick notes, velocity 120,
      for the transient comparison.
Renders that exist are reused.
"""
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
RENDERER = STUDY / "bin" / "AcustraPerformanceRenderer"
sys.path.insert(0, str(REPO / "Tools"))
import MeasureMaterialLoudness as meter  # noqa: E402
import CalibrateConstructionLoudness as calib  # noqa: E402
import MeasureNoteEvenness as evenness  # noqa: E402

SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
VARIANTS = {"mic": ("stereo_mic", "0"), "piezo": ("piezo", "0"),
            "default": ("stereo_mic", "0.2")}
PICK_NOTES = ((1, 40), (4, 55), (6, 64), (6, 66), (5, 60))  # E2, G3, E4, F#4, C5


def run(command, log=None):
    result = subprocess.run(command, check=True, capture_output=True, text=True)
    if log is not None:
        Path(log).write_text(result.stdout + result.stderr)


def sweep_jobs():
    for shape in SHAPES:
        for wood in WOODS:
            for name, (capture, mix) in VARIANTS.items():
                out = STUDY / "sweeps" / f"{shape}-{wood}" / name
                yield ([sys.executable, str(REPO / "Tools" / "MeasureNoteEvenness.py"),
                        str(RENDERER), str(out), "--capture", capture,
                        "--technique", "finger", "--last", "84",
                        "--body-shape", shape, "--body-material", wood,
                        "--room", "0.5", "--piezo-mix", mix],
                       STUDY / "sweeps" / f"{shape}-{wood}" / f"{name}.log")


def phrase_jobs():
    work = STUDY / "phrases"
    work.mkdir(parents=True, exist_ok=True)
    scores = {"strums": work / "strums.events", "calibration": work / "calibration.events"}
    if not scores["strums"].exists():
        meter.write_performance(*meter.strums(), scores["strums"])
    if not scores["calibration"].exists():
        meter.write_performance(*calib.phrase_set(), scores["calibration"])
    for shape in SHAPES:
        for wood in WOODS:
            for phrase, score in scores.items():
                for name, (capture, mix) in VARIANTS.items():
                    out = work / f"{shape}-{wood}-{phrase}-{name}.f32"
                    if out.exists():
                        continue
                    yield ([str(RENDERER), str(score), str(out), capture, "finger",
                            "--body-shape", shape, "--body-material", wood,
                            "--room", "0.5", "--piezo-mix", mix], None)


def pick_jobs():
    work = STUDY / "picks"
    work.mkdir(parents=True, exist_ok=True)
    for string, midi in PICK_NOTES:
        score = work / f"pick-{midi}.events"
        score.write_text(
            f"ACUSTRA_PERFORMANCE_V1 48000 96000\n0 {string} {midi} 120 0\n"
            f"57600 {string} {midi} 0 0\n")
        for shape in SHAPES:
            for wood in WOODS:
                for name, (capture, mix) in VARIANTS.items():
                    out = work / f"{shape}-{wood}-{midi}-{name}.f32"
                    if out.exists():
                        continue
                    yield ([str(RENDERER), str(score), str(out), capture, "pick",
                            "--body-shape", shape, "--body-material", wood,
                            "--room", "0.5", "--piezo-mix", mix], None)


def main():
    jobs = list(sweep_jobs()) + list(phrase_jobs()) + list(pick_jobs())
    for command, log in jobs:
        if log is not None:
            Path(log).parent.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(lambda job: run(*job), jobs))
    print(f"{len(jobs)} jobs done")


if __name__ == "__main__":
    main()
