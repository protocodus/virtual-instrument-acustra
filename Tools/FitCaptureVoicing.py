#!/usr/bin/env python3
"""Fit the Original guitar's capture voicing to real flat-top recordings.

The Original's radiation bank is g21's two close microphones, morphed to the
steel-string Shapes. Real steel-string flat-tops are recorded from further
away, and every open recording here hears a different balance from them. This
tool measures that difference and fits it as one smooth gain
(Source/DSP/CaptureVoicingData.h), which the engine applies as a causal
filter to the summed microphone pressure, preserving modal cancellation.

Protocol (deterministic; no audio is committed or downloaded):

1. Sources, each a render of the build under test beside its recordings:
   - ``--bank DIR``: an AcustraPhysicalFitRenderer output; its ``flattop.json``
     (the reference bank's eight finger-plucked flat-top notes) is read.
   - ``--open DIR``: a Tools/BenchmarkOpenCorpora.py ``--keep`` output over the
     prepared Eastman E1D (Tools/PrepareEastmanCorpus.py) and Martin HD28
     (Tools/PrepareMartinCorpus.py) rows: ``eastman.flattop-pick.json``,
     ``eastman.flattop-finger-all.json`` and ``martin-hd28.martin-hd28.json``.
   - ``--guitarset DIR``: a Tools/BenchmarkPerformances.py output (its
     ``*.f32`` renders and ``*-reference.wav`` clips).
2. Spectra. For a note manifest, each note's onset is found with
   FitPhysicalModel's rule; its 0.03-1.0 s spectrum (Hann window, mono sum)
   is summed into third octaves from 25 Hz to 20 kHz and normalised to unit
   total, and the notes are summed: an equal-loudness note sum, recording
   and model alike. For GuitarSet each whole 12 s clip is one such spectrum.
   A source's difference is recording minus model, in dB per band.
3. Consensus. Each source's difference has its weighted mean removed (a
   level is not a balance); the Eastman's picked and finger-plucked takes
   count half each, one guitar, so the Eastman, the Martin, the bank's
   flat-top and GuitarSet weigh equally. Bands 90 Hz-6 kHz weigh 1, the rest
   of 70 Hz-11 kHz 0.35; outside it nothing is fitted (the recordings' rumble
   below and hiss above are not the guitar's).
4. Fit. The renders already carry the voicing the build has, read from
   CaptureVoicingData.h, so the target is that voicing plus the consensus.
   The section structure is fixed - a low shelf at 120 Hz (Q 0.7) and peaks
   at 125, 250, 500, 1000 and 1400 Hz (Q 1.2) - and only the gains are
   fitted, bounded to +-6 dB, by least squares on each band's
   mean analog magnitude with a ridge of 0.25 per dB, the level removed as
   in step 3. ``--write-header`` writes the gains; the header's level is
   kept unless ``--level-db`` gives a new one
   (Tools/CalibrateConstructionLoudness.py --json measures the default
   construction's loudness before and after).

Usage:

    python3 Tools/FitCaptureVoicing.py --bank B --open O --guitarset G \\
        [--json report.json] [--write-header] [--level-db DB]
    python3 Tools/FitCaptureVoicing.py --self-test

Rerun after a change that moves the Original's radiation: render the three
sources with the new build, fit, rebuild, and repeat until the gains settle
(they move by under 0.1 dB from the second pass).
"""

from __future__ import annotations

import argparse
import json
import math
import re
import sys
import tempfile
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.optimize import least_squares
from scipy.signal import resample_poly

sys.path.insert(0, str(Path(__file__).resolve().parent))
import FitPhysicalModel as scorer  # noqa: E402  (the onset rule)

RATE = 48_000
HEADER = (Path(__file__).resolve().parent.parent
          / "Source/DSP/CaptureVoicingData.h")
