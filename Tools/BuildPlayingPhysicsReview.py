#!/usr/bin/env python3
"""Freeze two DSP trees and build the playing-physics review: blind A/B pairs
with a separate key.

Usage: python3 Tools/BuildPlayingPhysicsReview.py BASELINE_SOURCE CANDIDATE_SOURCE NEW_OUTPUT

BASELINE_SOURCE and CANDIDATE_SOURCE are repository checkouts. Both get the
same renderer (Tools/RenderPlayingPhysicsReview.cpp); the candidate's build
also switches single mechanisms off through PerformanceRealism. Requires
clang++ and NumPy. Nothing is fetched, installed or modified in either tree.

The output keeps the frozen sources, both renderers, raw native-level float
WAVs, a plain listening page with 16-bit copies at one gain shared by every
file, manifest.json (hashes and measurements) and key.json (which of A and B
is the candidate). The candidate sits on A in half the pairs, drawn from a
generator seeded by the candidate renderer's hash. Checks that fail stop the
build:
  * every score renders bit for bit the baseline's fall-off render with
    every mechanism off and the fall let go (the release fall's measured
    curve replaced the authored one, so it has no switch of its own);
  * the open-string control renders bit for bit the baseline;
  * each mechanism's own score differs when only that mechanism is off, and
    the staccato score differs from the baseline with the measured fall alone.
"""
import hashlib
import json
import random
import shutil
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np

RATE = 48000
BASELINE_VARIANTS = ["baseline", "fall-off"]
CANDIDATE_VARIANTS = ["candidate", "all-off", "only-fall", "no-fingerChords", "no-mutedStrings",
                      "no-strumScatter", "no-twoStringStrokes", "no-strokeSpanLaw", "no-strokeEdges"]
