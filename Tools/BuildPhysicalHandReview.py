#!/usr/bin/env python3
"""Freeze two DSP trees and render identical physical-hand scores for review.

Usage: python3 Tools/BuildPhysicalHandReview.py BASELINE_SOURCE CANDIDATE_SOURCE NEW_OUTPUT
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
    renderer = Path(__file__).with_name("RenderPhysicalHandReview.cpp")
    shutil.copy2(renderer, output / renderer.name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG", "-fPIC"]
    manifest = {"protocol": "physical-hand-20261011", "sample_rate": 48000,
                "duration_seconds": 8, "flags": flags,
                "compiler": subprocess.check_output(["clang++", "--version"], text=True),
                "renderer_sha256": sha(renderer), "sources": {}, "pairs": []}
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
        subprocess.run([str(output / f"{label}-renderer"), str(output / f"{label}-raw")], check=True)

    titles = ["Curved hand sweep", "Contact position and force", "Pre-bent steel string",
              "Thumb contact roughness", "Picking-hand damping and release"]
    descriptions = ["Listen to the spacing of notes inside repeated strums.",
                    "Listen to repeated strokes: contact position now changes deflection as well as colour.",
                    "Repeated 0, +2 and +4 semitone pre-bends at the same MIDI velocity.",
                    "Four velocity layers, preserving the existing Thumb periodic release.",
                    "Short picked notes and slides; Release Noise is explicitly set to 50% in this example."]
    rows = []
    for index, before_path in enumerate(sorted((output / "before-raw").glob("*.wav"))):
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
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Acustra · Physical hand</title>
<style>body{max-width:860px;margin:48px auto;padding:0 24px;background:#181a19;color:#eee9df;
font:17px/1.6 system-ui}h1{font-size:40px;line-height:1.15}h2{font-size:22px}p{color:#c6c4bb}
section{padding:22px 0;border-top:1px solid #444}label{display:inline-flex;flex-direction:column;
gap:8px;margin:0 20px 16px 0}audio{max-width:100%}a{color:#d7c295}</style>
<h1>Acustra · The physical hand</h1><p>Five before/after studies. Same notes, velocities, controls,
sample rate and renderer. Each pair uses one common gain; no EQ, independent normalization,
compression or timing alignment. Dry microphone capture. These are development comparisons,
not evidence of a listening preference. Each study emphasizes one mechanism; each pair compares the complete baseline and candidate.</p>''' + ''.join(rows) + '''
<p><a href="manifest.json">Source hashes and audio measurements</a>. Raw float WAVs and frozen
DSP source accompany this page. Stop one player before starting the other.</p></html>''')
    print(json.dumps(manifest["pairs"], indent=2))


if __name__ == "__main__":
    main()
