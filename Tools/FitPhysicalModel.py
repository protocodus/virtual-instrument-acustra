#!/usr/bin/env python3
"""Score physical-model renders against recording targets.

This is the offline analysis half of Acustra's fitter. Recordings are read-only
targets: this tool extracts perceptual/physical descriptors and compares them
with renders supplied by a separate physical-model renderer. It never emits or
modifies plug-in DSP coefficients.

Manifest format (paths are relative to the manifest):

    {
      "analysis_sample_rate": 48000,
      "sample_rate": 48000,
      "channels": 2,
      "examples": [
        {
          "id": "steel-E2-v16-rr1",
          "midi": 40,
          "velocity": 16,
          "dynamic_group": "steel-E2-rr1",
          "target": "targets/E2-v16.wav",
          "model": {"path": "renders/E2-v16.f32", "channels": 2}
        }
      ]
    }

WAV metadata is read from the file. Headerless little-endian float32 files need
``sample_rate`` and ``channels`` either globally or in their file object.

Observation (which signal of each file the descriptors are read from):

- ``matched`` (version 2, the default since 2026-10-10): each recording is
  compared with the model capture it corresponds to. A two-channel target is
  a stereo recording (the Eastman takes and the bank's flat-top are a
  coincident pair), read as per-channel power: every power spectrum, energy
  and level is the mean of its channels' powers, and the model's two output
  channels are read the same way, at the Width they were rendered with. A
  one-channel target is a single microphone (the archtop bank, the Martin
  HD28, GuitarSet's U87) and is compared with the model's Mono mic capture:
  a render made with that capture as it is, and a dry Stereo mic render
  through its upper-bout microphone, which the Width law (each output is
  mid +/- Width x side) gives back exactly; the manifest's ``model_controls``
  (capture, stereo_width, room) say which, and the recovery refuses a render
  with Room, no Width or a limited peak. The model's microphones are two
  near-field omnis 20 cm apart whose L/R average cancels (-8.5 dB at 1.25
  kHz on a strummed chord), which no coincident pair or single microphone
  does; see Docs/capture-observation-2026-10-10.md.
- ``mid`` (version 1): the L/R average of target and model alike, as every
  score before 2026-10-10 was read. Reproduce those with ``--observation
  mid`` (or ``"observation": "mid"`` in the manifest).

A manifest may name its observation (``"observation"``); ``--observation``
overrides it, and a manifest without one is read ``matched``. Mono files read
identically under both. Paired comparisons require one observation.

Usage:

    python3 Tools/FitPhysicalModel.py fit-manifest.json [--observation matched|mid]
    python3 Tools/FitPhysicalModel.py candidate.json --compare baseline.json
    python3 Tools/FitPhysicalModel.py --floor fit-manifest.json \
        [--control sample-manifest.json]
    python3 Tools/FitPhysicalModel.py --self-test

``--floor`` scores each recorded take against another take of the same note and
layer instead of against a model, which is the only number that says how small
a term can honestly get: see ``floor_report``. It also rescores the model, and
the ``--control`` manifest when one is given, against the same trim-corrected
targets, so every column of that table is a distance to the same signal.

``--compare`` reports paired aggregate, material and descriptor changes. It
requires identical example identities, target bytes, target format and dynamics
grouping; paths may differ. Both manifests' declared controls and provenance
are retained in the report. Negative changes mean closer descriptors, not a
listening preference; the mode neither fits parameters nor selects a winner.
Manifests explicitly marked ``model_render_complete: false`` by an interrupted
models-only render are rejected before any descriptors are read.

NumPy and SciPy are required; both are already used by Acustra's measurement
tools. The JSON report's ``weighted_residuals`` can be returned directly from a
bounded least-squares objective. Its squared norm equals ``score``.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import tempfile
from pathlib import Path
from typing import Any

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, stft, windows


TERM_WEIGHTS = {
    "attack": 0.20,
    "harmonics": 0.20,
    "tuning": 0.07,
    "pitch_trajectory": 0.03,
    "decay": 0.25,
    "body": 0.15,
    "dynamics": 0.10,
}
HUBER_DELTA = 2.0
ATTACK_WINDOWS = ((0.000, 0.012), (0.012, 0.040), (0.040, 0.100))
HARMONIC_WINDOWS = ((0.080, 0.250), (0.400, 0.900))
PITCH_TRAJECTORY_WINDOWS = ((0.020, 0.100), (0.080, 0.200), (0.180, 0.400))
MAX_HARMONIC = 12
# The DAFx-26 differentiable Karplus--Strong study uses these prime FFT
# lengths for a six-scale log-magnitude loss. MIDI pitch and onset remain
# separately constrained below; the spectral loss is not asked to discover
# either one.
MULTISCALE_WINDOWS = (67, 127, 257, 509, 1021, 2053)
# The observation protocols (module docstring), each with its version number.
OBSERVATIONS = {"matched": 2, "mid": 1}
DEFAULT_OBSERVATION = "matched"
# The engine's safetyLimit is exactly linear below this knee (-1 dBFS).
LIMITER_KNEE = 0.89125094
# Below this Width the pair's side signal is too small to recover a microphone.
MIN_RECOVERY_WIDTH = 0.05


def _log_edges(low: float, high: float, count: int) -> np.ndarray:
    return np.geomspace(low, high, count + 1)


ATTACK_BANDS = _log_edges(80.0, 12_000.0, 18)
BODY_BANDS = _log_edges(80.0, 8_000.0, 40)
DECAY_BANDS = _log_edges(80.0, 8_000.0, 6)


def _as_file_spec(value: Any) -> dict[str, Any]:
    if isinstance(value, str):
        return {"path": value}
    if isinstance(value, dict) and isinstance(value.get("path"), str):
        return value
    raise ValueError("audio entry must be a path string or an object with path")


def _read_audio(
    value: Any,
    base: Path,
    default_rate: int | None,
    default_channels: int | None,
    *,
    keep_channels: bool = False,
) -> tuple[int, np.ndarray]:
    """Return (rate, audio): the L/R average, as ever, or with keep_channels a
    stereo file as (frames, 2) and a mono one as a vector."""
    spec = _as_file_spec(value)
    path = Path(spec["path"])
    if not path.is_absolute():
        path = base / path
    if path.suffix.lower() == ".wav":
        rate, data = wavfile.read(path)
        if np.issubdtype(data.dtype, np.integer):
            info = np.iinfo(data.dtype)
            scale = float(max(abs(info.min), info.max))
            data = data.astype(np.float64) / scale
        else:
            data = data.astype(np.float64)
    elif path.suffix.lower() == ".f32":
        rate = spec.get("sample_rate", default_rate)
        channels = spec.get("channels", default_channels)
        if not isinstance(rate, int) or rate < 8_000:
            raise ValueError(f"{path}: f32 sample_rate must be an integer >= 8000")
        if not isinstance(channels, int) or channels not in (1, 2):
            raise ValueError(f"{path}: f32 channels must be 1 or 2")
        flat = np.fromfile(path, dtype="<f4").astype(np.float64)
        if flat.size % channels != 0:
            raise ValueError(f"{path}: f32 sample count is not divisible by channels")
        data = flat.reshape(-1, channels) if channels > 1 else flat
    else:
        raise ValueError(f"{path}: expected .wav or .f32")

    if data.ndim == 2:
        if data.shape[1] not in (1, 2):
            raise ValueError(f"{path}: expected mono or stereo audio")
        if keep_channels:
            data = data[:, 0] if data.shape[1] == 1 else data
        else:
            data = data.mean(axis=1)
    if data.ndim not in (1, 2) or data.shape[0] < 64:
        raise ValueError(f"{path}: audio is empty or malformed")
    if not np.all(np.isfinite(data)):
        raise ValueError(f"{path}: audio contains non-finite samples")
    return int(rate), data


def _resample(signal: np.ndarray, source_rate: int, target_rate: int) -> np.ndarray:
    # resample_poly's axis 0 is time for a (frames, channels) observation too.
    if source_rate == target_rate:
        return signal
    divisor = math.gcd(source_rate, target_rate)
    return resample_poly(signal, target_rate // divisor, source_rate // divisor)


def _check_observation(observation: str) -> str:
    if observation not in OBSERVATIONS:
        raise ValueError(f"observation must be one of {sorted(OBSERVATIONS)}, "
                         f"not {observation!r}")
    return observation


def mono_mic_from_stereo(audio: np.ndarray, width: float) -> np.ndarray:
    """The Mono mic from a dry Stereo mic render: its upper-bout microphone.

    The engine writes each output as mid +/- Width x side of its two
    microphones (treble bridge left, upper bout right) and plays the Mono
    capture from the upper-bout one alone (AcustraEngine::process, BodyBank::
    render), so with no Room and no Piezo Mix that microphone is exactly
    ((1 + w) R - (1 - w) L) / (2 w), up to the Mono capture's level trim.
    """
    w = float(np.float32(width))
    if not w >= MIN_RECOVERY_WIDTH:
        raise ValueError(f"cannot recover a microphone from Width {width}")
    left, right = audio[:, 0], audio[:, 1]
    return ((1.0 + w) * right - (1.0 - w) * left) / (2.0 * w)


def observe_model(audio: np.ndarray, target_channels: int,
                  controls: dict[str, Any] | None) -> tuple[np.ndarray, str]:
    """The matched observation of a model render for a target with this many
    channels: per-channel power for a stereo target, the Mono mic for a mono
    one. Returns the signal and how it was observed."""
    if audio.ndim == 1:
        return audio, "mono render"
    if np.array_equal(audio[:, 0], audio[:, 1]):
        # A mono capture (Mono mic or piezo) writes the same sample to both.
        return audio[:, 0].copy(), "mono capture"
    if target_channels == 2:
        return audio, "per-channel power"
    controls = controls or {}
    capture = controls.get("capture")
    width = controls.get("stereo_width")
    room = controls.get("room")
    if capture != "stereo_mic" or not isinstance(width, (int, float)) \
            or not isinstance(room, (int, float)):
        raise ValueError(
            "a single-microphone target needs the model's Mono mic: render with "
            "the mono_mic capture, or give model_controls with capture "
            "stereo_mic, stereo_width and room for a dry Stereo mic render")
    if room != 0:
        raise ValueError(f"cannot recover the Mono mic from a render with Room {room}: "
                         "the room reaches it as the pair's mid alone")
    if float(np.max(np.abs(audio))) >= LIMITER_KNEE:
        raise ValueError("cannot recover the Mono mic: the output reached the "
                         "safety limiter's knee")
    return mono_mic_from_stereo(audio, width), "Mono mic recovered from the Stereo mic render"


def _onset(signal: np.ndarray, rate: int) -> int:
    # A 1 ms energy follower is stable on both pick noise and soft notes.
    # A (frames, channels) observation follows its per-channel power.
    width = max(1, round(0.001 * rate))
    squared = signal * signal if signal.ndim == 1 else np.mean(signal * signal, axis=1)
    energy = np.convolve(squared, np.ones(width) / width, mode="same")
    limit = min(squared.size, round(0.250 * rate))
    peak = float(np.max(energy[:limit]))
    if peak <= 1.0e-20:
        return 0
    indices = np.flatnonzero(energy[:limit] >= peak * 0.0025)  # -26 dB energy
    return int(indices[0]) if indices.size else 0


def _spectrum(
    signal: np.ndarray, rate: int, onset: int, begin: float, end: float
) -> tuple[np.ndarray, np.ndarray]:
    first = max(0, onset + round(begin * rate))
    last = min(signal.shape[0], onset + round(end * rate))
    if last - first < 32:
        return np.empty(0), np.empty(0)
    segment = signal[first:last].astype(np.float64, copy=True)
    if segment.ndim == 2:
        # Per-channel power: each channel's spectrum, its powers averaged.
        segment -= np.mean(segment, axis=0)
        taper = windows.hann(segment.shape[0], sym=False)
        nfft = 1 << max(8, int(math.ceil(math.log2(segment.shape[0] * 4))))
        transformed = np.fft.rfft(segment * taper[:, None], nfft, axis=0)
        power = (np.mean(np.abs(transformed) ** 2, axis=1)
                 / max(float(np.sum(taper * taper)), 1.0e-30))
        return np.fft.rfftfreq(nfft, 1.0 / rate), power
    segment -= np.mean(segment)
    taper = windows.hann(segment.size, sym=False)
    nfft = 1 << max(8, int(math.ceil(math.log2(segment.size * 4))))
    transformed = np.fft.rfft(segment * taper, nfft)
    power = np.abs(transformed) ** 2 / max(float(np.sum(taper * taper)), 1.0e-30)
    return np.fft.rfftfreq(nfft, 1.0 / rate), power


def _band_levels(
    signal: np.ndarray,
    rate: int,
    onset: int,
    begin: float,
    end: float,
    edges: np.ndarray,
) -> np.ndarray:
    frequency, power = _spectrum(signal, rate, onset, begin, end)
    result = np.full(edges.size - 1, np.nan)
    if frequency.size == 0:
        return result
    floor = max(float(np.sum(power)) * 1.0e-12, 1.0e-30)
    for index, (low, high) in enumerate(zip(edges[:-1], edges[1:])):
        selected = (frequency >= low) & (frequency < high)
        if np.any(selected):
            result[index] = 10.0 * math.log10(max(float(np.sum(power[selected])), floor))
    return result


def _multiscale_log_magnitude(
    signal: np.ndarray, rate: int, onset: int
) -> np.ndarray:
    length = round(0.900 * rate)
    segment = np.zeros((length,) + signal.shape[1:])
    available = min(length, signal.shape[0] - onset)
    if available > 0:
        segment[:available] = signal[onset : onset + available]
    centres = np.sqrt(ATTACK_BANDS[:-1] * ATTACK_BANDS[1:])
    features: list[np.ndarray] = []
    for size in MULTISCALE_WINDOWS:
        frequency, _, spectrum = stft(
            segment if segment.ndim == 1 else segment.T,
            fs=rate,
            window="hann",
            nperseg=size,
            noverlap=size // 2,
            nfft=size,
            boundary=None,
            padded=False,
        )
        # Per-channel power: the RMS magnitude across channels.
        magnitude = (np.abs(spectrum) if segment.ndim == 1
                     else np.sqrt(np.mean(np.abs(spectrum) ** 2, axis=0)))
        floor = max(float(np.max(magnitude)) * 1.0e-6, 1.0e-12)
        # Mean log magnitude retains quiet decay frames instead of allowing a
        # single loud attack frame to own the full 900 ms descriptor.
        mean_log = np.mean(20.0 * np.log10(np.maximum(magnitude, floor)), axis=1)
        sampled = np.interp(centres, frequency, mean_log,
                            left=mean_log[0], right=mean_log[-1])
        features.append(_normalise_levels(sampled))
    return np.concatenate(features)


def _normalise_levels(values: np.ndarray) -> np.ndarray:
    result = values.copy()
    finite = np.isfinite(result)
    if np.any(finite):
        result[finite] -= np.mean(result[finite])
    return result


def _peak(
    frequency: np.ndarray,
    power: np.ndarray,
    expected: float,
    cents: float,
) -> tuple[float, float]:
    if frequency.size < 3 or expected <= 0.0:
        return math.nan, math.nan
    ratio = 2.0 ** (cents / 1200.0)
    selected = np.flatnonzero(
        (frequency >= expected / ratio) & (frequency <= expected * ratio)
    )
    if selected.size == 0:
        return math.nan, math.nan
    index = int(selected[np.argmax(power[selected])])
    if power[index] <= max(float(np.max(power)) * 1.0e-10, 1.0e-30):
        return math.nan, math.nan

    # Parabolic interpolation in log power gives stable sub-bin frequencies.
    offset = 0.0
    peak_power = float(power[index])
    if 0 < index < power.size - 1:
        adjacent = np.log(np.maximum(power[index - 1 : index + 2], 1.0e-30))
        denominator = adjacent[0] - 2.0 * adjacent[1] + adjacent[2]
        if abs(denominator) > 1.0e-12:
            offset = float(np.clip(0.5 * (adjacent[0] - adjacent[2]) / denominator,
                                   -0.5, 0.5))
            peak_power = math.exp(float(adjacent[1] - 0.25
                                  * (adjacent[0] - adjacent[2]) * offset))
    bin_width = frequency[1] - frequency[0]
    return float(frequency[index] + offset * bin_width), 10.0 * math.log10(
        max(peak_power, 1.0e-30)
    )


def _harmonic_features(
    signal: np.ndarray, rate: int, onset: int, midi: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    expected_fundamental = 440.0 * 2.0 ** ((midi - 69.0) / 12.0)
    settled_frequency, settled_power = _spectrum(signal, rate, onset, 0.400, 1.200)
    if settled_frequency.size == 0:
        settled_frequency, settled_power = _spectrum(signal, rate, onset, 0.080, 0.250)
    partial_frequencies = np.full(MAX_HARMONIC, np.nan)
    partial_levels = np.full(MAX_HARMONIC, np.nan)
    tuning = np.full(MAX_HARMONIC, np.nan)
    for harmonic in range(1, MAX_HARMONIC + 1):
        expected = harmonic * expected_fundamental
        if expected >= 0.46 * rate:
            continue
        found, level = _peak(settled_frequency, settled_power, expected, 65.0)
        partial_frequencies[harmonic - 1] = found
        partial_levels[harmonic - 1] = level
        if math.isfinite(found):
            tuning[harmonic - 1] = 1200.0 * math.log2(found / expected)

    levels = np.full((len(HARMONIC_WINDOWS), MAX_HARMONIC), np.nan)
    for window_index, (begin, end) in enumerate(HARMONIC_WINDOWS):
        frequency, power = _spectrum(signal, rate, onset, begin, end)
        for harmonic in range(1, MAX_HARMONIC + 1):
            expected = harmonic * expected_fundamental
            if expected >= 0.46 * rate:
                continue
            _, level = _peak(frequency, power, expected, 65.0)
            levels[window_index, harmonic - 1] = level
        levels[window_index] = _normalise_levels(levels[window_index])
    return levels, tuning, partial_frequencies, partial_levels


def _pitch_trajectory_features(
    signal: np.ndarray,
    rate: int,
    onset: int,
    settled_partials: np.ndarray,
    settled_levels: np.ndarray,
) -> np.ndarray:
    """Return robust early pitch offsets relative to settled partials.

    One short low-E window cannot resolve a few cents from H1 alone. Taking
    the median common shift of the first eight measured partials estimates the
    equivalent H1 trajectory while rejecting attack noise and fixed string
    inharmonicity.
    """
    result = np.full(len(PITCH_TRAJECTORY_WINDOWS), np.nan)
    _, settled_power = _spectrum(signal, rate, onset, 0.400, 1.200)
    if settled_power.size == 0:
        _, settled_power = _spectrum(signal, rate, onset, 0.080, 0.250)
    settled_floor = (
        10.0 * math.log10(max(float(np.max(settled_power)), 1.0e-30)) - 50.0
        if settled_power.size else math.inf
    )
    for window_index, (begin, end) in enumerate(PITCH_TRAJECTORY_WINDOWS):
        frequency, power = _spectrum(signal, rate, onset, begin, end)
        if power.size == 0:
            continue
        early_floor = (
            10.0 * math.log10(max(float(np.max(power)), 1.0e-30)) - 50.0
        )
        shifts: list[float] = []
        for partial, settled_level in zip(
            settled_partials[:8], settled_levels[:8]
        ):
            expected = float(partial)
            if (not math.isfinite(expected) or expected >= 0.46 * rate
                    or not math.isfinite(float(settled_level))
                    or settled_level < settled_floor):
                continue
            found, level = _peak(frequency, power, expected, 45.0)
            if (math.isfinite(found) and math.isfinite(level)
                    and level >= early_floor):
                shifts.append(1200.0 * math.log2(found / expected))
        fundamental = float(settled_partials[0])
        required = (
            2
            if (math.isfinite(fundamental)
                and fundamental * (end - begin) >= 20.0)
            else 3
        )
        if len(shifts) < required:
            continue
        values = np.asarray(shifts)
        median = float(np.median(values))
        mad = float(np.median(np.abs(values - median)))
        survivors = values[np.abs(values - median) <= max(6.0, 3.0 * mad)]
        if (survivors.size >= 3
                or (survivors.size == 2 and np.ptp(survivors) <= 6.0)):
            result[window_index] = float(np.median(survivors))
    return result


def _robust_line(times: np.ndarray, values: np.ndarray) -> float:
    finite = np.isfinite(times) & np.isfinite(values)
    times = times[finite]
    values = values[finite]
    if times.size < 5 or times[-1] - times[0] < 0.18:
        return math.nan
    peak = float(np.max(values))
    tail_count = max(3, values.size // 5)
    noise = float(np.median(values[-tail_count:]))
    useful = values >= max(noise + 10.0, peak - 55.0)
    times = times[useful]
    values = values[useful]
    if times.size < 5 or times[-1] - times[0] < 0.18:
        return math.nan

    matrix = np.column_stack((np.ones(times.size), times))
    weights = np.ones(times.size)
    solution = np.zeros(2)
    for _ in range(4):
        weighted = np.sqrt(weights)
        solution, *_ = np.linalg.lstsq(
            matrix * weighted[:, None], values * weighted, rcond=None
        )
        residual = values - matrix @ solution
        scale = max(1.0, 1.4826 * float(np.median(np.abs(
            residual - np.median(residual)
        ))))
        weights = np.minimum(1.0, 1.5 * scale / np.maximum(np.abs(residual), 1.0e-12))
    slope = float(solution[1])
    if slope >= -0.1:
        return math.nan
    # Report the decay rate itself, in dB/s. What a decay error does to a note
    # is a level difference, and after t seconds that difference is exactly
    # (rate_model - rate_target) * t dB. Comparing log2(T60) instead measures
    # the relative error, which under-weights a fast-decaying partial by the
    # ratio of its T60 to a slow one: the same 1.4x error is 25 dB after one
    # second on a 0.5 s partial and 3 dB on a 4 s partial.
    return float(np.clip(-slope, 2.0, 600.0))


def _decay_features(
    signal: np.ndarray,
    rate: int,
    onset: int,
    partial_frequencies: np.ndarray,
) -> np.ndarray:
    tail = signal[onset : min(signal.shape[0], onset + round(4.2 * rate))]
    if tail.shape[0] < 1024:
        return np.full(MAX_HARMONIC + DECAY_BANDS.size - 1, np.nan)
    window = min(8192, 1 << int(math.floor(math.log2(tail.shape[0]))))
    window = max(512, window)
    frequency, times, spectrum = stft(
        tail if tail.ndim == 1 else tail.T,
        fs=rate,
        window="hann",
        nperseg=window,
        noverlap=3 * window // 4,
        nfft=window,
        boundary=None,
        padded=False,
    )
    power = (np.abs(spectrum) ** 2 if tail.ndim == 1
             else np.mean(np.abs(spectrum) ** 2, axis=0))
    selected_time = (times >= 0.12) & (times <= 4.0)
    times = times[selected_time]
    power = power[:, selected_time]
    result: list[float] = []
    for partial in partial_frequencies:
        if not math.isfinite(float(partial)) or partial >= 0.46 * rate:
            result.append(math.nan)
            continue
        half_width = max(8.0, float(partial) * (2.0 ** (35.0 / 1200.0) - 1.0))
        bins = (frequency >= partial - half_width) & (frequency <= partial + half_width)
        curve = np.max(power[bins], axis=0) if np.any(bins) else np.empty(0)
        levels = 10.0 * np.log10(np.maximum(curve, 1.0e-30))
        result.append(_robust_line(times, levels))
    for low, high in zip(DECAY_BANDS[:-1], DECAY_BANDS[1:]):
        bins = (frequency >= low) & (frequency < high)
        curve = np.sum(power[bins], axis=0) if np.any(bins) else np.empty(0)
        levels = 10.0 * np.log10(np.maximum(curve, 1.0e-30))
        result.append(_robust_line(times, levels))
    return np.asarray(result)


def extract_features(signal: np.ndarray, rate: int, midi: int) -> dict[str, Any]:
    """Descriptors of a mono signal, or of a (frames, channels) observation
    read as per-channel power (every power and level averaged over channels)."""
    if signal.ndim not in (1, 2):
        raise ValueError("expected a mono signal or a (frames, channels) array")
    onset = _onset(signal, rate)
    attack = np.concatenate([
        _band_levels(signal, rate, onset, begin, end, ATTACK_BANDS)
        for begin, end in ATTACK_WINDOWS
    ])
    attack = _normalise_levels(attack)
    multiscale = _multiscale_log_magnitude(signal, rate, onset)
    harmonics, tuning, partials, partial_levels = _harmonic_features(
        signal, rate, onset, midi
    )
    pitch_trajectory = _pitch_trajectory_features(
        signal, rate, onset, partials, partial_levels
    )
    decay = _decay_features(signal, rate, onset, partials)
    body = _normalise_levels(
        _band_levels(signal, rate, onset, 0.080, 0.900, BODY_BANDS)
    )
    begin = min(signal.shape[0], onset + round(0.020 * rate))
    end = min(signal.shape[0], onset + round(0.500 * rate))
    level = math.nan
    if end > begin:
        level = 20.0 * math.log10(max(
            float(np.sqrt(np.mean(signal[begin:end] ** 2))), 1.0e-12
        ))
    return {
        "latency_seconds": onset / rate,
        "attack": attack,
        "multiscale": multiscale,
        "harmonics": harmonics.ravel(),
        "tuning": tuning,
        "pitch_trajectory": pitch_trajectory,
        "decay": decay,
        "body": body,
        "level_db": level,
    }


def _differences(target: np.ndarray, model: np.ndarray, tolerance: float) -> list[float]:
    target = np.asarray(target, dtype=np.float64).ravel()
    model = np.asarray(model, dtype=np.float64).ravel()
    if target.shape != model.shape:
        raise ValueError("target/model feature shapes differ")
    # Target availability owns the residual layout.  A model-side missing
    # partial receives a fixed penalty, while a target-side missing partial is
    # ignored.  This keeps the residual vector dimension constant throughout
    # bounded least-squares fitting even when a candidate loses a weak mode.
    available = np.isfinite(target)
    result = np.full(int(np.count_nonzero(available)), 4.0)
    comparable = available & np.isfinite(model)
    target_indices = np.flatnonzero(available)
    comparable_indices = np.flatnonzero(comparable)
    if comparable_indices.size:
        result[np.searchsorted(target_indices, comparable_indices)] = (
            model[comparable] - target[comparable]
        ) / tolerance
    return result.tolist()


def _example_residuals(
    target: dict[str, Any], model: dict[str, Any]
) -> dict[str, list[float]]:
    return {
        "attack": [
            (model["latency_seconds"] - target["latency_seconds"]) / 0.0015
        ] + _differences(target["attack"], model["attack"], 3.0)
          + _differences(target["multiscale"], model["multiscale"], 3.0),
        "harmonics": _differences(target["harmonics"], model["harmonics"], 3.0),
        "tuning": _differences(target["tuning"], model["tuning"], 5.0),
        "pitch_trajectory": _differences(
            target["pitch_trajectory"], model["pitch_trajectory"], 3.0
        ),
        # 6 dB/s is a 6 dB level error after one second of decay. At the
        # corpus's median T60 it is also close to the 0.35 log2(T60) tolerance
        # this term used before, so the term keeps a comparable magnitude.
        "decay": _differences(target["decay"], model["decay"], 6.0),
        "body": _differences(target["body"], model["body"], 3.0),
        "dynamics": [],
    }


def _huber_squared(values: np.ndarray) -> np.ndarray:
    magnitude = np.abs(values)
    return np.where(
        magnitude <= HUBER_DELTA,
        values * values,
        2.0 * HUBER_DELTA * magnitude - HUBER_DELTA * HUBER_DELTA,
    )


def score_examples(
    examples: list[dict[str, Any]], analysis_rate: int
) -> dict[str, Any]:
    terms: dict[str, list[float]] = {name: [] for name in TERM_WEIGHTS}
    dynamic_groups: dict[str, list[tuple[float, float, float]]] = {}

    for example in examples:
        target = example.get("target_features")
        if target is None:
            target = extract_features(
                example["target_audio"], analysis_rate, example["midi"]
            )
        model = example.get("model_features")
        if model is None:
            model = extract_features(
                example["model_audio"], analysis_rate, example["midi"]
            )
        residuals = _example_residuals(target, model)
        for name, values in residuals.items():
            terms[name].extend(values)
        group = example.get("dynamic_group")
        if group is not None and math.isfinite(target["level_db"]) and math.isfinite(model["level_db"]):
            dynamic_groups.setdefault(str(group), []).append(
                (float(example["velocity"]), target["level_db"], model["level_db"])
            )

    for rows in dynamic_groups.values():
        by_velocity: dict[float, list[tuple[float, float]]] = {}
        for velocity, target_level, model_level in rows:
            by_velocity.setdefault(velocity, []).append((target_level, model_level))
        if len(by_velocity) < 2:
            continue
        velocities = sorted(by_velocity)
        target_levels = np.array([
            np.mean([item[0] for item in by_velocity[value]]) for value in velocities
        ])
        model_levels = np.array([
            np.mean([item[1] for item in by_velocity[value]]) for value in velocities
        ])
        target_levels -= np.mean(target_levels)
        model_levels -= np.mean(model_levels)
        terms["dynamics"].extend(((model_levels - target_levels) / 2.0).tolist())

    active_weight = sum(
        TERM_WEIGHTS[name] for name, values in terms.items() if values
    )
    if active_weight <= 0.0:
        raise ValueError("manifest produced no comparable features")

    weighted: list[float] = []
    term_report: dict[str, Any] = {}
    for name, values in terms.items():
        array = np.asarray(values, dtype=np.float64)
        if array.size == 0:
            term_report[name] = {"count": 0, "score": None}
            continue
        costs = _huber_squared(array)
        score = float(np.mean(costs))
        scale = math.sqrt(TERM_WEIGHTS[name] / (active_weight * array.size))
        weighted.extend((np.sign(array) * np.sqrt(costs) * scale).tolist())
        term_report[name] = {"count": int(array.size), "score": score}

    weighted_array = np.asarray(weighted)
    return {
        "score": float(np.dot(weighted_array, weighted_array)),
        "terms": term_report,
        "weighted_residuals": weighted,
    }


class PreparedManifest:
    """A manifest whose immutable target descriptors are extracted once.

    The physical renderer overwrites only model files during fitting. Keeping
    target features here avoids decoding the reference bank and running the
    six STFT scales on unchanged audio for every optimizer evaluation.

    ``observation`` (module docstring) overrides the manifest's own; without
    either the manifest is read ``matched``.
    """

    def __init__(self, path: Path, observation: str | None = None):
        self.path = path
        with path.open("r", encoding="utf-8") as stream:
            manifest = json.load(stream)
        if not isinstance(manifest, dict) or not isinstance(
            manifest.get("examples"), list
        ):
            raise ValueError("manifest must contain an examples array")
        if not manifest["examples"]:
            raise ValueError("manifest examples array is empty")
        if manifest.get("model_render_complete", True) is not True:
            raise ValueError("model render is incomplete; rerun the renderer before scoring")
        analysis_rate = manifest.get("analysis_sample_rate", 48_000)
        if not isinstance(analysis_rate, int) or not 8_000 <= analysis_rate <= 192_000:
            raise ValueError(
                "analysis_sample_rate must be an integer from 8000 to 192000"
            )
        declared = manifest.get("observation")
        if declared is not None:
            _check_observation(declared)
        self.declared_observation = declared
        self.observation = _check_observation(
            observation or declared or DEFAULT_OBSERVATION)
        controls = manifest.get("model_controls")
        self.model_controls = controls if isinstance(controls, dict) else None
        self.analysis_rate = analysis_rate
        self.default_rate = manifest.get("sample_rate")
        self.default_channels = manifest.get("channels")
        self.examples: list[dict[str, Any]] = []

        target_cache: dict[tuple[Any, ...], tuple[dict[str, Any], int]] = {}
        identifiers: set[str] = set()
        for row, source in enumerate(manifest["examples"], 1):
            if not isinstance(source, dict):
                raise ValueError(f"example {row} must be an object")
            identifier = source.get("id", str(row))
            if (
                not isinstance(identifier, str)
                or not identifier
                or identifier in identifiers
            ):
                raise ValueError(f"example {row} has an empty or duplicate id")
            identifiers.add(identifier)
            midi = source.get("midi")
            velocity = source.get("velocity")
            if not isinstance(midi, int) or not 0 <= midi <= 127:
                raise ValueError(
                    f"{identifier}: midi must be an integer from 0 to 127"
                )
            if not isinstance(velocity, (int, float)) or not math.isfinite(velocity):
                raise ValueError(f"{identifier}: velocity must be finite")
            if "target" not in source or "model" not in source:
                raise ValueError(f"{identifier}: target and model are required")

            target_key = self._feature_key(source["target"], midi)
            cached = target_cache.get(target_key)
            if cached is None:
                cached = self._read_target(source["target"], midi)
                target_cache[target_key] = cached
            target, target_channels = cached
            self.examples.append({
                "id": identifier,
                "material": source.get("material"),
                "midi": midi,
                "velocity": float(velocity),
                "round_robin": source.get("round_robin"),
                # The renderer records the per-zone playback trim it applied to
                # this target so the floor below can undo it; see floor_report.
                "playback_trim": _as_file_spec(source["target"]).get(
                    "playback_trim"),
                "dynamic_group": source.get("dynamic_group"),
                "target_features": target,
                "target_channels": target_channels,
                "model_spec": source["model"],
            })

    def _feature_key(self, value: Any, midi: int) -> tuple[Any, ...]:
        spec = _as_file_spec(value)
        path = Path(spec["path"])
        if not path.is_absolute():
            path = self.path.parent / path
        return (
            str(path.resolve()),
            spec.get("sample_rate", self.default_rate),
            spec.get("channels", self.default_channels),
            midi,
        )

    def _read_target(self, value: Any, midi: int) -> tuple[dict[str, Any], int]:
        """A recording's descriptors and its channel count."""
        rate, audio = _read_audio(
            value, self.path.parent, self.default_rate, self.default_channels,
            keep_channels=True,
        )
        channels = 1 if audio.ndim == 1 else audio.shape[1]
        if self.observation == "mid" and audio.ndim == 2:
            audio = audio.mean(axis=1)  # the arithmetic _read_audio always did
        return extract_features(
            _resample(audio, rate, self.analysis_rate), self.analysis_rate, midi
        ), channels

    def _model_view(self, source: dict[str, Any]) -> str:
        if self.observation == "mid":
            return "mid"
        return "per-channel" if source["target_channels"] == 2 else "mono"

    def _read_model(self, source: dict[str, Any]) -> tuple[dict[str, Any], str]:
        """A model render's descriptors as this manifest's observation sees
        them for this example's target, and how it was observed."""
        value, midi = source["model_spec"], source["midi"]
        if self.observation == "mid":
            rate, audio = _read_audio(
                value, self.path.parent, self.default_rate, self.default_channels)
            how = "L/R average"
        else:
            rate, audio = _read_audio(
                value, self.path.parent, self.default_rate, self.default_channels,
                keep_channels=True)
            spec = _as_file_spec(value)
            controls = {**(self.model_controls or {}),
                        **{key: spec[key] for key in ("capture", "stereo_width", "room")
                           if key in spec}}
            audio, how = observe_model(audio, source["target_channels"], controls)
        return extract_features(
            _resample(audio, rate, self.analysis_rate), self.analysis_rate, midi
        ), how

    def model_features(self, source: dict[str, Any],
                       cache: dict[tuple[Any, ...], tuple[dict[str, Any], str]]
                       | None = None) -> tuple[dict[str, Any], str]:
        """One example's model descriptors (cached by render and view)."""
        key = self._feature_key(source["model_spec"], source["midi"]) + (
            self._model_view(source),)
        if cache is not None and key in cache:
            return cache[key]
        result = self._read_model(source)
        if cache is not None:
            cache[key] = result
        return result

    def observation_report(self, views: set[str] | None = None) -> dict[str, Any]:
        return {"name": self.observation, "version": OBSERVATIONS[self.observation],
                "manifest_declared": self.declared_observation,
                "model": sorted(views) if views is not None else None}

    def score(
        self,
        *,
        material: str | None = None,
        identifiers: set[str] | None = None,
        undo_playback_trim: bool = False,
    ) -> dict[str, Any]:
        selected = [
            example
            for example in self.examples
            if (material is None or example["material"] == material)
            and (identifiers is None or example["id"] in identifiers)
        ]
        if not selected:
            raise ValueError("manifest selection produced no examples")

        # Round robins intentionally share one deterministic model render.
        # Extract its descriptors once per score call and compare each target
        # repetition against that same physical note (per observed view).
        model_cache: dict[tuple[Any, ...], tuple[dict[str, Any], str]] = {}
        examples: list[dict[str, Any]] = []
        for source in selected:
            model, _ = self.model_features(source, model_cache)
            examples.append({
                **source,
                # The floor undoes the export's per-zone playback trim on the
                # target, so a model score meant to be read against that floor
                # has to be measured against the same target; see floor_report.
                "target_features": (
                    _trim_corrected(source["target_features"],
                                    source["playback_trim"])
                    if undo_playback_trim else source["target_features"]),
                "model_features": model,
            })
        report = score_examples(examples, self.analysis_rate)
        report["analysis_sample_rate"] = self.analysis_rate
        report["example_count"] = len(examples)
        report["unique_model_count"] = len({key[:-1] for key in model_cache})
        report["observation"] = self.observation_report(
            {how for _, how in model_cache.values()})
        return report


