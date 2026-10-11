#!/usr/bin/env python3
"""Freeze and audition Wood models, using the same score and capture settings.

Usage: python3 Tools/BuildWoodReview.py PRE_WOOD_SOURCE CANDIDATE_SOURCE NEW_OUTPUT
The reference should include the same hand/string model as the candidate.
Native before/after also includes each version's construction level calibration.
Retains native float WAVs, source, compiler and hashes; needs clang++ and NumPy.
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


def pcm(path, samples, gain):
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(48000)
        wav.writeframes(np.rint(samples * gain * 32767).astype("<i2").tobytes())


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    before, after, output = (Path(p).resolve() for p in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=False)
    renderer = Path(__file__).with_name("RenderMaterialGestureReview.cpp")
    shutil.copy2(renderer, output / renderer.name)
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG", "-fPIC"]
    manifest = {"protocol": "wood-materials-20261011", "sample_rate": 48000,
                "frames": 8 * 48000, "block_frames": 64, "flags": flags,
                "compiler": subprocess.check_output(["clang++", "--version"], text=True),
                "renderer_sha256": sha(output / renderer.name),
                "builder_sha256": sha(output / Path(__file__).name),
                "score": "Dreadnought, Finger, MIDI velocity92; E2, G3, E4, then E-major arpeggio. Fresh Performer per render; dry source defaults, PiezoMix0.",
                "comparison": "Wood model plus each source's construction level calibration. RMS matching removes one whole-clip level factor; this is not a mechanism ablation.",
                "sources": {}, "renders": []}
    for label, source in (("before", before), ("after", after)):
        frozen = output / f"{label}-source"
        shutil.copytree(source / "Source/DSP", frozen / "Source/DSP",
                        ignore=shutil.ignore_patterns("SampleBank"))
        manifest["sources"][label] = {
            str(p.relative_to(frozen)): sha(p) for p in sorted(frozen.rglob("*")) if p.is_file()}
        executable = output / f"{label}-renderer"
        subprocess.run(["clang++", *flags, "-I" + str(frozen / "Source"),
                        str(output / renderer.name), str(frozen / "Source/DSP/AcustraEngine.cpp"),
                        str(frozen / "Source/DSP/AcustraPerformer.cpp"), "-o", str(executable)], check=True)
        manifest.setdefault("executable_sha256", {})[label] = sha(executable)
        subprocess.run([str(executable), str(output / f"{label}-raw"), "--woods"], check=True)

    sections = []
    for capture, title in (("stereo", "Stereo microphones"), ("mono", "Mono microphone"),
                           ("piezo", "Under-saddle piezo")):
        records = []
        for wood in ("spruce", "mahogany", "maple"):
            for version in ("before", "after"):
                raw = output / f"{version}-raw" / f"{capture}-{wood}.wav"
                samples = np.frombuffer(raw.read_bytes()[44:], dtype="<f4").astype(np.float64)
                if samples.size != 8 * 48000 * 2 or not np.isfinite(samples).all():
                    raise ValueError(f"Invalid audio: {raw}")
                peak = float(np.max(np.abs(samples)))
                rms = float(np.sqrt(np.mean(samples * samples)))
                if rms < 1e-7 or peak >= .89125094:
                    raise ValueError(f"Silent or safety-limited review render: {raw}")
                records.append((wood, version, raw, samples, peak, rms))
        target = min(r[-1] for r in records)
        rows = []
        for wood in ("spruce", "mahogany", "maple"):
            players = []
            for item in (r for r in records if r[0] == wood):
                _, version, raw, samples, peak, rms = item
                stem = f"{capture}-{wood}-{version}"
                gains = {"native": 1.0, "matched": target / rms}
                files = {}
                for level, gain in gains.items():
                    path = output / f"{stem}-{level}.wav"
                    pcm(path, samples, gain)
                    files[level] = {"file": path.name, "gain": gain, "sha256": sha(path)}
                manifest["renders"].append({"wood": wood, "version": version,
                    "capture": capture, "native_peak": peak, "native_rms": rms,
                    "raw_sha256": sha(raw), "pcm": files})
                label = "Previous model" if version == "before" else "Candidate"
                players.append(f'<label>{label}<audio controls preload="metadata" '
                    f'data-native="{files["native"]["file"]}" data-matched="{files["matched"]["file"]}" '
                    f'src="{files["matched"]["file"]}"></audio></label>')
            rows.append(f'<article><h3>{wood.title()}</h3>{"".join(players)}</article>')
        sections.append(f'<section><h2>{title}</h2>{"".join(rows)}</section>')
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "index.html").write_text('''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Acustra · Wood</title>
<style>body{max-width:920px;margin:48px auto;padding:0 24px;background:#181a19;color:#eee9df;
font:17px/1.6 system-ui}h1{font-size:40px;line-height:1.15}h2{font-size:26px}h3{font-size:20px}
p{color:#c6c4bb}section{margin-top:30px;border-top:1px solid #555}article{padding:4px 0 14px}
label{display:inline-flex;flex-direction:column;gap:6px;margin-right:25px}audio{max-width:100%}
select{font:inherit;padding:8px}a{color:#d7c295}</style>
<h1>Acustra · Wood</h1><p>Same performance, Dreadnought shape, playing force and capture.
Compare the three woods within a column, or the previous and new model within a row.
The score plays low E, G, high E, then an E-major arpeggio.</p>
<p>Wood now changes material mass, directional stiffness, coupled air/top resonances and bridge response.
Spruce is the physical reference. This is an effective plate substitution at fixed thickness;
it is not a measurement of three complete guitars.</p>
<label for="level">Playback level<select id="level"><option value="matched">Match RMS within each capture</option>
<option value="native">Native instrument levels</option></select></label>
<p>RMS matching applies one constant attenuation to each full clip. Native before/after also includes
the updated construction level calibration. Matching removes one whole-clip level factor;
this is not an isolated mechanism ablation. No EQ, compression or alignment. These comparisons establish differences,
not a listening preference. Stop one player before starting another.</p>''' + ''.join(sections) + '''
<p><a href="manifest.json">Source, renderer, raw audio hashes and exact gains</a>.</p>
<script>const players=[...document.querySelectorAll('audio')];
players.forEach(p=>p.addEventListener('play',()=>players.forEach(q=>{if(q!==p)q.pause()})));
document.getElementById('level').addEventListener('change',e=>players.forEach(p=>{
const t=p.currentTime;p.pause();p.src=p.dataset[e.target.value];
p.addEventListener('loadedmetadata',()=>{p.currentTime=Math.min(t,p.duration)},{once:true});}));</script></html>''')
    print(f"Wrote {len(manifest['renders'])} frozen-source Wood comparisons to {output}")


if __name__ == "__main__":
    main()
