#!/usr/bin/env python3
"""Freeze two DSP trees and render identical material/gesture scores for review.

Usage: python3 Tools/BuildMaterialGestureReview.py BASELINE_SOURCE CANDIDATE_SOURCE NEW_OUTPUT
Requires clang++ and NumPy. Does not fetch, install, or modify either source tree.
The source snapshots, renderer, raw native-level audio and hashes stay in OUTPUT.
"""
import hashlib
import json
import shutil
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    baseline, candidate, output = (Path(p).resolve() for p in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=False)
    renderer = Path(__file__).with_name("RenderMaterialGestureReview.cpp")
    shutil.copy2(renderer, output / renderer.name)
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG", "-fPIC"]
    manifest = {"protocol": "material-gestures-20261011", "sample_rate": 48000,
                "duration_seconds": 8, "block_frames": 64, "flags": flags,
                "compiler": subprocess.check_output(["clang++", "--version"], text=True),
                "renderer_sha256": sha(output / renderer.name),
                "builder_sha256": sha(output / Path(__file__).name),
                "sources": {}, "executable_sha256": {}, "pairs": []}
    for label, source in (("before", baseline), ("after", candidate)):
        frozen = output / f"{label}-source"
        shutil.copytree(source / "Source/DSP", frozen / "Source/DSP",
                        ignore=shutil.ignore_patterns("SampleBank"))
        manifest["sources"][label] = {
            str(p.relative_to(frozen)): sha(p) for p in sorted(frozen.rglob("*")) if p.is_file()}
        command = ["clang++", *flags, "-I" + str(frozen / "Source"),
                   str(output / renderer.name),
                   str(frozen / "Source/DSP/AcustraEngine.cpp"),
                   str(frozen / "Source/DSP/AcustraPerformer.cpp"),
                   "-o", str(output / f"{label}-renderer")]
        subprocess.run(command, check=True)
        manifest["executable_sha256"][label] = sha(output / f"{label}-renderer")
        subprocess.run([str(output / f"{label}-renderer"), str(output / f"{label}-raw")], check=True)

    titles = ["Open and fretted: the same pitch", "Plain steel up the neck",
              "Wound strings, open and octave-fretted", "One hand across mixed frets",
              "Fretting-hand and picking-hand stops", "Phrases separated by rests",
              "Continuous release brush", "Slides to and from the nut", "Fretting vibrato and the nut"]
    descriptions = [
        "E4 on open high E, fifth-fret B and ninth-fret G. Identical input velocity; string construction, stopping length and contact give each its own tone.",
        "B and high E at frets 0, 5, 12 and 19. Listen for the changing upper partials as the actual steel wire gets shorter.",
        "Each wound string open, then at its octave. Its approved effective-core calibration remains; this checks hand and stopping behavior alongside the plain-string change.",
        "A thumb arpeggio across one mixed-fret chord. Shared hand movement now covers the same millimetres on every string.",
        "Short picked high-E notes, alternating nut and fret terminations. The damping contact uses continuous spatial geometry.",
        "Three repeated-note phrases. Hand posture follows continuous physical time through the rests, with independent small contact variation retained.",
        "The release velocity follows a smooth crescendo and diminuendo. Faster oblique pad slip produces a shorter, brighter wound-string brush. Release Noise 50%; this explicit-velocity case is available in MIDI-capable front ends, while Rack supplies a nominal release.",
        "The A string alternates an open-note slide up seven semitones and a seventh-fret note slid back to the nut. The current physical stopping point must determine the ending, rather than the original MIDI fret.",
        "The same physical slide comparison with Mod Wheel vibrato held at 110/127. Fretting-hand vibrato engages on the stopped string and withdraws as its stopping point reaches the nut."]
    rows = []
    before_paths = sorted((output / "before-raw").glob("*.wav"))
    after_paths = sorted((output / "after-raw").glob("*.wav"))
    if len(before_paths) != len(titles) or [p.name for p in before_paths] != [p.name for p in after_paths]:
        raise ValueError("expected the same nine listening scores in both renders")
    for index, before_path in enumerate(before_paths):
        after_path = output / "after-raw" / before_path.name
        arrays = [np.frombuffer(p.read_bytes()[44:], dtype="<f4").astype(np.float64)
                  for p in (before_path, after_path)]
        if any(x.size != 8 * 48000 * 2 or not np.isfinite(x).all() for x in arrays):
            raise ValueError("wrong length or nonfinite render")
        peaks = [float(np.max(np.abs(x))) for x in arrays]
        rms = [float(np.sqrt(np.mean(x*x))) for x in arrays]
        if min(rms) < 1e-7:
            raise ValueError("silent render")
        difference = float(np.sqrt(np.mean((arrays[1]-arrays[0])**2)))
        if difference == 0:
            raise ValueError("unchanged listening pair")
        # One common attenuation per pair, only if needed for PCM headroom.
        # Never independently normalize; the changed native dynamics remain.
        gain = min(1.0, 0.85/max(peaks))
        files = []
        for label, audio in zip(("before", "after"), arrays):
            path = output / f"{before_path.stem}-{label}.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setnchannels(2); wav.setsampwidth(2); wav.setframerate(48000)
                wav.writeframes(np.rint(audio * gain * 32767).astype("<i2").tobytes())
            files.append(path.name)
        manifest["pairs"].append({"title": titles[index], "files": files,
            "raw_sha256": [sha(before_path), sha(after_path)],
            "pcm_sha256": [sha(output / f) for f in files],
            "native_peak": peaks, "native_rms": rms, "common_gain": gain,
            "rms_change_db": float(20*np.log10(rms[1]/rms[0])),
            "difference_rms": difference})
        rows.append(f'<section><h2>{index+1}. {titles[index]}</h2><p>{descriptions[index]}</p>'
                    + ''.join(f'<label>{label}<audio controls preload="metadata" src="{file}"></audio></label>'
                              for label, file in zip(("Before", "After"), files)) + '</section>')
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "index.html").write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Acustra · Materials and gestures</title>
<style>body{max-width:860px;margin:48px auto;padding:0 24px;background:#181a19;color:#eee9df;
font:17px/1.6 system-ui}h1{font-size:40px;line-height:1.15}h2{font-size:22px}p{color:#c6c4bb}
section{padding:22px 0;border-top:1px solid #444}label{display:inline-flex;flex-direction:column;
gap:8px;margin:0 20px 16px 0}audio{max-width:100%}a{color:#d7c295}</style>
<h1>Acustra · Materials and gestures</h1><p>Nine before/after studies. Same notes, velocities, controls,
sample rate and renderer. Each pair uses one common gain; no EQ, independent normalization,
compression or timing alignment. Dry microphone capture. These are development comparisons,
not evidence of a listening preference. Each study emphasizes one mechanism; each pair compares the complete baseline and candidate.</p>
<p>Dynamics come from a continuous force and contact model. The seventh study samples a smooth
playing gesture at each note; MIDI 1 rounds its input to 127 nonzero velocities, without selecting
sample layers or a small set of timbres.</p>''' + ''.join(rows) + '''
<p><a href="manifest.json">Source hashes and audio measurements</a>. Raw float WAVs and frozen
DSP source accompany this page. Stop one player before starting the other.</p></html>''')
    print(json.dumps(manifest["pairs"], indent=2))


if __name__ == "__main__":
    main()
