#!/usr/bin/env python3
"""Holdout: the E2-C6 sweep with the Pick (MeasureNoteEvenness, --technique pick,
velocity 96, Room 0.5), microphones (Piezo Mix 0) and piezo, all 12 constructions.
Renders pick-sweeps/<shape>-<wood>/<mic|piezo>/note-<midi>.f32 and logs."""
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path("/home/user/virtual-instrument-acustra")
STUDY = Path(__file__).resolve().parent.parent
RENDERER = STUDY / "bin" / "AcustraPerformanceRenderer"
SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")


def job(args):
    shape, wood, name, capture = args
    out = STUDY / "pick-sweeps" / f"{shape}-{wood}" / name
    result = subprocess.run([sys.executable, str(REPO / "Tools" / "MeasureNoteEvenness.py"),
                             str(RENDERER), str(out), "--capture", capture, "--technique", "pick",
                             "--last", "84", "--body-shape", shape, "--body-material", wood,
                             "--room", "0.5", "--piezo-mix", "0"],
                            check=True, capture_output=True, text=True)
    (out.parent / f"{name}.log").write_text(result.stdout)


jobs = [(s, w, n, c) for s in SHAPES for w in WOODS for n, c in (("mic", "stereo_mic"), ("piezo", "piezo"))]
with ThreadPoolExecutor(max_workers=4) as pool:
    list(pool.map(job, jobs))
print(len(jobs), "sweeps")