CENTRES = 1000.0 * 2.0 ** (np.arange(-16, 14) / 3.0)
FIT_LOW, FIT_HIGH = 70.0, 11_000.0
FULL_LOW, FULL_HIGH = 90.0, 6_000.0
EDGE_WEIGHT = 0.35
RIDGE = 0.25
BOUND = 6.0
NOTE_WINDOW = (0.03, 1.0)
# (kind, frequency, Q): the fixed structure; only the gains are fitted.
STRUCTURE = (
    ("LowShelf", 120.0, 0.7),
    ("Peak", 125.0, 1.2),
    ("Peak", 250.0, 1.2),
    ("Peak", 500.0, 1.2),
    ("Peak", 1000.0, 1.2),
    ("Peak", 1400.0, 1.2),
)
NOTE_SOURCES = (
    ("eastman-pick", "open", "eastman.flattop-pick.json", 0.5),
    ("eastman-finger", "open", "eastman.flattop-finger-all.json", 0.5),
    ("martin-hd28", "open", "martin-hd28.martin-hd28.json", 1.0),
    ("bank-flattop", "bank", "flattop.json", 1.0),
)


def section_power(kind: str, frequency_hz: float, gain_db: float, q: float,
                  frequency: np.ndarray) -> np.ndarray:
    """|H|^2 of an RBJ analog prototype; the engine's captureVoicingGain."""
    a = 10.0 ** (gain_db / 40.0)
    w = np.asarray(frequency, dtype=np.float64) / frequency_hz
    w2 = w * w
    if kind == "Peak":
        edge = (1.0 - w2) ** 2
        return (edge + (w * a / q) ** 2) / (edge + (w / (a * q)) ** 2)
    slope = math.sqrt(a) * w / q
    low_edge = (a - w2) ** 2
    high_edge = (1.0 - a * w2) ** 2
    if kind == "LowShelf":
        return a * a * (low_edge + slope ** 2) / (high_edge + slope ** 2)
    if kind == "HighShelf":
        return a * a * (high_edge + slope ** 2) / (low_edge + slope ** 2)
    raise ValueError(f"unknown section kind {kind}")


def voicing_db(sections: list[tuple[str, float, float, float]],
               frequency: np.ndarray) -> np.ndarray:
    power = np.ones_like(np.asarray(frequency, dtype=np.float64))
    for kind, f0, gain, q in sections:
        power = power * section_power(kind, f0, gain, q, frequency)
    return 10.0 * np.log10(power)


def band_voicing_db(sections) -> np.ndarray:
    """Each third octave's mean power gain, in dB."""
    result = []
    for centre in CENTRES:
        grid = np.geomspace(centre * 2 ** (-1 / 6), centre * 2 ** (1 / 6), 33)
        result.append(10.0 * math.log10(float(np.mean(
            10.0 ** (voicing_db(sections, grid) / 10.0)))))
    return np.asarray(result)


def read_header(path: Path | None = None) -> tuple[list, float]:
    path = HEADER if path is None else path
    text = path.read_text(encoding="utf-8")
    sections = [(kind, float(f), float(g), float(q)) for kind, f, g, q in re.findall(
        r"CaptureVoicingKind::(\w+),\s*([-0-9.e]+)f,\s*([-0-9.e]+)f,\s*([-0-9.e]+)f", text)]
    level = re.search(r"captureVoicingLevelDb\s*=\s*([-0-9.e]+)f", text)
    if not sections or level is None:
        raise ValueError(f"{path}: no capture voicing table")
    return sections, float(level.group(1))