def score_manifest(path: Path, observation: str | None = None) -> dict[str, Any]:
    return PreparedManifest(path, observation).score()


def compare_manifests(baseline_path: Path, candidate_path: Path,
                      observation: str | None = None) -> dict[str, Any]:
    prepared = [PreparedManifest(path, observation)
                for path in (baseline_path, candidate_path)]
    baseline, candidate = prepared
    if baseline.analysis_rate != candidate.analysis_rate:
        raise ValueError("paired comparison requires identical analysis sample rates")
    if baseline.observation != candidate.observation:
        # A scorer change would otherwise read as a model change.
        raise ValueError("paired comparison requires one observation: "
                         f"{baseline.observation} against {candidate.observation}")
    originals = [json.loads(path.read_text(encoding="utf-8"))
                 for path in (baseline_path, candidate_path)]
    rows = [{row["id"]: row for row in manifest.examples} for manifest in prepared]
    if rows[0].keys() != rows[1].keys():
        raise ValueError("paired comparison requires identical example IDs")

    # Hash the actual targets, not the descriptor vectors: different audio can
    # have the same lossy descriptors, and the same filename can be overwritten.
    fingerprints: dict[Path, str] = {}
    target_signatures = []
    for manifest, original in zip(prepared, originals):
        signatures = {}
        for index, source in enumerate(original["examples"], 1):
            spec = _as_file_spec(source["target"])
            path = (manifest.path.parent / spec["path"]).resolve()
            if path not in fingerprints:
                fingerprints[path] = hashlib.sha256(path.read_bytes()).hexdigest()
            metadata = {key: value for key, value in spec.items() if key != "path"}
            if path.suffix.lower() == ".f32":
                metadata.setdefault("sample_rate", manifest.default_rate)
                metadata.setdefault("channels", manifest.default_channels)
            signatures[source.get("id", str(index))] = (
                fingerprints[path], path.suffix.lower(), metadata)
        target_signatures.append(signatures)

    for identifier, source in rows[0].items():
        other = rows[1][identifier]
        for field in ("midi", "velocity", "material", "round_robin", "dynamic_group"):
            if source[field] != other[field]:
                raise ValueError(f"{identifier}: paired {field} differs")
        if target_signatures[0][identifier] != target_signatures[1][identifier]:
            raise ValueError(f"{identifier}: paired target content or metadata differs")
    # Keep summation order identical even if a manifest lists its rows in a
    # different order. Repeated takes retain the scorer's existing weighting.
    candidate.examples = [rows[1][source["id"]] for source in baseline.examples]

    def changes(first: dict[str, Any], second: dict[str, Any]) -> dict[str, Any]:
        def delta(a: float | None, b: float | None) -> dict[str, Any]:
            return {
                "baseline": a, "candidate": b,
                "delta": None if a is None or b is None else b - a,
                "change_percent": (100.0 * (b / a - 1.0)
                                   if a is not None and a > 0 and b is not None
                                   else None),
            }

        return {
            "example_count": first["example_count"],
            "score": delta(first["score"], second["score"]),
            "terms": {
                name: {**delta(first["terms"][name]["score"],
                               second["terms"][name]["score"]),
                       "baseline_count": first["terms"][name]["count"],
                       "candidate_count": second["terms"][name]["count"]}
                for name in TERM_WEIGHTS
            },
        }

    materials = [row["material"] for row in baseline.examples]
    if any(material is not None and not isinstance(material, str)
           for material in materials):
        raise ValueError("paired material labels must be strings or null")
    return {
        "analysis_sample_rate": baseline.analysis_rate,
        "observation": baseline.observation_report(),
        "target_verification": "identical SHA-256 content, format and example metadata",
        "interpretation": "negative changes mean closer descriptors on these same "
                          "recordings; no listening preference or market ranking is inferred",
        "baseline": {"manifest": str(baseline_path), "metadata": {
            key: value for key, value in originals[0].items() if key != "examples"}},
        "candidate": {"manifest": str(candidate_path), "metadata": {
            key: value for key, value in originals[1].items() if key != "examples"}},
        "aggregate": changes(baseline.score(), candidate.score()),
        "by_material": {
            material: changes(baseline.score(material=material),
                              candidate.score(material=material))
            for material in sorted({value for value in materials if value is not None})
        },
    }


