#!/usr/bin/env python3
"""Freeze two DSP trees and build the stroke depth / alternate picking /
release fall review: blind A/B pairs with a separate key.

Usage: python3 Tools/BuildStrokeReleaseReview.py BASELINE_SOURCE CANDIDATE_SOURCE NEW_OUTPUT

BASELINE_SOURCE and CANDIDATE_SOURCE are repository checkouts. Both get the
same renderer (Tools/RenderStrokeReleaseReview.cpp); the candidate's build
also switches single mechanisms off through PerformanceRealism. Requires
clang++ and NumPy. Nothing is fetched, installed or modified in either tree.

The output keeps the frozen sources, both renderers, raw native-level float
WAVs, the listening page with 16-bit copies at one gain shared by every file,
manifest.json (hashes and measurements) and key.json (which of A and B is the
candidate). Checks that fail stop the build:
  * every score renders bit for bit the baseline with all three switches off;
  * the open-string control renders bit for bit the baseline with them on;
  * each mechanism's own score differs when only that mechanism is off.
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
VARIANTS = ["all-off", "no-depth", "no-alternate", "no-fall", "candidate"]
# (score, variant heard against the candidate, title, description)
PAIRS = [
    ("1-strum-eighths", "no-depth", "Down-up strumming",
     "Eighth-note strumming with a pick, G, C, D and E minor, the upstrokes "
     "a little softer as a player accents the beat. One version strikes every "
     "stroke evenly across the strings; the other lets each upstroke land "
     "lighter on the bass strings and harder on the treble ones, by the amount "
     "GuitarSet's hex-pickup comping measures."),
    ("2-picked-line", "no-alternate", "Picked line and tremolo",
     "Single notes with a pick in eighths and sixteenths, then a sixteenth-note "
     "tremolo on one note. One version picks every note downward; the other "
     "alternates down and up, as a plectrum does at this speed."),
    ("3-staccato-fingerstyle", "no-fall", "Staccato fingerstyle",
     "Fretted bass and treble notes played with the fingers and let go after "
     "half their length, then fretted chord stabs into rests. One version keeps "
     "each released note at its pitch while the hand damps it; in the other the "
     "lifting fingertip becomes the string's end and the dying note falls "
     "toward the fret below."),
    ("4-song", "baseline", "All three together",
     "Bass notes, down-up strums, a picked fill and a stopped ending, with a "
     "pick. The earlier engine against the candidate with all three changes."),
    ("5-control-open-strings", "baseline", "Unchanged control",
     "Open strings plucked one at a time with the fingers: no fret to fall "
     "from, no plectrum, no strum. Both files are bit-identical by design."),
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


def band_energy(mono, start, count, corner):
    """Energy below a one-pole corner and of the first difference."""
    segment = mono[start:start + count]
    pole = np.exp(-2.0 * np.pi * corner / RATE)
    low, state = np.zeros_like(segment), 0.0
    for n, x in enumerate(segment):
        state = (1.0 - pole) * x + pole * state
        low[n] = state
    return float(np.sum(low * low)), float(np.sum(np.diff(segment) ** 2))


def upstroke_bands(variant_audio, candidate_audio):
    """The strum score's upstrokes: low and high band change over 60 ms."""
    eighth = 30.0 / 104.0
    lows, highs = [], []
    for stroke in range(1, 26, 2):
        start = int(round((0.1 + stroke * eighth) * RATE))
        a = band_energy(variant_audio.mean(axis=1), start, int(0.06 * RATE), 250.0)
        b = band_energy(candidate_audio.mean(axis=1), start, int(0.06 * RATE), 250.0)
        lows.append(10.0 * np.log10(b[0] / a[0]))
        highs.append(10.0 * np.log10(b[1] / a[1]))
    return {"low_band_below_250_hz_db_median": float(np.median(lows)),
            "first_difference_band_db_median": float(np.median(highs)),
            "upstrokes": len(lows)}


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    baseline, candidate, output = (Path(p).resolve() for p in sys.argv[1:])
    output.mkdir(parents=True, exist_ok=False)
    renderer = Path(__file__).with_name("RenderStrokeReleaseReview.cpp")
    shutil.copy2(renderer, output / renderer.name)
    shutil.copy2(Path(__file__), output / Path(__file__).name)
    flags = ["-std=c++20", "-O3", "-DNDEBUG"]
    manifest = {
        "protocol": "stroke-release-20261013", "sample_rate": RATE,
        "duration_seconds": 8, "block_frames": 64, "flags": flags,
        "controls": "plug-in defaults: Output 0 dB, Room 50 %, Piezo Mix 20 %, "
                    "Release Noise 10 %, stereo microphones, Dreadnought Spruce",
        "compiler": subprocess.check_output(["clang++", "--version"], text=True),
        "renderer_sha256": sha(output / renderer.name),
        "builder_sha256": sha(output / Path(__file__).name),
        "sources": {}, "executable_sha256": {}, "renders": {}, "pairs": []}
    builds = {"baseline": (baseline, []),
              "candidate": (candidate, ["-DACUSTRA_REVIEW_SWITCHES=1"])}
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
    subprocess.run([str(output / "baseline-renderer"), str(raw / "baseline"), "baseline"], check=True)
    for variant in VARIANTS:
        subprocess.run([str(output / "candidate-renderer"), str(raw / variant), variant], check=True)

    audio = {}
    for variant in ["baseline", *VARIANTS]:
        for score in SCORES:
            path = raw / variant / f"{score}.wav"
            audio[(variant, score)] = read_float_wave(path)
            manifest["renders"][f"{variant}/{score}"] = sha(path)

    # Which mechanism reaches which score: a variant identical to the
    # candidate leaves that score alone.
    reach = {score: {variant: not np.array_equal(audio[(variant, score)], audio[("candidate", score)])
                     for variant in ["no-depth", "no-alternate", "no-fall"]} for score in SCORES}
    manifest["mechanism_reaches_score"] = reach
    for score in SCORES:
        if not np.array_equal(audio[("all-off", score)], audio[("baseline", score)]):
            raise ValueError(f"all switches off does not reproduce the baseline: {score}")
    manifest["all_off_reproduces_baseline"] = True
    if not np.array_equal(audio[("candidate", "5-control-open-strings")],
                          audio[("baseline", "5-control-open-strings")]):
        raise ValueError("the open-string control changed")
    for score, variant, *_ in PAIRS[:3]:
        if not reach[score][variant]:
            raise ValueError(f"{variant} does not change {score}")

    peak = max(float(np.max(np.abs(x))) for x in audio.values())
    gain = min(1.0, 10.0 ** (-1.0 / 20.0) / peak)
    manifest["shared_pcm_gain"] = gain
    review = output / "review"
    review.mkdir()
    chooser = random.Random(manifest["executable_sha256"]["candidate"])
    key, sections = {}, []
    for index, (score, variant, title, description) in enumerate(PAIRS, start=1):
        before, after = audio[(variant, score)], audio[("candidate", score)]
        candidate_is = chooser.choice(["A", "B"])
        letters = {"A": after if candidate_is == "A" else before,
                   "B": before if candidate_is == "A" else after}
        files = {}
        for letter, signal in letters.items():
            name = f"{index}-{letter}.wav"
            with wave.open(str(review / name), "wb") as stream:
                stream.setnchannels(2); stream.setsampwidth(2); stream.setframerate(RATE)
                stream.writeframes(np.rint(np.clip(signal * gain, -1.0, 1.0) * 32767)
                                   .astype("<i2").tobytes())
            files[letter] = name
        key[str(index)] = {"title": title, "candidate": candidate_is,
                           "other": variant, "other_is": "A" if candidate_is == "B" else "B"}
        difference = after - before
        rms = [float(np.sqrt(np.mean(x * x))) for x in (before, after)]
        changed = np.nonzero(np.any(difference != 0.0, axis=1))[0]
        entry = {
            "index": index, "title": title, "score": score, "compared_with": variant,
            "files": files, "pcm_sha256": {k: sha(review / v) for k, v in files.items()},
            "native_peak": [float(np.max(np.abs(x))) for x in (before, after)],
            "native_rms": rms, "rms_change_db": float(20.0 * np.log10(rms[1] / rms[0])),
            "difference_rms_relative_db": (float(20.0 * np.log10(np.sqrt(np.mean(difference ** 2)) / rms[0]))
                                           if changed.size else None),
            "first_difference_seconds": float(changed[0] / RATE) if changed.size else None,
            "bit_identical": changed.size == 0}
        if score == "1-strum-eighths":
            entry["upstrokes"] = upstroke_bands(before, after)
        manifest["pairs"].append(entry)
        sections.append(
            f'<section><h2>{index}. {title}</h2><p>{description}</p>'
            + "".join(f'<label>{letter}<audio controls preload="metadata" src="{files[letter]}">'
                      f'</audio></label>' for letter in ("A", "B")) + "</section>")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "key.json").write_text(json.dumps(key, indent=2) + "\n")
    (review / "index.html").write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1">'
        '<title>Acustra · Stroke and release</title><style>'
        'body{max-width:860px;margin:48px auto;padding:0 16px;background:#181a19;color:#eee9df;'
        'font:17px/1.6 system-ui}h1{font-size:36px;line-height:1.15}h2{font-size:22px}'
        'p{color:#c6c4bb}section{padding:22px 0;border-top:1px solid #444}'
        'label{display:inline-flex;flex-direction:column;gap:8px;margin:0 20px 16px 0}'
        'audio{max-width:100%}</style>'
        '<h1>Acustra · Stroke and release</h1>'
        '<p>Blind pairs: in each, one file is the candidate and one is not; which is which '
        'is in key.json, kept apart from this page. Identical scores at the plug-in\'s '
        'default controls, 48 kHz, 64-frame blocks, one shared gain for every file. No EQ, '
        'separate normalization, compression or timing alignment.</p>'
        + "".join(sections) + "</html>\n")
    print(json.dumps({"reach": reach, "pairs": manifest["pairs"]}, indent=2))


if __name__ == "__main__":
    main()
