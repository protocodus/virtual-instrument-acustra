#!/usr/bin/env python3
"""Audit MIDI 0-127 through the shipping Performer and factory preset path.

The EventList renderer uses a fresh instance, 48 kHz and 256-frame blocks.
Every key is rendered, including silent keyswitches and out-of-range keys.
Room is disabled to expose string/body/capture behaviour. Pitched notes are
held 1.2 s in a 2 s file, matching MeasureFretboardSweep's metric windows.
The default matrix covers Finger, Thumb, Pick, DADGAD and Open G at three
velocities. It complements the per-string fretboard survey: allocation and
special-key behaviour here are the real public MIDI path.

Neighbour and velocity flags are leads for investigation, not automatic
claims of a model defect. Physical body resonances, string changes, the
microphone sum and short-window spectral leakage can all create differences.
Decay estimates are finite-window fits, not measured complete T60 decays.
No interharmonic metric is used to infer a missing noise mechanism.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import subprocess
from pathlib import Path

import numpy as np
from scipy.io import wavfile

from MeasureFretboardSweep import OPEN_NOTES, PRESETS, note_metrics, note_name

NOISE_KEYS = {12, 31, 32, 33, 34, 35}  # Performer::noiseKeys
KEYSWITCHES = {24, 25, 26, 27, 29, 30}  # no held chord in a fresh instance


def classification(preset: int, midi: int) -> str:
    if midi in NOISE_KEYS:
        return "gesture_noise"
    if midi in KEYSWITCHES:
        return "silent_keyswitch"
    opens = OPEN_NOTES[PRESETS[preset][3]]
    if any(0 <= midi - note <= 20 for note in opens):
        return "pitched"
    return "intentional_silence"


def db(power: float) -> float:
    return float(10.0 * np.log10(max(float(power), 1e-30)))


def audit(renderer: Path, directory: Path, preset: int, midi: int,
          velocity: int, keep_audio: bool) -> dict:
    stem = f"p{preset}-m{midi:03d}-v{velocity:03d}"
    events, wav = directory / f"{stem}.txt", directory / f"{stem}.wav"
    events.write_text(f"0 144 {midi} {velocity}\n1.2 128 {midi} 0\n")
    subprocess.run([str(renderer), str(preset), str(events), str(wav), "2.0",
                    "--room", "0"], check=True, capture_output=True, text=True)
    rate, audio = wavfile.read(wav)
    audio = audio.astype(np.float64)
    if rate != 48000 or audio.shape != (96000, 2):
        raise ValueError(f"Unexpected audio dimensions for {stem}: {rate}, {audio.shape}")
    category = classification(preset, midi)
    finite = bool(np.isfinite(audio).all())
    row = {"preset": preset, "preset_name": PRESETS[preset][0], "midi": midi,
           "name": note_name(midi), "velocity": velocity, "category": category,
           "finite": finite, "peak_stereo_dbfs": db(np.max(np.abs(audio)) ** 2),
           "rms_stereo_dbfs": db(np.mean(audio ** 2))}
    if not finite:
        raise ValueError(f"Non-finite output: {stem}")
    if category == "pitched" and row["peak_stereo_dbfs"] > -150:
        row.update(note_metrics(audio, midi))
    # Unresolved/growing finite-window decay fits are unavailable metrics,
    # not non-finite audio. Keep the report valid JSON and distinguish them.
    row = {key: None if isinstance(value, float) and not math.isfinite(value) else value
           for key, value in row.items()}
    events.unlink()
    if not keep_audio:
        wav.unlink()
    return row


def findings(rows: list[dict]) -> list[dict]:
    flags = []
    for row in rows:
        reason = None
        if row["category"] in ("silent_keyswitch", "intentional_silence"):
            if row["peak_stereo_dbfs"] > -150:
                reason = "unexpected_output"
        elif row["peak_stereo_dbfs"] < -120:
            reason = "unexpected_silence"
        elif row["peak_stereo_dbfs"] > 0.01:
            reason = "sample_peak_above_unity"
        if reason:
            flags.append({"preset": row["preset"], "midi": row["midi"],
                          "velocity": row["velocity"], "reason": reason,
                          "value": row["peak_stereo_dbfs"]})
    pitched = {(r["preset"], r["velocity"], r["midi"]): r for r in rows
               if r["category"] == "pitched"}
    for key, row in pitched.items():
        preset, velocity, midi = key
        neighbours = [pitched.get((preset, velocity, midi + delta))
                      for delta in (-2, -1, 1, 2)]
        neighbours = [r for r in neighbours if r is not None]
        for metric in ("attack_dbfs", "sustain_dbfs", "sustain_left_dbfs",
                       "sustain_right_dbfs"):
            values = [r[metric] for r in neighbours if metric in r]
            if metric in row and len(values) >= 2:
                difference = row[metric] - float(np.median(values))
                if abs(difference) > 4.0:
                    flags.append({"preset": preset, "midi": midi,
                                  "velocity": velocity, "reason": metric + "_neighbour",
                                  "value": difference})
        lower = [r for (p, v, m), r in pitched.items()
                 if p == preset and m == midi and v < velocity and "sustain_dbfs" in r]
        if lower and "sustain_dbfs" in row:
            previous = max(lower, key=lambda r: r["velocity"])
            difference = row["sustain_dbfs"] - previous["sustain_dbfs"]
            if difference < -1.0:
                flags.append({"preset": preset, "midi": midi, "velocity": velocity,
                              "reason": "velocity_sustain_reversal", "value": difference})
    return flags


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("renderer", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--presets", default="0,2,3,5,6")
    parser.add_argument("--velocities", default="16,64,112")
    parser.add_argument("--keep-audio", action="store_true")
    args = parser.parse_args()
    renderer = args.renderer.resolve()
    presets = [int(x) for x in args.presets.split(",")]
    velocities = [int(x) for x in args.velocities.split(",")]
    if any(not 0 <= p < len(PRESETS) for p in presets) or any(not 1 <= v <= 127 for v in velocities):
        parser.error("Preset or velocity outside the valid range")
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    with (args.output / "progress.jsonl").open("w") as progress:
        for preset in presets:
            for velocity in velocities:
                for midi in range(128):
                    row = audit(renderer, args.output, preset, midi, velocity, args.keep_audio)
                    rows.append(row)
                    progress.write(json.dumps(row, allow_nan=False) + "\n")
                    progress.flush()
                print(f"Finished preset {preset}, velocity {velocity}: {len(rows)} keys", flush=True)
    flags = findings(rows)
    report = {"renderer": str(renderer), "renderer_sha256": hashlib.sha256(renderer.read_bytes()).hexdigest(),
              "sample_rate": 48000, "block_size": 256, "hold_seconds": 1.2,
              "render_seconds": 2.0, "room": 0.0, "presets": presets,
              "metric_tools_sha256": {name: hashlib.sha256(
                  Path(__file__).with_name(name).read_bytes()).hexdigest()
                  for name in ("MeasureKeyboardResponse.py", "MeasureFretboardSweep.py")},
              "velocities": velocities, "rows": rows, "flags": flags}
    (args.output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    fields = list(dict.fromkeys(k for row in rows for k in row))
    with (args.output / "notes.csv").open("w", newline="") as output:
        writer = csv.DictWriter(output, fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"Completed {len(rows)} keys; {len(flags)} diagnostic flags. See report.json.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
