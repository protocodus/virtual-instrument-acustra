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
     ``*.f32`` renders, ``*-reference.wav`` clips and ``report.json``),
     rendered with the Stereo mic as by default.
   - ``--guitarset-mono DIR`` (optional, recommended): the same benchmark
     rendered with ``--capture mono_mic``. Its renders are the GuitarSet Mono
     mic observation, and each must equal the one recovered from the Stereo
     mic render (step 2) up to the Mono capture's level, or the fit stops: the
     recovery's premise checked against the build under test.
2. Observation (``--observation``; the report records name and version).
   ``matched`` (version 2, the default since 2026-10-10) compares each
   recording with the model capture it corresponds to. A two-channel
   recording - the Eastman's takes and the bank's flat-top, one coincident
   pair - is read as per-channel power (each channel's spectrum, the powers
   averaged) against the model's two output channels, at the Width they were
   rendered with. A one-channel recording - GuitarSet's U87, and the Martin,
   a mono file whose capture is undocumented and is read as one microphone -
   is compared with the Mono mic: a mono_mic render as it is, a dry Stereo
   mic render through its upper-bout microphone, which the engine's Width law
   gives back exactly (FitPhysicalModel.mono_mic_from_stereo; the manifests'
   model_controls supply capture, Width and Room). ``mid`` (version 1)
   averaged L and R of recording and model alike, as every fit before
   2026-10-10 did, and reproduces those. The model's microphones are
   near-field omnis 20 cm apart whose average cancels where they are in
   antiphase (-8.5 dB at 1.25 kHz on a strummed chord), so version 1 asked
   the contour to fill a hole that neither output channel has
   (Docs/capture-observation-2026-10-10.md).
3. Spectra. For a note manifest, each note's onset is found with
   FitPhysicalModel's rule (on the observed signal, per-channel energy for a
   pair); its 0.03-1.0 s spectrum (Hann window) is summed into third
   octaves from 25 Hz to 20 kHz and normalised to unit total, and the notes
   are summed: an equal-loudness note sum, recording and model alike. For
   GuitarSet each whole 12 s clip is one such spectrum. A source's
   difference is recording minus model, in dB per band.
