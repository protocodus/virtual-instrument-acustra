#!/usr/bin/env python3
"""Freeze native construction renders and measure resonance changes.

Example:
  python3 Tools/CompareConstructionResonance.py \
    --renderer baseline=build-resonance/AcustraPerformanceRenderer \
    --output Docs/audio/resonance-20261009
  python3 Tools/CompareConstructionResonance.py \
    --renderer baseline=Docs/audio/resonance-20261009/baseline/AcustraPerformanceRenderer \
    --renderer candidate=build-resonance/AcustraPerformanceRenderer \
    --output Docs/audio/resonance-20261009

Stereo microphone covers every MIDI40..52, E4/G4/B4, A3/A4 and their
neighbors, and a second A2/A#2 fingering; mono microphone and piezo cover
the A2/A3/A4 neighbor groups and E4/G4/B4. Every construction gets those isolated notes and
an overlapping phrase. Each note has a fresh engine and is held 1.2s.
Native f32 is never normalized. Audition WAVs receive one shared gain
over every version/capture/case. Signal metrics establish no listener
preference, perceived boominess, or instrument realism.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
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
HOLD_SECONDS = 1.2
NOTE_SECONDS = 2.0
PHRASE_SECONDS = 4.5
MODELS = ("original", "bellido1978")
SHAPES = ("parlor", "auditorium", "dreadnought", "jumbo")
WOODS = ("spruce", "mahogany", "maple")
TECHNIQUES = ("finger", "pick", "thumb")
CAPTURES = ("stereo_mic", "mono_mic", "piezo")
NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
NOTES = [(NAMES[midi % 12] + str(midi // 12 - 1), midi,
          1 if midi < 45 else 2 if midi < 50 else 3)
         for midi in range(40, 53)]
NOTES += [("A2_lowE", 45, 1), ("A#2_lowE", 46, 1),
          ("G#3", 56, 4), ("A3", 57, 4), ("A#3", 58, 4),
          ("E4", 64, 6), ("G4", 67, 6), ("G#4", 68, 6),
          ("A4", 69, 6), ("A#4", 70, 6), ("B4", 71, 6)]
FOCUSED = {"G#2", "A2", "A#2", "A2_lowE", "A#2_lowE",
           "G#3", "A3", "A#3", "E4", "G4", "G#4", "A4", "A#4", "B4"}
BANDS = {"bass_70_180": (70, 180), "low_mid_180_650": (180, 650),
         "presence_650_2000": (650, 2000), "treble_2000_6000": (2000, 6000)}
WINDOWS = {"attack": (.005, .08), "sustain": (.08, 1.15),
           "early": (.10, .30), "late": (.95, 1.15),
           "release": (1.25, 1.90)}


def digest(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def write_json(path, value):
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")
    temporary.replace(path)


def freeze_text(path, value):
    if path.exists() and path.read_text() != value:
        raise ValueError(f"frozen score differs: {path}")
    path.write_text(value)


def write_score(path, duration, events):
    rows = [f"ACUSTRA_PERFORMANCE_V1 {RATE} {round(duration * RATE)}"]
    rows += [f"{round(time * RATE)} {channel} {midi} {velocity} 0"
             for time, channel, midi, velocity in sorted(
                 events, key=lambda event: (event[0], event[3] != 0))]
    freeze_text(path, "\n".join(rows) + "\n")
    return path


def make_scores(directory):
    directory.mkdir(exist_ok=True)
    paths = {}
    for name, midi, channel in NOTES:
        safe = name.replace("#", "sharp")
        paths[name] = write_score(directory / f"{safe}.events", NOTE_SECONDS,
            [(0, channel, midi, 96), (HOLD_SECONDS, channel, midi, 0)])
    events = []
    for time, channel, midi, velocity in ((.1, 1, 40, 90), (.4, 2, 45, 88),
            (.7, 3, 52, 78), (1., 4, 55, 84), (1.3, 5, 59, 88),
            (1.5, 6, 64, 91)):
        events += [(time, channel, midi, velocity), (1.9, channel, midi, 0)]
    for index, midi in enumerate((40, 47, 52, 55, 59, 64)):
        events += [(2.15 + .012 * index, index + 1, midi, 96),
                   (3.3, index + 1, midi, 0)]
    paths["phrase"] = write_score(directory / "phrase.events", PHRASE_SECONDS, events)
    return paths


def db(power):
    return float(10 * np.log10(max(float(power), 1e-30)))


def spectrum(signal):
    length = min(16384, len(signal))
    frequencies, power = welch(signal, fs=RATE, axis=0, nperseg=length,
        noverlap=length // 2, window="hann", detrend=False, scaling="density")
    return frequencies, power.mean(axis=1)


def metric(signal, midi=None):
    signal = signal.astype(np.float64)
    power = float(np.mean(signal * signal))
    peak = float(np.max(np.abs(signal)))
    frequencies, density = spectrum(signal)
    step = frequencies[1] - frequencies[0]

    def integrate(low, high):
        return float(np.sum(density[(frequencies >= low) & (frequencies < high)]) * step)

    total = integrate(20, 10000)
    bands = {name: integrate(*bounds) for name, bounds in BANDS.items()}
    result = {"rms_dbfs": db(power), "peak_dbfs": db(peak * peak),
              "peak": peak, "samples_at_or_above_full_scale": int(np.sum(np.abs(signal) >= 1)),
              "band_power_dbfs": {name: db(value) for name, value in bands.items()},
              "bass_fraction_db": db(bands["bass_70_180"] / max(total, 1e-30)),
              "low_mid_to_presence_db": db(bands["low_mid_180_650"]
                  / max(bands["presence_650_2000"], 1e-30)),
              "treble_fraction_db": db(bands["treble_2000_6000"] / max(total, 1e-30)),
              "spectral_centroid_hz": float(np.sum(frequencies * density)
                  / max(float(np.sum(density)), 1e-30))}
    if midi is not None:
        fundamental = 440 * 2 ** ((midi - 69) / 12)
        # +/-1.5% includes detuning while separating neighboring semitones.
        partials = [integrate(fundamental * index - max(step, fundamental * index * .015),
                             fundamental * index + max(step, fundamental * index * .015))
                    for index in range(1, 9)]
        result.update({"nominal_fundamental_hz": fundamental,
            "fundamental_power_dbfs": db(partials[0]),
            "fundamental_fraction_db": db(partials[0] / max(total, 1e-30)),
            "upper_partial_to_fundamental_db": db(sum(partials[1:]) / max(partials[0], 1e-30)),
            "partial_power_dbfs": [db(value) for value in partials]})
    return result


def note_metrics(audio, midi):
    result = {name: metric(audio[round(begin * RATE):round(end * RATE)], midi)
              for name, (begin, end) in WINDOWS.items()}
    result["held_decay_db"] = result["late"]["rms_dbfs"] - result["early"]["rms_dbfs"]
    result["fundamental_decay_db"] = (result["late"]["fundamental_power_dbfs"]
                                        - result["early"]["fundamental_power_dbfs"])
    result["release_relative_to_late_db"] = (result["release"]["rms_dbfs"]
                                               - result["late"]["rms_dbfs"])
    return result


def render_note(renderer, raw, events, options, duration):
    command = [str(renderer), str(events), str(raw), *options]
    if raw.exists():
        audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2)
    else:
        subprocess.run(command, check=True, capture_output=True, text=True)
        audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2)
    if len(audio) != round(duration * RATE) or not np.isfinite(audio).all() or not np.any(audio):
        raise ValueError(f"non-finite, silent, or incorrectly sized render: {raw}")
    return audio, command


def summary_row(case):
    notes = case["notes"]
    note = notes["A2"]["metrics"]
    result = {name: case[name] for name in ("model", "shape", "wood", "technique", "capture")}
    result.update({"A2_rms_dbfs": note["sustain"]["rms_dbfs"],
        "A2_neighbor_excess_db": note["sustain"]["rms_dbfs"] - .5 * (
            notes["G#2"]["metrics"]["sustain"]["rms_dbfs"]
            + notes["A#2"]["metrics"]["sustain"]["rms_dbfs"]),
        "A2_fundamental_fraction_db": note["sustain"]["fundamental_fraction_db"],
        "A2_bass_fraction_db": note["sustain"]["bass_fraction_db"],
        "A2_held_decay_db": note["held_decay_db"],
        "A2_lowE_rms_dbfs": notes["A2_lowE"]["metrics"]["sustain"]["rms_dbfs"],
        "A2_string_difference_db": note["sustain"]["rms_dbfs"]
            - notes["A2_lowE"]["metrics"]["sustain"]["rms_dbfs"],
        "treble_attack_fraction_db": float(np.mean([
            notes[name]["metrics"]["attack"]["treble_fraction_db"]
            for name in ("E4", "G4", "B4")])),
        "treble_upper_partial_ratio_db": float(np.mean([
            notes[name]["metrics"]["sustain"]["upper_partial_to_fundamental_db"]
            for name in ("E4", "G4", "B4")])),
        "treble_held_decay_db": float(np.mean([
            notes[name]["metrics"]["held_decay_db"] for name in ("E4", "G4", "B4")]))})
    for name, middle, lower, upper in (("A2_same_string", "A2_lowE", "G#2", "A#2_lowE"),
            ("A3_same_string", "A3", "G#3", "A#3"),
            ("A4_same_string", "A4", "G#4", "A#4")):
        result[name + "_neighbor_excess_db"] = (
            notes[middle]["metrics"]["sustain"]["rms_dbfs"] - .5 * (
            notes[lower]["metrics"]["sustain"]["rms_dbfs"]
            + notes[upper]["metrics"]["sustain"]["rms_dbfs"]))
    return result


def version(label, source, directory, scores, jobs):
    destination = directory / label
    destination.mkdir(exist_ok=True)
    frozen = destination / "AcustraPerformanceRenderer"
    renderer_hash = digest(source)
    if frozen.exists() and digest(frozen) != renderer_hash:
        raise ValueError(f"renderer changed for frozen label {label}; use a new label")
    if not frozen.exists():
        shutil.copy2(source, frozen)
    manifest_path = destination / "manifest.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {
        "renderer_source": str(source), "renderer_sha256": renderer_hash, "cases": {}}
    for row in manifest["cases"].values():
        for item in [*row["notes"].values(), row["phrase"]]:
            if digest(directory / item["raw_file"]) != item["sha256"]:
                raise ValueError(f"frozen native render changed: {item['raw_file']}")
    combinations = list(itertools.product(MODELS, SHAPES, WOODS, TECHNIQUES, CAPTURES))
    # Deliver the affected dry microphone and Auditorium measurements first.
    combinations.sort(key=lambda item: (CAPTURES.index(item[4]),
        item[1] != "auditorium", item[0] != "bellido1978", SHAPES.index(item[1]),
        WOODS.index(item[2]), TECHNIQUES.index(item[3])))
    with ThreadPoolExecutor(max_workers=jobs) as workers:
        for index, (model, shape, wood, technique, capture) in enumerate(combinations, 1):
            name = f"{model}-{shape}-{wood}-{technique}-{capture}"
            if name in manifest["cases"]:
                continue
            selected = NOTES if capture == "stereo_mic" else [note for note in NOTES if note[0] in FOCUSED]
            options = [capture, technique, "--guitar-model", model, "--body-shape", shape,
                "--body-material", wood, "--touch", "0.58", "--pluck-position", "0.28", "--room", "0"]
            case_directory = destination / name
            case_directory.mkdir(exist_ok=True)

            def isolated(note):
                note_name, midi, channel = note
                raw = case_directory / f"{note_name.replace('#', 'sharp')}.f32"
                audio, command = render_note(frozen, raw, scores[note_name], options, NOTE_SECONDS)
                return note_name, {"midi": midi, "channel": channel,
                    "raw_file": str(raw.relative_to(directory)), "sha256": digest(raw),
                    "command": command, "metrics": note_metrics(audio, midi), "native": metric(audio)}

            notes = dict(workers.map(isolated, selected))
            raw = case_directory / "phrase.f32"
            audio, command = render_note(frozen, raw, scores["phrase"], options, PHRASE_SECONDS)
            row = {"model": model, "shape": shape, "wood": wood,
                "technique": technique, "capture": capture, "notes": notes,
                "phrase": {"raw_file": str(raw.relative_to(directory)), "sha256": digest(raw),
                           "command": command, "native": metric(audio)}}
            row["summary"] = summary_row(row)
            manifest["cases"][name] = row
            write_json(manifest_path, manifest)
            csv_summary(directory / f"{label}-summary.csv",
                        [case["summary"] for case in manifest["cases"].values()])
            print(f"{label} {index}/{len(combinations)}: {name}", flush=True)
    return manifest


def csv_summary(path, rows):
    with path.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def markdown_summary(path, manifests, report):
    lines = ["# Construction resonance measurements", "", report["protocol"], "",
        "A2 neighbor excess is sustain RMS minus mean G#2/A#2 RMS; G#2 uses low E string. "
        "A2 is open A string; alternate A2 uses low E fret 5. Values are native dB; "
        "treble values average E4/G4/B4. No metric alone establishes boominess or banjo character.", ""]
    for label, manifest in manifests.items():
        lines += [f"## {label}", "", "StereoMic construction table (ranges over Finger/Pick/Thumb):", "",
            "| Model | Shape | Wood | A2 neighbor excess dB | A2 same string excess dB | A3 same string excess dB | A4 same string excess dB | Treble partial/fundamental dB |",
            "|---|---|---|---:|---:|---:|---:|---:|"]
        for model, shape, wood in itertools.product(MODELS, SHAPES, WOODS):
            rows = [row["summary"] for row in manifest["cases"].values()
                if row["model"] == model and row["shape"] == shape and row["wood"] == wood
                and row["capture"] == "stereo_mic"]
            def bounds(field):
                values = [row[field] for row in rows]
                return f"{min(values):+.2f} to {max(values):+.2f}"
            lines += [f"| {model} | {shape} | {wood} | {bounds('A2_neighbor_excess_db')} | "
                f"{bounds('A2_same_string_neighbor_excess_db')} | {bounds('A3_same_string_neighbor_excess_db')} | "
                f"{bounds('A4_same_string_neighbor_excess_db')} | "
                f"{bounds('treble_upper_partial_ratio_db')} |"]
        lines += ["", "Largest A2 neighbor excess cases:", ""]
        for row in sorted((case["summary"] for case in manifest["cases"].values()),
                          key=lambda row: row["A2_neighbor_excess_db"], reverse=True)[:8]:
            lines += [f"- {row['model']} / {row['shape']} / {row['wood']} / {row['technique']} / "
                f"{row['capture']}: {row['A2_neighbor_excess_db']:+.2f} dB; "
                f"A2 RMS {row['A2_rms_dbfs']:.2f} dBFS."]
        lines += [""]
    if "deltas" in report:
        lines += ["## Changes relative to first renderer", ""]
        for label, deltas in report["deltas"].items():
            for capture in CAPTURES:
                rows = [row for row in deltas.values() if row["capture"] == capture]
                excess = [row["A2_neighbor_excess_db"] for row in rows]
                rms = [row["A2_rms_dbfs"] for row in rows]
                lines += [f"- {label}, {capture}: A2 neighbor excess change "
                    f"{min(excess):+.2f} to {max(excess):+.2f} dB; "
                    f"A2 native RMS change {min(rms):+.2f} to {max(rms):+.2f} dB."]
        lines += [""]
    lines += [f"Shared audition gain: {report['listening_gain_db']:+.3f} dB; "
        "WAV files have this same gain across all versions and cases. Native f32 retains original level.", "",
        "Reproduction commands and hashes are in report.json and per-version manifest.json.", ""]
    path.write_text("\n".join(lines))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", action="append", required=True, metavar="LABEL=PATH")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--no-wav", action="store_true", help="measure only; save disk/time during diagnosis")
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 32:
        raise ValueError("jobs must be 1..32")
    directory = args.output.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    tool_hash = digest(Path(__file__))
    frozen_tool = directory / Path(__file__).name
    if frozen_tool.exists() and digest(frozen_tool) != tool_hash:
        raise ValueError("scorer differs from frozen version; use another output directory")
    if frozen_tool.resolve() != Path(__file__).resolve():
        shutil.copy2(__file__, frozen_tool)
    scores = make_scores(directory / "scores")
    manifests = {}
    for item in args.renderer:
        label, source = item.split("=", 1)
        if not label.isidentifier() or label in manifests:
            raise ValueError("renderer labels must be unique identifiers")
        manifests[label] = version(label, Path(source).resolve(strict=True), directory, scores, args.jobs)
        csv_summary(directory / f"{label}-summary.csv",
                    [row["summary"] for row in manifests[label]["cases"].values()])
    all_files = [item for manifest in manifests.values() for row in manifest["cases"].values()
                 for item in [*row["notes"].values(), row["phrase"]]]
    peak = max(item["native"]["peak"] for item in all_files)
    gain = .9 / max(peak, 1e-30)
    if not args.no_wav:
        for item in all_files:
            raw = directory / item["raw_file"]
            wav = raw.with_suffix(".wav")
            audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2)
            wavfile.write(wav, RATE, (audio * gain).astype(np.float32))
            item["audition_file"] = str(wav.relative_to(directory))
    report = {"protocol": "Exhaustive native StereoMic construction/technique matrix, with focused MonoMic/Piezo contrasts; no perceptual preference claims.",
        "sample_rate": RATE, "tool_sha256": tool_hash,
        "runtime": {"python": platform.python_version(), "numpy": np.__version__,
                    "scipy": scipy.__version__, "platform": platform.platform()},
        "controls": {"touch": .58, "pluck_position": .28, "room": 0,
            "velocity": 96, "hold_seconds": HOLD_SECONDS, "note_seconds": NOTE_SECONDS,
            "fresh_engine_for_every_isolated_note": True, "tuning": "standard"},
        "notes": [{"name": name, "midi": midi, "channel": channel} for name, midi, channel in NOTES],
        "focused_capture_notes": sorted(FOCUSED), "windows_seconds": WINDOWS,
        "band_edges_hz": BANDS, "spectral_method": "Welch Hann, at most16384 samples/50% overlap, mean L/R power; partial bands +/-max(bin width, 1.5% partial frequency). Native levels and powers remain unnormalized.",
        "A2_neighbor_excess_definition": "Sustain A2 RMS dBFS minus arithmetic mean G#2/A#2 RMS dBFS. G#2 lowE string and A2/A#2 A string differ, so compare A2_lowE as a control.",
        "scores": {str(path.relative_to(directory)): digest(path) for path in scores.values()},
        "cases_per_renderer": len(next(iter(manifests.values()))["cases"]),
        "renders_per_renderer": len(all_files) // len(manifests),
        "audio_seconds_per_renderer": 72 * (len(NOTES) * NOTE_SECONDS + PHRASE_SECONDS
                                             + 2 * (len(FOCUSED) * NOTE_SECONDS + PHRASE_SECONDS)),
        "listening_gain": gain, "listening_gain_db": db(gain * gain),
        "listening_gain_scope": "One shared scalar for every note/phrase/capture/version, peak ceiling0.9; native f32 untouched.",
        "wav_generated": not args.no_wav, "renderers": manifests}
    if len(manifests) > 1:
        baseline_label = next(iter(manifests))
        report["deltas_relative_to"] = baseline_label
        report["deltas"] = {}
        for label, manifest in list(manifests.items())[1:]:
            report["deltas"][label] = {}
            for name, row in manifest["cases"].items():
                baseline = manifests[baseline_label]["cases"][name]["summary"]
                report["deltas"][label][name] = {
                    key: value - baseline[key] if isinstance(value, (int, float)) else value
                    for key, value in row["summary"].items()}
            csv_summary(directory / f"{label}-deltas.csv", list(report["deltas"][label].values()))
    write_json(directory / "report.json", report)
    markdown_summary(directory / "summary.md", manifests, report)
    print(f"Saved {directory / 'summary.md'} ({report['renders_per_renderer']} native clips/version)")


if __name__ == "__main__":
    main()
