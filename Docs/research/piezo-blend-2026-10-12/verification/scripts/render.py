#!/usr/bin/env python3
"""Independent renders for the piezo-mix reproduction.

render.py OUTROOT SET [constructions...]
SET: sweep (E2-C6 lowest-fret, Finger v96), sweep_v64, sweep_room0, strings (6 x frets 0-20 Finger v96)
Each note is rendered three ways: mic (Piezo Mix 0), piezo (Capture Piezo), default (Piezo Mix 0.2).
"""
import subprocess, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, "/home/user/virtual-instrument-acustra/Tools")
import MeasureNoteEvenness as mne  # noqa: E402

RENDERER = "/home/user/virtual-instrument-acustra/build-dsp/AcustraPerformanceRenderer"
SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
ALL = [f"{s}-{w}" for s in SHAPES for w in WOODS]

def score_text(string, midi, velocity):
    rate = mne.RATE
    return (f"ACUSTRA_PERFORMANCE_V1 {rate} {round(mne.NOTE_SECONDS * rate)}\n"
            f"0 {string} {midi} {velocity} 0\n"
            f"{round(mne.HOLD_SECONDS * rate)} {string} {midi} 0 0\n")

def notes_for(set_name):
    if set_name.startswith("sweep"):
        return [(mne.lowest_fret_string(m), m) for m in range(40, 85)]
    if set_name == "strings":
        return [(s + 1, mne.OPEN_NOTES[s] + f) for s in range(6) for f in range(21)]
    raise ValueError(set_name)

def settings(set_name):
    velocity, room, technique = 96, "0.5", "finger"
    if set_name == "sweep_v64":
        velocity = 64
    if set_name == "sweep_room0":
        room = "0"
    if set_name == "sweep_pick":
        technique = "pick"
    if set_name == "sweep_v120":
        velocity = 120
    return velocity, room, technique

def jobs(root, set_name, constructions):
    velocity, room, technique = settings(set_name)
    out = []
    for c in constructions:
        shape, wood = c.split("-")
        common = ["--body-shape", shape, "--body-material", wood, "--room", room]
        variants = {"mic": ["stereo_mic", technique, *common, "--piezo-mix", "0"],
                    "piezo": ["piezo", technique, *common],
                    "default": ["stereo_mic", technique, *common, "--piezo-mix", "0.2"]}
        for vname, opts in variants.items():
            d = root / set_name / c / vname
            d.mkdir(parents=True, exist_ok=True)
            for string, midi in notes_for(set_name):
                ev = d / f"s{string}-n{midi}.events"
                ev.write_text(score_text(string, midi, velocity))
                raw = d / f"s{string}-n{midi}.f32"
                if not raw.exists():
                    out.append([RENDERER, str(ev), str(raw), *opts])
    return out

def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"{cmd}: {r.stderr}")

if __name__ == "__main__":
    root = Path(sys.argv[1]); set_name = sys.argv[2]
    cons = sys.argv[3:] or ALL
    todo = jobs(root, set_name, cons)
    print(f"{len(todo)} renders", flush=True)
    with ThreadPoolExecutor(4) as pool:
        list(pool.map(run, todo))
    print("done", flush=True)
