#!/usr/bin/env python3
"""Cut the CC0 Eastman E1D takes into an isolated-note benchmark corpus.

Protocol (every step is deterministic; no audio enters the repository):

1. Source. ``picked.opus`` (plectrum, ferrosintesis ``DR0000_0192``) and
   ``plucked.opus`` (fingers, ``DR0000_0191``) are Arthur's first-party CC0
   chromatic walks on an Eastman E1D dreadnought. Their SHA-256 values are the
   ones recorded in THIRD_PARTY_NOTICES.md; a mismatch aborts before decoding.
2. Decode. ffmpeg decodes each whole take once to 48 kHz stereo float32, the
   rate at which the existing flat-top benchmark and the sample bank decode
   the same Opus copies (Opus is natively 48 kHz; ffmpeg drops the pre-skip).
3. Events. The onset strength is a log-magnitude spectral flux (1024-sample
   Hann, 5 ms hop, 10 ms lag; the reference frame is maximum-filtered across
   three bins so beating and slight drift do not register). A frame is a
   candidate when it is the maximum within +-30 ms and exceeds both three
   times and 1.5 above the moving median (+-0.75 s): an adaptive threshold.
   A candidate is an event when the energy it adds (the positive per-bin
   power change from the 25 ms before to the 20 ms after) is at least -3 dB
   relative to what is already ringing (+6 dB within 150 ms of the previous
   event, so attack beating is not an event) and 6 dB above the take's
   quietest frames. Every event -- plucks, the fretting hand's hammer-ons of
   the next pitch, squeaks, handling noise -- ends the clean region of the
   note before it; only pitched, isolated events become rows.
4. Onset. Each event's onset is Tools/FitPhysicalModel.py's own ``_onset``
   rule (1 ms energy follower, first sample at -26 dB of the 250 ms window
   peak) iterated to its fixed point on the exact window the target starts
   with. It must converge between 100 ms before and 30 ms after the flux
   peak (a pick or finger contact noise just before the release is part of
   the onset, a later pluck is never borrowed); otherwise the event keeps
   its flux-frame time and cannot become a row. Each written target is read
   back and must measure exactly 20 ms of scorer latency.
5. Damping. A mute or fret release ends a note without a transient. On the
   note's own partials (h <= 12 within +-30 cents, 85 ms frames, 10 ms hop)
   the clean region also ends where the level falls at least 6 dB within
   50 ms and, over 0.2-0.6 s later, stays at least 12 dB (plus the natural
   decay the preceding 0.4 s predicts) below the 100 ms before, provided the
   note was 15 dB above its floor. Beating recovers within the look-ahead
   and natural decay is neither that steep nor faster than its own recent
   rate; gradual light mutes are deliberately left in.
6. Pitch. The settled f0 comes from a harmonic sum over 0.40-1.20 s after
   the onset (the scorer's settled-tuning window): a 2^19-point spectrum of
   the Hann-windowed mono mix, per-partial excess over a local median noise
   floor (capped at 45 dB), eight partials with a stretched-partial
   tolerance, on a 2-cent candidate grid from 72 Hz to 1.5 kHz. The winner is
   re-centred on its measured partials, then moved an octave down only when
   the odd partials of f/2 are within 10 dB of its even ones (sympathetic
   open strings ring 20-30 dB weaker) and an octave up when three of its
   own first four odd partials are absent: subharmonic-safe both ways.
   f_h = h f0 sqrt(1 + B h^2) is fitted to the interpolated partial peaks;
   ``measured_f0_hz`` is the H1 peak when it stands 25 dB clear of the floor
   (normally the very peak the scorer's own H1 tuning reads), else the
   fitted f0.
   The same estimate over 0.05-0.30 s must agree (within 35 cents; below
   170 Hz an integer multiple is allowed for a weak attack H1), so a
   sympathetic open string outlasting a fast high note is not its pitch.
7. Tuning. Each take's global offset is the circular median (in cents) of the
   fractional semitone of its confidently pitched, isolated notes. ``midi``
   is the nearest note after removing that offset; ``cents_from_midi`` is the
   raw deviation from 440 Hz equal temperament (it still contains the
   offset, which each row's notes repeat).
8. Selection. A row needs clean_seconds >= 1.25 s and isolation_db >= 30 dB
   (isolation = sample peak of the first 300 ms minus the RMS level of the
   100 ms before the onset, both over the two channels), a scorer-located
   onset, a confident harmonic fit (>= 3 partials including H1 or H2) that
   the attack agrees with, and a pitch within 40 cents of the take's tuning. ``clean_seconds`` is
   min(next event - 20 ms, damping, onset + 4.2 s) - onset.
9. Targets. Each target is float32 little-endian stereo at 48 kHz, from 20 ms
   before the onset to onset + clean_seconds, with a 60 ms terminal
   half-cosine fade and no gain change. ``velocity`` is 91 for every row,
   the level the existing flat-top split renders this session at; the
   natural level spread stays visible in ``peak_dbfs``. ``round_robin`` is
   the occurrence index of that MIDI note among the take's rows.

Splits: ``flattop-pick`` (picked take, picking "pick") and
``flattop-finger-all`` (plucked take, picking "finger").

Outputs, in --out: rows.json (shared corpus schema), targets/<id>.f32,
analysis.json (every event, its measurements and its rejection reason) and
SOURCE.md (source URL, licence and hashes). The eight README anchors per take
are checked: each must be detected within 50 ms with the expected MIDI.

Usage:

    python3 Tools/PrepareEastmanCorpus.py --source DIR --out DIR \
        [--ffmpeg /opt/homebrew/bin/ffmpeg]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import numpy as np
from scipy.ndimage import maximum_filter1d, median_filter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import FitPhysicalModel as scorer  # noqa: E402  (onset rule shared with scorer)


CORPUS = "eastman"
LICENSE = "CC0-1.0"
LICENSE_URL = "https://creativecommons.org/publicdomain/zero/1.0/"
SOURCE_URL = (
    "https://github.com/0x4D44/ferrosintesis/tree/"
    "810318c92e33e31b36638b0ffa7ffc834a2ae6a2/samples/acoustic-guitar-eastman-e1d"
)
RATE = 48_000
TAKES = {
    "picked": {
        "file": "picked.opus",
        "bytes": 5_957_761,
        "sha256": "35e9e45b42a70f2fada2c9d93bf809d9046562fa3fa40cbac415ef75b92d926d",
        "split": "flattop-pick",
        "picking": "pick",
        "master": "DR0000_0192.wav",
        # README "Zone slice map (verified anchors)": onset seconds, MIDI.
        "anchors": [(1.720, 40), (44.490, 47), (81.150, 52), (132.250, 58),
                    (180.480, 64), (211.480, 70), (244.080, 77), (276.320, 83)],
    },
    "plucked": {
        "file": "plucked.opus",
        "bytes": 5_945_086,
        "sha256": "16f5a8c7cde555441143243f264a03eff142cb8aafb6924236a82755a0396ce0",
        "split": "flattop-finger-all",
        "picking": "finger",
        "master": "DR0000_0191.wav",
        "anchors": [(5.700, 40), (64.120, 46), (102.840, 52), (139.040, 58),
                    (189.450, 64), (228.900, 71), (254.100, 77), (282.050, 83)],
    },
}
VELOCITY = 91

PREROLL = 0.020
MAX_AFTER = 4.2
NEXT_GUARD = 0.020
FADE = 0.060
MIN_CLEAN = 1.25
MIN_ISOLATION = 30.0
PEAK_WINDOW = 0.300
ANCHOR_TOLERANCE = 0.050

# Onset strength.
FRAME = 1024
HOP = 240
LAG = 2
LOG_GAIN = 1.0e4
_BINS = np.fft.rfftfreq(FRAME, 1.0 / RATE)
BAND = (_BINS >= 40.0) & (_BINS <= 16_000.0)
PEAK_HALF_WIDTH = 6        # frames: +-30 ms local maximum
MEDIAN_HALF_WIDTH = 150    # frames: +-0.75 s moving median
FLUX_RATIO = 5.0
FLUX_MARGIN = 1.5
SIGNIFICANCE_DB = -3.0     # added energy vs. what is already ringing
REFRACTORY_SECONDS = 0.300
REFRACTORY_SIGNIFICANCE_DB = 6.0
AUDIBLE_DB = 6.0           # added energy above the take's quietest frames
MERGE_SECONDS = 0.030
ONSET_EARLIEST = 0.100    # scorer-rule onset may precede the flux peak by this much
ONSET_LATEST = 0.030      # ... and follow it by at most this much
FLUX_LEAD = 0.015         # otherwise the event starts this long before the frame centre

# Pitch.
SETTLED = (0.400, 1.200)
ATTACK_PITCH = (0.050, 0.300)
ATTACK_AGREEMENT_CENTS = 35.0
ATTACK_MULTIPLES_BELOW_HZ = 170.0
F0_RANGE = (72.0, 1500.0)
F0_STEP_CENTS = 2.0
PARTIALS = 8
PITCH_FFT = 1 << 19
EXCESS_CAP = 45.0
CONFIDENT_EXCESS = 15.0
STRONG_H1_EXCESS = 25.0
SUBOCTAVE_MARGIN_DB = 10.0
REFINE_SEARCH_CENTS = 50.0
REFINE_CENTS = 20.0

# Damping.
LEVEL_HOP = 0.010
DAMP_FRAME = 4096
DAMP_FIRST = 0.150
DAMP_EARLIEST = 0.300
DAMP_DROP_TIME = 0.200
DAMP_RECOVERY = 0.600
DAMP_DROP_DB = 12.0
DAMP_ABOVE_FLOOR = 15.0
DAMP_STEEP_DB = 6.0        # the cut itself: at least this fall within 50 ms


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def decode(ffmpeg: str, path: Path) -> np.ndarray:
    raw = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(path), "-map", "0:a:0", "-ac", "2",
         "-ar", str(RATE), "-c:a", "pcm_f32le", "-f", "f32le", "-"],
        check=True, stdout=subprocess.PIPE,
    ).stdout
    audio = np.frombuffer(raw, dtype="<f4").reshape(-1, 2).astype(np.float64)
    if audio.shape[0] < RATE:
        raise RuntimeError(f"{path}: decoded audio is too short")
    return audio


# --------------------------------------------------------------------------
# Event detection


def frame_power(mono: np.ndarray, frames: np.ndarray) -> np.ndarray:
    """Per-bin power (sine-amplitude scale) of 1024-sample Hann frames."""
    window = np.hanning(FRAME + 1)[:-1]
    scale = 2.0 / float(np.sum(window))
    index = frames[:, None] * HOP + np.arange(FRAME)[None, :]
    magnitude = np.abs(np.fft.rfft(mono[index] * window, axis=1))[:, BAND] * scale
    return magnitude * magnitude


def onset_strength(mono: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Log spectral flux per 5 ms frame, and each frame's total power (dB).

    The reference frame (LAG frames earlier) is maximum-filtered across three
    bins so slow beating and small pitch drift do not register as flux.
    """
    count = 1 + (mono.size - FRAME) // HOP
    logs = np.empty((count, int(np.count_nonzero(BAND))), dtype=np.float32)
    total_db = np.empty(count)
    for first in range(0, count, 4096):
        last = min(count, first + 4096)
        power = frame_power(mono, np.arange(first, last))
        logs[first:last] = np.log10(1.0 + LOG_GAIN * np.sqrt(power))
        total_db[first:last] = 10.0 * np.log10(np.maximum(np.sum(power, axis=1), 1.0e-20))
    reference = maximum_filter1d(logs, size=3, axis=1)
    flux = np.zeros(count)
    flux[LAG:] = np.sum(np.maximum(0.0, logs[LAG:] - reference[:-LAG]), axis=1)
    return flux, total_db


