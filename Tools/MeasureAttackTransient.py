#!/usr/bin/env python3
"""Measure the energy a note's attack carries between its partials, target and model.

The scorer's attack bands cannot say whether an attack's missing energy is the
string's own partials or something between them, and the two call for different
mechanisms. This reads a rendered corpus (a FitPhysicalModel manifest) and, for
every row's recording and render, reports:

* between-partial level against time (TIMES, 0-300 ms after the scorer's onset)
  and third-octave band (250 Hz - 12.5 kHz centres), in dB re the signal's own
  mean power over 0-100 ms. Frames are Blackman windows nine nominal periods long
  (at least 3 ms), where a partial's main lobe is +-f0/3 and its side lobes are
  under -58 dB. A band spanning at least 2.5 partial intervals is read from its
  lowest 30% of bins: their mean, over the band, plus 7.8 dB, which is what the
  lowest 30% of an exponentially distributed noise periodogram reads under its
  mean, so no partial positions are needed. A narrower band excludes +-0.4 f0
  about each of the frame's own partial peaks, found within +-0.25 f0 of the
  series f_n = n f0 sqrt(1 + B n^2) fitted to the settled spectrum (30-400 ms);
  synthetic checks put both readings within 3 dB of a known noise floor 40 dB
  under the partials. The recording's floor per band is the same reading on its
  last 0.4 s, and on its lead-in where it has 1.5 frames of one.
* the band total over the same frames, the 12-40 ms and 40-200 ms slopes of the
  between level (floor-guarded, 1-12.5 kHz power mean), and, across a note's
  round robins, the range of its 0-12 ms between level and of its 0-40 ms
  spectral centroid;
* periodicity: for 1-2, 2-4, 4-8 and 8-14 kHz band-passed signals, the largest
  normalised correlation of the first max(20 ms, 3 periods) with itself shifted
  by 0.70-1.03 of a period (a stiff string's upper partials recur early), and as
  a reference the largest at 0.35-0.65 of a period. Energy the string carries
  recurs every round trip; energy that does not is not the string's.

usage: MeasureAttackTransient.py RUN_DIR SPLIT [--material M] [--out FILE.json]
       [--jobs N]
SPLIT is a manifest name in RUN_DIR (train, validation, flattop, or an open
corpus's manifest stem). Registers: low <= MIDI 51, mid 52-66, high >= 67.
"""

from __future__ import annotations

import argparse
import json
import math
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from scipy.ndimage import maximum_filter1d, median_filter
from scipy.signal import butter, sosfiltfilt

import FitPhysicalModel as fit

RATE = 48_000
CENTRES = np.array([250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500,
                    3150, 4000, 5000, 6300, 8000, 10000, 12500], float)
EDGES = np.concatenate([CENTRES / 2 ** (1 / 6), [CENTRES[-1] * 2 ** (1 / 6)]])
TIMES = np.array([0, 2, 4, 6, 8, 10, 12, 15, 20, 25, 30, 35, 40, 50, 60, 80, 100,
                  150, 200, 300], float) / 1000
VALLEY = 0.30
VALLEY_DB = 10 * math.log10((1 - (1 - VALLEY) * (1 - math.log(1 - VALLEY))) / VALLEY)
PERIODIC_BANDS = ((1000, 2000), (2000, 4000), (4000, 8000), (8000, 14000))
GROUPS = (("0.5-0.8k", 500, 800), ("1-1.6k", 1000, 1600), ("2-3.2k", 2000, 3150),
          ("4-6.3k", 4000, 6300), ("8-12.5k", 8000, 12500))


def load(spec, base: Path) -> np.ndarray:
    rate, audio = fit._read_audio(spec, base, None, None)
    return fit._resample(audio, rate, RATE)


