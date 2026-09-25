#!/usr/bin/env python3
"""Create local, unintegrated scalar guitar candidates from Mark Rau's measurements.

Source: https://rau.mit.edu/projects/GuitarMeasurements/ and its public ZIP.
The archive has calibrated admittance/radiation IRs and force/velocity records,
but no explicit redistribution license or microphone/impact-position metadata.
Never relabel the data CC licensed, infer stereo from repeated measurements,
or install these experimental coefficients into the shipping instrument here.
The 48 kHz interpretation follows Rau/Smith/Abel, Forum Acusticum 2023, section
3 (one-second, 48 kHz measurements), and the 48,000-sample archive records.

The final numbered capture is excluded from pole, residue, delay and fit
selection. Earlier captures are averaged without amplitude/phase alignment.
The supplied complex radiation phase is retained, including an explicitly
represented integer propagation delay. Delay is inferred by bounded modal
approximation, not identified as a microphone distance. Pressure is conditioned
on a 3,000-sample causal window with a final 300-sample cosine fade; room
reflections within that window cannot be ruled out by the supplied metadata.
Bridge fitting uses nonnegative residues and the engine's prewarped bilinear
mobility, preserving passivity without putting a phase delay in its feedback.

Only generated numerical coefficients and reports are written to --output.
Raw measurements remain outside the repository. Nothing is published: the
engine no longer reads these coefficients, and .gitignore keeps a local copy
of the header or report out of commits.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

import numpy as np
import scipy
from scipy.io import loadmat
from scipy.ndimage import gaussian_filter1d, uniform_filter1d
from scipy.optimize import minimize_scalar, nnls
from scipy.signal import find_peaks

import GenerateMeasuredBody as body
import GenerateMeasuredBridge as bridge

RATE = 48_000
SIZE = 65_536
FREQUENCY = np.fft.rfftfreq(SIZE, 1 / RATE)
SOURCES = {
    "Washburn_1897": ("Washburn (1897), parlor", "e4e7df315d6283035e58bd5daf79bbef3996e0d578cc5456448423fc95b8e0a6"),
    "SCGC_192900": ("Santa Cruz 1929-00, small body", "584d1f6247b3038199cba99a888219351bc8a672ee5b3ed15ec02a98de7923dc"),
    "SCGC_OM3": ("Santa Cruz OM3 (2022), orchestra model", "862f703a4c308d678514d9df8f07ec58e0784d77b9636eed5e518def6108ddde"),
    "SCGC_OM2": ("Santa Cruz OM2 (2019), orchestra model", "367b6cb4612410726db6b36de02e95a767202e2a5b26c3683f3e1cab452681c1"),
    "Martin_D18": ("Martin D18V (2007), dreadnought", "e1de5307511e15adc2b6e0398565905cca3b3825332597b36af6be433a4b0d4d"),
    "Martin_D28": ("Martin D28 (2024), dreadnought", "36735038079e7f25164f051b4bacfe4d2d7db8680e0f8a756c131a48b12c9bb7"),
    "Nishihara_ReydenSJ": ("Nishihara ReydenSJ (2013), body dimensions unspecified", "37e6df08ea9dd14e48d2c226d1cd0cd5badf020d78df5da9f58160752b1b1372"),
    "Ramirez": ("Ramirez classical, model/year unspecified", "2a1762798d0a961eac1075e2a9609a8ddfe893cda1a642a9f5ab79343d7aba7d"),
}


def windowed(values: np.ndarray, keep: int = 3000) -> np.ndarray:
    result = np.zeros(SIZE)
    result[:keep] = values[:keep]
    fade = keep // 10
    result[keep - fade:keep] *= .5 + .5 * np.cos(np.linspace(0, np.pi, fade))
    return np.fft.rfft(result)


def metrics(target: np.ndarray, model: np.ndarray, low: float = 80) -> dict:
    selected = (FREQUENCY >= low) & (FREQUENCY <= 10000)
    desired, actual = target[selected], model[selected]
    db = np.abs(20 * np.log10(np.maximum(abs(actual), 1e-30)
                            / np.maximum(abs(desired), 1e-30)))
    bands = []
    for lower, upper in body.erb_bands(5000., 10000.):
        use = (FREQUENCY >= lower) & (FREQUENCY < upper)
        bands.append(abs(10 * np.log10(np.sum(abs(model[use]) ** 2)
                                      / max(np.sum(abs(target[use]) ** 2), 1e-30))))
    return {"complex_relative_l2": float(np.linalg.norm(actual - desired) / np.linalg.norm(desired)),
            "magnitude_abs_median_db": float(np.median(db)),
            "magnitude_abs_p90_db": float(np.percentile(db, 90)),
            "erb_level_abs_max_db": float(max(bands))}


def pressure_passes(value: dict) -> bool:
    return (value["complex_relative_l2"] <= body.MAX_COMPLEX_RELATIVE_ERROR
            and value["magnitude_abs_median_db"] <= body.MAX_MEDIAN_MAGNITUDE_ERROR_DB
            and value["magnitude_abs_p90_db"] <= body.MAX_P90_MAGNITUDE_ERROR_DB
            and value["erb_level_abs_max_db"] <= body.MAX_BAND_MAGNITUDE_ERROR_DB)


def bridge_passes(value: dict) -> bool:
    return (value["complex_relative_l2"] <= bridge.MAX_COMPLEX_RELATIVE_ERROR
            and value["magnitude_abs_median_db"] <= bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB)


def pressure_response(modes: np.ndarray, residues: np.ndarray) -> np.ndarray:
    z = np.exp(-2j * np.pi * FREQUENCY / RATE)
    result = np.zeros(len(z), dtype=complex)
    for (frequency, q, *_), residue in zip(modes, residues):
        p = np.exp((-np.pi * frequency / q + 2j * np.pi * frequency) / RATE)
        result += residue / (1 - p * z) + residue.conjugate() / (1 - p.conjugate() * z)
    return result


def fit_pressure(targets: list[np.ndarray], mobility: np.ndarray) -> tuple:
    target = np.mean(targets, axis=0)
    pool = body.candidate_poles(targets + [mobility])
    modes = np.array(sorted(sorted(pool, key=lambda m: (-m[2], m[0]))[:256]))
    # Poles and magnitude-derived weights are independent of pure delay. Solve
    # all trial delays together instead of recomputing an identical factorization.
    indices = np.flatnonzero((FREQUENCY >= 80) & (FREQUENCY <= 10000))[::5]
    f = FREQUENCY[indices]
    z = np.exp(-2j * np.pi * f / RATE)
    columns = []
    for freq, q, _ in modes:
        pole = np.exp((-np.pi * freq / q + 2j * np.pi * freq) / RATE)
        plus, minus = 1 / (1 - pole * z), 1 / (1 - pole.conjugate() * z)
        columns.extend((plus + minus, 1j * (plus - minus)))
    basis = np.column_stack(columns)
    envelope = gaussian_filter1d(abs(target), 128)[indices]
    weight = 1 / np.maximum(envelope, envelope.max() * 1e-3)
    weighted = basis * weight[:, None]
    matrix = np.vstack((weighted.real, weighted.imag))
    normal = matrix.T @ matrix
    normal += body.RIDGE * np.trace(normal) / len(normal) * np.eye(len(normal))
    # Bound the bulk delay by the strongest early impulse, with no negative
    # delay permitted. This is a fitted factorization of the observed phase.
    impulse = np.fft.irfft(target, SIZE)
    peak = int(np.argmax(uniform_filter1d(impulse[:1024] ** 2, 8)))
    delays = np.arange(0, min(512, peak) + 1, 4, dtype=int)
    if not len(delays):
        delays = np.array([0])
    desired = target[indices, None] * np.exp(2j * np.pi * f[:, None] * delays / RATE)
    rhs = desired * weight[:, None]
    vectors = np.vstack((rhs.real, rhs.imag))
    solutions = np.linalg.solve(normal, matrix.T @ vectors)
    residual = np.linalg.norm(matrix @ solutions - vectors, axis=0)
    candidates = []
    # Validation is the unchanged pressure gate, in the original phase basis.
    for index in np.argsort(residual):
        solution = solutions[:, index]
        residues = (solution[::2].astype(np.float32).astype(float)
                    + 1j * solution[1::2].astype(np.float32).astype(float))
        rounded_modes = modes.astype(np.float32).astype(float)
        delay = int(delays[index])
        fitted = pressure_response(rounded_modes, residues) * np.exp(-2j * np.pi * FREQUENCY * delay / RATE)
        error = metrics(target, fitted)
        candidates.append((pressure_passes(error), float(residual[index]), delay,
                           rounded_modes, residues, fitted, error))
        if pressure_passes(error):
            break
    selected = min(candidates, key=lambda item: (not item[0], item[1]))
    passed, _, delay, modes, residues, fitted, error = selected
    return modes, residues, fitted, {"mode_count": len(modes), "delay_samples_48k": delay,
            "fit": error, "fit_passed": passed, "candidate_delays_evaluated": len(candidates),
            "delay_search_max_samples": int(delays[-1])}


def bridge_basis(frequency: np.ndarray, modes: np.ndarray) -> np.ndarray:
    s = 2j * RATE * np.tan(np.pi * frequency[:, None] / RATE)
    omega = 2 * RATE * np.tan(np.pi * modes[:, 0] / RATE)
    return s / (s * s + omega / modes[:, 1] * s + omega * omega)


def fit_bridge(target: np.ndarray) -> tuple:
    useful = np.flatnonzero((FREQUENCY >= 60) & (FREQUENCY <= 10000))
    magnitude = gaussian_filter1d(20 * np.log10(np.maximum(abs(target), 1e-30)), 1.5)
    peaks, _ = find_peaks(magnitude[useful], prominence=bridge.PEAK_PROMINENCE_DB, distance=3)
    modes = np.array(bridge.candidate_modes(FREQUENCY, target, min(65, len(peaks))))[:, :2]
    indices = np.flatnonzero((FREQUENCY >= 60) & (FREQUENCY <= 10000))[::4]
    f, raw = FREQUENCY[indices], target[indices]
    basis = bridge_basis(f, modes)
    matrix = np.vstack((basis.real, basis.imag))
    orthogonal, triangular = np.linalg.qr(matrix, mode="reduced")

    def solve(delay: float, polarity: int) -> tuple:
        adjusted = polarity * raw * np.exp(2j * np.pi * f * delay)
        desired = np.r_[adjusted.real, adjusted.imag]
        residues = nnls(triangular, orthogonal.T @ desired, maxiter=10000)[0]
        error = np.linalg.norm(basis @ residues - adjusted) / np.linalg.norm(adjusted)
        return float(error), residues

    choices = []
    for polarity in (-1, 1):
        # Several archive measurements have an approximately 59-sample LDV
        # latency; a +/-1 ms interval would falsely force those fits to its rail.
        grid = np.linspace(-.002, .002, 161)
        errors = [solve(delay, polarity)[0] for delay in grid]
        best = int(np.argmin(errors))
        result = minimize_scalar(lambda delay: solve(delay, polarity)[0],
            bounds=(grid[max(0, best - 1)], grid[min(len(grid) - 1, best + 1)]),
            method="bounded", options={"xatol": 1e-10})
        choices += [(errors[best], float(grid[best]), polarity),
                    (float(result.fun), float(result.x), polarity)]
    _, delay, polarity = min(choices)
    _, residues = solve(delay, polarity)
    keep = residues > residues.max() * 1e-8
    modes, residues = modes[keep].astype(np.float32).astype(float), residues[keep].astype(np.float32).astype(float)
    fitted = bridge_basis(FREQUENCY, modes) @ residues
    aligned = target * polarity * np.exp(2j * np.pi * FREQUENCY * delay)
    error = metrics(aligned, fitted, 60)
    return modes, residues, fitted, {"mode_count": len(modes), "fit": error,
        "phase_polarity": polarity, "phase_advance_seconds": delay,
        "phase_search_bounds_seconds": [-.002, .002],
        "fit_passed": bridge_passes(error),
        "minimum_real_mobility": float(fitted.real.min())}


def header(banks: list[dict]) -> str:
    lines = ["// Local experimental coefficients: not integrated or cleared for redistribution.",
        "// Source: Mark Rau, https://rau.mit.edu/projects/GuitarMeasurements/",
        "// No explicit source data license supplied. Mono pressure, scalar normal mobility.",
        "// Pressure h[n] = 2 Re(sum R*p^n), p=exp((-pi*f/q + 2j*pi*f)/48000).",
        "// Apply radiationDelaySamples after summing the pressure modes; rescale at other rates.",
        "// Bridge uses positive residues with the engine's prewarped bilinear transform.",
        "// See report.json for source hashes, assumptions, failed gates and held-out captures.",
        "#pragma once", "#include <array>", "namespace acustra::experimental {",
        "struct RauRadiationMode { float frequency, q, real, imaginary; };",
        "struct RauBridgeMode { float frequency, q, residue; };"]
    for bank in banks:
        name = bank["id"]
        lines += [f"// {bank['name']}; pressure gate: {bank['pressure']['fit_passed']}; bridge gate: {bank['bridge']['fit_passed']}.",
            f"inline constexpr int {name}_radiationDelaySamples = {bank['pressure']['delay_samples_48k']};"]
        for suffix, datatype in (("radiation", "RauRadiationMode"), ("mobility", "RauBridgeMode")):
            rows = bank[suffix + "_coefficients"]
            lines += [f"inline constexpr std::array<{datatype}, {len(rows)}> {name}_{suffix} {{{{"]
            lines += ["    { " + ", ".join(bridge.cpp_float(v) for v in row) + " }," for row in rows]
            lines += ["}};"]
    return "\n".join(lines + ["} // namespace acustra::experimental", ""])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--guitars", nargs="+", choices=SOURCES, default=["Washburn_1897", "SCGC_OM3", "Martin_D18", "Nishihara_ReydenSJ", "Ramirez"])
    args = parser.parse_args()
    # Snapshot executable provenance before any fit, so a concurrently edited
    # helper cannot silently be identified by its later contents.
    started_at = datetime.now(timezone.utc).isoformat()
    dependency_hashes = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                         for path in (Path(__file__), Path(body.__file__), Path(bridge.__file__))}
    provenance = {"started_at_utc": started_at,
        "source_sha256_at_start": dependency_hashes,
        "versions": {"python": sys.version.split()[0], "numpy": np.__version__, "scipy": scipy.__version__},
        "selected_ids": args.guitars}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "report.json").write_text(json.dumps({**provenance, "complete": False,
        "completed_at_utc": None, "qualified_ids": [], "guitars": []}, indent=2) + "\n")
    banks = []
    for guitar in args.guitars:
        path = args.raw_dir / (guitar + "_IR.mat")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if SOURCES[guitar][1] and digest != SOURCES[guitar][1]:
            raise ValueError(f"{guitar}: unexpected source hash")
        values = loadmat(path, simplify_cells=True)[guitar + "_IR"]
        ids = sorted(values, key=lambda key: int(key.split("_")[-1]))
        captures = [values[key] for key in ids]
        if len(captures) < 2:
            raise ValueError("at least two numbered capture records required")
        for capture in captures:
            for field in ("admittance", "radiation", "force", "velocity"):
                if np.shape(capture[field]) != (48000,) or not np.isfinite(capture[field]).all():
                    raise ValueError(f"{guitar}: invalid {field}")
        pressures = [windowed(capture["radiation"]) for capture in captures]
        mobilities = [windowed(capture["admittance"], 12000) for capture in captures]
        mobility = np.mean(mobilities[:-1], axis=0)
        p_modes, p_residues, p_fit, p_report = fit_pressure(pressures[:-1], mobility)
        p_report["held_out_capture"] = ids[-1]
        p_report["held_out_error"] = metrics(pressures[-1], p_fit)
        p_report["train_mean_to_held_out_error"] = metrics(pressures[-1], np.mean(pressures[:-1], axis=0))
        p_report["held_out_passed"] = pressure_passes(p_report["held_out_error"])
        b_modes, b_residues, b_fit, b_report = fit_bridge(mobility)
        b_held = mobilities[-1] * b_report["phase_polarity"] * np.exp(2j * np.pi * FREQUENCY * b_report["phase_advance_seconds"])
        b_report["held_out_error"] = metrics(b_held, b_fit, 60)
        b_report["held_out_passed"] = bridge_passes(b_report["held_out_error"])
        bank = {"id": guitar, "name": SOURCES[guitar][0], "raw_sha256": digest,
            "fit_captures": ids[:-1], "held_out_capture": ids[-1], "pressure": p_report, "bridge": b_report,
            "radiation_coefficients": [[float(f), float(q), float(r.real), float(r.imag)] for (f, q, _), r in zip(p_modes, p_residues)],
            "mobility_coefficients": [[float(f), float(q), float(r)] for (f, q), r in zip(b_modes, b_residues)]}
        bank["measurement_fit_qualified"] = all((p_report["fit_passed"], p_report["held_out_passed"],
                                                 b_report["fit_passed"], b_report["held_out_passed"]))
        banks.append(bank)
        (args.output / (guitar + ".json")).write_text(json.dumps(bank, indent=2) + "\n")
        np.savez_compressed(args.output / (guitar + ".npz"), frequency=FREQUENCY,
            pressure_target=np.mean(pressures[:-1], axis=0), pressure_model=p_fit,
            pressure_held_out=pressures[-1], mobility_target=mobility,
            mobility_model=b_fit, mobility_held_out=mobilities[-1])
        print(guitar, "pressure", p_report["fit"], "held-out", p_report["held_out_error"], "bridge", b_report["fit"], flush=True)
        report = {**provenance,
            "complete": len(banks) == len(args.guitars),
            "completed_at_utc": datetime.now(timezone.utc).isoformat() if len(banks) == len(args.guitars) else None,
            "source": "https://rau.mit.edu/projects/GuitarMeasurements/",
            "source_zip": "https://rau.mit.edu/projectFiles/GuitarMeasurements/GuitarMeasurements.zip",
            "source_license": "not specified by the webpage or ZIP; local analysis only",
            "source_sample_rate_evidence": "48,000-sample records; Rau/Smith/Abel FA2023 section 3 specifies 1 second at 48 kHz",
            "sample_rate_hz": RATE, "radiation_channels": 1,
            "unidentified": ["microphone distance and direction", "impact and LDV coordinates", "room reflection timing", "capture grouping beyond numbered records"],
            "pressure_keep_samples": 3000, "bridge_keep_samples": 12000,
            "held_out_protocol": "final numbered capture is excluded from all fitting and selection",
            "qualification_scope": "conditional transfer approximation and withheld capture prediction; not playing realism, room separation, absolute SPL, or redistribution clearance",
            "qualified_ids": [bank["id"] for bank in banks if bank["measurement_fit_qualified"]],
            "guitars": banks}
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        (args.output / "RauGuitarCandidates.h").write_text(header(banks))
        (args.output / "RauQualifiedGuitarData.h").write_text(header([bank for bank in banks if bank["measurement_fit_qualified"]]))


if __name__ == "__main__":
    main()
