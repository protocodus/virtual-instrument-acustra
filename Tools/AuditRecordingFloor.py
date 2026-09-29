#!/usr/bin/env python3
"""Score a rendered corpus as the benchmark does, and again with each recording's
own noise floor added to the model it is compared with.

The reference recordings carry a hiss floor the engine does not. Where a note's
upper partials have died - a soft pluck's 5-12 kHz from its first frame, or any
string losing its upper partials as fast as the recordings do - the recording's
bands and log magnitudes hold that floor while the engine's keep falling, and the
scorer's band, body and multiscale descriptors charge the difference to the model.
This is a diagnostic, not a benchmark: it reports how much of a paired change is
that charge, by recording the model through the same floor.

The floor is each recording's quieter of its last 0.4 s and its lead-in before the
onset (the open corpora keep 20 ms), per 94 Hz bin above 1.5 kHz, where the notes'
partials are gone by then; it is synthesised as stationary random-phase noise
(seeded, so a paired run adds the same noise to both engines) and added to the
model at the level it has relative to the recording's 20-500 ms level, the
scorer's own level window, so the model keeps its own velocity law.

usage: AuditRecordingFloor.py MANIFEST.json [--material steel|flattop]
Prints the plain and the floor-matched score with their terms.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import numpy as np

import FitPhysicalModel as fit

RATE = 48_000


def _psd(segment: np.ndarray, size: int) -> np.ndarray:
    taper = np.hanning(size)
    frames = [segment[start:start + size] * taper
              for start in range(0, max(segment.size - size, 0) + 1, size // 2)]
    return (np.mean([np.abs(np.fft.rfft(frame, size)) ** 2 for frame in frames], 0)
            / np.sum(taper * taper))


def floor_noise(recording: np.ndarray, length: int,
                generator: np.random.Generator) -> np.ndarray:
    size = 512
    power = _psd(recording[-int(0.4 * RATE):], size)
    onset = fit._onset(recording, RATE)
    if onset >= size + 64:
        power = np.minimum(power, _psd(recording[:onset - 64], size))
    frequency = np.fft.rfftfreq(size, 1.0 / RATE)
    power[frequency < 1500.0] = 0.0
    padded = 1 << int(math.ceil(math.log2(length)))
    magnitude = np.sqrt(np.interp(np.fft.rfftfreq(padded, 1.0 / RATE),
                                  frequency, power))
    white = np.fft.rfft(generator.standard_normal(padded))
    return np.fft.irfft(white * magnitude, padded)[:length]


def _level(signal: np.ndarray) -> float:
    onset = fit._onset(signal, RATE)
    window = signal[onset + int(0.02 * RATE):onset + int(0.5 * RATE)]
    return float(np.sqrt(np.mean(window * window)))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--material")
    arguments = parser.parse_args()
    manifest = json.loads(arguments.manifest.read_text(encoding="utf-8"))
    base = arguments.manifest.parent
    generator = np.random.default_rng(1)

    def load(spec):
        rate, audio = fit._read_audio(spec, base, manifest.get("sample_rate"),
                                      manifest.get("channels"))
        return fit._resample(audio, rate, RATE)

    plain, floored = [], []
    models: dict[tuple[str, int], np.ndarray] = {}
    for example in manifest["examples"]:
        material = example.get("material")
        if arguments.material and (material or "flattop") != arguments.material:
            continue
        recording = load(example["target"])
        target = fit.extract_features(recording, RATE, example["midi"])
        key = (json.dumps(example["model"], sort_keys=True), example["midi"])
        if key not in models:
            models[key] = load(example["model"])
        model = models[key]
        gain = _level(recording) / max(_level(model), 1.0e-12)
        heard = model + floor_noise(recording, model.size, generator) / gain
        row = {"id": example["id"], "material": material, "midi": example["midi"],
               "velocity": float(example["velocity"]),
               "dynamic_group": example.get("dynamic_group"),
               "target_features": target}
        plain.append({**row, "model_features":
                      fit.extract_features(model, RATE, example["midi"])})
        floored.append({**row, "model_features":
                        fit.extract_features(heard, RATE, example["midi"])})
    if not plain:
        parser.error("the selection holds no rows")
    for name, rows in (("plain", plain), ("floor-matched", floored)):
        report = fit.score_examples(rows, RATE)
        terms = " ".join(f"{term} {value['score']:.3f}"
                         for term, value in report["terms"].items()
                         if value["score"] is not None)
        print(f"{name:14s} {report['score']:.4f}  {terms}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