def _trim_corrected(
    features: dict[str, Any], trim: Any
) -> dict[str, Any]:
    """Undo the sample bank's per-zone playback trim on the level descriptor.

    Every other descriptor this file extracts is normalised to its own mean, or
    is a frequency or a decay rate, so a scalar gain on the audio reaches the
    score through ``level_db`` alone and can be removed here rather than by
    re-exporting the corpus. The trim matters because ``Library::prepare``
    equalises one-second energy across every zone of an archtop root: on the
    exported targets two takes of the same note have been made the same
    loudness, so a level floor measured on them is the trim's, not the
    player's.
    """
    if trim is None:
        return features
    value = float(trim)
    if not math.isfinite(value) or value <= 0.0:
        return features
    return {**features, "level_db": features["level_db"] - 20.0 * math.log10(value)}


def _floor_examples(
    prepared: PreparedManifest, material: str, *, reverse: bool
) -> list[dict[str, Any]]:
    by_note: dict[tuple[int, float], dict[int, dict[str, Any]]] = {}
    for example in prepared.examples:
        if example["material"] != material or example["round_robin"] is None:
            continue
        by_note.setdefault((example["midi"], example["velocity"]), {})[
            int(example["round_robin"])] = example
    examples: list[dict[str, Any]] = []
    for (midi, velocity), takes in sorted(by_note.items()):
        for first in sorted(takes):
            for second in sorted(takes):
                if (first < second) == reverse or first == second:
                    continue
                target, model = takes[first], takes[second]
                examples.append({
                    "id": f"{material}-m{midi}-v{velocity:g}-rr{first}-{second}",
                    "material": material,
                    "midi": midi,
                    "velocity": velocity,
                    # One take pair is one dynamic group, so the dynamics term
                    # compares how loudness followed velocity in take `first`
                    # with how it did in take `second`.
                    "dynamic_group": f"{material}-m{midi}-rr{first}-{second}",
                    "target_features": _trim_corrected(
                        target["target_features"], target["playback_trim"]),
                    "model_features": _trim_corrected(
                        model["target_features"], model["playback_trim"]),
                })
    return examples


