#!/usr/bin/env python3
"""Measure every playable note of the fretboard at two velocities.

Protocol. For one factory preset (or every preset) this renders each string at
every fret 0-20 through AcustraPerformanceRenderer, one fresh engine per note,
the string fixed by the performance channel, held 1.2 s in a 2.0 s render at
48 kHz, at each velocity asked for (default 48 and 112, the two layers a
sample library would hold). Room is left at the renderer's 0 so the string,
bridge, body and capture are measured and not the ambience. The mono sum is
measured for:

  peak     sample peak (dBFS)
  att      attack level, 5-80 ms (dBFS)
  sus      sustained level, 0.08-1.0 s (dBFS); sus_l and sus_r the same for
           each channel alone, so a hole in the sum that neither channel has
           is the microphone pair's spacing, not the body's
  cent_a   spectral centroid of the attack, 5-80 ms (Hz)
  cent_s   spectral centroid of the sustain, 0.1-0.6 s (Hz)
  t60_f    T60 of the fundamental, a line fitted to its log energy over
           0.1-1.1 s (s)
  t60_hi   T60 of 2-6 kHz, fitted the same way over 0.03-0.5 s (s)
  h2, h3   second and third partial against the fundamental, 0.08-0.4 s (dB)
  floor    energy half-way between partials 1-12 against the partials'
           own energy, 0.05-0.3 s (dB); what sounds between the harmonics
  rel      the release: peak in the 100 ms after key-up against the RMS of
           the 100 ms before it (dB), a click or a thump reads high
  tail     level 0.5-0.7 s after key-up against the level before it (dB)
  att_c    pitch of the attack, 0.03-0.15 s, in cents from equal temperament
           at A440: the strongest peak within 8 % of the nominal fundamental,
           Hann window, 8x zero padding, parabolic interpolation
  set_c    the same over 0.3-0.9 s, the settled pitch; glide = att_c - set_c
  B        inharmonicity coefficient from resolved partials 1-6 over
           0.1-0.6 s, f_n = n f_ideal sqrt(1 + B n^2), least squares on
           f_n^2 / n^2; unavailable when harmonic identity is ambiguous

Every metric is also expressed as a deviation from its neighbourhood: the
mean of the same metric at frets +/-1 and +/-2 on the same string, with the
note left out. A level deviation beyond 3 dB, a centroid beyond 35 %, a
fundamental T60 beyond 40 % of the neighbourhood or a settled pitch 4 cents
from it is listed as an outlier, as is a settled pitch more than 6 cents from
equal temperament outright (the strings are tuned to it; a real guitar's
intonation error is the player's setup, not the model's);
the same pitch on two strings is also compared, where a real guitar differs
by construction but not by a hole. Signal metrics establish no listener
preference; they say where a step is, not whether it is heard as one.

Renders already in the output directory are reused: keep one directory per
engine variant. --calibration FILE passes a Tools/CalibrationFile.h file to
the renderer to try a calibration without rebuilding.

  python3 Tools/MeasureFretboardSweep.py build-dsp/AcustraPerformanceRenderer \
      out/sweep --preset 0
  python3 Tools/MeasureFretboardSweep.py build-dsp/AcustraPerformanceRenderer \
      out/sweep --preset all --json out/sweep/report.json
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.signal import stft

RATE = 48000
NOTE_SECONDS = 2.0
HOLD_SECONDS = 1.2
FRETS = range(0, 21)
NAMES = ("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

OPEN_NOTES = {
    "standard": (40, 45, 50, 55, 59, 64),
    "drop_d": (38, 45, 50, 55, 59, 64),
    "dadgad": (38, 45, 50, 55, 57, 62),
    "open_g": (38, 43, 50, 55, 59, 62),
}

# The factory bank (Source/DSP/AcustraPresets.h) as renderer options; the
# Room and Output controls are left out on purpose.
PRESETS = [
    ("dreadnought-steel", "stereo_mic", "finger", "standard", "dreadnought", "spruce", 0.58, 0.28, 0.0),
    ("parlor-steel-fingerstyle", "stereo_mic", "finger", "standard", "parlor", "mahogany", 0.46, 0.22, 0.0),
    ("mahogany-steel-thumb", "stereo_mic", "thumb", "standard", "auditorium", "mahogany", 0.38, 0.38, 0.0),
    ("jumbo-steel-pick", "stereo_mic", "pick", "standard", "jumbo", "spruce", 0.72, 0.18, 0.0),
    ("palm-muted-steel-pick", "stereo_mic", "pick", "standard", "dreadnought", "mahogany", 0.70, 0.16, 0.0),
    ("dadgad-mono-mic", "mono_mic", "finger", "dadgad", "dreadnought", "mahogany", 0.50, 0.32, 0.0),
    ("open-g-slide-ready", "stereo_mic", "finger", "open_g", "auditorium", "mahogany", 0.55, 0.28, 0.0),
    ("piezo-stage-steel", "piezo", "pick", "standard", "dreadnought", "spruce", 0.62, 0.24, 0.0),
]


def note_name(midi: int) -> str:
    return NAMES[midi % 12] + str(midi // 12 - 1)


def db(power: float) -> float:
    return float(10 * np.log10(max(float(power), 1e-30)))


def preset_options(preset: tuple, calibration: str | None) -> list[str]:
    _, capture, technique, tuning, shape, wood, touch, pluck, piezo_mix = preset
    # Every contact at its reference pitch: the fretting finger's own draw
    # (frettingPitchCents, +-3 cents fretted, +-1 open) is the player, and
    # this measures the instrument.
    options = [capture, technique, "--tuning", tuning, "--body-shape", shape,
               "--body-material", wood, "--touch", f"{touch}",
               "--pluck-position", f"{pluck}", "--exact-intonation", "1"]
    if piezo_mix:
        options += ["--piezo-mix", f"{piezo_mix}"]
    if calibration:
        options += ["--calibration", calibration]
    return options


def write_score(path: Path, string: int, midi: int, velocity: int) -> None:
    path.write_text(
        f"ACUSTRA_PERFORMANCE_V1 {RATE} {round(NOTE_SECONDS * RATE)}\n"
        f"0 {string} {midi} {velocity} 0\n"
        f"{round(HOLD_SECONDS * RATE)} {string} {midi} 0 0\n")


def render(renderer: str, score: Path, raw: Path, options: list[str]) -> np.ndarray:
    if not raw.exists():
        subprocess.run([renderer, str(score), str(raw), *options], check=True,
                       capture_output=True, text=True)
    audio = np.fromfile(raw, dtype="<f4").reshape(-1, 2).astype(np.float64)
    if len(audio) != round(NOTE_SECONDS * RATE) or not np.isfinite(audio).all():
        raise ValueError(f"non-finite or incorrectly sized render: {raw}")
    return audio


def seg(signal: np.ndarray, start: float, end: float) -> np.ndarray:
    return signal[round(start * RATE):round(end * RATE)]


def centroid(x: np.ndarray) -> float:
    window = np.hanning(len(x))
    spectrum = np.abs(np.fft.rfft(x * window)) ** 2
    frequencies = np.fft.rfftfreq(len(x), 1 / RATE)
    keep = (frequencies >= 40) & (frequencies <= 12000)
    power = spectrum[keep]
    return float((frequencies[keep] * power).sum() / max(power.sum(), 1e-30))


def band_t60(mono: np.ndarray, low: float, high: float, start: float,
             end: float) -> float:
    """T60 from a line through the log energy of a band over a window."""
    frequencies, times, z = stft(mono, fs=RATE, nperseg=4096, noverlap=3072,
                                 window="hann", boundary=None, padded=False)
    band = (frequencies >= low) & (frequencies <= high)
    energy = (np.abs(z[band]) ** 2).sum(axis=0)
    keep = (times >= start) & (times <= end) & (energy > 1e-20)
    if keep.sum() < 4:
        return float("nan")
    slope, _ = np.polyfit(times[keep], 10 * np.log10(energy[keep]), 1)
    if slope >= -1e-3:
        return float("inf")
    return float(-60.0 / slope)


def partial_levels(mono: np.ndarray, f0: float, start: float, end: float,
                   count: int) -> tuple[np.ndarray, np.ndarray]:
    """Energy at each partial and half-way between partials."""
    x = seg(mono, start, end)
    window = np.hanning(len(x))
    spectrum = np.abs(np.fft.rfft(x * window)) ** 2
    frequencies = np.fft.rfftfreq(len(x), 1 / RATE)
    step = frequencies[1]

    def around(centre: float, relative: float) -> float:
        width = max(2 * step, centre * relative)
        return float(spectrum[(frequencies >= centre - width)
                              & (frequencies <= centre + width)].sum())

    partials = np.array([around(f0 * n, 0.02) for n in range(1, count + 1)])
    # Between n and n+1, a band a third of the gap wide around the midpoint.
    between = np.array([float(spectrum[(frequencies >= f0 * (n + 0.5) - f0 / 6)
                                       & (frequencies <= f0 * (n + 0.5) + f0 / 6)].sum())
                        for n in range(1, count + 1)])
    return partials, between


def peak_frequency(x: np.ndarray, low: float, high: float) -> float:
    """Frequency of the strongest spectral peak in [low, high], Hz."""
    window = np.hanning(len(x))
    size = 8 * len(x)
    spectrum = np.abs(np.fft.rfft(x * window, size))
    frequencies = np.fft.rfftfreq(size, 1 / RATE)
    band = np.flatnonzero((frequencies >= low) & (frequencies <= high))
    if len(band) < 3:
        return float("nan")
    k = band[np.argmax(spectrum[band])]
    if k == 0 or k + 1 >= len(spectrum):
        return float(frequencies[k])
    a, b, c = np.log(spectrum[k - 1:k + 2] + 1e-30)
    curvature = a - 2 * b + c
    shift = 0.5 * (a - c) / curvature if curvature < -1e-12 else 0.0
    return float((k + shift) * frequencies[1])


def pitch_cents(mono: np.ndarray, f0: float, start: float, end: float) -> float:
    measured = peak_frequency(seg(mono, start, end), f0 * 0.92, f0 * 1.08)
    return float(1200 * np.log2(measured / f0)) if np.isfinite(measured) else float("nan")


def inharmonicity(mono: np.ndarray, f0: float, start: float, end: float,
                  count: int = 6) -> float:
    """B from identifiable partials; NaN is preferable to a wrong mode label.

    Each search is bounded by non-overlapping nominal harmonic cells. The
    former increasing upper limit for H6 included H7: a strong seventh could
    therefore be fitted as the sixth and fabricate a large stiffness B.
    Very stiff spectra can leave these conservative cells. They require a
    separate tracked-modal analysis, rather than relabelling a nearby peak.
    The maximum residual guard also rejects spectra that these six peaks
    cannot describe with one stiff-string law (e.g. unresolved coupled modes).
    """
    x = seg(mono, start, end)
    size = 8 * len(x)
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x)), size))
    frequencies = np.fft.rfftfreq(size, 1 / RATE)
    n = np.arange(1, count + 1)
    peaks = []
    for k in n:
        low = max(k * 0.97, k - 0.45) * f0
        high = min(k * (1.03 + 0.004 * k * k), k + 0.45) * f0
        band = np.flatnonzero((frequencies >= low) & (frequencies <= high))
        if len(band) < 3:
            return float("nan")
        peak = band[np.argmax(spectrum[band])]
        if (peak == band[0] or peak == band[-1]
                or spectrum[peak] < 1.0e-4 * np.max(spectrum)):
            return float("nan")
        a, b, c = np.log(spectrum[peak - 1:peak + 2] + 1e-30)
        curvature = a - 2 * b + c
        if curvature >= -1e-12:
            return float("nan")
        shift = 0.5 * (a - c) / curvature
        peaks.append((peak + shift) * frequencies[1])
    peaks = np.asarray(peaks)
    if not np.isfinite(peaks).all() or f0 * count > 12000:
        return float("nan")
    # (f_n / n)^2 = f_ideal^2 (1 + B n^2): a line in n^2.
    slope, intercept = np.polyfit(n ** 2, (peaks / n) ** 2, 1)
    if intercept <= 0 or np.any(intercept + slope * n ** 2 <= 0):
        return float("nan")
    fitted = n * np.sqrt(intercept + slope * n ** 2)
    if np.max(np.abs(peaks - fitted)) > 0.04 * f0:
        return float("nan")
    return float(slope / intercept)


def note_metrics(audio: np.ndarray, midi: int) -> dict:
    mono = audio.mean(axis=1)
    f0 = 440.0 * 2 ** ((midi - 69) / 12)
    attack = seg(mono, 0.005, 0.08)
    sustain = seg(mono, 0.08, 1.0)
    before = seg(mono, HOLD_SECONDS - 0.1, HOLD_SECONDS)
    after = seg(mono, HOLD_SECONDS, HOLD_SECONDS + 0.1)
    tail = seg(mono, HOLD_SECONDS + 0.5, HOLD_SECONDS + 0.7)
    partials, between = partial_levels(mono, f0, 0.08, 0.4, 3)
    floor_partials, floor_between = partial_levels(mono, f0, 0.05, 0.3, 12)
    usable = (np.arange(1, 13) + 0.5) * f0 < 12000
    return {
        "midi": midi, "name": note_name(midi), "f0_hz": f0,
        "peak_dbfs": db(np.max(np.abs(mono)) ** 2),
        "attack_dbfs": db(np.mean(attack ** 2)),
        "sustain_dbfs": db(np.mean(sustain ** 2)),
        # Each channel alone: a hole in the mono sum that neither channel
        # has is the pair's spacing, not the body's.
        "sustain_left_dbfs": db(np.mean(seg(audio[:, 0], 0.08, 1.0) ** 2)),
        "sustain_right_dbfs": db(np.mean(seg(audio[:, 1], 0.08, 1.0) ** 2)),
        "attack_centroid_hz": centroid(attack),
        "sustain_centroid_hz": centroid(seg(mono, 0.1, 0.6)),
        "t60_fundamental_s": band_t60(mono, f0 * 0.94, f0 * 1.06, 0.1, 1.1),
        "t60_high_s": band_t60(mono, 2000, 6000, 0.03, 0.5),
        "h2_db": db(partials[1] / max(partials[0], 1e-30)),
        "h3_db": db(partials[2] / max(partials[0], 1e-30)),
        "floor_db": db(floor_between[usable].sum() / max(floor_partials[usable].sum(), 1e-30)),
        "release_db": db(np.max(np.abs(after)) ** 2) - db(np.mean(before ** 2)),
        "tail_db": db(np.mean(tail ** 2)) - db(np.mean(before ** 2)),
        "attack_cents": pitch_cents(mono, f0, 0.03, 0.15),
        "settled_cents": pitch_cents(mono, f0, 0.3, 0.9),
        "inharmonicity_b": inharmonicity(mono, f0, 0.1, 0.6),
    }


METRICS = ("peak_dbfs", "attack_dbfs", "sustain_dbfs", "sustain_left_dbfs",
           "sustain_right_dbfs", "attack_centroid_hz",
           "sustain_centroid_hz", "t60_fundamental_s", "t60_high_s", "h2_db",
           "h3_db", "floor_db", "release_db", "tail_db", "attack_cents",
           "settled_cents", "inharmonicity_b")
LOG_METRICS = ("attack_centroid_hz", "sustain_centroid_hz", "t60_fundamental_s",
               "t60_high_s", "inharmonicity_b")


def deviations(rows: list[dict], reach: int = 2) -> None:
    """Each metric against the mean over frets +/-reach on the same string."""
    by_string: dict[int, list[dict]] = {}
    for row in rows:
        by_string.setdefault(row["string"], []).append(row)
    for string_rows in by_string.values():
        string_rows.sort(key=lambda r: r["fret"])
        for metric in METRICS:
            values = np.array([r[metric] for r in string_rows], dtype=float)
            if metric in LOG_METRICS:
                values = 20 * np.log10(np.clip(values, 1e-3, 1e6)) if "hz" in metric \
                    else 10 * np.log10(np.clip(values, 1e-7, 1e3))
            for index, row in enumerate(string_rows):
                neighbours = [other for other in range(max(0, index - reach),
                                                       min(len(values), index + reach + 1))
                              if other != index and np.isfinite(values[other])]
                row[f"{metric}_dev"] = (float(values[index] - values[neighbours].mean())
                                        if neighbours and np.isfinite(values[index])
                                        else float("nan"))


OUTLIER_LIMITS = {"peak_dbfs": 3.0, "attack_dbfs": 3.0, "sustain_dbfs": 3.0,
                  "sustain_left_dbfs": 3.0, "sustain_right_dbfs": 3.0,
                  "attack_centroid_hz": 20 * np.log10(1.35),
                  "sustain_centroid_hz": 20 * np.log10(1.35),
                  "t60_fundamental_s": 10 * np.log10(1.4),
                  "t60_high_s": 10 * np.log10(1.6),
                  "h2_db": 6.0, "h3_db": 6.0, "floor_db": 6.0,
                  "release_db": 6.0, "tail_db": 6.0,
                  "attack_cents": 8.0, "settled_cents": 4.0,
                  # B doubles every six frets on one string (1/L^2), so the
                  # neighbourhood mean is biased; a 3x step is still a hole.
                  "inharmonicity_b": 10 * np.log10(3.0)}
# Outright limits, against the nominal rather than the neighbours.
ABSOLUTE_LIMITS = {"settled_cents": 6.0}


def outliers(rows: list[dict]) -> list[dict]:
    found = []
    for row in rows:
        for metric, limit in OUTLIER_LIMITS.items():
            dev = row.get(f"{metric}_dev", float("nan"))
            if np.isfinite(dev) and abs(dev) > limit:
                found.append({"string": row["string"], "fret": row["fret"],
                              "name": row["name"], "velocity": row["velocity"],
                              "metric": metric, "value": row[metric], "dev": dev})
        for metric, limit in ABSOLUTE_LIMITS.items():
            value = row.get(metric, float("nan"))
            if np.isfinite(value) and abs(value) > limit:
                found.append({"string": row["string"], "fret": row["fret"],
                              "name": row["name"], "velocity": row["velocity"],
                              "metric": metric + "_abs", "value": value, "dev": value})
    return found


def same_pitch_steps(rows: list[dict]) -> list[dict]:
    """Level and centroid of the same pitch on adjacent strings."""
    by_pitch: dict[tuple[int, int], list[dict]] = {}
    for row in rows:
        by_pitch.setdefault((row["midi"], row["velocity"]), []).append(row)
    steps = []
    for rows_at_pitch in by_pitch.values():
        rows_at_pitch.sort(key=lambda r: r["string"])
        for lower, upper in zip(rows_at_pitch, rows_at_pitch[1:]):
            if upper["string"] != lower["string"] + 1:
                continue
            steps.append({"midi": lower["midi"], "name": lower["name"],
                          "velocity": lower["velocity"],
                          "strings": (lower["string"], upper["string"]),
                          "frets": (lower["fret"], upper["fret"]),
                          "sustain_step_db": upper["sustain_dbfs"] - lower["sustain_dbfs"],
                          "attack_step_db": upper["attack_dbfs"] - lower["attack_dbfs"],
                          "centroid_ratio_db": 20 * np.log10(
                              max(upper["sustain_centroid_hz"], 1.0)
                              / max(lower["sustain_centroid_hz"], 1.0)),
                          "t60_ratio_db": 10 * np.log10(
                              min(max(upper["t60_fundamental_s"], 1e-3), 1e3)
                              / min(max(lower["t60_fundamental_s"], 1e-3), 1e3))})
    return steps


def sweep(renderer: str, output: Path, preset: tuple, velocities: list[int],
          calibration: str | None, workers: int) -> dict:
    slug = preset[0]
    options = preset_options(preset, calibration)
    open_notes = OPEN_NOTES[preset[3]]
    directory = output / slug
    directory.mkdir(parents=True, exist_ok=True)
    jobs = []
    for velocity in velocities:
        for string in range(1, 7):
            for fret in FRETS:
                midi = open_notes[string - 1] + fret
                stem = directory / f"s{string}-f{fret:02d}-v{velocity}"
                jobs.append((string, fret, midi, velocity, stem))

    def run(job):
        string, fret, midi, velocity, stem = job
        score = stem.with_suffix(".events")
        if not score.exists():
            write_score(score, string, midi, velocity)
        mono = render(renderer, score, stem.with_suffix(".f32"), options)
        row = note_metrics(mono, midi)
        row.update({"string": string, "fret": fret, "velocity": velocity})
        return row

    with ThreadPoolExecutor(max_workers=workers) as pool:
        rows = list(pool.map(run, jobs))
    for velocity in velocities:
        deviations([r for r in rows if r["velocity"] == velocity])
    return {"preset": slug, "options": options, "velocities": velocities,
            "notes": rows, "outliers": outliers(rows),
            "same_pitch": same_pitch_steps(rows)}


def print_report(report: dict) -> None:
    print(f"== {report['preset']}")
    for velocity in report["velocities"]:
        print(f"-- velocity {velocity}")
        print(f"{'str':>3} {'fret':>4} {'note':>5} {'peak':>6} {'att':>6} {'sus':>6} "
              f"{'sus_l':>6} {'sus_r':>6} {'cent_a':>7} {'cent_s':>7} {'t60_f':>6} {'t60_hi':>6} {'h2':>6} "
              f"{'h3':>6} {'floor':>6} {'rel':>6} {'tail':>6} {'att_c':>6} {'set_c':>6} {'B':>8}")
        for row in sorted((r for r in report["notes"] if r["velocity"] == velocity),
                          key=lambda r: (r["string"], r["fret"])):
            print(f"{row['string']:>3} {row['fret']:>4} {row['name']:>5} "
                  f"{row['peak_dbfs']:6.1f} {row['attack_dbfs']:6.1f} "
                  f"{row['sustain_dbfs']:6.1f} {row['sustain_left_dbfs']:6.1f} "
                  f"{row['sustain_right_dbfs']:6.1f} {row['attack_centroid_hz']:7.0f} "
                  f"{row['sustain_centroid_hz']:7.0f} {row['t60_fundamental_s']:6.2f} "
                  f"{row['t60_high_s']:6.2f} {row['h2_db']:6.1f} {row['h3_db']:6.1f} "
                  f"{row['floor_db']:6.1f} {row['release_db']:6.1f} {row['tail_db']:6.1f} "
                  f"{row['attack_cents']:6.1f} {row['settled_cents']:6.1f} "
                  f"{row['inharmonicity_b']:8.2e}")
    print("-- outliers against frets +/-2 on the same string")
    for item in sorted(report["outliers"], key=lambda o: -abs(o["dev"])):
        print(f"  s{item['string']} f{item['fret']:02d} {item['name']:>4} v{item['velocity']:>3} "
              f"{item['metric']:<20} {item['value']:9.2f} dev {item['dev']:+6.1f}")
    print("-- same pitch on adjacent strings, steps beyond 3 dB or 35 %")
    for step in report["same_pitch"]:
        if (abs(step["sustain_step_db"]) > 3 or abs(step["attack_step_db"]) > 3
                or abs(step["centroid_ratio_db"]) > 20 * np.log10(1.35)):
            print(f"  {step['name']:>4} v{step['velocity']:>3} s{step['strings'][0]}f{step['frets'][0]:02d}"
                  f" -> s{step['strings'][1]}f{step['frets'][1]:02d}: sus {step['sustain_step_db']:+5.1f}"
                  f" att {step['attack_step_db']:+5.1f} cent {step['centroid_ratio_db']:+5.1f}"
                  f" t60 {step['t60_ratio_db']:+5.1f}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("renderer")
    parser.add_argument("output", type=Path)
    parser.add_argument("--preset", default="0",
                        help="factory preset index, slug, or 'all'")
    parser.add_argument("--velocities", default="48,112")
    parser.add_argument("--calibration")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--quiet", action="store_true",
                        help="print outliers and steps only")
    args = parser.parse_args()
    velocities = [int(v) for v in args.velocities.split(",")]
    if args.preset == "all":
        presets = PRESETS
    elif args.preset.isdigit():
        presets = [PRESETS[int(args.preset)]]
    else:
        presets = [p for p in PRESETS if p[0] == args.preset]
        if not presets:
            raise SystemExit(f"unknown preset {args.preset}")
    reports = []
    for preset in presets:
        report = sweep(args.renderer, args.output, preset, velocities,
                       args.calibration, args.workers)
        reports.append(report)
        if args.quiet:
            print(f"== {report['preset']}")
            full = {"preset": report["preset"], "velocities": [], "notes": [],
                    "outliers": report["outliers"], "same_pitch": report["same_pitch"]}
            print_report(full)
        else:
            print_report(report)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(reports, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