def settled_partials(x: np.ndarray, onset: int, midi: int) -> tuple[np.ndarray, float]:
    """The partial series of the settled spectrum: f0 and B by a grid search over
    the whole series' prominence, each partial refined to its own peak."""
    segment = x[onset + int(0.03 * RATE):onset + int(0.40 * RATE)].astype(float)
    segment -= segment.mean()
    size = 1 << int(math.ceil(math.log2(max(segment.size, 64) * 4)))
    frequency = np.fft.rfftfreq(size, 1 / RATE)
    level = 10 * np.log10(np.abs(np.fft.rfft(segment * np.hanning(segment.size), size)) ** 2
                          + 1e-30)
    nominal = 440.0 * 2 ** ((midi - 69) / 12)
    step = frequency[1] - frequency[0]
    median = median_filter(level, size=max(3, int(round(nominal / step)) | 1), mode="nearest")
    peak = maximum_filter1d(level, size=max(3, int(round(8.0 / step)) | 1), mode="nearest")
    prominence = np.clip(peak - median, 0, 30)
    order = np.arange(1, max(4, int(7000 / nominal)) + 1, dtype=float)
    best = (-1.0, nominal, 0.0)
    for inharmonicity in np.concatenate([[0.0], np.geomspace(1e-7, 3e-3, 70)]):
        candidates = nominal * 2 ** (np.arange(-45, 45.01, 0.5) / 1200)
        series = candidates[:, None] * order[None, :] * np.sqrt(1 + inharmonicity * order ** 2)
        total = np.interp(series, frequency, prominence, right=0.0).sum(axis=1)
        index = int(np.argmax(total))
        if total[index] > best[0]:
            best = (float(total[index]), float(candidates[index]), float(inharmonicity))
    _, f0, inharmonicity = best
    count = np.arange(1, int(22000 / f0) + 3)
    return count * f0 * np.sqrt(1 + inharmonicity * count * count), f0


class Frames:
    def __init__(self, midi: int):
        nominal = 440.0 * 2 ** ((midi - 69) / 12)
        self.size = max(144, int(round(9.0 * RATE / nominal)))
        self.fft = 1 << int(math.ceil(math.log2(4 * self.size)))
        self.window = np.blackman(self.size)
        self.frequency = np.fft.rfftfreq(self.fft, 1 / RATE)
        self.bins = [np.flatnonzero((self.frequency >= low) & (self.frequency < high))
                     for low, high in zip(EDGES[:-1], EDGES[1:])]
        self.norm = np.sum(self.window ** 2) * self.fft / 2.0

    def power(self, x: np.ndarray, centre: int) -> np.ndarray:
        first = centre - self.size // 2
        segment = np.zeros(self.size)
        low, high = max(first, 0), min(first + self.size, x.size)
        if high > low:
            segment[low - first:high - first] = x[low:high]
        return np.abs(np.fft.rfft(segment * self.window, self.fft)) ** 2 / self.norm

    def bands(self, power: np.ndarray, reference: float, partials: np.ndarray,
              f0: float) -> tuple[np.ndarray, np.ndarray]:
        total = np.full(CENTRES.size, np.nan)
        between = np.full(CENTRES.size, np.nan)
        for index, selected in enumerate(self.bins):
            if selected.size < 4:
                continue
            total[index] = 10 * np.log10(power[selected].sum() + 1e-30) - reference
            if EDGES[index + 1] - EDGES[index] >= 2.5 * f0:
                ordered = np.sort(power[selected])
                count = max(1, int(round(VALLEY * ordered.size)))
                between[index] = (10 * np.log10(ordered[:count].mean() * ordered.size + 1e-30)
                                  - VALLEY_DB - reference)
                continue
            keep = np.ones(selected.size, bool)
            for centre in partials[(partials > EDGES[index] - f0)
                                   & (partials < EDGES[index + 1] + f0)]:
                near = np.flatnonzero(np.abs(self.frequency - centre) <= 0.25 * f0)
                if near.size:
                    centre = self.frequency[near[np.argmax(power[near])]]
                keep &= np.abs(self.frequency[selected] - centre) >= 0.4 * f0
            if np.count_nonzero(keep) >= 3:
                between[index] = (10 * np.log10(power[selected][keep].mean() * selected.size
                                                + 1e-30) - reference)
        return total, between


