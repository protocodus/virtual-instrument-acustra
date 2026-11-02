#!/usr/bin/env python3
"""Paired native-level review of RenderFretUpperReview snapshots.

Protocol: no onset alignment or gain adjustment before measurement. Notes start
at 50 ms in the manifest. Stereo-channel powers are summed (never a mono fold)
through a Hann window; partial bands track ideal equal temperament within +/-
3%, at least 20 Hz. H2-H6/H1 and a 100 Hz-12 kHz centroid use 20-120 ms after
note-on; the upper-partial H3-H6 fall compares 20-120 and 250-450 ms, expressed
as dB/s between window centres. This is a descriptor, not a fitted T60 or an
audibility/preference verdict. The noise null is (amount - amount 0) over
5-100 ms after known new-fret arrivals, before that note's own key-up. It also
contains any preceding key-up residue; controls expose that baseline floor.

The listening set uses whole-file RMS matching, fixed identical crops and
letters only. A is the first input snapshot. Native files remain untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.io import wavfile


def db(power):
    return float(10.0 * np.log10(max(float(power), 1.0e-30)))


def load(directory):
    manifest = json.loads((directory / "manifest.json").read_text())
    cases = {case["id"]: case for case in manifest["cases"]}
    if len(cases) != len(manifest["cases"]):
        raise ValueError("duplicate case IDs")
    return manifest, cases


def audio(directory, case):
    rate, value = wavfile.read(directory / case["file"])
    if rate != case["sample_rate"] or value.shape != (case["frames"], 2):
        raise ValueError(f"audio shape/rate differs: {case['id']}")
    if value.dtype != np.float32 or not np.isfinite(value).all():
        raise ValueError(f"expected finite float32: {case['id']}")
    return value.astype(np.float64)


def band_power(value, rate, start, end):
    x = value[round(rate * start):round(rate * end)]
    window = np.hanning(len(x))
    size = 1 << max(15, (len(x) * 4 - 1).bit_length())
    power = np.sum(np.abs(np.fft.rfft(x * window[:, None], n=size, axis=0)) ** 2, axis=1)
    # Normalize by window energy so unequal window lengths have a common basis.
    power /= max(float(np.dot(window, window)) * size, 1.0e-30)
    return np.fft.rfftfreq(size, 1.0 / rate), power


def partials(frequencies, power, fundamental):
    return np.array([np.sum(power[np.abs(frequencies - n * fundamental)
                                     <= max(20.0, 0.03 * n * fundamental)])
                     for n in range(1, 7)])


def upper_metrics(x, case):
    rate = case["sample_rate"]
    fundamental = 440.0 * 2.0 ** ((case["midi"] - 69) / 12.0)
    f, p = band_power(x, rate, .07, .17)
    harmonic = partials(f, p, fundamental)
    band = (f >= 100) & (f < min(12000, .45 * rate))
    centroid = float(np.dot(f[band], p[band]) / max(float(np.sum(p[band])), 1.e-30))
    late_f, late_p = band_power(x, rate, .30, .50)
    late_harmonic = partials(late_f, late_p, fundamental)
    fall = (db(np.sum(harmonic[2:])) - db(np.sum(late_harmonic[2:]))) / .28
    return {
        "h2_h6_over_h1_db": db(np.sum(harmonic[1:]) / max(harmonic[0], 1.e-30)),
        "centroid_hz": centroid,
        "h3_h6_early_to_late_fall_db_per_second": fall,
        "rms_dbfs": db(np.mean(x * x)),
        "peak_dbfs": db(np.max(x * x)),
    }


def noise_metrics(x, bypass, case):
    rate = case["sample_rate"]
    selector = np.zeros(len(x), dtype=bool)
    for shift in case["shift_seconds"]:
        selector[round((shift + .005) * rate):round((shift + .100) * rate)] = True
    if not selector.any():
        raise ValueError(f"no arrival windows: {case['id']}")
    difference = x[selector] - bypass[selector]
    null_power = float(np.mean(difference * difference))
    signal_power = float(np.mean(x[selector] * x[selector]))
    return {
        "null_rms_dbfs": db(null_power),
        "null_relative_to_signal_db": db(null_power / max(signal_power, 1.e-30)),
        "rms_dbfs": db(np.mean(x * x)),
        "peak_dbfs": db(np.max(x * x)),
    }


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compare(baseline, candidate):
    am, ac = load(baseline)
    bm, bc = load(candidate)
    if am != bm:
        raise ValueError("manifests differ; identical-score paired measurement required")
    rows = []
    cache = {}
    for name, case in ac.items():
        ax, bx = audio(baseline, case), audio(candidate, bc[name])
        row = {key: case[key] for key in ("id", "category", "family", "sample_rate",
                   "technique", "string_number", "fret", "velocity", "release_noise")}
        if case["category"] == "upper":
            a, b = upper_metrics(ax, case), upper_metrics(bx, case)
        elif case["release_noise"] > 0:
            bypass_id = name.rsplit("-n", 1)[0] + "-n0"
            if bypass_id not in cache:
                cache[bypass_id] = (audio(baseline, ac[bypass_id]), audio(candidate, bc[bypass_id]))
            az, bz = cache[bypass_id]
            a, b = noise_metrics(ax, az, case), noise_metrics(bx, bz, case)
        else:
            a = {"rms_dbfs": db(np.mean(ax * ax)), "peak_dbfs": db(np.max(ax * ax))}
            b = {"rms_dbfs": db(np.mean(bx * bx)), "peak_dbfs": db(np.max(bx * bx))}
        row.update({"baseline": a, "candidate": b,
                    "delta": {key: b[key] - a[key] for key in a},
                    "byte_identical": bool(np.array_equal(ax, bx)),
                    "baseline_sha256": digest(baseline / case["file"]),
                    "candidate_sha256": digest(candidate / case["file"])})
        rows.append(row)
    return rows


def summary(rows):
    result = {"cases": len(rows), "byte_identical": sum(row["byte_identical"] for row in rows)}
    upper = [row for row in rows if row["category"] == "upper"]
    result["upper_frets_14_to_20_by_velocity_technique"] = {}
    for velocity in (40, 100):
      for technique in ("Finger", "Pick", "Thumb"):
        selected = [row for row in upper if row["technique"] == technique
                    and row["velocity"] == velocity and row["fret"] >= 14]
        result["upper_frets_14_to_20_by_velocity_technique"][f"{velocity}-{technique}"] = {
            key: {"median": float(np.median([row["delta"][key] for row in selected])),
                  "min": min(row["delta"][key] for row in selected),
                  "max": max(row["delta"][key] for row in selected)}
            for key in selected[0]["delta"]}
    result["upper_frets_9_to_13_identical"] = all(row["byte_identical"] for row in upper if row["fret"] <= 13)
    result["fret_default_48k"] = [{"family": row["family"], "baseline": row["baseline"],
                                  "candidate": row["candidate"], "delta": row["delta"]}
        for row in rows if row["category"] == "fret" and row["sample_rate"] == 48000
        and .09 < row["release_noise"] < .11]
    return result


def listening(baseline, candidate, destination):
    if destination.exists():
        raise ValueError("listening destination already exists")
    destination.mkdir(parents=True)
    _, cases = load(baseline)
    groups = {
        "upper": [f"upper-48000-{technique}-s6-f{fret}-v{velocity}"
                  for technique in ("Finger", "Pick", "Thumb")
                  for velocity in (40, 100) for fret in (16, 20)],
        # Pick is unchanged by the pad-release correction, so this score
        # isolates shift arrivals. The Finger wound line in the raw matrix
        # is useful combined context but would mix both changes in one A/B.
        "fret": ["fret-48000-barre-shifts-n10"],
    }
    trims = {}
    for group, ids in groups.items():
        folder = destination / group
        folder.mkdir()
        a = np.concatenate([audio(baseline, cases[name]) for name in ids])
        b = np.concatenate([audio(candidate, cases[name]) for name in ids])
        gain = np.sqrt(np.mean(a * a) / max(float(np.mean(b * b)), 1.e-30))
        # If matching would overload either letter, attenuate both equally.
        common = min(1.0, .99 / max(np.max(np.abs(a)), np.max(np.abs(b * gain)), 1.e-30))
        wavfile.write(folder / "A.wav", 48000, (a * common).astype(np.float32))
        wavfile.write(folder / "B.wav", 48000, (b * gain * common).astype(np.float32))
        trims[group] = {"A_db": db(common * common), "B_db": db(gain * gain * common * common),
                        "whole_file_rms_dbfs": db(np.mean(a * a) * common * common), "case_ids": ids}
    (destination / "key.md").write_text(
        "# Listening key — read after choosing\n\n"
        f"A: first snapshot, `{baseline}`.\n\nB: second snapshot, `{candidate}`.\n\n"
        "Every group is matched on whole-file stereo RMS, with identical unaligned native crops. "
        "The upper group concatenates E-string frets 16, 20 at velocity 40, then 100: "
        "Finger, then Pick, then Thumb. "
        "The fret group is Pick barre shifts at Release Noise 10%, isolating the shift arrivals "
        "because Pick has no pad-release correction. The Finger wound-line native recordings "
        "remain in the full measurement matrix as combined-change context.\n\n"
        "```json\n" + json.dumps(trims, indent=2) + "\n```\n")
    return trims


def self_test():
    """Independent known-amplitude spectra, decays and paired nulls."""
    for rate in (44100, 48000, 96000):
        t = np.arange(round(.72 * rate)) / rate
        components = ((.1, 440), (.05, 880), (.02, 1320))
        mono = sum(amplitude * np.sin(2 * np.pi * frequency * t)
                   for amplitude, frequency in components)
        # Opposite stereo phases deliberately cancel on a mono fold. The
        # descriptor must retain their independent channel powers.
        stereo = np.column_stack((mono, -mono))
        case = {"sample_rate": rate, "midi": 69}
        metrics = upper_metrics(stereo, case)
        expected_ratio = 10 * np.log10((.05 ** 2 + .02 ** 2) / .1 ** 2)
        expected_centroid = sum(frequency * amplitude ** 2
                                for amplitude, frequency in components) / sum(
                                    amplitude ** 2 for amplitude, _ in components)
        if abs(metrics["h2_h6_over_h1_db"] - expected_ratio) > .01:
            raise AssertionError(f"partial power ratio at {rate}: {metrics}")
        if abs(metrics["centroid_hz"] - expected_centroid) > .05:
            raise AssertionError(f"stereo power centroid at {rate}: {metrics}")
        if abs(metrics["h3_h6_early_to_late_fall_db_per_second"]) > .01:
            raise AssertionError(f"stationary partial acquired decay at {rate}: {metrics}")

        envelope = np.exp(-10 * np.log(10) * t / 20)
        metrics = upper_metrics(stereo * envelope[:, None], case)
        # Compare to the analytic exponential sampled through the actual
        # Hann-squared observation windows, not to a fitted FFT slope.
        means = []
        for start, end in ((.07, .17), (.30, .50)):
            segment = envelope[round(start * rate):round(end * rate)]
            squared_window = np.hanning(len(segment)) ** 2
            means.append(np.dot(squared_window, segment ** 2) / squared_window.sum())
        expected_fall = 10 * np.log10(means[0] / means[1]) / .28
        if abs(metrics["h3_h6_early_to_late_fall_db_per_second"] - expected_fall) > .01:
            raise AssertionError(f"known exponential decay at {rate}: {metrics}")

        # Identical noise in the two channels remains independent of the
        # anti-phase signal when channel powers are pooled.
        noise = .001 * np.sin(2 * np.pi * 3500 * t)
        noisy = stereo + noise[:, None]
        noise_case = {"sample_rate": rate, "shift_seconds": [.1, .4]}
        metrics = noise_metrics(noisy, stereo, noise_case)
        expected_null = 10 * np.log10(.001 ** 2 / (
            .1 ** 2 + .05 ** 2 + .02 ** 2 + .001 ** 2))
        if abs(metrics["null_relative_to_signal_db"] - expected_null) > .15:
            raise AssertionError(f"paired arrival null at {rate}: {metrics}")
        if noise_metrics(stereo, stereo, noise_case)["null_rms_dbfs"] != -300.0:
            raise AssertionError(f"identical pair did not null at {rate}")
    print("PASS: spectra, stereo powers, exponential decay and arrival null at 44.1/48/96 kHz")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path, nargs="?")
    parser.add_argument("candidate", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--listening", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.baseline is None or args.candidate is None or args.output is None:
        parser.error("baseline, candidate and --output are required for paired analysis")
    rows = compare(args.baseline, args.candidate)
    report = {"protocol": __doc__, "baseline": str(args.baseline.resolve()),
              "candidate": str(args.candidate.resolve()), "summary": summary(rows), "rows": rows}
    if args.listening:
        report["listening_trims"] = listening(args.baseline, args.candidate, args.listening)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(json.dumps(report["summary"], indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
