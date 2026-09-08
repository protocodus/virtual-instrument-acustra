#!/usr/bin/env python3
"""Fit unintegrated g35/g36 body candidates with the existing measured methods.

Robert Mores, https://zenodo.org/records/4604577, CC BY 4.0. The three
microphones and bass/centre/treble impacts are distinct documented positions.
No captures are relabelled as stereo. Phase-preserving endpoint residues are
converted into heave/rock coordinates exactly as GenerateBodyForcePair does.
The bridge uses the existing positive-semidefinite two-point reduction; its
reciprocity constraint and heuristic high-frequency corner remain assumptions.
The unused centre impact tests spatial interpolation, not statistical repeats.
Only new candidate files are written; the engine and shipped data are untouched.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

import numpy as np
import scipy

import GenerateBodyForcePair as pair

spatial, body = pair.spatial, pair.body
bridge = spatial.bridge


def extract(values: np.ndarray, guitars: list[int]) -> tuple[dict, dict]:
    """The existing calibrated H1 extraction, parameterized by archive ID."""
    frequency = pair.FREQUENCY
    taper = .5 + .5 * np.cos(np.arange(1, 48001) * np.pi / 48000)
    responses, quality = {}, {}
    for guitar in guitars:
        for impact in range(3):
            record = values[guitar - 1, impact * 48000:(impact + 1) * 48000]
            if not np.isfinite(record).all():
                raise ValueError(f"g{guitar} impact {impact}: nonfinite source")
            force = record[:, 0] * taper * bridge.HAMMER_NEWTONS_PER_FULL_SCALE
            spectrum = np.fft.rfft(force, bridge.FFT_SIZE)
            denominator = abs(spectrum) ** 2
            denominator += denominator.max() * 1e-12
            band = abs(spectrum)[(frequency >= 60) & (frequency < 10000)]
            quality[f"g{guitar}_impact{impact}"] = dict(peak_force_N=float(abs(force).max()),
                force_peak_sample=int(abs(force).argmax()),
                band_force_spectrum_min_over_max=float(band.min() / band.max()))
            for channel in spatial.CHANNELS:
                if channel <= 2:
                    response = bridge.transfer(values, guitar, impact, channel)
                else:
                    pressure = record[:, channel] * taper * body.MIC_PASCALS_PER_FULL_SCALE
                    response = np.fft.rfft(pressure, bridge.FFT_SIZE) * np.conj(spectrum) / denominator
                responses[guitar, impact, channel] = response
    return responses, quality


def header(guitar: int, arrays: dict, mobility: dict | None) -> str:
    modes = arrays[f"g{guitar}_frequency_q"]
    residues = pair.paired(arrays[f"g{guitar}_endpoint_residues"])
    lines = ["// Generated unintegrated candidate; no engine selection is changed.",
        "// Robert Mores, https://zenodo.org/records/4604577, CC BY 4.0.",
        "// Modified by calibrated H1 extraction, causal taper, joint modal fitting.",
        "// Raw complex pressure phase retained. F=Fb+Ft, T=Ft-Fb=M/a.",
        "// Rh=(Rt+Rb)/2, Rm=(Rt-Rb)/2; arbitrary string positions assume rigidity.",
        f"// g{guitar}: {body.GUITAR_DESCRIPTION[guitar]}",
        "#pragma once", "#include <array>", "namespace acustra::experimental {",
        f"struct NylonG{guitar}RadiationMode {{ float frequency, q;",
        "    float trebleReal, trebleImaginary, bassReal, bassImaginary, upperReal, upperImaginary;",
        "    float trebleMomentReal, trebleMomentImaginary, bassMomentReal, bassMomentImaginary;",
        "    float upperMomentReal, upperMomentImaginary; };",
        f"inline constexpr std::array<NylonG{guitar}RadiationMode, {len(modes)}> nylonG{guitar}Radiation {{{{"]
    for index, (frequency, q) in enumerate(modes):
        row = [frequency, q]
        for axis in (0, 1):
            for mic in (1, 2, 0):
                residue = residues[mic, axis, index]
                row.extend((residue.real, residue.imag))
        lines.append("    { " + ", ".join(body.cpp_float(v) for v in row) + " },")
    lines.append("}};")
    if mobility is not None:
        rows = mobility["modes"]
        lines.extend([f"struct NylonG{guitar}BridgeMode {{ float frequency, q, heave, cross, rock; }};",
            f"inline constexpr std::array<NylonG{guitar}BridgeMode, {len(rows)}> nylonG{guitar}Mobility {{{{"])
        lines.extend("    { " + ", ".join(bridge.cpp_float(v) for v in row) + " }," for row in rows)
        lines.append("}};")
    return "\n".join(lines + ["} // namespace acustra::experimental", ""])


def fit_bridge(raw: Path, guitar: int, trials: list[dict]) -> dict:
    """Try bounded measured pole prefixes without changing the passive gates."""
    frequency, treble, bass, cross = bridge.extract_two_point(raw, guitar)
    corner, disagreement = bridge.coherence_corner(raw, guitar)
    useful = (frequency >= bridge.MINIMUM_FREQUENCY) & (frequency <= bridge.MAXIMUM_FREQUENCY)
    magnitude = bridge.gaussian_filter1d(20 * np.log10(np.maximum(abs(treble + bass), 1e-30)), 1.5)
    peaks, _ = bridge.find_peaks(magnitude[useful], prominence=bridge.PEAK_PROMINENCE_DB, distance=3)
    counts = sorted(set(min(len(peaks), count) for count in (65, 80, 96, 128)))
    for count in counts:
        trial = dict(candidate_count=count)
        trials.append(trial)
        try:
            modes = bridge.candidate_modes(frequency, treble + bass, count)
            fitted, phase, error, magnitude = bridge.positive_semidefinite_fit(
                frequency, treble, bass, cross, corner, modes)
            trial["passed"] = True
            return dict(guitar=guitar, candidate_count=count, modes=fitted,
                corner=corner, corner_disagreement_bands=disagreement,
                phase_advance=phase, relative_error=error,
                magnitude_error=max(magnitude), magnitude_errors=magnitude,
                rocking=sum(1 for mode in fitted if mode[4] > 0))
        except ValueError as exc:
            trial.update(passed=False, failure=str(exc))
            print(f"g{guitar}: bridge {count}-pole trial rejected: {exc}", flush=True)
    raise ValueError("all bounded measured bridge pole prefixes failed unchanged gates")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-mat", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--guitars", nargs="+", type=int, choices=(35, 36), default=[35, 36])
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError("output must be a new directory")
    hashes = {p.name: spatial.sha256(p) for p in (Path(__file__), Path(pair.__file__),
        Path(pair.pair.__file__), Path(spatial.__file__), Path(body.__file__), Path(bridge.__file__))}
    report = dict(protocol=__doc__, complete=False, started_at_utc=datetime.now(timezone.utc).isoformat(),
        selected_ids=args.guitars, source_url="https://zenodo.org/records/4604577",
        source_license="CC BY 4.0", raw_md5=bridge.digest(args.raw_mat),
        raw_sha256=spatial.sha256(args.raw_mat), raw_bytes=args.raw_mat.stat().st_size,
        source_sha256_at_start=hashes,
        versions=dict(python=sys.version.split()[0], numpy=np.__version__, scipy=scipy.__version__),
        sample_rate=body.SAMPLE_RATE, fft_size=body.FFT_SIZE,
        realization="H(z)=sum R/(1-p*z^-1)+conj(R)/(1-conj(p)*z^-1), p=exp(-pi*f/(Q*Fs)+2j*pi*f/Fs); no added delay",
        pressure_units="Pa/N for endpoint forces and normalized moment T=M/a",
        selected_capture_protocol="one selected impact per position; center excluded from endpoint fitting",
        qualifications="pressure path gates and passive bridge fit only; center interpolation diagnostic is separately reported; no new playing or perceptual validation",
        runtime_integration="none; local candidates", banks=[])
    values = bridge.load_matrix(args.raw_mat)
    report["raw_shape"] = list(values.shape)
    responses, report["force_quality"] = extract(values, args.guitars)
    args.output.mkdir(parents=True)
    def save() -> None:
        (args.output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    save()
    for guitar in args.guitars:
        bank = dict(guitar=guitar, instrument=body.GUITAR_DESCRIPTION[guitar])
        report["banks"].append(bank)
        try:
            keep, convergence = body.converged_keep_samples(args.raw_mat, guitar)
            bank["window_convergence"] = dict(keep_samples=keep, low_mode_q_worst_relative_difference=convergence)
            print(f"g{guitar}: causal keep {keep}, low-mode Q convergence {convergence:.6f}", flush=True)
            pressure, arrays = pair.fit_bank(guitar, keep, responses)
            bank["pressure"] = pressure
            bank["center_interpolation"] = []
            for channel, name in spatial.CHANNELS.items():
                for low, high in spatial.BANDS:
                    selection = (pair.FREQUENCY >= low) & (pair.FREQUENCY < high)
                    endpoint_mean = .5 * (responses[guitar, 0, channel] + responses[guitar, 2, channel])
                    bank["center_interpolation"].append(dict(channel=name, low_hz=low, high_hz=high,
                        **spatial.metrics(responses[guitar, 1, channel][selection], endpoint_mean[selection])))
            np.savez_compressed(args.output / f"g{guitar}-body.npz", frequency=pair.FREQUENCY, **arrays)
            bank["body_npz_sha256"] = spatial.sha256(args.output / f"g{guitar}-body.npz")
            print(f"g{guitar}: pressure {pressure['mode_count']} modes, all path/balance gates passed", flush=True)
            mobility = None
            bank["bridge_trials"] = []
            try:
                mobility = fit_bridge(args.raw_mat, guitar, bank["bridge_trials"])
                bank["bridge"] = mobility
                print(f"g{guitar}: bridge {len(mobility['modes'])} modes, complex error {mobility['relative_error']:.6f}", flush=True)
            except ValueError as exc:
                bank["bridge_failure"] = str(exc)
                print(f"g{guitar}: bridge rejected: {exc}", flush=True)
            path = args.output / f"NylonG{guitar}CandidateData.h"
            path.write_text(header(guitar, arrays, mobility))
            bank["header_sha256"] = spatial.sha256(path)
            bank["transfer_fit_qualified"] = mobility is not None
        except ValueError as exc:
            bank["failure"] = str(exc)
            bank["transfer_fit_qualified"] = False
            print(f"g{guitar}: rejected: {exc}", flush=True)
        save()
    report["complete"] = True
    report["completed_at_utc"] = datetime.now(timezone.utc).isoformat()
    save()


if __name__ == "__main__":
    main()