def floor_report(
    prepared: PreparedManifest,
    control: PreparedManifest | None = None,
) -> dict[str, Any]:
    """Score each take of a note against another take of the same note.

    The scorer cannot tell how much of a term is the model's error and how much
    is the spread the recordings themselves have, and the corpus answers that
    directly: the archtop was captured four times per note and layer, so the
    same seven terms can be run recording against recording. That is the floor
    below which a model score means nothing about the model. Both orderings are
    reported because the residual layout is owned by the target side, so a
    partial one take found and the other did not scores 4.0 in one direction
    and is skipped in the other; the two sides agreeing is the check that no
    such asymmetry dominates a term.

    The model, and the control when one is given, are scored here as well
    rather than taken from the ordinary run, because the floor undoes the
    export's per-zone playback trim on the target and a column read against it
    has to be measured against that same target. Only the level term moves.

    The flat-top rows were captured once per note, so they have no floor here
    and are reported as such.
    """
    materials = sorted({
        example["material"] for example in prepared.examples
        if example["material"] is not None
    })
    out: dict[str, Any] = {
        "manifest": str(prepared.path),
        "analysis_sample_rate": prepared.analysis_rate,
        "observation": prepared.observation_report(),
        "materials": {},
    }
    for material in materials:
        forward = _floor_examples(prepared, material, reverse=False)
        reverse = _floor_examples(prepared, material, reverse=True)
        node: dict[str, Any] = {"pair_count": len(forward)}
        if not forward:
            node["floor"] = None
            node["note"] = "one take per note: no recording-versus-recording floor"
            out["materials"][material] = node
            continue
        trims = [example["playback_trim"] for example in prepared.examples
                 if example["material"] == material]
        node["level_terms_trim_corrected"] = all(
            trim is not None for trim in trims)
        forward_report = score_examples(forward, prepared.analysis_rate)
        reverse_report = score_examples(reverse, prepared.analysis_rate)
        node["floor"] = {
            "score": forward_report["score"],
            "terms": {name: value["score"]
                      for name, value in forward_report["terms"].items()},
        }
        node["reversed"] = {
            "score": reverse_report["score"],
            "terms": {name: value["score"]
                      for name, value in reverse_report["terms"].items()},
        }
        node["symmetry_max_term_difference"] = max(
            abs(forward_report["terms"][name]["score"]
                - reverse_report["terms"][name]["score"])
            for name in forward_report["terms"]
            if forward_report["terms"][name]["score"] is not None
            and reverse_report["terms"][name]["score"] is not None
        )
        identifiers = {
            example["id"] for example in prepared.examples
            if example["material"] == material
            and example["round_robin"] is not None
        }
        # The model column of this table has to be measured against the same
        # target the floor is, or the two numbers in a row are distances to two
        # different signals. Only the level descriptor moves: every other one is
        # normalised to its own mean, or is a frequency or a rate.
        model_report = prepared.score(
            material=material, identifiers=identifiers,
            undo_playback_trim=node["level_terms_trim_corrected"])
        node["model"] = {
            "level_terms_trim_corrected": node["level_terms_trim_corrected"],
            "score": model_report["score"],
            "terms": {name: value["score"]
                      for name, value in model_report["terms"].items()},
        }
        if control is not None:
            control_report = control.score(
                material=material, identifiers=identifiers,
                undo_playback_trim=node["level_terms_trim_corrected"])
            node["control"] = {
                "level_terms_trim_corrected": node[
                    "level_terms_trim_corrected"],
                "score": control_report["score"],
                "terms": {name: value["score"]
                          for name, value in control_report["terms"].items()},
                "at_or_below_floor": {
                    name: (None if node["floor"]["terms"][name] is None
                           or value["score"] is None
                           else bool(value["score"] <= node["floor"]["terms"][name]))
                    for name, value in control_report["terms"].items()
                },
            }
        out["materials"][material] = node
    return out