def pick_events(mono: np.ndarray, flux: np.ndarray, total_db: np.ndarray
                ) -> tuple[list[dict[str, float]], float]:
    """Adaptive-threshold flux peaks that add energy comparable to the ringing."""
    local_max = maximum_filter1d(flux, size=2 * PEAK_HALF_WIDTH + 1)
    moving_median = median_filter(flux, size=2 * MEDIAN_HALF_WIDTH + 1, mode="nearest")
    candidates = np.flatnonzero(
        (flux >= local_max)
        & (flux >= FLUX_RATIO * moving_median)
        & (flux >= moving_median + FLUX_MARGIN)
    )
    candidates = candidates[(candidates >= 9) & (candidates < flux.size - 5)]
    noise_db = float(np.percentile(total_db, 5))
    refractory = round(REFRACTORY_SECONDS * RATE / HOP)
    events: list[dict[str, float]] = []
    for frame in candidates.tolist():
        power = frame_power(mono, np.arange(frame - 9, frame + 5))
        before = np.mean(power[0:5], axis=0)       # frames n-9 .. n-5
        after = np.mean(power[10:14], axis=0)      # frames n+1 .. n+4
        added = float(np.sum(np.maximum(0.0, after - before)))
        added_db = 10.0 * math.log10(max(added, 1.0e-20))
        significance = added_db - 10.0 * math.log10(max(float(np.sum(before)), 1.0e-20))
        near = bool(events) and frame - int(events[-1]["frame"]) <= refractory
        needed = REFRACTORY_SIGNIFICANCE_DB if near else SIGNIFICANCE_DB
        if significance < needed or added_db < noise_db + AUDIBLE_DB:
            continue
        if events and frame - int(events[-1]["frame"]) < round(MERGE_SECONDS * RATE / HOP):
            continue
        events.append({"frame": frame, "flux_ratio": float(flux[frame] / max(moving_median[frame], 1e-9)),
                       "significance_db": significance, "added_db": added_db})
    return events, noise_db


