#!/usr/bin/env python3
"""Freeze two DSP trees and render identical fret-motion scores for review.

Usage: python3 Tools/BuildFretMotionReview.py BASELINE_SOURCE CANDIDATE_SOURCE NEW_OUTPUT
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
    renderer = Path(__file__).with_name("RenderFretMotionReview.cpp")
    shutil.copy2(renderer, output / renderer.name)
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG", "-fPIC"]
    manifest = {"protocol": "fret-motion-20261011", "sample_rate": 48000,
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

    titles = ["Wound A: slow slides", "Wound G: brief slides",
              "Plain high E: unchanged control", "Ordinary notes: unchanged control",
              "Release Noise zero: unchanged control", "MPE member bend: unchanged control"]
    descriptions = [
        "A held seventh-fret A string slides gently up, back, to the nut and away. Release Noise is explicitly 70% for this gesture comparison (the former plug-in/Rack default). The brush is intentionally faint.",
        "Shorter slides on a held wound G string. Speed changes the texture continuously; no per-note noise layers are selected.",
        "The slow trajectory on plain high E. No new wound-string roughness is added.",
        "Normal stationary fretted notes. The existing release contacts are preserved; no extra random fret noise is added.",
        "The same wound-A slides with Release Noise at zero. The new source is bypassed.",
        "The same controller trajectory as an MPE member tension bend. The fingertip remains at its fret, so this source stays quiet."]
    rows = []
    before_paths = sorted((output / "before-raw").glob("*.wav"))
    after_paths = sorted((output / "after-raw").glob("*.wav"))
    if len(before_paths) != len(titles) or [p.name for p in before_paths] != [p.name for p in after_paths]:
        raise ValueError("expected the same six listening scores in both renders")
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
        if (index < 2 and difference == 0) or (index >= 2 and difference != 0):
            raise ValueError("expected motion witness or exact unchanged control")
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
            "difference_rms": difference,
            "difference_relative_db": float(20*np.log10(difference/rms[0])) if difference else None,
            "bit_identical": difference == 0})
        rows.append(f'<section><h2>{index+1}. {titles[index]}</h2><p>{descriptions[index]}</p>'
                    + ''.join(f'<label>{label}<audio controls preload="metadata" src="{file}"></audio></label>'
                              for label, file in zip(("Before", "After"), files)) + '</section>')
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "index.html").write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Acustra · Subtle fret motion</title>
<style>body{max-width:860px;margin:48px auto;padding:0 24px;background:#181a19;color:#eee9df;
font:17px/1.6 system-ui}h1{font-size:40px;line-height:1.15}h2{font-size:22px}p{color:#c6c4bb}
section{padding:22px 0;border-top:1px solid #444}label{display:inline-flex;flex-direction:column;
gap:8px;margin:0 20px 16px 0}audio{max-width:100%}a{color:#d7c295}</style>
<h1>Acustra · Subtle fret motion</h1><p>A faint brush during actual stopped-string motion.
The source follows continuous finger speed, contact near the nut and the existing Release Noise
control. Ordinary notes and idle hand movement get no extra noise.</p>
<p>Two before/after slide studies and four exact unchanged controls. Identical scores,
48 kHz, 64-frame blocks, dry stereo microphones, native gain. No EQ, separate normalization,
compression, timing alignment or amplified difference track. These comparisons do not establish
listener preference. This is a reduced transverse contact approximation, not a full longitudinal
string squeak model.</p>''' + ''.join(rows) + '''
<p><a href="manifest.json">Source hashes and audio measurements</a>. Raw float WAVs and frozen
DSP source accompany this page. Stop one player before starting the other.</p></html>''')
    print(json.dumps(manifest["pairs"], indent=2))


if __name__ == "__main__":
    main()