def periodicity(x: np.ndarray, onset: int, midi: int) -> list[tuple[float, float]]:
    period = RATE / (440.0 * 2 ** ((midi - 69) / 12))
    width = int(max(0.020 * RATE, 3 * period))
    result = []
    for low, high in PERIODIC_BANDS:
        y = sosfiltfilt(butter(4, [low, high], btype="band", fs=RATE, output="sos"), x)
        segment = y[onset:onset + width + int(1.1 * period) + 2]
        if segment.size < width + int(1.03 * period) + 1:
            result.append((math.nan, math.nan))
            continue

        def correlation(lag: int) -> float:
            first, second = segment[:width], segment[lag:lag + width]
            return float(np.dot(first, second)
                         / math.sqrt(np.dot(first, first) * np.dot(second, second) + 1e-30))
        recur = max(correlation(lag) for lag in range(int(0.70 * period), int(1.03 * period) + 1))
        other = max(abs(correlation(lag)) for lag in range(int(0.35 * period),
                                                            int(0.65 * period) + 1))
        result.append((recur, other))
    return result


def analyse(x: np.ndarray, midi: int) -> dict:
    onset = fit._onset(x, RATE)
    frames = Frames(midi)
    partials, f0 = settled_partials(x, onset, midi)
    head = x[onset:onset + int(0.1 * RATE)]
    reference = 10 * np.log10(np.mean(head * head) + 1e-30)
    total = np.full((TIMES.size, CENTRES.size), np.nan)
    between = np.full_like(total, np.nan)
    for index, time in enumerate(TIMES):
        total[index], between[index] = frames.bands(
            frames.power(x, onset + int(round(time * RATE))), reference, partials, f0)

    def mean_power(first: int, last: int):
        centres = list(range(first + frames.size // 2, last - frames.size // 2,
                             max(1, frames.size // 2)))
        return np.mean([frames.power(x, c) for c in centres], axis=0) if centres else None
    floors = []
    tail = mean_power(x.size - int(0.4 * RATE), x.size)
    if tail is not None:
        floors.append(frames.bands(tail, reference, partials, f0)[1])
    if onset - 64 >= int(1.5 * frames.size):
        lead = mean_power(0, onset - 64)
        if lead is not None and np.any(lead > 0):
            floors.append(frames.bands(lead, reference, partials, f0)[1])
    with np.errstate(all="ignore"):
        floor = np.nanmin(np.array(floors), axis=0) if floors else np.full(CENTRES.size, np.nan)
    length = int(0.040 * RATE)
    segment = np.pad(x[onset:onset + length], (0, max(0, length - x[onset:onset + length].size)))
    size = 1 << int(math.ceil(math.log2(4 * length)))
    power = np.abs(np.fft.rfft(segment * np.hanning(length), size)) ** 2
    frequency = np.fft.rfftfreq(size, 1 / RATE)
    band = (frequency >= 80) & (frequency <= 16000)
    centroid = 1200 * math.log2(np.sum(frequency[band] * power[band])
                                / np.sum(power[band]) / 1000.0)
    return {"onset_ms": onset * 1000 / RATE, "total": total, "between": between,
            "floor": floor, "centroid": centroid,
            "periodicity": periodicity(x, onset, midi)}


def band_mean(values: np.ndarray, low: float, high: float) -> np.ndarray:
    selected = [index for index, centre in enumerate(CENTRES) if low <= centre <= high]
    with np.errstate(all="ignore"):
        return 10 * np.log10(np.nanmean(10 ** (np.asarray(values)[..., selected] / 10), axis=-1))


def slope(level: np.ndarray, floor: float, first: float, last: float) -> float:
    use = (TIMES >= first) & (TIMES <= last) & np.isfinite(level) & (level > floor + 10)
    return float(np.polyfit(TIMES[use], level[use], 1)[0]) if use.sum() >= 3 else math.nan


def register(midi: int) -> str:
    return "low" if midi <= 51 else "mid" if midi <= 66 else "high"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run", type=Path)
    parser.add_argument("split")
    parser.add_argument("--material")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--jobs", type=int, default=6)
    arguments = parser.parse_args()
    manifest = json.loads((arguments.run / f"{arguments.split}.json").read_text())
    work, seen = [], set()
    for example in manifest["examples"]:
        if arguments.material and example.get("material") != arguments.material:
            continue
        for side in ("target", "model"):
            spec = example[side]
            path = spec["path"] if isinstance(spec, dict) else spec
            if (side, path) not in seen:
                seen.add((side, path))
                work.append((example, side, spec))

    def measure(item):
        example, side, spec = item
        return example, side, analyse(load(spec, arguments.run), example["midi"])
    with ThreadPoolExecutor(arguments.jobs) as pool:
        rows = list(pool.map(measure, work))
    groups: dict = {}
    for example, side, result in rows:
        key = (example.get("material") or "flattop", int(example["velocity"]),
               register(example["midi"]))
        groups.setdefault(key, {}).setdefault(side, []).append((example, result))
    report = {}
    shown = [TIMES.tolist().index(t / 1000) for t in (0, 4, 8, 12, 20, 40, 100, 200)]
    for key in sorted(groups, key=lambda k: (k[0], k[1], ("low", "mid", "high").index(k[2]))):
        entry = {}
        print(f"=== {key}: between-partial level, dB re 0-100 ms power, at "
              f"{[round(TIMES[i] * 1000) for i in shown]} ms (recording | model)")
        for name, low, high in GROUPS:
            line = []
            for side in ("target", "model"):
                items = groups[key].get(side, [])
                if not items:
                    continue
                values = np.nanmedian([band_mean(r["between"], low, high) for _, r in items], 0)
                entry.setdefault(side, {})[name] = values.tolist()
                line.append(" ".join(f"{v:6.1f}" for v in values[shown]))
            print(f"  {name:>9s} " + " | ".join(line))
        for side in ("target", "model"):
            items = groups[key].get(side, [])
            if not items:
                continue
            early = [slope(band_mean(r["between"], 1000, 12500),
                           float(band_mean(r["floor"], 1000, 12500)), 0.012, 0.040)
                     for _, r in items]
            late = [slope(band_mean(r["between"], 1000, 12500),
                          float(band_mean(r["floor"], 1000, 12500)), 0.040, 0.200)
                    for _, r in items]
            recur = np.nanmedian([[p[0] for p in r["periodicity"]] for _, r in items], 0)
            other = np.nanmedian([[p[1] for p in r["periodicity"]] for _, r in items], 0)
            per_note: dict = {}
            for example, r in items:
                per_note.setdefault(example["midi"], []).append(r)
            spread = [np.ptp([float(np.nanmean(band_mean(r["between"][:7], 1000, 12500)))
                              for r in v]) for v in per_note.values() if len(v) > 1]
            centroid = [np.ptp([r["centroid"] for r in v]) for v in per_note.values()
                        if len(v) > 1]
            entry.setdefault(side, {}).update({
                "slope_12_40_db_per_s": float(np.nanmedian(early)),
                "slope_40_200_db_per_s": float(np.nanmedian(late)),
                "periodicity": recur.tolist(), "periodicity_reference": other.tolist(),
                "take_range_db": float(np.median(spread)) if spread else None,
                "centroid_take_range_cents": float(np.median(centroid)) if centroid else None})
            print(f"  {side:6s} 1-12.5 kHz slope 12-40 ms {np.nanmedian(early):6.0f} dB/s, "
                  f"40-200 ms {np.nanmedian(late):6.0f}; recurrence at a period "
                  + " ".join(f"{a:.2f}/{b:.2f}" for a, b in zip(recur, other))
                  + (f"; take range {np.median(spread):.1f} dB, centroid "
                     f"{np.median(centroid):.0f} cents" if spread else ""))
        report[" ".join(map(str, key))] = entry
    if arguments.out:
        arguments.out.write_text(json.dumps(report, indent=1) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