def _read(spec: dict, base: Path) -> np.ndarray:
    path = Path(spec["path"])
    if not path.is_absolute():
        path = base / path
    channels = int(spec.get("channels", 2))
    audio = np.fromfile(path, dtype="<f4").astype(np.float64).reshape(-1, channels)
    rate = int(spec.get("sample_rate", RATE))
    if rate != RATE:
        divisor = math.gcd(RATE, rate)
        audio = resample_poly(audio, RATE // divisor, rate // divisor, axis=0)
    return audio.mean(axis=1)


def _bands(signal: np.ndarray) -> np.ndarray:
    window = np.hanning(signal.size)
    size = 1 << max(16, int(math.ceil(math.log2(signal.size))))
    power = np.abs(np.fft.rfft(signal * window, size)) ** 2
    frequency = np.fft.rfftfreq(size, 1.0 / RATE)
    bands = np.array([power[(frequency >= c * 2 ** (-1 / 6))
                            & (frequency < c * 2 ** (1 / 6))].sum() for c in CENTRES])
    return bands / max(float(bands.sum()), 1e-30) + 1e-30


def note_difference(manifest: Path) -> tuple[np.ndarray, int]:
    document = json.loads(manifest.read_text(encoding="utf-8"))
    base = manifest.parent
    totals = [np.zeros(CENTRES.size), np.zeros(CENTRES.size)]
    count = 0
    for example in document["examples"]:
        for index, key in enumerate(("target", "model")):
            signal = _read(example[key], base)
            onset = scorer._onset(signal, RATE)
            begin = onset + round(NOTE_WINDOW[0] * RATE)
            end = min(signal.size, onset + round(NOTE_WINDOW[1] * RATE))
            if end - begin < RATE // 4:
                raise ValueError(f"{example['id']}: under 0.25 s after its onset")
            totals[index] += _bands(signal[begin:end])
        count += 1
    if count == 0:
        raise ValueError(f"{manifest}: no examples")
    return 10.0 * np.log10(totals[0] / totals[1]), count


def guitarset_difference(directory: Path) -> tuple[np.ndarray, int]:
    totals = [np.zeros(CENTRES.size), np.zeros(CENTRES.size)]
    clips = sorted(directory.glob("*-reference.wav"))
    for reference in clips:
        track = reference.name[: -len("-reference.wav")]
        rate, target = wavfile.read(reference)
        if rate != RATE:
            raise ValueError(f"{reference}: expected {RATE} Hz")
        model = np.fromfile(directory / f"{track}.f32", dtype="<f4")
        model = model.astype(np.float64).reshape(-1, 2).mean(axis=1)
        totals[0] += _bands(np.asarray(target, dtype=np.float64))
        totals[1] += _bands(model)
    if not clips:
        raise ValueError(f"{directory}: no GuitarSet clips")
    return 10.0 * np.log10(totals[0] / totals[1]), len(clips)


def _weights() -> tuple[np.ndarray, np.ndarray]:
    kept = (CENTRES >= FIT_LOW) & (CENTRES <= FIT_HIGH)
    weight = np.where((CENTRES >= FULL_LOW) & (CENTRES <= FULL_HIGH), 1.0, EDGE_WEIGHT)
    return kept, weight


def consensus(differences: dict[str, tuple[np.ndarray, float]]) -> np.ndarray:
    kept, weight = _weights()
    total = np.zeros(CENTRES.size)
    weights = 0.0
    for difference, source_weight in differences.values():
        centred = difference - np.average(difference[kept], weights=weight[kept])
        total += source_weight * centred
        weights += source_weight
    return total / weights


def fit(target_db: np.ndarray) -> tuple[list, np.ndarray]:
    kept, weight = _weights()

    def sections(gains: np.ndarray) -> list:
        return [(kind, f0, float(gain), q)
                for (kind, f0, q), gain in zip(STRUCTURE, gains)]

    def residual(gains: np.ndarray) -> np.ndarray:
        difference = (band_voicing_db(sections(gains)) - target_db)[kept]
        difference = difference - np.average(difference, weights=weight[kept])
        return np.concatenate([weight[kept] * difference, RIDGE * gains])

    solution = least_squares(residual, np.zeros(len(STRUCTURE)),
                             bounds=(-BOUND, BOUND))
    return sections(solution.x), band_voicing_db(sections(solution.x))


def write_header(sections: list, level_db: float, path: Path | None = None) -> None:
    path = HEADER if path is None else path
    text = path.read_text(encoding="utf-8")
    rows = "".join(
        f"    {{ CaptureVoicingKind::{kind}, {f0:.1f}f, {gain:.2f}f, {q:.1f}f }},\n"
        for kind, f0, gain, q in sections)
    text, count = re.subn(
        r"(inline constexpr CaptureVoicingSection captureVoicingSections\[\] \{\n).*?(\};)",
        lambda match: match.group(1) + rows + match.group(2), text, flags=re.S)
    text, levels = re.subn(r"(captureVoicingLevelDb\s*=\s*)[-0-9.e]+f",
                           lambda match: f"{match.group(1)}{level_db:.2f}f", text)
    if count != 1 or levels != 1:
        raise ValueError(f"{path}: the table or level was not found once")
    path.write_text(text, encoding="utf-8")


def run(bank: Path, open_dir: Path, guitarset: Path) -> dict:
    current, level = read_header()
    differences: dict[str, tuple[np.ndarray, float]] = {}
    counts = {}
    for name, where, manifest, weight in NOTE_SOURCES:
        directory = open_dir if where == "open" else bank
        differences[name], counts[name] = note_difference(directory / manifest)
        differences[name] = (differences[name], weight)
    differences["guitarset"], counts["guitarset"] = guitarset_difference(guitarset)
    differences["guitarset"] = (differences["guitarset"], 1.0)
    wanted = consensus(differences)
    target = band_voicing_db(current) + wanted
    sections, fitted = fit(target)
    kept, weight = _weights()
    centred = lambda values: values - np.average(values[kept], weights=weight[kept])  # noqa: E731
    return {
        "tool": "Tools/FitCaptureVoicing.py",
        "header_before": {"sections": current, "level_db": level},
        "sections": sections,
        "notes": counts,
        "band_centres_hz": CENTRES.tolist(),
        "fitted_bands": kept.tolist(),
        "source_residual_db": {name: centred(value[0]).tolist()
                               for name, value in differences.items()},
        "consensus_residual_db": wanted.tolist(),
        "target_voicing_db": centred(target).tolist(),
        "fitted_voicing_db": centred(fitted).tolist(),
        "residual_rms_db": float(np.sqrt(np.average(
            (centred(fitted) - centred(target))[kept] ** 2, weights=weight[kept]))),
    }


def self_test() -> None:
    """A known voicing applied to a synthetic 'recording' is recovered."""
    rng = np.random.default_rng(20261001)
    truth = [(kind, f0, gain, q) for (kind, f0, q), gain in zip(
        STRUCTURE, (1.0, 3.5, -2.0, -4.5, 2.5, 4.0))]
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        for name in ("open", "bank", "guitarset"):
            (root / name).mkdir()
        size = RATE * 2
        frequency = np.fft.rfftfreq(size, 1.0 / RATE)
        gain = np.sqrt(10.0 ** (voicing_db(truth, np.maximum(frequency, 1.0)) / 10.0))

        def pair() -> tuple[np.ndarray, np.ndarray]:
            model = rng.standard_normal(size) * np.exp(-np.arange(size) / RATE)
            model[: RATE // 50] = 0.0
            model[RATE // 50] = 4.0
            recording = np.fft.irfft(np.fft.rfft(model) * gain, size)
            return recording, model

        for name, where, manifest, _ in NOTE_SOURCES:
            examples = []
            for index in range(3):
                recording, model = pair()
                stem = f"{name}-{index}"
                for key, audio in (("target", recording), ("model", model)):
                    np.stack([audio, audio], axis=1).astype("<f4").tofile(
                        root / where / f"{stem}-{key}.f32")
                examples.append({"id": stem, "midi": 52,
                                 "target": {"path": f"{stem}-target.f32", "channels": 2},
                                 "model": {"path": f"{stem}-model.f32", "channels": 2}})
            (root / where / manifest).write_text(json.dumps({"examples": examples}))
        recording, model = pair()
        wavfile.write(root / "guitarset" / "00_test-reference.wav", RATE,
                      recording.astype(np.float32))
        np.stack([model, model], axis=1).astype("<f4").tofile(
            root / "guitarset" / "00_test.f32")
        header = root / "CaptureVoicingData.h"
        header.write_text(
            "inline constexpr CaptureVoicingSection captureVoicingSections[] {\n"
            + "".join(f"    {{ CaptureVoicingKind::{kind}, {f0}f, 0.0f, {q}f }},\n"
                      for kind, f0, q in STRUCTURE)
            + "};\ninline constexpr float captureVoicingLevelDb = 0.0f;\n")
        global HEADER
        saved, HEADER = HEADER, header
        try:
            report = run(root / "bank", root / "open", root / "guitarset")
            write_header(report["sections"], -1.25, header)
            written, level = read_header(header)
        finally:
            HEADER = saved
    kept, weight = _weights()
    expected = band_voicing_db(truth)
    expected = expected - np.average(expected[kept], weights=weight[kept])
    error = np.abs(np.asarray(report["fitted_voicing_db"]) - expected)[kept]
    assert float(error.max()) < 0.8, f"recovered voicing off by {error.max():.2f} dB"
    assert report["residual_rms_db"] < 0.3, report["residual_rms_db"]
    assert len(written) == len(STRUCTURE) and abs(level + 1.25) < 1e-6
    assert all(abs(a[2] - b[2]) < 0.006 for a, b in zip(written, report["sections"]))
    # The engine's formula: a +6 dB peak reads +6 dB at its centre and a
    # shelf its gain far past its corner.
    peak = voicing_db([("Peak", 500.0, 6.0, 1.2)], np.array([500.0]))[0]
    shelf = voicing_db([("LowShelf", 120.0, 3.0, 0.7)], np.array([5.0, 20_000.0]))
    assert abs(peak - 6.0) < 1e-9 and abs(shelf[0] - 3.0) < 0.01 and abs(shelf[1]) < 0.01
    print(f"self-test passed: worst band {error.max():.2f} dB, "
          f"residual {report['residual_rms_db']:.3f} dB rms")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bank", type=Path)
    parser.add_argument("--open", type=Path, dest="open_dir")
    parser.add_argument("--guitarset", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--write-header", action="store_true")
    parser.add_argument("--level-db", type=float)
    parser.add_argument("--self-test", action="store_true")
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    if not (arguments.bank and arguments.open_dir and arguments.guitarset):
        parser.error("--bank, --open and --guitarset are required")
    report = run(arguments.bank, arguments.open_dir, arguments.guitarset)
    kept, _ = _weights()
    print("  band   target  fitted   " + " ".join(
        f"{name[:9]:>9s}" for name in report["source_residual_db"]))
    for index, centre in enumerate(CENTRES):
        if kept[index]:
            print(f"{centre:7.0f} {report['target_voicing_db'][index]:+7.2f} "
                  f"{report['fitted_voicing_db'][index]:+7.2f}   " + " ".join(
                      f"{values[index]:+9.2f}"
                      for values in report["source_residual_db"].values()))
    print("gains: " + ", ".join(f"{kind} {f0:g} Hz {gain:+.2f} dB"
                                for kind, f0, gain, _ in report["sections"]))
    print(f"fit residual {report['residual_rms_db']:.3f} dB rms")
    if arguments.json:
        arguments.json.write_text(json.dumps(report, indent=1), encoding="utf-8")
    if arguments.write_header:
        _, level = read_header()
        write_header(report["sections"],
                     level if arguments.level_db is None else arguments.level_db)
        print(f"wrote {HEADER}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