def _synthetic_note(
    rate: int,
    midi: int,
    velocity: float,
    *,
    latency: float,
    brightness: float,
    t60: float,
    inharmonicity: float,
    velocity_exponent: float,
    attack_pitch_cents: float = 0.0,
    attack_pitch_tau: float = 0.075,
) -> np.ndarray:
    duration = 2.4
    samples = round(duration * rate)
    time = np.arange(samples) / rate
    local = np.maximum(time - latency, 0.0)
    active = time >= latency
    pitch_ratio = np.exp2(
        attack_pitch_cents
        * np.exp(-local / max(attack_pitch_tau, 1.0e-6)) / 1200.0
    )
    fundamental = 440.0 * 2.0 ** ((midi - 69.0) / 12.0)
    signal = np.zeros(samples)
    for harmonic in range(1, 9):
        frequency = harmonic * fundamental * math.sqrt(
            (1.0 + inharmonicity * harmonic * harmonic)
            / (1.0 + inharmonicity)
        )
        amplitude = brightness ** (harmonic - 1) / harmonic
        partial_t60 = t60 / (1.0 + 0.12 * (harmonic - 1))
        envelope = np.exp(math.log(0.001) * local / partial_t60)
        if attack_pitch_cents == 0.0:
            phase = 2.0 * np.pi * frequency * local
        else:
            phase = (2.0 * np.pi
                     * np.cumsum(active * frequency * pitch_ratio) / rate)
        signal += active * amplitude * envelope * np.sin(phase + 0.23 * harmonic)
    attack = 1.0 - np.exp(-local / 0.0018)
    return (velocity ** velocity_exponent) * attack * signal