def refine_onset(mono: np.ndarray, approximate: int) -> tuple[int, bool]:
    """Iterate the scorer's onset rule on the exact target window.

    Returns the onset and whether the scorer's rule located it. The rule
    finds the first sample at -26 dB of the peak of the 250 ms window that
    will start the target; its fixed point is the onset whose 20 ms pre-roll
    stays below that threshold. It is trusted when it converges between
    ONSET_EARLIEST before and ONSET_LATEST after the flux peak (so a pick or
    finger contact noise just before the release is the onset, but a later,
    louder pluck is never borrowed). Otherwise -- a weak event inside a
    louder tail, or low-frequency rumble above the threshold -- the event
    keeps its flux-frame time and cannot become a row.
    """
    # The scorer convolves the whole target before taking the 250 ms peak, so
    # hand it a slightly longer slice to reproduce its energy at the edge.
    window = round(0.250 * RATE) + round(0.002 * RATE)
    preroll = round(PREROLL * RATE)
    earliest = approximate - round(ONSET_EARLIEST * RATE)
    latest = approximate + round(ONSET_LATEST * RATE)
    fallback = max(0, approximate - round(FLUX_LEAD * RATE))
    start = max(0, approximate - round(0.040 * RATE))
    onset = start + scorer._onset(mono[start:start + window], RATE)
    for _ in range(12):
        if not earliest <= onset <= latest or onset < preroll:
            return fallback, False
        start = onset - preroll
        updated = start + scorer._onset(mono[start:start + window], RATE)
        if updated == onset:
            return onset, True
        onset = updated
    return fallback, False


# --------------------------------------------------------------------------
# Damping


