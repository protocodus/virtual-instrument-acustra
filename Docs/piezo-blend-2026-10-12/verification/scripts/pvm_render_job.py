#!/usr/bin/env python3
"""Renders for the methodology check of the piezo-polarity study.
Frozen renderer: piezo-study/bin (sha256 d31f7bb1..., = build-dsp at 8ea51f9).
"""
import subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path("/home/user/virtual-instrument-acustra")
SP = Path("/tmp/claude-0/-home-user/4688934d-1b89-5f41-91ba-35a404398aec/scratchpad")
OUT = SP / "piezo-verify-methodology"
RENDERER = SP / "piezo-study" / "bin" / "AcustraPerformanceRenderer"
sys.path.insert(0, str(REPO / "Tools"))
import MeasureNoteEvenness as ev  # noqa
import MeasureMaterialLoudness as meter  # noqa

SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
# condition -> (capture, room)
CONDS = {"mic_r0": ("stereo_mic", "0"), "mic_r1": ("stereo_mic", "1"),
         "mono_r05": ("mono_mic", "0.5"), "mono_r0": ("mono_mic", "0")}
OPEN = ev.OPEN_NOTES


def note_score(path, string, midi, rate=48000, vel=96):
    path.write_text(f"ACUSTRA_PERFORMANCE_V1 {rate} {round(2.0*rate)}\n"
                    f"0 {string} {midi} {vel} 0\n{round(1.2*rate)} {string} {midi} 0 0\n")


def chord_score(path, notes, rate=48000, vel=90, spread=0.008):
    lines = [f"ACUSTRA_PERFORMANCE_V1 {rate} {round(2.0*rate)}"]
    for rank, (string, midi) in enumerate(notes):
        lines.append(f"{round(rank*spread*rate)} {string} {midi} {vel} 0")
    ev_off = [f"{round(1.6*rate)} {s} {m} 0 0" for s, m in notes]
    path.write_text("\n".join(lines + ev_off) + "\n")


def eshape(f):  # E-shape barre, root on string 1 at fret f
    return [(1, 40+f), (2, 47+f), (3, 52+f), (4, 56+f), (5, 59+f), (6, 64+f)]


def ashape(f):  # A-shape barre, root on string 2 at fret f
    return [(2, 45+f), (3, 52+f), (4, 57+f), (5, 61+f), (6, 64+f)]


def jobs():
    sc = OUT / "scores"; sc.mkdir(parents=True, exist_ok=True)
    for shape in SHAPES:
        for wood in WOODS:
            cons = f"{shape}-{wood}"
            base = ["--body-shape", shape, "--body-material", wood]
            # E2-C6 sweeps (lowest-fret string) and per-string sweeps
            for cond, (cap, room) in CONDS.items():
                for midi in range(40, 85):
                    s = ev.lowest_fret_string(midi)
                    p = sc / f"note-s{s}-{midi}.events"
                    if not p.exists(): note_score(p, s, midi)
                    o = OUT / "sweeps" / cons / cond / f"note-{midi}.f32"
                    yield p, o, [cap, "finger", *base, "--room", room, "--piezo-mix", "0"]
                if cond == "mono_r0":
                    continue
                for s, op in enumerate(OPEN, start=1):
                    for fret in range(21):
                        midi = op + fret
                        p = sc / f"note-s{s}-{midi}.events"
                        if not p.exists(): note_score(p, s, midi)
                        o = OUT / "strings" / cons / cond / f"s{s}-{midi}.f32"
                        yield p, o, [cap, "finger", *base, "--room", room, "--piezo-mix", "0"]
            # chord sweeps: E-shape frets 0..12, A-shape frets 0..12
            for kind, fn in (("E", eshape), ("A", ashape)):
                for f in range(0, 13):
                    p = sc / f"chord-{kind}{f}.events"
                    if not p.exists(): chord_score(p, fn(f))
                    for cond, (cap, room) in {"mic_r05": ("stereo_mic", "0.5"), "mic_r0": ("stereo_mic", "0"),
                                             "mic_r1": ("stereo_mic", "1"), "mono_r05": ("mono_mic", "0.5"),
                                             "piezo": ("piezo", "0.5")}.items():
                        o = OUT / "chords" / cons / cond / f"{kind}{f}.f32"
                        yield p, o, [cap, "finger", *base, "--room", room, "--piezo-mix", "0"]
            # strums for other rooms / mono
            p = SP / "piezo-study" / "phrases" / "strums.events"
            for cond, (cap, room) in CONDS.items():
                o = OUT / "phrases" / f"{cons}-strums-{cond}.f32"
                yield p, o, [cap, "finger", *base, "--room", room, "--piezo-mix", "0"]


DONE = 0


def run(job):
    p, o, opts = job
    if o.exists():
        return
    o.parent.mkdir(parents=True, exist_ok=True)
    tmp = o.with_name(o.name + ".part")
    if tmp.exists():
        tmp.unlink()
    r = subprocess.run([str(RENDERER), str(p), str(tmp), *opts], capture_output=True, text=True)
    if r.returncode != 0:
        print("FAIL", o, r.returncode, r.stderr[-300:], flush=True)
        return
    tmp.rename(o)
    global DONE
    DONE += 1
    if DONE % 500 == 0:
        print("progress", DONE, flush=True)


if __name__ == "__main__":
    order = {'sweeps': 0, 'phrases': 1, 'chords': 2, 'strings': 3}
    todo = sorted(jobs(), key=lambda j: order[j[1].relative_to(OUT).parts[0]])
    with ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(run, todo))
    print(len(todo), "jobs")