def cancelling_pair(rate: int, seconds: float = 2.4, band: tuple[float, float] = (
        1000.0, 1600.0), seed: int = 20261010) -> tuple[np.ndarray, np.ndarray]:
    """A decaying broadband note and its band-limited part: the regression
    fixture for a spaced pair whose channels cancel in one band."""
    rng = np.random.default_rng(seed)
    frames = round(seconds * rate)
    onset = round(0.02 * rate)
    time = np.arange(frames - onset) / rate
    note = np.concatenate((np.zeros(onset), rng.standard_normal(frames - onset)
                           * np.exp(-time / 0.6) * (1.0 - np.exp(-time / 0.002))))
    note *= 0.25 / float(np.max(np.abs(note)))
    spectrum = np.fft.rfft(note)
    frequency = np.fft.rfftfreq(frames, 1.0 / rate)
    in_band = (frequency >= band[0]) & (frequency < band[1])
    return note, np.fft.irfft(np.where(in_band, spectrum, 0.0), frames)


def _observation_self_test(root: Path, rate: int) -> dict[str, float]:
    """A spaced pair whose L/R average cancels in 1.0-1.6 kHz must not read as
    a hole there (which a fit would boost) under the matched observation,
    while the legacy L/R average still shows it (the negative control)."""
    note, band = cancelling_pair(rate)
    width = 0.62
    inside = [index for index, (low, high) in enumerate(zip(BODY_BANDS[:-1], BODY_BANDS[1:]))
              if low >= 1050.0 and high <= 1500.0]
    # Mono target (a single microphone): the model's microphones are the
    # note itself (upper bout) and the note with the band inverted (bridge);
    # Width spreads them as the engine does, so the mid loses the band.
    upper, bridge = note, note - 2.0 * band
    mid, side = 0.5 * (bridge + upper), 0.5 * (bridge - upper)
    spaced = np.stack([mid + width * side, mid - width * side], axis=1)
    # Stereo target (a coincident pair): each model channel holds the band,
    # in antiphase between them.
    rest = note - band
    antiphase = np.stack([rest + band, rest - band], axis=1)
    note.astype("<f4").tofile(root / "pair-target-mono.f32")
    np.stack([note, note], axis=1).astype("<f4").tofile(root / "pair-target-stereo.f32")
    spaced.astype("<f4").tofile(root / "pair-model-spaced.f32")
    antiphase.astype("<f4").tofile(root / "pair-model-antiphase.f32")

    def manifest(name: str, target: str, channels: int, model: str,
                 controls: dict[str, Any] | None) -> Path:
        path = root / name
        document: dict[str, Any] = {"examples": [{
            "id": "pair", "midi": 57, "velocity": 91,
            "target": {"path": target, "sample_rate": rate, "channels": channels},
            "model": {"path": model, "sample_rate": rate, "channels": 2}}]}
        if controls is not None:
            document["model_controls"] = controls
        path.write_text(json.dumps(document), encoding="utf-8")
        return path

    dry = {"capture": "stereo_mic", "stereo_width": width, "room": 0}
    cases = {
        "mono": manifest("pair-mono.json", "pair-target-mono.f32", 1,
                         "pair-model-spaced.f32", dry),
        "stereo": manifest("pair-stereo.json", "pair-target-stereo.f32", 2,
                           "pair-model-antiphase.f32", dry),
    }
    result: dict[str, float] = {}
    for name, path in cases.items():
        for observation in OBSERVATIONS:
            prepared = PreparedManifest(path, observation)
            target = prepared.examples[0]["target_features"]["body"]
            model, _ = prepared.model_features(prepared.examples[0])
            residual = (model["body"] - target)[inside]
            # Body levels are each normalised to their own mean; compare the
            # band against the rest of the spectrum it was cut from.
            result[f"{name}_{observation}"] = float(np.mean(residual))
    for name in cases:
        matched, legacy = result[f"{name}_matched"], result[f"{name}_mid"]
        if abs(matched) > 1.0:
            raise AssertionError(f"{name} target: the matched observation reads a "
                                 f"{matched:+.2f} dB hole the pair's channels do not have")
        if legacy > -6.0:
            raise AssertionError(f"{name} target: the legacy L/R average no longer "
                                 f"exposes the cancelling band ({legacy:+.2f} dB)")
    # A single-microphone target cannot be matched without knowing the render.
    for name, controls in (("pair-no-controls.json", None),
                           ("pair-room.json", {**dry, "room": 0.5}),
                           ("pair-no-width.json", {**dry, "stereo_width": 0.0})):
        prepared = PreparedManifest(
            manifest(name, "pair-target-mono.f32", 1, "pair-model-spaced.f32", controls))
        try:
            prepared.score()
        except ValueError:
            pass
        else:
            raise AssertionError(f"{name}: recovered a Mono mic without a dry, known Width")
    # The recovery is the inverse of the Width law, sample for sample.
    recovered = mono_mic_from_stereo(
        np.fromfile(root / "pair-model-spaced.f32", dtype="<f4").astype(
            np.float64).reshape(-1, 2), width)
    if float(np.max(np.abs(recovered - note))) > 1.0e-6:
        raise AssertionError("the Mono mic recovery does not invert the Width law")
    # Paired comparisons never mix observations, and the legacy observation
    # stays the L/R average of target and model alike.
    declared = json.loads(cases["mono"].read_text())
    declared["observation"] = "mid"
    mixed = root / "pair-mono-mid.json"
    mixed.write_text(json.dumps(declared), encoding="utf-8")
    try:
        compare_manifests(cases["mono"], mixed)
    except ValueError as error:
        assert "observation" in str(error)
    else:
        raise AssertionError("paired comparison mixed two observations")
    legacy = PreparedManifest(mixed)
    assert legacy.observation == "mid"
    expected = extract_features(spaced.astype("<f4").astype(np.float64).mean(axis=1), rate, 57)
    actual, how = legacy.model_features(legacy.examples[0])
    assert how == "L/R average" and np.array_equal(actual["body"], expected["body"])
    return result