4. Consensus. Each source's difference has its weighted mean removed (a
   level is not a balance); the Eastman's picked and finger-plucked takes
   count half each, one guitar, so the Eastman, the Martin, the bank's
   flat-top and GuitarSet weigh equally. (The bank's flat-top is eight notes
   of the same Eastman finger-plucked take, cut differently: the Eastman
   guitar carries half of the consensus.) Bands 90 Hz-6 kHz weigh 1, the rest
   of 70 Hz-11 kHz 0.35; outside it nothing is fitted (the recordings' rumble
   below and hiss above are not the guitar's).
5. Fit. The native renders carry the generated voicing plus the authored
   refinements in MicrophoneBalanceData.h. Read both headers and add the
   current balanced contour to the measured consensus when fitting the
   generated base. The authored refinement remains a separate runtime step;
   otherwise a later refit would subtract and cancel that refinement.
   The section structure is fixed - a low shelf at 120 Hz (Q 0.7) and peaks
   at 125, 250, 500, 1000 and 1400 Hz (Q 1.2) - and only the gains are
   fitted, bounded to +-6 dB, by least squares on each band's
   mean analog magnitude with a ridge of 0.25 per dB, the level removed as
   in step 4. ``--write-header`` writes the gains; the header's level is
   kept unless ``--level-db`` gives a new one
   (Tools/CalibrateConstructionLoudness.py --json measures the default
   construction's loudness before and after).
6. Diagnostics beside the fit (``matched`` only; reported, never fitted):
   the mono-sum retention of every source (the model's L/R average power
   relative to its per-channel power, per band; a pair's recording too); the
   Mono mic relative to the per-channel Stereo output on the same renders
   (level removed); and the fixed structure fitted to every source against
   each capture alone (``capture_contours``: what a Stereo-only and a
   Mono-only contour would be, today one contour serves both).

Usage:

    python3 Tools/FitCaptureVoicing.py --bank B --open O --guitarset G \\
        [--guitarset-mono GM] [--observation matched|mid] \\
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
import FitPhysicalModel as scorer  # noqa: E402  (onset rule and observations)

RATE = 48_000
HEADER = (Path(__file__).resolve().parent.parent
          / "Source/DSP/CaptureVoicingData.h")
BALANCE_HEADER = HEADER.with_name("MicrophoneBalanceData.h")
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
GUITARSET_WEIGHT = 1.0
# The PerformanceRenderer has no Width option: the engine default.
GUITARSET_WIDTH = 0.62
# A direct Mono mic render must match the recovered one to this (relative
# residual energy after the Mono capture's level trim).
MONO_CHECK = 1.0e-10
OBSERVATIONS = scorer.OBSERVATIONS
DEFAULT_OBSERVATION = scorer.DEFAULT_OBSERVATION


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


def read_balance(path: Path | None = None) -> dict[str, float]:
    """Read authored constants; fail instead of guessing missing refinements."""
    path = BALANCE_HEADER if path is None else path
    text = path.read_text(encoding="utf-8")
    adjustments = {}
    for name in ("originalBassShelfAdjustmentDb", "originalAirPeakAdjustmentDb",
                 "originalCaptureLevelAdjustmentDb"):
        matches = re.findall(r"\b" + name + r"\s*=\s*([-+0-9.eE]+)f\s*;", text)
        if len(matches) != 1 or not math.isfinite(float(matches[0])):
            raise ValueError(f"{path}: expected one finite numeric {name}")
        adjustments[name] = float(matches[0])
    return adjustments


def balanced_sections(sections: list, adjustments: dict[str, float]) -> list:
    """The authored gain offsets on the fixed generated section structure."""
    result = []
    matched = {"originalBassShelfAdjustmentDb": 0, "originalAirPeakAdjustmentDb": 0}
    for kind, frequency, gain, q in sections:
        name = ("originalBassShelfAdjustmentDb" if kind == "LowShelf" and frequency == 120.0
                else "originalAirPeakAdjustmentDb" if kind == "Peak" and frequency == 125.0
                else None)
        if name is not None:
            gain += adjustments[name]
            matched[name] += 1
        result.append((kind, frequency, gain, q))
    if any(count != 1 for count in matched.values()):
        raise ValueError("generated capture structure must contain one 120 Hz shelf and 125 Hz peak")
    return result


def _read_channels(spec: dict, base: Path) -> np.ndarray:
    """A file as (frames, channels) at the analysis rate."""
    path = Path(spec["path"])
    if not path.is_absolute():
        path = base / path
    channels = int(spec.get("channels", 2))
    audio = np.fromfile(path, dtype="<f4").astype(np.float64).reshape(-1, channels)
    rate = int(spec.get("sample_rate", RATE))
    if rate != RATE:
        divisor = math.gcd(RATE, rate)
        audio = resample_poly(audio, RATE // divisor, rate // divisor, axis=0)
    return audio


def _read(spec: dict, base: Path) -> np.ndarray:
    """The L/R average (the version 1 observation)."""
    return _read_channels(spec, base).mean(axis=1)


def _band_power(signal: np.ndarray) -> np.ndarray:
    """Third-octave power of a signal, or per-channel power of a pair."""
    window = np.hanning(signal.shape[0])
    size = 1 << max(16, int(math.ceil(math.log2(signal.shape[0]))))
    if signal.ndim == 1:
        power = np.abs(np.fft.rfft(signal * window, size)) ** 2
    else:
        power = np.mean(np.abs(np.fft.rfft(signal * window[:, None], size, axis=0)) ** 2,
                        axis=1)
    frequency = np.fft.rfftfreq(size, 1.0 / RATE)
    return np.array([power[(frequency >= c * 2 ** (-1 / 6))
                           & (frequency < c * 2 ** (1 / 6))].sum() for c in CENTRES])


def _bands(signal: np.ndarray) -> np.ndarray:
    bands = _band_power(signal)
    return bands / max(float(bands.sum()), 1e-30) + 1e-30


def _note_segment(signal: np.ndarray, name: str) -> np.ndarray:
    onset = scorer._onset(signal, RATE)
    begin = onset + round(NOTE_WINDOW[0] * RATE)
    end = min(signal.shape[0], onset + round(NOTE_WINDOW[1] * RATE))
    if end - begin < RATE // 4:
        raise ValueError(f"{name}: under 0.25 s after its onset")
    return signal[begin:end]


def _views(audio: np.ndarray, controls: dict | None, *, recording: bool
           ) -> tuple[dict[str, np.ndarray | None], dict[str, str]]:
    """Every observed signal of one file. A recording: its per-channel power
    ("channels") and L/R average ("mid"). A model render: its per-channel
    output ("stereo"), its Mono mic ("mono") and its L/R average ("mid")."""
    mid = audio.mean(axis=1)  # the version 1 arithmetic, kept exactly
    if recording:
        return {"channels": audio if audio.shape[1] == 2 else audio[:, 0], "mid": mid}, {}
    views: dict[str, np.ndarray | None] = {"mid": mid}
    notes: dict[str, str] = {}
    if audio.shape[1] == 1 or np.array_equal(audio[:, 0], audio[:, 1]):
        # A mono capture: both output channels are its one signal.
        views["stereo"] = views["mono"] = audio[:, 0]
        notes["stereo"] = notes["mono"] = "mono capture"
        return views, notes
    views["stereo"] = audio
    notes["stereo"] = "per-channel power"
    try:
        views["mono"], notes["mono"] = scorer.observe_model(audio, 1, controls)
    except ValueError as error:
        views["mono"], notes["mono"] = None, f"unavailable: {error}"
    return views, notes


class Spectra:
    """One source's equal-loudness band sums in every view, and its
    mono-sum retention (L/R average power over per-channel power)."""

    def __init__(self, name: str, weight: float):
        self.name, self.weight = name, weight
        self.count = 0
        self.recording_channels: set[int] = set()
        self.recording: dict[str, np.ndarray] = {}
        self.model: dict[str, np.ndarray | None] = {}
        self.model_notes: dict[str, set[str]] = {}
        self.retention: dict[str, list[np.ndarray]] = {
            "model_mid": [], "model_stereo": [], "recording_mid": [], "recording_channels": []}

    @staticmethod
    def _add(totals: dict, key: str, value: np.ndarray | None) -> None:
        if value is None or (key in totals and totals[key] is None):
            totals[key] = None
        else:
            totals[key] = totals.get(key, 0.0) + value

    def add(self, recording: np.ndarray, model: np.ndarray, controls: dict | None,
            label: str, *, whole: bool = False, mono_override: np.ndarray | None = None) -> None:
        self.recording_channels.add(recording.shape[1])
        cut = (lambda signal: signal) if whole else (
            lambda signal: _note_segment(signal, label))
        recording_views, _ = _views(recording, None, recording=True)
        for key, signal in recording_views.items():
            self._add(self.recording, key, _bands(cut(signal)))
        model_views, notes = _views(model, controls, recording=False)
        if mono_override is not None:
            model_views["mono"], notes["mono"] = mono_override, "Mono mic render"
        for key, signal in model_views.items():
            self._add(self.model, key, None if signal is None else _bands(cut(signal)))
            self.model_notes.setdefault(key, set()).add(notes.get(key, "L/R average"))
        # Retention: both views of one window, normalised by its per-channel total.
        for prefix, pair in (("model", model_views.get("stereo")),
                             ("recording", recording if recording.shape[1] == 2 else None)):
            if pair is None or pair.ndim != 2:
                continue
            segment = cut(pair)
            per_channel = _band_power(segment)
            total = max(float(per_channel.sum()), 1e-30)
            self.retention[f"{prefix}_mid"].append(_band_power(segment.mean(axis=1)) / total)
            self.retention[f"{prefix}_{'stereo' if prefix == 'model' else 'channels'}"].append(
                per_channel / total)
        self.count += 1

    def difference(self, recording_view: str, model_view: str) -> np.ndarray | None:
        model = self.model.get(model_view)
        if model is None:
            return None
        return 10.0 * np.log10(self.recording[recording_view] / model)

    def retention_db(self, prefix: str) -> np.ndarray | None:
        other = "stereo" if prefix == "model" else "channels"
        if not self.retention[f"{prefix}_mid"]:
            return None
        return 10.0 * np.log10(np.sum(self.retention[f"{prefix}_mid"], axis=0)
                               / np.maximum(np.sum(self.retention[f"{prefix}_{other}"], axis=0),
                                            1e-30) + 1e-30)


def note_spectra(manifest: Path, name: str, weight: float) -> Spectra:
    document = json.loads(manifest.read_text(encoding="utf-8"))
    base = manifest.parent
    controls = document.get("model_controls")
    spectra = Spectra(name, weight)
    for example in document["examples"]:
        model_spec = example["model"]
        local = {**(controls or {}), **{key: model_spec[key] for key in
                                        ("capture", "stereo_width", "room") if key in model_spec}}
        spectra.add(_read_channels(example["target"], base),
                    _read_channels(model_spec, base), local or None, example["id"])
    if spectra.count == 0:
        raise ValueError(f"{manifest}: no examples")
    return spectra


def _guitarset_controls(directory: Path) -> dict | None:
    path = directory / "report.json"
    if not path.is_file():
        return None
    report = json.loads(path.read_text(encoding="utf-8"))
    return {"capture": report.get("capture", "stereo_mic"),
            "room": float(report.get("room", 0.0)),
            "stereo_width": float(report.get("observation", {}).get(
                "stereo_width", GUITARSET_WIDTH))}


def guitarset_spectra(directory: Path, mono_directory: Path | None = None) -> tuple[Spectra, dict]:
    spectra = Spectra("guitarset", GUITARSET_WEIGHT)
    controls = _guitarset_controls(directory)
    checks = {}
    clips = sorted(directory.glob("*-reference.wav"))
    for reference in clips:
        track = reference.name[: -len("-reference.wav")]
        rate, target = wavfile.read(reference)
        if rate != RATE:
            raise ValueError(f"{reference}: expected {RATE} Hz")
        model = np.fromfile(directory / f"{track}.f32", dtype="<f4").astype(
            np.float64).reshape(-1, 2)
        mono = None
        if mono_directory is not None:
            direct = np.fromfile(mono_directory / f"{track}.f32", dtype="<f4").astype(
                np.float64).reshape(-1, 2)
            if not np.array_equal(direct[:, 0], direct[:, 1]):
                raise ValueError(f"{mono_directory / track}.f32 is not a mono capture")
            mono = direct[:, 0]
            recovered, _ = scorer.observe_model(model, 1, controls)
            trim = float(np.dot(mono, recovered) / np.dot(recovered, recovered))
            error = float(np.sum((trim * recovered - mono) ** 2) / np.sum(mono ** 2))
            checks[track] = {"mono_trim": trim, "relative_residual": error}
            if not error < MONO_CHECK:
                raise ValueError(
                    f"{track}: the Mono mic render differs from the upper-bout microphone "
                    f"recovered from the Stereo mic render (residual {error:.3g}); the "
                    "observation's premise does not hold for this build")
        signal = np.asarray(target, dtype=np.float64)
        spectra.add(signal.reshape(-1, 1), model, controls, track, whole=True,
                    mono_override=mono)
    if not clips:
        raise ValueError(f"{directory}: no GuitarSet clips")
    return spectra, checks


def _weights() -> tuple[np.ndarray, np.ndarray]:
    kept = (CENTRES >= FIT_LOW) & (CENTRES <= FIT_HIGH)
    weight = np.where((CENTRES >= FULL_LOW) & (CENTRES <= FULL_HIGH), 1.0, EDGE_WEIGHT)
    return kept, weight


def _centred(values: np.ndarray) -> np.ndarray:
    kept, weight = _weights()
    return values - np.average(values[kept], weights=weight[kept])


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


def _pairing(spectra: Spectra, observation: str) -> tuple[str, str]:
    """(recording view, model view) the observation compares for a source."""
    if observation == "mid":
        return "mid", "mid"
    channels = spectra.recording_channels
    if channels == {2}:
        return "channels", "stereo"
    if channels == {1}:
        return "channels", "mono"
    raise ValueError(f"{spectra.name}: recordings mix channel counts {sorted(channels)}")


def _fit_report(differences: dict[str, tuple[np.ndarray, float]], current: list,
                authored_offset: np.ndarray) -> dict:
    wanted = consensus(differences)
    # The native consensus compares recordings with B+O. Add O back before
    # fitting B so the later runtime O retains the listening decision. The
    # global level adjustment cancels when spectra/residuals are centered.
    target = band_voicing_db(current) + authored_offset + wanted
    sections, fitted = fit(target)
    kept, weight = _weights()
    return {
        "sections": sections,
        "source_residual_db": {name: _centred(value[0]).tolist()
                               for name, value in differences.items()},
        "consensus_residual_db": wanted.tolist(),
        "target_voicing_db": _centred(target).tolist(),
        "fitted_voicing_db": _centred(fitted).tolist(),
        "residual_rms_db": float(np.sqrt(np.average(
            (_centred(fitted) - _centred(target))[kept] ** 2, weights=weight[kept]))),
    }


def collect(bank: Path, open_dir: Path, guitarset: Path,
            guitarset_mono: Path | None = None) -> tuple[list[Spectra], dict]:
    sources = [note_spectra((open_dir if where == "open" else bank) / manifest, name, weight)
               for name, where, manifest, weight in NOTE_SOURCES]
    guitar, checks = guitarset_spectra(guitarset, guitarset_mono)
    return sources + [guitar], checks


def run(bank: Path, open_dir: Path, guitarset: Path, *,
        balance_header: Path | None = BALANCE_HEADER,
        observation: str = DEFAULT_OBSERVATION,
        guitarset_mono: Path | None = None) -> dict:
    scorer._check_observation(observation)
    current, level = read_header()
    # Production renders include this contour. None is an explicit fixture
    # option for synthetic renders made without the production refinement.
    adjustments = read_balance(balance_header) if balance_header is not None else None
    balanced = balanced_sections(current, adjustments) if adjustments is not None else current
    authored_offset = band_voicing_db(balanced) - band_voicing_db(current)
    sources, checks = collect(bank, open_dir, guitarset, guitarset_mono)
    differences: dict[str, tuple[np.ndarray, float]] = {}
    pairings = {}
    for spectra in sources:
        pairing = _pairing(spectra, observation)
        difference = spectra.difference(*pairing)
        if difference is None:
            raise ValueError(f"{spectra.name}: no {pairing[1]} observation of the model: "
                             + "; ".join(sorted(spectra.model_notes.get(pairing[1], []))))
        differences[spectra.name] = (difference, spectra.weight)
        pairings[spectra.name] = {
            "recording_channels": sorted(spectra.recording_channels),
            "recording": "L/R average" if pairing[0] == "mid" else "per-channel power",
            "model": sorted(spectra.model_notes[pairing[1]]) if pairing[1] != "mid"
            else ["L/R average"],
        }
    report = {
        "tool": "Tools/FitCaptureVoicing.py",
        "observation": {"name": observation, "version": OBSERVATIONS[observation],
                        "sources": pairings, "guitarset_mono_check": checks or None},
        "header_before": {"sections": current, "level_db": level},
        "authored_balance": {
            "header": str(balance_header) if balance_header is not None else None,
            "adjustments_db": adjustments,
            "balanced_sections_before": balanced,
            "band_offset_db": authored_offset.tolist(),
            "policy": "Fit the generated recording base; retain the authored runtime refinement.",
        },
        "notes": {spectra.name: spectra.count for spectra in sources},
        "band_centres_hz": CENTRES.tolist(),
        "fitted_bands": _weights()[0].tolist(),
        **_fit_report(differences, current, authored_offset),
    }
    if observation == "matched":
        report["diagnostics"] = diagnostics(sources, current, authored_offset)
    return report


def diagnostics(sources: list[Spectra], current: list, authored_offset: np.ndarray) -> dict:
    """Reported beside the fit, never fitted (protocol step 6)."""
    retention = {"model": {}, "recording": {}}
    offsets = {}
    # A source rendered with a mono capture has no Stereo output to read.
    paired = [spectra for spectra in sources
              if "mono capture" not in spectra.model_notes.get("stereo", set())]
    for spectra in sources:
        for prefix in ("model", "recording"):
            value = spectra.retention_db(prefix)
            if value is not None:
                retention[prefix][spectra.name] = value.tolist()
    for spectra in paired:
        mono, stereo = spectra.model.get("mono"), spectra.model.get("stereo")
        if mono is not None and stereo is not None:
            offsets[spectra.name] = (_centred(10.0 * np.log10(mono / stereo)), spectra.weight)
    consensus_offset = (sum(weight * value for value, weight in offsets.values())
                        / sum(weight for _, weight in offsets.values())) if offsets else None
    contours = {}
    for view in ("stereo", "mono"):
        differences = {spectra.name: (spectra.difference("channels", view), spectra.weight)
                       for spectra in (paired if view == "stereo" else sources)
                       if spectra.difference("channels", view) is not None}
        contours[view] = ({"sources": sorted(differences),
                           **_fit_report(differences, current, authored_offset)}
                          if differences else None)
    return {
        "mono_sum_retention_db": retention,
        "mono_minus_stereo_db": {
            "sources": {name: value.tolist() for name, (value, _) in offsets.items()},
            "consensus": None if consensus_offset is None else consensus_offset.tolist()},
        "capture_contours": contours,
        "note": "Diagnostics only. Retention is L/R-average power over per-channel power "
                "(an equal-loudness note sum); mono_minus_stereo is the Mono mic over the "
                "per-channel Stereo output, level removed; capture_contours fit every "
                "source against one capture alone.",
    }


def _write_corpus(root: Path, recordings: dict[str, list[tuple[np.ndarray, np.ndarray]]],
                  controls: dict | None) -> None:
    """A synthetic --bank/--open/--guitarset tree: per source, (recording,
    model) pairs as (frames,) or (frames, channels) arrays."""
    for name in ("open", "bank", "guitarset"):
        (root / name).mkdir(parents=True, exist_ok=True)
    for name, where, manifest, _ in NOTE_SOURCES:
        examples = []
        for index, (recording, model) in enumerate(recordings[name]):
            stem = f"{name}-{index}"
            entries = {}
            for key, audio in (("target", recording), ("model", model)):
                audio = audio if audio.ndim == 2 else audio[:, None]
                audio.astype("<f4").tofile(root / where / f"{stem}-{key}.f32")
                entries[key] = {"path": f"{stem}-{key}.f32", "channels": audio.shape[1]}
            examples.append({"id": stem, "midi": 52, **entries})
        document = {"examples": examples}
        if controls is not None:
            document["model_controls"] = controls
        (root / where / manifest).write_text(json.dumps(document))
    for index, (recording, model) in enumerate(recordings["guitarset"]):
        wavfile.write(root / "guitarset" / f"{index:02d}_test-reference.wav", RATE,
                      recording.astype(np.float32))
        model.astype("<f4").tofile(root / "guitarset" / f"{index:02d}_test.f32")
    if controls is not None:
        (root / "guitarset" / "report.json").write_text(json.dumps(
            {"capture": controls["capture"], "room": controls["room"]}))


def _cancelling_pair_self_test(truth: list, header: Path) -> dict:
    """The regression: models whose L/R average cancels 1.0-1.6 kHz, whose
    output channels (against a coincident pair) or upper-bout microphone
    (against one microphone) carry the band as the recording does, must not
    drive a boost there. The legacy L/R average must (negative control)."""
    width = 0.62
    rng = np.random.default_rng(20261010)
    size = RATE * 2
    frequency = np.fft.rfftfreq(size, 1.0 / RATE)
    gain = np.sqrt(10.0 ** (voicing_db(truth, np.maximum(frequency, 1.0)) / 10.0))
    in_band = (frequency >= 1000.0) & (frequency < 1600.0)

    def note() -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        model = rng.standard_normal(size) * np.exp(-np.arange(size) / RATE)
        model[: RATE // 50] = 0.0
        model[RATE // 50] = 4.0
        model *= 0.1  # a render's level, under the engine limiter's knee
        spectrum = np.fft.rfft(model)
        band = np.fft.irfft(np.where(in_band, spectrum, 0.0), size)
        return model, band, np.fft.irfft(spectrum * gain, size)

    def coincident() -> tuple[np.ndarray, np.ndarray]:
        model, band, recording = note()
        return (np.stack([recording, recording], axis=1),
                np.stack([model, model - 2.0 * band], axis=1))

    def single() -> tuple[np.ndarray, np.ndarray]:
        model, band, recording = note()
        bridge = model - 2.0 * band
        mid, side = 0.5 * (bridge + model), 0.5 * (bridge - model)
        return recording, np.stack([mid + width * side, mid - width * side], axis=1)

    recordings = {name: [coincident() if name != "martin-hd28" else single()
                         for _ in range(3)] for name, *_ in NOTE_SOURCES}
    recordings["guitarset"] = [single()]
    controls = {"capture": "stereo_mic", "stereo_width": width, "room": 0.0}
    kept, weight = _weights()
    expected = band_voicing_db(truth)
    expected = expected - np.average(expected[kept], weights=weight[kept])
    band = kept & (CENTRES >= 1000.0) & (CENTRES <= 1600.0)
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        _write_corpus(root, recordings, controls)
        reports = {observation: run(root / "bank", root / "open", root / "guitarset",
                                    balance_header=None, observation=observation)
                   for observation in OBSERVATIONS}
        # A source without the controls to recover its Mono mic stops the fit.
        manifest = root / "open" / "martin-hd28.martin-hd28.json"
        document = json.loads(manifest.read_text())
        del document["model_controls"]
        manifest.write_text(json.dumps(document))
        try:
            run(root / "bank", root / "open", root / "guitarset", balance_header=None)
        except ValueError:
            pass
        else:
            raise AssertionError("a single-microphone source was fitted without its Mono mic")
    matched = np.asarray(reports["matched"]["fitted_voicing_db"])
    legacy = np.asarray(reports["mid"]["fitted_voicing_db"])
    matched_error = float(np.max(np.abs(matched - expected)[kept]))
    legacy_boost = float(np.max((legacy - expected)[band]))
    assert matched_error < 0.8, f"cancelling pair: matched fit off by {matched_error:.2f} dB"
    assert legacy_boost > 2.0, f"negative control: L/R average boosted only {legacy_boost:.2f} dB"
    retention = reports["matched"]["diagnostics"]["mono_sum_retention_db"]["model"]
    inside = (CENTRES >= 1100.0) & (CENTRES <= 1300.0)
    worst = max(float(np.max(np.asarray(value)[inside])) for value in retention.values())
    outside = max(float(np.max(np.abs(np.asarray(value)[(CENTRES >= 200.0) & (CENTRES <= 500.0)])))
                  for value in retention.values())
    assert worst < -10.0 and outside < 0.5, (worst, outside)
    assert reports["matched"]["observation"]["sources"]["martin-hd28"]["model"] == [
        "Mono mic recovered from the Stereo mic render"]
    return {"matched_error": matched_error, "legacy_boost": legacy_boost,
            "retention_in_band": worst}


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
            report = run(root / "bank", root / "open", root / "guitarset", balance_header=None)
            legacy_report = run(root / "bank", root / "open", root / "guitarset",
                                balance_header=None, observation="mid")
            write_header(report["sections"], -1.25, header)
            written, level = read_header(header)
            # A second corpus models a delivered engine with a nonzero base
            # AND a separate authored balance. Its recordings retain the same
            # known recording target. Refit must recover that base target,
            # rather than compensate away the runtime listening refinement.
            current = [(kind, f0, gain, q) for (kind, f0, q), gain in zip(
                STRUCTURE, (0.8, 1.2, -0.7, -1.3, 0.5, 1.0))]
            write_header(current, 0.0, header)
            balance_header = root / "MicrophoneBalanceData.h"
            balance_header.write_text(
                "inline constexpr float originalBassShelfAdjustmentDb = -2.0f;\n"
                "inline constexpr float originalAirPeakAdjustmentDb = -1.5f;\n"
                "inline constexpr float originalCaptureLevelAdjustmentDb = 0.75f;\n")
            adjustments = read_balance(balance_header)
            assert adjustments == {"originalBassShelfAdjustmentDb": -2.0,
                                   "originalAirPeakAdjustmentDb": -1.5,
                                   "originalCaptureLevelAdjustmentDb": 0.75}
            # Build the fixture independently of balanced_sections/read_balance.
            # The literal adjustments are the ones written in this fixture's
            # header, so applying them to the wrong runtime section must fail.
            rendered_sections = [(kind, f0, gain
                                  + (-2.0 if kind == "LowShelf" and f0 == 120.0 else
                                     -1.5 if kind == "Peak" and f0 == 125.0 else 0.0), q)
                                 for kind, f0, gain, q in current]
            rendered_gain = np.sqrt(10.0 ** (voicing_db(
                rendered_sections, np.maximum(frequency, 1.0)) / 10.0))
            rendered_gain *= 10.0 ** (0.75 / 20.0)
            model_paths = list(root.rglob("*-model.f32")) + [root / "guitarset" / "00_test.f32"]
            for model_path in model_paths:
                raw = np.fromfile(model_path, dtype="<f4").reshape(-1, 2).mean(axis=1)
                rendered = np.fft.irfft(np.fft.rfft(raw) * rendered_gain, size)
                np.stack([rendered, rendered], axis=1).astype("<f4").tofile(model_path)
            balanced_report = run(root / "bank", root / "open", root / "guitarset",
                                  balance_header=balance_header)
            cancelled_report = run(root / "bank", root / "open", root / "guitarset",
                                   balance_header=None)
            # Missing metadata must fail closed; production may not guess the
            # offsets or silently treat a refined engine as an unrefined one.
            invalid_balance = root / "missing-balance-constant.h"
            invalid_balance.write_text(balance_header.read_text().replace(
                "originalAirPeakAdjustmentDb", "retiredAirPeakAdjustmentDb"))
            try:
                read_balance(invalid_balance)
            except ValueError:
                pass
            else:
                raise AssertionError("missing authored balance was silently accepted")
            write_header([(kind, f0, 0.0, q) for kind, f0, q in STRUCTURE], 0.0, header)
            pair_result = _cancelling_pair_self_test(truth, header)
        finally:
            HEADER = saved
    kept, weight = _weights()
    expected = band_voicing_db(truth)
    expected = expected - np.average(expected[kept], weights=weight[kept])
    error = np.abs(np.asarray(report["fitted_voicing_db"]) - expected)[kept]
    assert float(error.max()) < 0.8, f"recovered voicing off by {error.max():.2f} dB"
    assert report["residual_rms_db"] < 0.3, report["residual_rms_db"]
    # Identical channels read the same under both observations.
    assert all(abs(a[2] - b[2]) < 1e-6 for a, b in zip(report["sections"],
                                                       legacy_report["sections"])), (
        "a corpus of identical channels moved with the observation")
    assert len(written) == len(STRUCTURE) and abs(level + 1.25) < 1e-6
    assert all(abs(a[2] - b[2]) < 0.006 for a, b in zip(written, report["sections"]))
    balanced_error = np.abs(np.asarray(balanced_report["fitted_voicing_db"]) - expected)[kept]
    cancelled_error = np.abs(np.asarray(cancelled_report["fitted_voicing_db"]) - expected)[kept]
    assert float(balanced_error.max()) < 0.8, (
        f"authored-balance refit off by {balanced_error.max():.2f} dB")
    assert float(cancelled_error.max()) > 0.8, (
        "negative control did not expose cancellation of the authored balance")
    assert balanced_report["authored_balance"]["adjustments_db"] == adjustments
    assert np.max(np.abs(balanced_report["authored_balance"]["band_offset_db"])) > 1.0
    # The engine's formula: a +6 dB peak reads +6 dB at its centre and a
    # shelf its gain far past its corner.
    peak = voicing_db([("Peak", 500.0, 6.0, 1.2)], np.array([500.0]))[0]
    shelf = voicing_db([("LowShelf", 120.0, 3.0, 0.7)], np.array([5.0, 20_000.0]))
    assert abs(peak - 6.0) < 1e-9 and abs(shelf[0] - 3.0) < 0.01 and abs(shelf[1]) < 0.01
    print(f"self-test passed: worst band {error.max():.2f} dB, "
          f"residual {report['residual_rms_db']:.3f} dB rms; "
          f"authored-balance refit {balanced_error.max():.2f} dB, "
          f"old-math negative control {cancelled_error.max():.2f} dB; "
          f"cancelling spaced pair: matched {pair_result['matched_error']:.2f} dB off, "
          f"L/R average boosts 1-1.6 kHz {pair_result['legacy_boost']:+.2f} dB, "
          f"retention {pair_result['retention_in_band']:+.1f} dB")


def _print_table(report: dict) -> None:
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--bank", type=Path)
    parser.add_argument("--open", type=Path, dest="open_dir")
    parser.add_argument("--guitarset", type=Path)
    parser.add_argument("--guitarset-mono", type=Path,
                        help="the GuitarSet benchmark rendered with --capture mono_mic")
    parser.add_argument("--observation", choices=sorted(OBSERVATIONS),
                        default=DEFAULT_OBSERVATION,
                        help="matched (version 2, default) or mid (version 1, the L/R "
                             "average of every fit before 2026-10-10)")
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
    report = run(arguments.bank, arguments.open_dir, arguments.guitarset,
                 observation=arguments.observation, guitarset_mono=arguments.guitarset_mono)
    print(f"observation: {arguments.observation} (version {OBSERVATIONS[arguments.observation]})")
    _print_table(report)
    if "diagnostics" in report:
        contours = report["diagnostics"]["capture_contours"]
        for view in ("stereo", "mono"):
            if contours[view] is None:
                print(f"{view} capture alone: no source renders it")
                continue
            print(f"{view} capture alone: " + ", ".join(
                f"{f0:g} Hz {gain:+.2f}" for _, f0, gain, _ in contours[view]["sections"])
                + f" ({len(contours[view]['sources'])} sources)")
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
