#!/usr/bin/env python3
"""Freeze matched native body/capture renders and make a shared-trim audition.

Example (the second invocation reuses the frozen baseline):
  CompareBodyCapture.py --renderer baseline=/path/before --output /tmp/body-ab
  CompareBodyCapture.py --renderer baseline=/path/before \
      --renderer candidate=/path/after --output /tmp/body-ab

Two models, three techniques, and dry/Room50 each receive eight isolated notes
and one overlapping phrase: 24 clips, 169.2 seconds per renderer. Measurements
describe signal changes; they do not establish listener preference or realism.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import platform
import shutil
import subprocess

import numpy as np
import scipy
from scipy.io import wavfile
from scipy.signal import welch


RATE = 48000
NOTES = (("E2", 40, 1), ("A2", 45, 2), ("D3", 50, 3), ("G3", 55, 4),
         ("B3", 59, 5), ("E4", 64, 6), ("G4", 67, 6), ("B4", 71, 6))
BANDS = {"bass_70_180": (70, 180), "low_mid_180_630": (180, 630),
         "presence_800_1600": (800, 1600), "treble_1600_6000": (1600, 6000)}
NOTE_SECONDS = 1.2
PHRASE_SECONDS = 4.5


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def freeze_text(path, content):
    if path.exists() and path.read_text() != content:
        raise ValueError(f"frozen content differs: {path}")
    path.write_text(content)


def score(path, duration, events):
    rows = [f"ACUSTRA_PERFORMANCE_V1 {RATE} {round(duration * RATE)}"]
    # At equal sample positions, release precedes a fresh contact.
    events = sorted(events, key=lambda event: (event[0], event[3] != 0))
    rows += [f"{round(time * RATE)} {channel} {note} {velocity} 0"
             for time, channel, note, velocity in events]
    freeze_text(path, "\n".join(rows) + "\n")
    return path


def scores(directory):
    directory.mkdir(exist_ok=True)
    isolated = [score(directory / f"{name}.events", NOTE_SECONDS,
                      [(0, channel, note, 96), (.7, channel, note, 0)])
                for name, note, channel in NOTES]
    events = []
    for time, channel, note, velocity in ((.1, 1, 40, 90), (.4, 3, 52, 78),
                                          (.7, 4, 55, 84), (1., 5, 59, 88),
                                          (1.3, 6, 64, 91)):
        events.extend(((time, channel, note, velocity), (1.8, channel, note, 0)))
    for index, (note, velocity) in enumerate(zip((40, 47, 52, 55, 59, 64),
                                                (96, 90, 87, 90, 94, 98))):
        events.extend(((2.15 + .012 * index, index + 1, note, velocity),
                       (3.3, index + 1, note, 0)))
    return {"isolated_notes": isolated,
            "phrase": [score(directory / "phrase.events", PHRASE_SECONDS, events)]}


def db(value):
    return float(10 * np.log10(max(float(value), 1e-30)))


def metrics(signal):
    signal = signal.astype(np.float64)
    mean_square = float(np.mean(signal * signal))
    peak = float(np.max(np.abs(signal)))
    mono = signal.mean(axis=1)
    channel_energy = np.sum(signal * signal, axis=0)
    denominator = float(np.sqrt(np.prod(channel_energy)))
    frequency, power = welch(signal, fs=RATE, axis=0,
                             nperseg=min(8192, len(signal)),
                             noverlap=min(4096, len(signal) // 2),
                             window="hann", detrend=False, scaling="density")
    power = power.mean(axis=1)
    bin_width = frequency[1] - frequency[0]
    band_power = {name: float(np.sum(power[(frequency >= low) & (frequency < high)])
                               * bin_width)
                  for name, (low, high) in BANDS.items()}
    return {"rms": float(np.sqrt(mean_square)), "peak": peak,
            "rms_dbfs": db(mean_square), "peak_dbfs": db(peak * peak),
            "samples_at_or_above_full_scale": int(np.sum(np.abs(signal) >= 1)),
            "stereo_correlation": (float(np.sum(signal[:, 0] * signal[:, 1]))
                                   / denominator if denominator > 1e-30 else 0.),
            "mono_retention_db": db(np.mean(mono * mono) / max(mean_square, 1e-30)),
            "band_power_dbfs": {name: db(value) for name, value in band_power.items()},
            "low_mid_to_presence_db": db(band_power["low_mid_180_630"]
                                        / max(band_power["presence_800_1600"], 1e-30))}


def render_case(renderer, output, event_paths, command_options, duration):
    parts = []
    commands = []
    part_hashes = []
    for event in event_paths:
        raw = output.with_name(f"{output.stem}-{event.stem}-part.f32")
        command = [str(renderer), str(event), str(raw), *command_options]
        subprocess.run(command, check=True, capture_output=True, text=True)
        audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2)
        if len(audio) != round(duration * RATE) or not np.isfinite(audio).all():
            raise ValueError(f"invalid renderer output: {raw}")
        commands.append(command)
        part_hashes.append(digest(raw))
        parts.append(audio)
        # The concatenated native float file retains every sample of each part.
        raw.unlink()
    audio = np.concatenate(parts)
    audio.astype("<f4").tofile(output)
    return audio, commands, part_hashes


def version(label, source, directory, event_scores):
    destination = directory / label
    destination.mkdir(exist_ok=True)
    frozen = destination / "AcustraPerformanceRenderer"
    source_hash = digest(source)
    if frozen.exists() and digest(frozen) != source_hash:
        raise ValueError(f"renderer changed for frozen label {label}; use a new label/directory")
    if not frozen.exists():
        shutil.copy2(source, frozen)
    manifest_path = destination / "manifest.json"
    if manifest_path.exists():
        manifest = json.loads(manifest_path.read_text())
        for row in manifest["cases"].values():
            if digest(directory / row["raw_file"]) != row["sha256"]:
                raise ValueError("frozen native render changed")
        return manifest
    manifest = {"renderer_source": str(source), "renderer_sha256": source_hash, "cases": {}}
    for model, technique, room, study in itertools.product(
            ("original", "bellido1978"), ("finger", "pick", "thumb"),
            (0., .5), ("isolated_notes", "phrase")):
        case = f"{model}-{technique}-room{int(room * 100)}-{study}"
        raw = destination / f"{case}.f32"
        # Models play their presets' construction; within each model every
        # renderer receives exactly the same controls and string assignments.
        options = ["stereo_mic", technique, "--guitar-model", model,
                   "--body-shape", "dreadnought" if model == "original" else "auditorium",
                   "--body-material", "spruce" if model == "original" else "mahogany",
                   "--touch", "0.58", "--pluck-position", "0.28", "--room", str(room)]
        audio, commands, part_hashes = render_case(
            frozen, raw, event_scores[study], options,
            NOTE_SECONDS if study == "isolated_notes" else PHRASE_SECONDS)
        row = {"model": model, "technique": technique, "room": room, "study": study,
               "raw_file": str(raw.relative_to(directory)), "sha256": digest(raw),
               "part_sha256": part_hashes, "commands": commands, "native": metrics(audio)}
        if study == "isolated_notes":
            row["notes"] = {}
            for index, (name, midi, channel) in enumerate(NOTES):
                start = round(index * NOTE_SECONDS * RATE)
                segment = audio[start + round(.025 * RATE):start + round(.65 * RATE)]
                row["notes"][name] = {"midi": midi, "channel": channel,
                                       "native": metrics(segment)}
        manifest["cases"][case] = row
        print(f"{label}: {case}", flush=True)
    write_json(manifest_path, manifest)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", action="append", required=True, metavar="LABEL=PATH")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    frozen_tool = directory / Path(__file__).name
    tool_hash = digest(Path(__file__))
    if frozen_tool.exists() and digest(frozen_tool) != tool_hash:
        raise ValueError("scorer differs from frozen version; use another output directory")
    if frozen_tool.resolve() != Path(__file__).resolve():
        shutil.copy2(__file__, frozen_tool)
    event_scores = scores(directory / "scores")
    manifests = {}
    for item in args.renderer:
        label, source = item.split("=", 1)
        if not label.isidentifier() or label in manifests:
            raise ValueError("renderer labels must be unique identifiers")
        manifests[label] = version(label, Path(source).resolve(strict=True), directory, event_scores)
    peak = max(row["native"]["peak"] for manifest in manifests.values()
               for row in manifest["cases"].values())
    # One scalar over every note/model/technique/version. A quieter candidate
    # remains quieter; no per-file or per-version loudness matching hides it.
    gain = .9 / max(peak, 1e-30)
    for label, manifest in manifests.items():
        for row in manifest["cases"].values():
            raw = directory / row["raw_file"]
            audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2)
            wav = raw.with_suffix(".wav")
            wavfile.write(wav, RATE, (audio * gain).astype(np.float32))
            row["listen_file"] = str(wav.relative_to(directory))
    report = {
        "protocol": "Native stereo microphone body/capture comparison; no listener preference claim.",
        "sample_rate": RATE, "tool_sha256": tool_hash,
        "runtime": {"python": platform.python_version(), "numpy": np.__version__,
                    "scipy": scipy.__version__, "platform": platform.platform()},
        "controls": {"capture": "stereo_mic", "touch": .58, "pluck_position": .28,
                     "isolated_note_velocity": 96, "isolated_note_off_seconds": .7,
                     "note_duration_seconds": NOTE_SECONDS,
                     "note_metric_window_seconds": [.025, .65],
                     "fresh_engine_for_every_isolated_note": True},
        "spectral_method": "Welch Hann, 8192 samples/4096 overlap, mean L/R power; band sums in Hz. Note windows exclude release. Correlation is uncentered L/R normalized cross energy; mono retention compares (L+R)/2 power with average L/R power.",
        "band_edges_hz": BANDS,
        "scores": {str(path.relative_to(directory)): digest(path)
                   for paths in event_scores.values() for path in paths},
        "listening_gain": gain, "listening_gain_db": db(gain * gain),
        "listening_gain_scope": "One shared scalar for every render in this report, peak ceiling 0.9. Native .f32 files have no trim.",
        "audio_seconds_per_renderer": 12 * (8 * NOTE_SECONDS + PHRASE_SECONDS),
        "renderers": manifests,
    }
    if len(manifests) >= 2:
        first = next(iter(manifests))
        deltas = {}
        for label in list(manifests)[1:]:
            deltas[label] = {}
            for case, row in manifests[label]["cases"].items():
                baseline = manifests[first]["cases"][case]
                fields = ("rms_dbfs", "peak_dbfs", "low_mid_to_presence_db", "mono_retention_db")
                result = {field: row["native"][field] - baseline["native"][field]
                          for field in fields}
                if "notes" in row:
                    result["notes"] = {
                        note: {field: detail["native"][field]
                                      - baseline["notes"][note]["native"][field]
                               for field in fields}
                        for note, detail in row["notes"].items()}
                deltas[label][case] = result
        report["deltas_relative_to"] = first
        report["deltas"] = deltas
    write_json(directory / "report.json", report)
    print(f"Saved {directory / 'report.json'}; shared listening trim {report['listening_gain_db']:.3f} dB")


if __name__ == "__main__":
    main()