def self_test() -> None:
    rate = 48_000
    with tempfile.TemporaryDirectory(prefix="acustra-fit-self-test-") as directory:
        root = Path(directory)
        pair = _observation_self_test(root, rate)
        close_examples = []
        far_examples = []
        for velocity in (0.25, 0.90):
            suffix = str(round(velocity * 100))
            target = _synthetic_note(
                rate, 45, velocity, latency=0.0040, brightness=0.72,
                t60=4.8, inharmonicity=8.0e-5, velocity_exponent=0.82,
                attack_pitch_cents=14.0, attack_pitch_tau=0.090,
            )
            close = _synthetic_note(
                rate, 45, velocity, latency=0.0042, brightness=0.70,
                t60=4.6, inharmonicity=8.5e-5, velocity_exponent=0.84,
                attack_pitch_cents=12.0, attack_pitch_tau=0.085,
            )
            far = _synthetic_note(
                rate, 45, velocity, latency=0.0110, brightness=0.42,
                t60=1.6, inharmonicity=8.0e-4, velocity_exponent=1.55,
            )
            target_path = root / f"target-{suffix}.wav"
            close_path = root / f"close-{suffix}.f32"
            far_path = root / f"far-{suffix}.f32"
            wavfile.write(target_path, rate, target.astype(np.float32))
            close.astype("<f4").tofile(close_path)
            far.astype("<f4").tofile(far_path)
            common = {
                "id": f"A2-{suffix}",
                "material": "synthetic",
                "midi": 45,
                "velocity": velocity,
                "dynamic_group": "A2",
                "target": target_path.name,
            }
            close_examples.append({**common, "model": close_path.name})
            far_examples.append({**common, "model": far_path.name})

        def write_manifest(name: str, examples: list[dict[str, Any]]) -> Path:
            path = root / name
            path.write_text(json.dumps({
                "sample_rate": rate,
                "channels": 1,
                "examples": examples,
            }), encoding="utf-8")
            return path

        close_report = score_manifest(write_manifest("close.json", close_examples))
        far_report = score_manifest(write_manifest("far.json", far_examples))
        if not close_report["score"] < far_report["score"]:
            raise AssertionError(
                f"closer model did not win: {close_report['score']} >= {far_report['score']}"
            )
        if close_report["terms"]["dynamics"]["count"] == 0:
            raise AssertionError("synthetic dynamics check produced no residual")

        paired = compare_manifests(root / "far.json", root / "close.json")
        assert paired["aggregate"]["score"]["delta"] < 0.0
        assert paired["by_material"]["synthetic"] == paired["aggregate"]
        reordered = write_manifest("reordered.json", list(reversed(close_examples)))
        same = compare_manifests(root / "close.json", reordered)
        assert same["aggregate"]["score"]["delta"] == 0.0
        incomplete = json.loads((root / "close.json").read_text())
        incomplete["model_render_complete"] = False
        incomplete_path = root / "incomplete.json"
        incomplete_path.write_text(json.dumps(incomplete), encoding="utf-8")
        try:
            compare_manifests(root / "close.json", incomplete_path)
        except ValueError as error:
            assert "incomplete" in str(error)
        else:
            raise AssertionError("paired comparison accepted a partial model render")
        # A target can be copied elsewhere, but changed content, format, note
        # identity or velocity grouping must never produce a paired score.
        copied = root / "copied.wav"
        copied.write_bytes((root / close_examples[0]["target"]).read_bytes())
        alternate = [{**close_examples[0], "target": copied.name}, close_examples[1]]
        alternate_path = write_manifest("alternate.json", alternate)
        assert compare_manifests(root / "close.json", alternate_path)[
            "aggregate"]["score"]["delta"] == 0.0
        for field, value in (("id", "different"), ("midi", 46),
                             ("dynamic_group", "different"),
                             ("target", {"path": copied.name, "playback_trim": 2.0})):
            invalid = [{**close_examples[0], field: value}, close_examples[1]]
            try:
                compare_manifests(root / "close.json", write_manifest("invalid.json", invalid))
            except ValueError:
                pass
            else:
                raise AssertionError(f"paired comparison accepted different {field}")
        wavfile.write(copied, rate, np.ones(rate, dtype=np.float32))
        try:
            compare_manifests(root / "close.json", alternate_path)
        except ValueError:
            pass
        else:
            raise AssertionError("paired comparison accepted changed target samples")

        # Isolate the new descriptor: these differ only in the attack glide.
        pitch_arguments = {
            "latency": 0.004,
            "brightness": 0.72,
            "t60": 4.8,
            "inharmonicity": 8.0e-5,
            "velocity_exponent": 0.82,
        }
        pitch_target = extract_features(_synthetic_note(
            rate, 45, 0.9, **pitch_arguments,
            attack_pitch_cents=14.0, attack_pitch_tau=0.090,
        ), rate, 45)
        pitch_close = extract_features(_synthetic_note(
            rate, 45, 0.9, **pitch_arguments,
            attack_pitch_cents=12.0, attack_pitch_tau=0.085,
        ), rate, 45)
        pitch_far = extract_features(_synthetic_note(
            rate, 45, 0.9, **pitch_arguments,
        ), rate, 45)
        close_pitch = _example_residuals(
            pitch_target, pitch_close
        )["pitch_trajectory"]
        far_pitch = _example_residuals(
            pitch_target, pitch_far
        )["pitch_trajectory"]
        if not np.mean(np.square(close_pitch)) < np.mean(np.square(far_pitch)):
            raise AssertionError("isolated closer pitch trajectory did not win")

        missing_pitch = {**pitch_close, "pitch_trajectory": np.full(3, np.nan)}
        missing_residuals = _example_residuals(
            pitch_target, missing_pitch
        )["pitch_trajectory"]
        expected_count = int(np.count_nonzero(np.isfinite(
            pitch_target["pitch_trajectory"]
        )))
        if (len(missing_residuals) != expected_count
                or any(value != 4.0 for value in missing_residuals)):
            raise AssertionError(
                "missing model pitch did not retain the fixed residual layout"
            )

        # High notes have enough cycles for a two-partial consensus. Keep that
        # guarded path covered without relaxing the three-partial bass rule.
        high_pitch = extract_features(_synthetic_note(
            rate, 84, 0.9,
            latency=0.004, brightness=0.05, t60=4.8,
            inharmonicity=8.0e-5, velocity_exponent=0.82,
            attack_pitch_cents=14.0, attack_pitch_tau=0.090,
        ), rate, 84)["pitch_trajectory"]
        if (not np.all(np.isfinite(high_pitch))
                or not high_pitch[0] > high_pitch[1] > high_pitch[2] > 0.0):
            raise AssertionError("two-partial high-note pitch trajectory failed")

        # Floor mode: two takes of the same note, the second written 6 dB
        # louder at the loud velocity the way a playback trim would write it.
        floor_examples: list[dict[str, Any]] = []
        for velocity in (0.25, 0.90):
            suffix = str(round(velocity * 100))
            second = _synthetic_note(
                rate, 45, velocity, latency=0.0043, brightness=0.71,
                t60=4.7, inharmonicity=8.2e-5, velocity_exponent=0.83,
                attack_pitch_cents=13.0, attack_pitch_tau=0.088,
            )
            trim = 2.0 if velocity > 0.5 else 1.0
            second_path = root / f"take2-{suffix}.f32"
            (second * trim).astype("<f4").tofile(second_path)
            for take, entry in ((0, {"path": f"target-{suffix}.wav",
                                     "playback_trim": 1.0}),
                                (1, {"path": second_path.name,
                                     "sample_rate": rate, "channels": 1,
                                     "playback_trim": trim})):
                floor_examples.append({
                    "id": f"floor-{suffix}-rr{take}",
                    "material": "synthetic",
                    "midi": 45,
                    "velocity": velocity,
                    "round_robin": take,
                    # Both velocities of one take are one dynamic group, so the
                    # model column below has a level term to correct.
                    "dynamic_group": f"synthetic-rr{take}",
                    "target": entry,
                    # Floor mode reads only the target side; the model side is
                    # required by the manifest schema.
                    "model": {"path": f"close-{suffix}.f32",
                              "sample_rate": rate, "channels": 1},
                })
        prepared = PreparedManifest(
            write_manifest("floor.json", floor_examples))
        trimmed = floor_report(prepared)["materials"]["synthetic"]
        if trimmed["pair_count"] != 2:
            raise AssertionError(
                f"floor built {trimmed['pair_count']} pairs, expected 2")
        if trimmed["symmetry_max_term_difference"] > 1.0e-9:
            raise AssertionError(
                "floor is not symmetric: "
                f"{trimmed['symmetry_max_term_difference']}")
        for example in prepared.examples:
            example["playback_trim"] = None
        uncorrected = floor_report(prepared)["materials"]["synthetic"]
        if not (trimmed["floor"]["terms"]["dynamics"]
                < uncorrected["floor"]["terms"]["dynamics"]):
            raise AssertionError(
                "undoing the playback trim did not lower the level floor: "
                f"{trimmed['floor']['terms']['dynamics']} vs "
                f"{uncorrected['floor']['terms']['dynamics']}")
        # The model column has to move with the floor and only on the level
        # term, or the table's two numbers are measured against two targets.
        if (trimmed["model"]["terms"]["dynamics"]
                == uncorrected["model"]["terms"]["dynamics"]):
            raise AssertionError(
                "the trim correction did not reach the model column")
        for name, value in trimmed["model"]["terms"].items():
            other = uncorrected["model"]["terms"][name]
            if name != "dynamics" and value != other:
                raise AssertionError(
                    f"the trim correction moved the {name} term: "
                    f"{value} vs {other}")
        print(
            "self-test passed: closer damped-harmonic model scored "
            f"{close_report['score']:.6f} < {far_report['score']:.6f}; "
            "cancelling spaced pair 1.05-1.5 kHz body residual, matched/legacy: "
            f"mono target {pair['mono_matched']:+.2f}/{pair['mono_mid']:+.2f} dB, "
            f"stereo target {pair['stereo_matched']:+.2f}/{pair['stereo_mid']:+.2f} dB"
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", nargs="?", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--compare", type=Path, metavar="BASELINE",
                        help="compare this candidate with a baseline on identical targets")
    parser.add_argument(
        "--floor", action="store_true",
        help="score each recorded take against another take of the same note",
    )
    parser.add_argument(
        "--control", type=Path,
        help="a manifest whose model side is the sample player, scored on the "
             "same examples for comparison with the floor",
    )
    parser.add_argument(
        "--observation", choices=sorted(OBSERVATIONS),
        help="matched (version 2, the default) or mid (version 1, the L/R "
             "average every score before 2026-10-10 read); overrides the manifest's",
    )
    arguments = parser.parse_args()
    if arguments.self_test:
        self_test()
        return 0
    if arguments.manifest is None:
        parser.error("manifest is required unless --self-test is used")
    observation = arguments.observation
    if arguments.compare is not None:
        if arguments.floor or arguments.control is not None:
            parser.error("--compare cannot be combined with --floor or --control")
        print(json.dumps(compare_manifests(arguments.compare, arguments.manifest,
                                           observation),
                         indent=2, sort_keys=True))
        return 0
    if arguments.floor:
        control = (PreparedManifest(arguments.control, observation)
                   if arguments.control is not None else None)
        print(json.dumps(
            floor_report(PreparedManifest(arguments.manifest, observation), control),
            indent=2, sort_keys=True))
        return 0
    if arguments.control is not None:
        parser.error("--control is only meaningful with --floor")
    print(json.dumps(score_manifest(arguments.manifest, observation),
                     indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