def own_partial_level(mono: np.ndarray, onset: int, end: int, f0: float,
                      inharmonicity: float) -> tuple[np.ndarray, np.ndarray]:
    """Level (dB) of the note's own partials, 85 ms frames every 10 ms."""
    size = DAMP_FRAME
    hop = round(LEVEL_HOP * RATE)
    frequency = np.fft.rfftfreq(size, 1.0 / RATE)
    mask = np.zeros(frequency.size, dtype=bool)
    step = frequency[1]
    for harmonic in range(1, 13):
        centre = harmonic * f0 * math.sqrt(1.0 + max(0.0, inharmonicity) * harmonic ** 2)
        if centre >= 10_000.0:
            break
        width = max(1.01 * step, centre * (2.0 ** (30.0 / 1200.0) - 1.0))
        mask |= np.abs(frequency - centre) <= width
    centres = np.arange(onset + round(DAMP_FIRST * RATE), end - size // 2, hop)
    if centres.size < 10:
        return np.empty(0), np.empty(0)
    window = np.hanning(size + 1)[:-1]
    levels = np.empty(centres.size)
    for first in range(0, centres.size, 256):
        block = centres[first:first + 256]
        index = (block - size // 2)[:, None] + np.arange(size)[None, :]
        power = np.abs(np.fft.rfft(mono[index] * window, axis=1)[:, mask]) ** 2
        levels[first:first + block.size] = 10.0 * np.log10(np.maximum(np.sum(power, axis=1), 1e-20))
    return (centres - onset) / RATE, levels


def damping_time(times: np.ndarray, levels: np.ndarray) -> float | None:
    """Seconds after onset where a mute or fret release cuts the note.

    A damping is a fast drop of the note's own-partial level that does not
    recover: the maximum over [t + DAMP_DROP_TIME, t + DAMP_RECOVERY] lies at
    least DAMP_DROP_DB (plus the natural decay the preceding 0.4 s predicts
    over DAMP_DROP_TIME) below the maximum of the 100 ms before t. Beating
    recovers within the look-ahead, and natural decay neither falls
    DAMP_STEEP_DB within 50 ms nor outpaces its own recent rate, so neither
    trips it; gradual, light mutes are deliberately left alone. The reported
    time is the frame before the steepest 50 ms fall, still at the old level.
    """
    if times.size < 30:
        return None
    smooth = maximum_filter1d(levels, size=7)
    floor = float(np.min(smooth))
    pre = round(0.100 / LEVEL_HOP)
    drop = round(DAMP_DROP_TIME / LEVEL_HOP)
    recovery = round(DAMP_RECOVERY / LEVEL_HOP)
    history = round(0.400 / LEVEL_HOP)
    fall = round(0.050 / LEVEL_HOP)
    for index in range(times.size):
        if times[index] < DAMP_EARLIEST:
            continue
        if index + recovery >= times.size:
            break
        before = float(np.max(smooth[max(0, index - pre):index + 1]))
        after = float(np.max(smooth[index + drop:index + recovery + 1]))
        earlier = max(0, index - history)
        rate = 0.0
        if index - earlier >= 10:
            # Least-squares decay rate of the preceding 0.4 s (dB/s, >= 0).
            t = times[earlier:index + 1] - times[earlier:index + 1].mean()
            y = levels[earlier:index + 1]
            rate = max(0.0, -float(np.dot(t, y - y.mean()) / np.dot(t, t)))
        if (before - after < DAMP_DROP_DB + rate * DAMP_DROP_TIME
                or before < floor + DAMP_ABOVE_FLOOR):
            continue
        span = np.arange(max(0, index - pre), min(times.size - fall, index + drop + 1))
        falls = levels[span] - levels[span + fall]
        if float(np.max(falls)) < DAMP_STEEP_DB:
            continue  # a steady, fast natural decay, not a cut
        return float(times[span[int(np.argmax(falls))]])
    return None


# --------------------------------------------------------------------------
# Pitch


def settled_spectrum(mono: np.ndarray, onset: int, begin: float, end: float
                     ) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    first = onset + round(begin * RATE)
    last = min(mono.size, onset + round(end * RATE))
    segment = mono[first:last] - np.mean(mono[first:last])
    window = np.hanning(segment.size + 1)[:-1]
    spectrum = np.abs(np.fft.rfft(segment * window, PITCH_FFT))
    spectrum *= 2.0 / float(np.sum(window))
    frequency = np.fft.rfftfreq(PITCH_FFT, 1.0 / RATE)
    level = 20.0 * np.log10(np.maximum(spectrum, 1.0e-12))
    # Local noise floor: median of 4 Hz cell maxima over +-60 Hz.
    cell = max(1, round(4.0 / (frequency[1] - frequency[0])))
    usable = level.size // cell * cell
    cells = level[:usable].reshape(-1, cell).max(axis=1)
    floor_cells = median_filter(cells, size=31, mode="nearest")
    floor = np.repeat(floor_cells, cell)
    floor = np.concatenate((floor, np.full(level.size - floor.size, floor[-1])))
    return frequency, level, floor


def partial_excess(frequency: np.ndarray, level: np.ndarray, floor: np.ndarray,
                   centre: float, low_cents: float, high_cents: float
                   ) -> tuple[float, float]:
    step = frequency[1] - frequency[0]
    first = int(math.floor(centre * 2.0 ** (-low_cents / 1200.0) / step))
    last = int(math.ceil(centre * 2.0 ** (high_cents / 1200.0) / step)) + 1
    if first < 1 or last >= level.size - 1:
        return 0.0, math.nan
    index = first + int(np.argmax(level[first:last]))
    excess = float(level[index] - floor[index])
    adjacent = level[index - 1:index + 2]
    denominator = adjacent[0] - 2.0 * adjacent[1] + adjacent[2]
    offset = 0.0
    if abs(denominator) > 1.0e-12:
        offset = float(np.clip(0.5 * (adjacent[0] - adjacent[2]) / denominator, -0.5, 0.5))
    return max(0.0, excess), float(frequency[index] + offset * step)


def partial_window(harmonic: int) -> tuple[float, float]:
    # Steel partials are stretched sharp by bending stiffness.
    return 25.0, 25.0 + 1.5 * harmonic * harmonic ** 0.5


def harmonic_profile(frequency, level, floor, f0: float, inharmonicity: float | None = None,
                     cap: bool = True, search_cents: float | None = None
                     ) -> tuple[np.ndarray, np.ndarray]:
    """Per-partial excess over the floor and interpolated peak frequency.

    Without an inharmonicity the windows are the asymmetric stretched-partial
    tolerance of the candidate grid (widened to +-search_cents when given);
    with one they are +-REFINE_CENTS around h f0 sqrt(1 + B h^2).
    """
    excess = np.zeros(PARTIALS)
    found = np.full(PARTIALS, np.nan)
    for harmonic in range(1, PARTIALS + 1):
        if inharmonicity is None:
            centre = harmonic * f0
            low, high = partial_window(harmonic)
            if search_cents is not None:
                low, high = max(low, search_cents), max(high, search_cents)
        else:
            centre = harmonic * f0 * math.sqrt(1.0 + max(0.0, inharmonicity) * harmonic ** 2)
            low = high = REFINE_CENTS
        if centre >= 0.45 * RATE:
            break
        excess[harmonic - 1], found[harmonic - 1] = partial_excess(
            frequency, level, floor, centre, low, high)
    return (np.minimum(excess, EXCESS_CAP) if cap else excess), found


def fit_series(profile: np.ndarray, found: np.ndarray) -> tuple[float, float, np.ndarray]:
    """Weighted fit of f_h = h f0 sqrt(1 + B h^2) to the confident partials."""
    usable = np.flatnonzero((profile >= CONFIDENT_EXCESS) & np.isfinite(found))
    if usable.size < 2:
        return math.nan, math.nan, usable
    harmonics = usable + 1.0
    measured = found[usable]
    weight = profile[usable]
    # f_h^2 / h^2 = f0^2 + f0^2 B h^2 is linear in (f0^2, f0^2 B).
    design = np.column_stack((np.ones(harmonics.size), harmonics ** 2))
    target = (measured / harmonics) ** 2
    solution, *_ = np.linalg.lstsq(design * weight[:, None], target * weight, rcond=None)
    f0_squared, stiffness = float(solution[0]), float(solution[1])
    if f0_squared <= 0.0 or stiffness < 0.0 or usable.size < 3:
        # Too few partials to separate stiffness: weighted mean of f_h / h.
        return float(np.sum(weight * measured / harmonics) / np.sum(weight)), 0.0, usable
    return math.sqrt(f0_squared), stiffness / f0_squared, usable


def refine(frequency, level, floor, f0: float) -> tuple[float, float]:
    """Re-centre a grid estimate on the measured partial series."""
    profile, found = harmonic_profile(frequency, level, floor, f0, search_cents=REFINE_SEARCH_CENTS)
    fitted, inharmonicity, _ = fit_series(profile, found)
    if not math.isfinite(fitted):
        return f0, 0.0
    for _ in range(3):
        profile, found = harmonic_profile(frequency, level, floor, fitted, inharmonicity)
        updated, stiffness, _ = fit_series(profile, found)
        if not math.isfinite(updated):
            break
        converged = abs(1200.0 * math.log2(updated / fitted)) < 0.05
        fitted, inharmonicity = updated, stiffness
        if converged:
            break
    return fitted, inharmonicity


def salience_argmax(frequency: np.ndarray, excess: np.ndarray) -> float:
    """Best f0 on the candidate grid by weighted, capped harmonic sum.

    The excess spectrum is max-pooled onto a 1-cent logarithmic grid, so each
    partial's stretched tolerance window is one running maximum per harmonic.
    """
    low = F0_RANGE[0] * 2.0 ** (-50.0 / 1200.0)
    high = min(0.45 * RATE, F0_RANGE[1] * PARTIALS * 2.0 ** (100.0 / 1200.0))
    cells = int(math.ceil(1200.0 * math.log2(high / low)))
    selected = (frequency >= low) & (frequency < high)
    cell = np.floor(1200.0 * np.log2(frequency[selected] / low)).astype(int)
    grid = np.zeros(cells + 1)
    np.maximum.at(grid, cell, np.minimum(excess[selected], EXCESS_CAP))
    # Below ~400 Hz a cent is narrower than an FFT bin; fill empty cells.
    centres = low * 2.0 ** ((np.arange(cells + 1) + 0.5) / 1200.0)
    occupied = np.zeros(cells + 1, dtype=bool)
    occupied[cell] = True
    grid[~occupied] = np.interp(centres[~occupied], frequency[selected],
                                np.minimum(excess[selected], EXCESS_CAP))
    count = int(math.floor(1200.0 * math.log2(F0_RANGE[1] / F0_RANGE[0]) / F0_STEP_CENTS))
    candidate_cents = 50.0 + np.arange(count + 1) * F0_STEP_CENTS
    salience = np.zeros(count + 1)
    for harmonic in range(1, PARTIALS + 1):
        below, above = partial_window(harmonic)
        span = int(round(below + above)) + 1
        # Window [c - below, c + above] maximum, anchored at its low edge.
        pooled = maximum_filter1d(grid, size=span, origin=-(span // 2), mode="constant")
        index = np.round(candidate_cents + 1200.0 * math.log2(harmonic) - below).astype(int)
        valid = (index >= 0) & (index < grid.size)
        contribution = np.zeros(count + 1)
        contribution[valid] = pooled[index[valid]]
        salience += harmonic ** -0.3 * contribution
    return float(F0_RANGE[0] * 2.0 ** ((candidate_cents[int(np.argmax(salience))] - 50.0)
                                       / 1200.0))


def estimate_pitch(mono: np.ndarray, onset: int, begin: float, end: float) -> dict[str, Any]:
    frequency, level, floor = settled_spectrum(mono, onset, begin, end)
    grid = float(salience_argmax(frequency, np.maximum(0.0, level - floor)))
    best, inharmonicity = refine(frequency, level, floor, grid)
    moves: list[str] = []
    for _ in range(3):
        # f/2 must carry odd partials comparable to its even ones (the
        # partials of f). Sympathetic open strings a twelfth or an octave
        # below ring 20-30 dB weaker and must not pull the estimate down.
        lower = best / 2.0
        if lower < F0_RANGE[0]:
            break
        profile, _ = harmonic_profile(frequency, level, floor, lower, inharmonicity, cap=False)
        odd, even = profile[0::2][:4], profile[1::2][:4]
        if (float(np.mean(odd)) >= float(np.mean(even)) - SUBOCTAVE_MARGIN_DB
                and np.count_nonzero(odd >= CONFIDENT_EXCESS) >= 2):
            best, inharmonicity = refine(frequency, level, floor, lower)
            moves.append("down")
        else:
            break
    for _ in range(3):
        profile, _ = harmonic_profile(frequency, level, floor, best, inharmonicity)
        odd = profile[0::2][:4]
        if np.count_nonzero(odd < 6.0) >= 3 and 2.0 * best <= F0_RANGE[1]:
            best, inharmonicity = refine(frequency, level, floor, 2.0 * best)
            moves.append("up")
        else:
            break
    profile, found = harmonic_profile(frequency, level, floor, best, inharmonicity)
    fitted, inharmonicity, usable = fit_series(profile, found)
    result: dict[str, Any] = {
        "grid_f0_hz": grid, "octave_moves": moves,
        "partial_excess_db": [round(float(v), 1) for v in profile],
        "partials_used": int(usable.size),
    }
    if not math.isfinite(fitted):
        result.update(f0_hz=math.nan, inharmonicity=math.nan, confident=False)
        return result
    first_partial = float(found[0]) if profile[0] >= CONFIDENT_EXCESS else math.nan
    confident = bool(usable.size >= 3 and (0 in usable or 1 in usable))
    # The settled f0 is the measured fundamental when it stands clear of the
    # floor; otherwise the stiff-string fit's f0 stands in for a weak H1.
    strong = profile[0] >= STRONG_H1_EXCESS
    result.update(f0_hz=first_partial if strong else fitted, fit_f0_hz=fitted,
                  inharmonicity=inharmonicity, h1_hz=first_partial,
                  f0_source="H1" if strong else "fit", confident=confident)
    return result


# --------------------------------------------------------------------------
# Take analysis


def analyse_take(name: str, audio: np.ndarray) -> dict[str, Any]:
    mono = audio.mean(axis=1)
    flux, total_db = onset_strength(mono)
    detected, frame_floor_db = pick_events(mono, flux, total_db)
    refined = []
    for item in detected:
        onset, located = refine_onset(mono, int(item["frame"]) * HOP + FRAME // 2)
        refined.append((onset, dict(item, scorer_rule_onset=located)))
    refined.sort(key=lambda entry: entry[0])
    onsets: list[tuple[int, dict[str, Any]]] = []
    for onset, item in refined:
        if onsets and onset - onsets[-1][0] < round(MERGE_SECONDS * RATE):
            # One physical event: keep the earlier onset (and whether the
            # scorer's rule located it) with the stronger candidate's numbers.
            kept_onset, kept = onsets[-1]
            if item["significance_db"] > kept["significance_db"]:
                onsets[-1] = (kept_onset, dict(item, scorer_rule_onset=kept["scorer_rule_onset"]))
            continue
        onsets.append((onset, item))
    block = round(0.100 * RATE)
    blocks = audio[:audio.shape[0] // block * block].reshape(-1, block * 2)
    noise_db = float(np.percentile(10.0 * np.log10(np.maximum(
        np.mean(blocks * blocks, axis=1), 1.0e-14)), 5))
    events: list[dict[str, Any]] = []
    for index, (onset, item) in enumerate(onsets):
        next_onset = onsets[index + 1][0] if index + 1 < len(onsets) else audio.shape[0]
        limit = min(next_onset - round(NEXT_GUARD * RATE), onset + round(MAX_AFTER * RATE),
                    audio.shape[0])
        pre = audio[max(0, onset - round(0.100 * RATE)):onset]
        pre_db = 10.0 * math.log10(max(float(np.mean(pre * pre)), 1.0e-14)) if pre.size else -140.0
        peak = float(np.max(np.abs(audio[onset:max(onset + 1, min(
            onset + round(PEAK_WINDOW * RATE), limit))])))
        peak_db = 20.0 * math.log10(max(peak, 1.0e-12))
        event: dict[str, Any] = {
            "onset_seconds": onset / RATE,
            "onset_sample": onset,
            "flux_ratio": item["flux_ratio"],
            "significance_db": item["significance_db"],
            "scorer_rule_onset": item["scorer_rule_onset"],
            "next_event_seconds": next_onset / RATE,
            "peak_dbfs": peak_db,
            "pre_rms_dbfs": pre_db,
            "isolation_db": peak_db - pre_db,
            "damping_seconds": None,
            "end_by": "4.2 s cap" if limit == onset + round(MAX_AFTER * RATE) else "next event",
        }
        end = limit
        if (limit - onset) / RATE >= SETTLED[1]:
            pitch = estimate_pitch(mono, onset, *SETTLED)
            event["pitch"] = pitch
            # The plucked note owns the attack. A settled estimate that the
            # attack does not share (a sympathetic open string outlasting a
            # fast high note, or a second string ringing an octave below) is
            # not that note's pitch. Only on the wound bass strings may a weak
            # attack H1 put the attack estimate on an integer multiple.
            early = estimate_pitch(mono, onset, *ATTACK_PITCH)
            pitch["attack_f0_hz"] = early["f0_hz"]
            ratio = early["f0_hz"] / pitch["f0_hz"] if math.isfinite(pitch["f0_hz"]) else math.nan
            multiple = round(ratio) if math.isfinite(ratio) else 0
            allowed = (1, 2, 3, 4) if pitch["f0_hz"] < ATTACK_MULTIPLES_BELOW_HZ else (1,)
            pitch["attack_agrees"] = bool(
                multiple in allowed
                and abs(1200.0 * math.log2(ratio / multiple)) <= ATTACK_AGREEMENT_CENTS)
            if math.isfinite(pitch["f0_hz"]):
                times, levels = own_partial_level(mono, onset, limit, pitch["f0_hz"],
                                                  pitch["inharmonicity"])
                damped = damping_time(times, levels)
                if damped is not None and onset + round(damped * RATE) < limit:
                    event["damping_seconds"] = damped
                    event["end_by"] = "damping"
                    end = onset + round(damped * RATE)
        elif (limit - onset) / RATE >= 0.30:
            event["pitch"] = estimate_pitch(mono, onset, 0.08, (limit - onset) / RATE)
            event["pitch"]["confident"] = False
            event["pitch"]["window"] = [0.08, (limit - onset) / RATE]
        event["end_sample"] = end
        event["clean_seconds"] = (end - onset) / RATE
        events.append(event)
    confident = [event for event in events
                 if event.get("pitch", {}).get("confident")
                 and event["pitch"].get("attack_agrees")
                 and event["scorer_rule_onset"]
                 and event["isolation_db"] >= MIN_ISOLATION
                 and event["clean_seconds"] >= MIN_CLEAN]
    semitones = [69.0 + 12.0 * math.log2(event["pitch"]["f0_hz"] / 440.0)
                 for event in confident]
    if not semitones:
        raise RuntimeError(f"{name}: no confidently pitched isolated notes")
    angles = np.array([2.0 * math.pi * (value - round(value)) for value in semitones])
    mean_angle = math.atan2(float(np.mean(np.sin(angles))), float(np.mean(np.cos(angles))))
    # Circular median seeded at the circular mean.
    wrapped = [((value - round(value)) * 100.0 - mean_angle / (2.0 * math.pi) * 100.0 + 50.0)
               % 100.0 - 50.0 for value in semitones]
    offset = float(mean_angle / (2.0 * math.pi) * 100.0 + np.median(wrapped))
    for event in events:
        pitch = event.get("pitch")
        if not pitch or not math.isfinite(pitch.get("f0_hz", math.nan)):
            continue
        semitone = 69.0 + 12.0 * math.log2(pitch["f0_hz"] / 440.0)
        midi = int(round(semitone - offset / 100.0))
        pitch["midi"] = midi
        pitch["cents_from_midi"] = 100.0 * (semitone - midi)
        pitch["cents_from_take_tuning"] = 100.0 * (semitone - midi) - offset
    return {"events": events, "tuning_offset_cents": offset, "noise_floor_dbfs": noise_db,
            "frame_floor_db": frame_floor_db, "confident_notes": len(confident)}


def reject_reason(event: dict[str, Any]) -> str | None:
    pitch = event.get("pitch")
    reasons = []
    if event["clean_seconds"] < MIN_CLEAN:
        reasons.append(f"clean {event['clean_seconds']:.2f} s < {MIN_CLEAN}")
    if event["isolation_db"] < MIN_ISOLATION:
        reasons.append(f"isolation {event['isolation_db']:.1f} dB < {MIN_ISOLATION:g}")
    if not event["scorer_rule_onset"]:
        reasons.append("scorer onset rule does not locate this event")
    if not pitch or not pitch.get("confident"):
        reasons.append("no confident harmonic fit")
    elif not pitch.get("attack_agrees"):
        reasons.append(f"attack pitch {pitch.get('attack_f0_hz', math.nan):.1f} Hz is not"
                       f" the settled {pitch['f0_hz']:.1f} Hz series")
    elif abs(pitch["cents_from_take_tuning"]) > 40.0:
        reasons.append(f"{pitch['cents_from_take_tuning']:+.0f} cents from the take tuning")
    return "; ".join(reasons) if reasons else None


def write_target(path: Path, audio: np.ndarray, onset: int, end: int) -> int:
    start = onset - round(PREROLL * RATE)
    if start < 0:
        raise RuntimeError(f"{path.name}: onset lacks a 20 ms pre-roll")
    clip = audio[start:end].astype(np.float64, copy=True)
    fade = round(FADE * RATE)
    ramp = 0.5 * (1.0 + np.cos(np.pi * (np.arange(fade) + 1.0) / fade))
    clip[-fade:] *= ramp[:, None]
    clip.astype("<f4").tofile(path)
    return clip.shape[0]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, required=True,
                        help="directory holding picked.opus and plucked.opus")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg") or "ffmpeg")
    args = parser.parse_args()

    hashes: dict[str, str] = {}
    for take in TAKES.values():
        path = args.source / take["file"]
        digest = sha256(path)
        if digest != take["sha256"] or path.stat().st_size != take["bytes"]:
            raise SystemExit(f"{path}: SHA-256 {digest} does not match THIRD_PARTY_NOTICES.md")
        hashes[take["file"]] = digest

    targets = args.out / "targets"
    targets.mkdir(parents=True, exist_ok=True)
    for stale in targets.glob(f"{CORPUS}-*.f32"):
        stale.unlink()
    rows: list[dict[str, Any]] = []
    analysis: dict[str, Any] = {"corpus": CORPUS, "rate": RATE, "takes": {}}
    anchor_failures: list[str] = []
    for name, take in TAKES.items():
        audio = decode(args.ffmpeg, args.source / take["file"])
        result = analyse_take(name, audio)
        offset = result["tuning_offset_cents"]
        occurrences: dict[int, int] = {}
        for event in result["events"]:
            reason = reject_reason(event)
            event["rejected"] = reason
            if reason:
                continue
            pitch = event["pitch"]
            midi = pitch["midi"]
            round_robin = occurrences.get(midi, 0)
            row_id = f"{CORPUS}-{name}-m{midi}-rr{round_robin}"
            path = targets / f"{row_id}.f32"
            frames = write_target(path, audio, event["onset_sample"], event["end_sample"])
            # The scorer must measure exactly the 20 ms pre-roll on the file.
            written = np.fromfile(path, dtype="<f4").astype(np.float64).reshape(-1, 2).mean(axis=1)
            latency = scorer._onset(written, RATE)
            if latency != round(PREROLL * RATE):
                path.unlink()
                event["rejected"] = f"scorer measures {latency / RATE * 1000:.1f} ms latency on the target"
                continue
            occurrences[midi] = round_robin + 1
            event["row_id"] = row_id
            notes = (f"{take['master']} take; settled f0 ({pitch['f0_source']}) from"
                     f" {pitch['partials_used']} partials, B={pitch['inharmonicity']:.2e};"
                     f" take tuning {offset:+.1f} cents, this note"
                     f" {pitch['cents_from_take_tuning']:+.1f} from it; ends at {event['end_by']};"
                     f" {frames} frames")
            rows.append({
                "id": row_id,
                "split": take["split"],
                "material": "steel",
                "picking": take["picking"],
                "midi": midi,
                "velocity": VELOCITY,
                "round_robin": round_robin,
                "dynamic_group": None,
                "dynamic_marking": None,
                "target": {"path": f"targets/{row_id}.f32", "sample_rate": RATE, "channels": 2},
                "onset_seconds_in_source": round(event["onset_seconds"], 6),
                "isolation_db": round(event["isolation_db"], 2),
                "clean_seconds": round(event["clean_seconds"], 4),
                "measured_f0_hz": round(pitch["f0_hz"], 3),
                "cents_from_midi": round(pitch["cents_from_midi"], 2),
                "peak_dbfs": round(event["peak_dbfs"], 2),
                "notes": notes,
            })
        anchors = []
        for seconds, midi in take["anchors"]:
            nearest = min(result["events"], key=lambda e: abs(e["onset_seconds"] - seconds))
            detected = nearest.get("pitch", {}).get("midi")
            ok = abs(nearest["onset_seconds"] - seconds) <= ANCHOR_TOLERANCE and detected == midi
            anchors.append({"readme_seconds": seconds, "readme_midi": midi,
                            "onset_seconds": nearest["onset_seconds"], "midi": detected,
                            "row_id": nearest.get("row_id"), "ok": ok,
                            "rejected": nearest.get("rejected")})
            if not ok:
                anchor_failures.append(f"{name} {seconds} s MIDI {midi}")
        analysis["takes"][name] = {
            "file": take["file"], "sha256": hashes[take["file"]],
            "split": take["split"], "tuning_offset_cents": offset,
            "noise_floor_dbfs": result["noise_floor_dbfs"],
            "events": result["events"], "anchors": anchors,
        }
    document = {"corpus": CORPUS, "license": LICENSE, "source": SOURCE_URL, "rows": rows}
    (args.out / "rows.json").write_text(json.dumps(document, indent=1) + "\n")
    (args.out / "analysis.json").write_text(json.dumps(analysis, indent=1, default=float) + "\n")
    write_source(args.out, hashes, analysis, rows)
    for split in sorted({row["split"] for row in rows}):
        selected = [row for row in rows if row["split"] == split]
        counts: dict[int, int] = {}
        for row in selected:
            counts[row["midi"]] = counts.get(row["midi"], 0) + 1
        print(f"{split}: {len(selected)} rows, {len(counts)} pitches, MIDI"
              f" {min(counts)}-{max(counts)}; per MIDI "
              + " ".join(f"{midi}:{count}" for midi, count in sorted(counts.items())))
    for name, take in analysis["takes"].items():
        print(f"{name}: tuning offset {take['tuning_offset_cents']:+.1f} cents,"
              f" {len(take['events'])} events")
        for anchor in take["anchors"]:
            print(f"  anchor {anchor['readme_seconds']:8.3f} s MIDI {anchor['readme_midi']}:"
                  f" onset {anchor['onset_seconds']:8.3f} s MIDI {anchor['midi']}"
                  f" {'row ' + anchor['row_id'] if anchor['row_id'] else 'not a row: ' + str(anchor['rejected'])}")
    if anchor_failures:
        print("anchor check FAILED: " + ", ".join(anchor_failures), file=sys.stderr)
        return 1
    print("anchor check: all 16 README anchors detected with the expected MIDI")
    return 0


def write_source(out: Path, hashes: dict[str, str], analysis: dict[str, Any],
                 rows: list[dict[str, Any]]) -> None:
    lines = [
        "# eastman corpus source",
        "",
        "Work: two first-party performance masters of an Eastman E1D dreadnought",
        "steel-string guitar (chromatic walks E2 to C6, recorded 2026-07-23).",
        "Creator/performer: Arthur, owner of the ferrosintesis repository.",
        "",
        f"Source: <{SOURCE_URL}>",
        "(introduction commit; Acustra's THIRD_PARTY_NOTICES.md also checked revision",
        "94edbcfef226986d6ac28330020bc301fa5207d9, where both Opus hashes are unchanged).",
        "",
        f"Licence: CC0 1.0 Universal, <{LICENSE_URL}>. Exact statement in the",
        "upstream sample README (ThirdParty/Eastman-E1D-README.md in Acustra):",
        "\"Licence | **CC0-1.0** (public-domain dedication by the repo owner)\".",
        "Each Opus file also carries the metadata tag LICENSE=CC0-1.0.",
        "",
        "| File | Master | Bytes | SHA-256 |",
        "|---|---|---|---|",
    ]
    for take in TAKES.values():
        lines.append(f"| {take['file']} | {take['master']} | {take['bytes']} |"
                     f" `{hashes[take['file']]}` |")
    lines += [
        "",
        "Both hashes were verified against THIRD_PARTY_NOTICES.md ('Eastman E1D",
        "steel-string recordings') before decoding.",
        "",
        "Processing: Tools/PrepareEastmanCorpus.py (protocol in its docstring). Whole",
        "takes are decoded once by ffmpeg to 48 kHz stereo float32; each target starts",
        "20 ms before an isolated onset located by FitPhysicalModel.py's own onset rule",
        "and ends at the next event, a detected mute or 4.2 s, with a 60 ms terminal",
        "half-cosine fade and no gain change. analysis.json lists every detected",
        "event with its measurements and rejection reason.",
        "",
        "| Take | Split | Rows | Tuning offset (cents) | Noise floor (dBFS, 100 ms RMS p5) |",
        "|---|---|---|---|---|",
    ]
    for name, take in analysis["takes"].items():
        count = sum(1 for row in rows if row["split"] == take["split"])
        lines.append(f"| {name} | {take['split']} | {count} | {take['tuning_offset_cents']:+.1f} |"
                     f" {take['noise_floor_dbfs']:.1f} |")
    lines += ["", "No audio from this corpus is committed to the Acustra repository.", ""]
    (out / "SOURCE.md").write_text("\n".join(lines))


if __name__ == "__main__":
    raise SystemExit(main())