# (score, (build, variant) heard as the candidate, (build, variant) against it,
#  title, description)
PAIRS = [
    ("01-finger-comping", ("candidate", "candidate"), ("candidate", "no-fingerChords"),
     "Fingerstyle comping",
     "The thumb's bass on the beats and three- and four-string chords between, with the "
     "fingers. One version sweeps every chord across its strings as a pick would; in the "
     "other the thumb and fingers pluck each chord together, a few milliseconds apart in "
     "no fixed order, as GuitarSet's bossa nova players do."),
    ("02-gapped-strums", ("candidate", "candidate"), ("candidate", "no-mutedStrings"),
     "Open-voiced strums",
     "Down-up strums with a pick of chords that leave strings out between the ones they "
     "sound (E2, B3 and E4, say). One version lets the pick pass through those strings; "
     "in the other it gives each a dead stroke under the fretting hand that keeps it quiet."),
    ("03-staccato-fingerstyle", ("candidate", "only-fall"), ("baseline", "baseline"),
     "Staccato fingerstyle",
     "Fretted notes and chord stabs let go into rests, with the fingers. Both versions let "
     "a released note's pitch fall as the lifting fingertip becomes the string's end: one "
     "along the 1.4.0 curve, the other along the curve GuitarSet's note ends measure, "
     "which holds the pitch longer and falls later."),
    ("04-repeated-strums", ("candidate", "candidate"), ("candidate", "no-strumScatter"),
     "One chord, strummed over and over",
     "E major and then A major strummed down and up at one velocity, as a sequencer sends "
     "it. One version sweeps every stroke's strings exactly along the wrist's arc; in the "
     "other each string sounds a few milliseconds off it, as a real pick meets them."),
    ("05-power-chords", ("candidate", "candidate"), ("candidate", "no-twoStringStrokes"),
     "Power chords",
     "A riff of two-note power chords with a pick. One version sounds both strings of each "
     "chord on the same instant; in the other the pick reaches one string just before the "
     "other, down and up."),
    ("06-small-strokes", ("candidate", "candidate"), ("candidate", "no-strokeSpanLaw"),
     "Small strokes",
     "Three- and four-string chords on the top strings strummed down and up with a pick. "
     "One version crosses them at a full six-string stroke's speed; the other crosses a "
     "small stroke's strings more slowly, as GuitarSet's strummers do."),
    ("07-strum-eighths", ("candidate", "candidate"), ("candidate", "no-strokeEdges"),
     "Down-up strumming",
     "Eighth-note strumming of G, C, D and E minor with a pick. One version tilts each "
     "upstroke toward the treble strings as 1.4.0 does; the other strikes the first and "
     "last strings of every stroke a little more lightly and tilts the upstroke less, as "
     "measured inside single strokes."),
    ("08-song-pick", ("candidate", "candidate"), ("baseline", "baseline"),
     "A picked song, all together",
     "Bass notes, down-up strums of full and open chords, a power-chord fill and a "
     "stopped ending, with a pick. The 1.4.0 engine against the candidate with every change."),
    ("09-song-finger", ("candidate", "candidate"), ("baseline", "baseline"),
     "A fingerstyle song, all together",
     "Bass notes, plucked chords, a melody over them and a final chord, with the fingers. "
     "The 1.4.0 engine against the candidate with every change."),
    ("10-control-open-strings", ("candidate", "candidate"), ("baseline", "baseline"),
     "Unchanged control",
     "Open strings plucked one at a time with the fingers: no chord, no plectrum, no fret "
     "to fall from. Both files are bit-identical by design."),
]
SCORES = sorted({score for score, *_ in PAIRS})


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_float_wave(path):
    data = Path(path).read_bytes()
    if data[:4] != b"RIFF" or data[36:40] != b"data":
        raise ValueError(f"unexpected WAV layout: {path}")
    audio = np.frombuffer(data[44:], dtype="<f4").astype(np.float64)
    if audio.size != 8 * RATE * 2 or not np.isfinite(audio).all():
        raise ValueError(f"wrong length or nonfinite render: {path}")
    return audio.reshape(-1, 2)


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    baseline, candidate, output = (Path(p).resolve() for p in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=False)
    renderer = Path(__file__).with_name("RenderPlayingPhysicsReview.cpp")
    shutil.copy2(renderer, output / renderer.name)
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG"]
    manifest = {
        "protocol": "playing-physics-20261014", "sample_rate": RATE,
        "duration_seconds": 8, "block_frames": 64, "flags": flags,
        "controls": "plug-in defaults: Output 0 dB, Room 50 %, Piezo Mix 20 %, "
                    "Release Noise 10 %, stereo microphones, Dreadnought Spruce",
        "compiler": subprocess.check_output(["clang++", "--version"], text=True),
        "renderer_sha256": sha(output / renderer.name),
        "builder_sha256": sha(output / Path(__file__).name),
        "sources": {}, "executable_sha256": {}, "renders": {}, "pairs": []}
    builds = {"baseline": (baseline, []),
              "candidate": (candidate, ["-DACUSTRA_REVIEW_CANDIDATE=1"])}
    for label, (source, defines) in builds.items():
        frozen = output / f"{label}-source"
        shutil.copytree(source / "Source/DSP", frozen / "Source/DSP",
                        ignore=shutil.ignore_patterns("SampleBank"))
        manifest["sources"][label] = {
            str(p.relative_to(frozen)): sha(p) for p in sorted(frozen.rglob("*")) if p.is_file()}
        executable = output / f"{label}-renderer"
        subprocess.run(["clang++", *flags, *defines, "-I" + str(frozen / "Source"),
                        str(output / renderer.name),
                        str(frozen / "Source/DSP/AcustraEngine.cpp"),
                        str(frozen / "Source/DSP/AcustraPerformer.cpp"),
                        "-o", str(executable)], check=True)
        manifest["executable_sha256"][label] = sha(executable)
    raw = output / "raw"
    renders = [("baseline", v) for v in BASELINE_VARIANTS] + [("candidate", v) for v in CANDIDATE_VARIANTS]
    for build, variant in renders:
        subprocess.run([str(output / f"{build}-renderer"), str(raw / build / variant), variant],
                       check=True)

    audio = {}
    for build, variant in renders:
        for score in SCORES:
            path = raw / build / variant / f"{score}.wav"
            audio[(build, variant, score)] = read_float_wave(path)
            manifest["renders"][f"{build}/{variant}/{score}"] = sha(path)

    for score in SCORES:
        if not np.array_equal(audio[("candidate", "all-off", score)],
                              audio[("baseline", "fall-off", score)]):
            raise ValueError(f"every mechanism off does not reproduce the baseline: {score}")
    manifest["all_off_reproduces_baseline_fall_off"] = True
    if not np.array_equal(audio[("candidate", "candidate", "10-control-open-strings")],
                          audio[("baseline", "baseline", "10-control-open-strings")]):
        raise ValueError("the open-string control changed")
    reach = {score: {variant: not np.array_equal(audio[("candidate", variant, score)],
                                                 audio[("candidate", "candidate", score)])
                     for variant in CANDIDATE_VARIANTS if variant.startswith("no-")}
             for score in SCORES}
    manifest["mechanism_reaches_score"] = reach
    for score, _, (build, variant), *_ in PAIRS:
        if variant.startswith("no-") and not reach[score][variant]:
            raise ValueError(f"{variant} does not change {score}")
    if np.array_equal(audio[("candidate", "only-fall", "03-staccato-fingerstyle")],
                      audio[("baseline", "baseline", "03-staccato-fingerstyle")]):
        raise ValueError("the measured fall does not change the staccato score")

    peak = max(float(np.max(np.abs(x))) for x in audio.values())
    gain = min(1.0, 10.0 ** (-1.0 / 20.0) / peak)
    manifest["shared_pcm_gain"] = gain
    review = output / "review"
    review.mkdir()
    chooser = random.Random(manifest["executable_sha256"]["candidate"])
    slots = ["A", "B"] * ((len(PAIRS) + 1) // 2)
    chooser.shuffle(slots)
    key, sections = {}, []
    for index, (score, cand, other, title, description) in enumerate(PAIRS, start=1):
        after, before = audio[(*cand, score)], audio[(*other, score)]
        candidate_is = slots[index - 1]
        letters = {"A": after if candidate_is == "A" else before,
                   "B": before if candidate_is == "A" else after}
        files = {}
        for letter, signal in letters.items():
            name = f"{index:02d}-{letter}.wav"
            with wave.open(str(review / name), "wb") as stream:
                stream.setnchannels(2); stream.setsampwidth(2); stream.setframerate(RATE)
                stream.writeframes(np.rint(np.clip(signal * gain, -1.0, 1.0) * 32767)
                                   .astype("<i2").tobytes())
            files[letter] = name
        key[str(index)] = {"title": title, "candidate": candidate_is,
                           "candidate_render": "/".join(cand), "other_render": "/".join(other),
                           "other_is": "A" if candidate_is == "B" else "B"}
        difference = after - before
        rms = [float(np.sqrt(np.mean(x * x))) for x in (before, after)]
        changed = np.nonzero(np.any(difference != 0.0, axis=1))[0]
        manifest["pairs"].append({
            "index": index, "title": title, "score": score,
            "candidate_render": "/".join(cand), "compared_with": "/".join(other),
            "files": files, "pcm_sha256": {k: sha(review / v) for k, v in files.items()},
            "native_peak": [float(np.max(np.abs(x))) for x in (before, after)],
            "native_rms": rms, "rms_change_db": float(20.0 * np.log10(rms[1] / rms[0])),
            "difference_rms_relative_db": (float(20.0 * np.log10(np.sqrt(np.mean(difference ** 2)) / rms[0]))
                                           if changed.size else None),
            "first_difference_seconds": float(changed[0] / RATE) if changed.size else None,
            "bit_identical": changed.size == 0})
        sections.append(
            f'<section><h2>{index}. {title}</h2><p>{description}</p>'
            + "".join(f'<label>{letter}<audio controls preload="metadata" src="{files[letter]}">'
                      f'</audio></label>' for letter in ("A", "B")) + "</section>")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "key.json").write_text(json.dumps(key, indent=2) + "\n")
    (review / "index.html").write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Acustra · Playing physics</title><style>'
        'body{max-width:860px;margin:48px auto;padding:0 16px;background:#181a19;color:#eee9df;'
        'font:17px/1.6 system-ui}h1{font-size:36px;line-height:1.15}h2{font-size:22px}'
        'p{color:#c6c4bb}section{padding:22px 0;border-top:1px solid #444}'
        'label{display:inline-flex;flex-direction:column;gap:8px;margin:0 20px 16px 0}'
        'audio{max-width:100%}</style>'
        '<h1>Acustra · Playing physics</h1>'
        '<p>Blind pairs: in each, one file is the candidate and one is not; which is which '
        'is in key.json, kept apart from this page. Identical scores at the plug-in\'s '
        'default controls, 48 kHz, 64-frame blocks, one shared gain for every file. No EQ, '
        'separate normalization, compression or timing alignment.</p>'
        + "".join(sections) + "</html>\n")
    print(json.dumps({"reach": reach, "pairs": manifest["pairs"]}, indent=2))


if __name__ == "__main__":
    main()
