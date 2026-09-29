#!/usr/bin/env python3
"""Fit two measured normal-force inputs to three microphones, preserving phase.

Use the verified Mores g21/g34 archive, authors' SI calibration and raw complex
H1 extraction from AuditBridgeSpatialMap. Apply a common causal window of
12000 samples (250 ms) to both guitars, the final tenth cosine-tapered. It is
the shortest window at which every mode below 700 Hz has its Q within 10% of
the 1 s value; g21 was once fitted at 3000 samples, whose 16 Hz bandwidth set
the Q of every mode below 300 Hz (--g21-keep 3000 reproduces that bank).
There is no minimum-phase conversion, delay alignment, gain fit or engine-data
calibration. Both measured instruments were nylon-strung. These windows discard
late/circular-end terms; this is an approximation of the retained responses.

Generate measured frequency/Q candidates jointly from all six bass/treble
impact x upper/treble/bass microphone paths. Search every prominence-ordered
prefix, starting with one mode. Fit endpoints with the existing residue solver,
round frequency/Q and residue components to float32, and apply the unchanged
complex, magnitude, ERB and microphone-balance gates. The first prefix passing
all endpoint AND derived heave/rock path gates is the selected bank. This is the
smallest prefix in that measured ordering, not an optimal arbitrary pole bank.

For endpoint forces Fb,Ft, define F=Fb+Ft and T=Ft-Fb. Then Hh=(Ht+Hb)/2,
Hr=(Ht-Hb)/2 and pressure=Hh*F+Hr*T. T=M/a requires impact half-spacing a;
interpreting this pair as a complete saddle model additionally assumes rigidity.
No horizontal transfer or saddle rotation axis is identified. Existing scalar
path gates do not bound every coherent force combination near cancellation;
phase and actual-load checks remain necessary before runtime use. No shipping
coefficients are changed. Outputs are an offline JSON report, coefficient/
response NPZ and native MeasuredBodyData.h inside the new output directory,
with axes and Pa/N units recorded in the report. Cross-product
diagnostics compare Ht*conj(Hb), with the existing 1%-of-product-peak mask.

--plate-q median reads g21's modes from 150 Hz to 10 kHz (T1, the rocking
modes and the plate modes, above the air group) against the anechoic flamenca
blancas of the same archive, each fitted at its own converged window with the
same gates: over the octave round each mode, the mode's Q is scaled by the
population's median Q over g21's own, never raised. Frequencies and residues
are kept. The committed header is written this way (--plate-q-band 150 10000,
the default); without the option the measured Qs are written unchanged, and
--plate-q-band 300 10000 writes the earlier band that left T1 alone.
--plate-q-t1-weight w moves the modes only the 150 Hz band reaches (T1 and the
rocking modes, below the earlier band's 300 Hz edge) w of the way in log Q:
Q = Q_asfitted^(1-w) * Q_rule^w, the rule's factor raised to w. Its default is
the steel blend's by-ear weight, read from Source/DSP/SteelBodyBlend.h
(ACUSTRA_STEEL_BLEND_T1_PLATE_Q), and the header records the weight it was
written with (measuredSteelT1PlateQWeight), which the engine checks against
that file. w = 1 writes Blind Set 18's D exactly.
--reweight DIR rewrites only that: it reads an earlier --plate-q median run's
body-force-pair.npz and report.json (g21's as-fitted Q and the population's
banks), applies the rule again with this run's --plate-q-t1-weight and writes
the header, npz and report to --output without refitting (seconds, not the
hour a fit takes). Every other field is the earlier run's.
The report keeps g21's as-fitted Q (plate_q.measured_frequency_q) and the band
beside the population's banks: GenerateMeasuredBridge.py --body-report reads
them to pair the bridge's modes with the radiation's and to damp the unpaired
ones in the same band by the same rule.

    python3 Tools/GenerateBodyForcePair.py --self-test
    python3 Tools/GenerateBodyForcePair.py --raw-mat /path/qualified_selected_impulses.mat --output /new/fit-directory
    python3 Tools/GenerateBodyForcePair.py --reweight /earlier/fit-directory --output /new/directory
"""
from __future__ import annotations

import argparse
from itertools import combinations
import json
from pathlib import Path
import sys

import numpy as np
import scipy

import AuditBodyForcePair as pair
import RemoveRoomTail

spatial, body = pair.spatial, pair.spatial.body
MICROPHONES = ("upper", "treble", "bass")
FREQUENCY = np.fft.rfftfreq(body.FFT_SIZE, 1 / body.SAMPLE_RATE)

# The flamenca blancas of the archive measured in the anechoic laboratory, the
# population g21's plate-mode damping is read against (--plate-q). g41 is kept
# in the list and left out where its bank fails the generator's gates.
PLATE_Q_POPULATION = (37, 38, 39, 41, 42, 43)
# The band corrected by default: every mode above the air group (below 150 Hz,
# where the by-ear air-mode gain acts), T1 and the 209 Hz rocking mode
# included, up to the fit's 10 kHz limit. Only Q changes, so the shape anchors,
# which read frequencies and residues, see the same modes.
PLATE_Q_BAND_HZ = (150.0, 10_000.0)
# The earlier band's lower edge: the modes from PLATE_Q_BAND_HZ[0] up to here
# are the ones Set 18's D added, and the ones --plate-q-t1-weight blends.
PLATE_Q_T1_UPPER_HZ = 300.0
STEEL_BLEND_HEADER = Path(__file__).resolve().parents[1] / "Source/DSP/SteelBodyBlend.h"


def steel_blend_t1_weight(header: Path = STEEL_BLEND_HEADER) -> float:
    """The by-ear D weight the engine's blend header declares."""
    import re
    match = re.search(r"#define ACUSTRA_STEEL_BLEND_T1_PLATE_Q ([0-9.]+)f", header.read_text())
    if match is None:
        raise ValueError(f"{header}: no ACUSTRA_STEEL_BLEND_T1_PLATE_Q")
    return float(match.group(1))


def octave_q(frequency_q: np.ndarray, frequency: float) -> np.ndarray:
    """The Qs of the modes in the octave centred on frequency (f/sqrt2 to f*sqrt2)."""
    low, high = frequency / np.sqrt(2.0), frequency * np.sqrt(2.0)
    return frequency_q[(frequency_q[:, 0] >= low) & (frequency_q[:, 0] < high), 1]


def plate_q_factor(frequency: float, steel_q: np.ndarray, population_q: list) -> float:
    """The "median" rule's factor at a frequency: over the octave round it, the
    population's median Q over the steel bank's own, never above 1.

    GenerateMeasuredBridge.py reads the same factor at a bridge mode that has
    no radiation twin, so the bridge's own modes are damped as the plate's are.
    """
    pooled = np.concatenate([np.asarray(bank)[:, :2] for bank in population_q])
    return min(1.0, np.median(octave_q(pooled, frequency))
                    / np.median(octave_q(np.asarray(steel_q)[:, :2], frequency)))


def plate_q_correction(steel_q: np.ndarray, population_q: list, rule: str,
                       band: tuple = PLATE_Q_BAND_HZ, t1_weight: float = 1.0) -> np.ndarray:
    """g21's Q per mode in band read against the anechoic population.

    Each mode is compared over the octave centred on it (f/sqrt2 to f*sqrt2):
    "median" scales it by the population's median Q over g21's own there,
    never raising one; "ceiling" holds it at the population's upper quartile.
    Frequencies and residues are unchanged: a mode given more loss keeps the
    amplitude it starts with and rings for a shorter time. Below
    PLATE_Q_T1_UPPER_HZ the "median" factor is raised to t1_weight (1 leaves it
    as it is), which moves those modes that share of the way in log Q.
    """
    pooled = np.concatenate([np.asarray(bank)[:, :2] for bank in population_q])
    corrected = np.array(steel_q, dtype=float)
    for index, (frequency, q) in enumerate(steel_q):
        if not band[0] <= frequency < band[1]:
            continue
        if rule == "median":
            factor = plate_q_factor(frequency, steel_q, population_q)
            if frequency < PLATE_Q_T1_UPPER_HZ and t1_weight != 1.0:
                factor = factor ** t1_weight
            corrected[index, 1] = q * factor
        elif rule == "ceiling":
            corrected[index, 1] = min(q, np.percentile(octave_q(pooled, frequency), 75))
        else:
            raise ValueError(f"unknown --plate-q rule {rule}")
    corrected[:, 1] = corrected[:, 1].astype(np.float32).astype(float)
    return corrected


def rounded(values: np.ndarray) -> np.ndarray:
    return values.real.astype(np.float32).astype(float) + 1j * values.imag.astype(np.float32).astype(float)


def response(modes: list, residues: np.ndarray) -> np.ndarray:
    """H(z)=sum R/(1-p/z)+conj(R)/(1-conj(p)/z), with h[0]=2 Re(sum R)."""
    z = np.exp(-2j * np.pi * FREQUENCY / body.SAMPLE_RATE)
    result = np.zeros(len(z), dtype=complex)
    for (frequency, q, _), residue in zip(modes, residues):
        pole = np.exp(-np.pi * frequency / (q * body.SAMPLE_RATE)
                      + 2j * np.pi * frequency / body.SAMPLE_RATE)
        result += residue / (1 - pole*z) + np.conj(residue) / (1 - np.conj(pole)*z)
    return result


def path_check(target: np.ndarray, modes: list, residues: np.ndarray) -> dict:
    # The existing evaluator accepts multiple paths. Repeating one path gives
    # its original complex/magnitude/ERB gates without adding a ratio condition.
    errors, _, band = body.fit_errors([target, target], modes, [residues, residues])
    return dict(complex_relative_l2=errors[0][0], magnitude_abs_median_db=errors[0][1],
                magnitude_abs_p90_db=errors[0][2], erb_level_abs_max_db=band,
                passed=body.within_limits(errors, 0.0, band))


def paired(values: np.ndarray) -> np.ndarray:
    return np.array([pair.force_pair(mic[0], mic[1]) for mic in values])


def render_header(arrays: dict, steel: int = 21, plate_q: tuple | None = None,
                  t1_weight: float | None = None) -> str:
    """Export the auditioned ABI: treble, bass, upper; heave then moment."""
    source = ("// response was measured. Both source guitars are nylon-strung; g21 is",
              "// adapted for steel. These reference residues do not include the")
    if plate_q:
        rule, low, high = plate_q
        source = ("// response was measured. Both source guitars are nylon-strung; g21 is",
                  f"// adapted for steel, its Q from {low:.0f} to {high:.0f} Hz read against the",
                  f"// anechoic flamencas' ({rule} over the octave). These reference",
                  "// residues do not include the")
    if steel != 21:
        source = (f"// response was measured. Both source guitars are nylon-strung; g{steel},",
                  "// measured anechoically, is adapted for steel. These reference",
                  "// residues do not include the")
    lines = ["// Generated by Tools/GenerateBodyForcePair.py; do not hand-edit.",
        "// Robert Mores, https://zenodo.org/records/4604577, CC BY 4.0:",
        "// https://creativecommons.org/licenses/by/4.0/",
        "// Modified by calibrated H1 extraction, common causal taper and joint",
        "// pole/residue fitting. Raw measured phase is retained; no added delay.",
        "// F=Fb+Ft, T=Ft-Fb=M/a; Rh=(Rt+Rb)/2, Rm=(Rt-Rb)/2.",
        "// F=1,T=-1 reconstructs the bass impact; F=1,T=+1 the treble impact.",
        "// The arbitrary-string map assumes a rigid saddle. No horizontal-force",
        *source,
        "// engine's authored construction morphs or establish absolute SPL.",
        "", "#pragma once", "#include <array>", "", "namespace acustra::detail", "{",
        "struct MeasuredBodyMode", "{", "    float frequency, q;",
        "    float leftReal, leftImaginary, rightReal, rightImaginary, upperReal, upperImaginary;",
        "    float leftMomentReal, leftMomentImaginary, rightMomentReal, rightMomentImaginary;",
        "    float upperMomentReal, upperMomentImaginary;", "};"]
    if t1_weight is not None:
        lines += ["", "// The share of the way the modes below "
                  f"{PLATE_Q_T1_UPPER_HZ:.0f} Hz moved in log Q (--plate-q-t1-weight):",
                  "// the steel blend's D weight (SteelBodyBlend.h).",
                  f"inline constexpr float measuredSteelT1PlateQWeight = {float(t1_weight)!r}f;"]
    for guitar, name in ((steel, "Steel"), (34, "Nylon")):
        modes = arrays[f"g{guitar}_frequency_q"]
        residues = paired(arrays[f"g{guitar}_endpoint_residues"])
        lines += ["", f"inline constexpr std::array<MeasuredBodyMode, {len(modes)}> measured{name}BodyModes {{{{"]
        for index, (frequency, q) in enumerate(modes):
            values = [frequency, q]
            for axis in (0, 1):
                for mic in (1, 2, 0):
                    residue = residues[mic, axis, index]
                    values.extend((residue.real, residue.imag))
            lines.append("    { " + ", ".join(body.cpp_float(value) for value in values) + " },")
        lines.append("}};")
    return "\n".join(lines + ["", "} // namespace acustra::detail", ""])


def fit_bank(guitar: int, keep: int, responses: dict) -> tuple[dict, dict]:
    targets = np.array([[spatial.windowed(responses[guitar, impact, channel], keep)
                         for impact in (0, 2)] for channel in (3, 4, 5)])
    derived = paired(targets)
    candidates = sorted(body.candidate_poles(list(targets.reshape(6, -1))),
                        key=lambda item: (-item[2], item[0]))
    trials = []
    for count in range(1, len(candidates) + 1):
        modes = [(float(np.float32(f)), float(np.float32(q)), prominence)
                 for f, q, prominence in sorted(candidates[:count], key=lambda item: item[0])]
        residues = np.zeros((3, 2, count), dtype=complex)
        checks, balances, failed = [], [], None
        for mic in range(3):
            for impact in range(2):
                residues[mic, impact] = rounded(body.fit_residues(targets[mic, impact], modes))
                check = dict(basis="endpoint", microphone=MICROPHONES[mic],
                             input=("bass", "treble")[impact],
                             **path_check(targets[mic, impact], modes, residues[mic, impact]))
                checks.append(check)
                if not check["passed"]:
                    failed = check
                    break
            if failed:
                break
        if failed is None:
            pair_residues = paired(residues)
            for mic in range(3):
                for axis in range(2):
                    check = dict(basis="force_pair", microphone=MICROPHONES[mic],
                                 input=("heave", "rock")[axis],
                                 **path_check(derived[mic, axis], modes, pair_residues[mic, axis]))
                    checks.append(check)
                    if not check["passed"]:
                        failed = check
                        break
                if failed:
                    break
        if failed is None:
            for impact in range(2):
                for first, second in combinations(range(3), 2):
                    _, ratio, _ = body.fit_errors([targets[first, impact], targets[second, impact]],
                        modes, [residues[first, impact], residues[second, impact]])
                    balance = dict(input=("bass", "treble")[impact],
                                   microphones=[MICROPHONES[first], MICROPHONES[second]],
                                   magnitude_ratio_p90_error_db=ratio)
                    balances.append(balance)
                    if ratio > body.MAX_STEREO_RATIO_P90_ERROR_DB and failed is None:
                        failed = dict(basis="endpoint_capture_balance", **balance)
        trials.append(dict(count=count, passed=failed is None, first_failure=failed))
        if failed is not None:
            continue
        models = np.array([[response(modes, residue) for residue in mic] for mic in residues])
        band = (FREQUENCY >= body.MINIMUM_FREQUENCY) & (FREQUENCY <= body.MAXIMUM_FREQUENCY)
        phase = [dict(microphone=name, **spatial.metrics(targets[mic, 1, band]
                     * np.conj(targets[mic, 0, band]), models[mic, 1, band]
                     * np.conj(models[mic, 0, band]))) for mic, name in enumerate(MICROPHONES)]
        report = dict(guitar=guitar, instrument=body.GUITAR_DESCRIPTION[guitar], keep_samples=keep,
            candidate_count=len(candidates), candidates_in_prefix_order=candidates,
            mode_count=count, modes=modes, trials=trials, path_checks=checks,
            endpoint_microphone_balance=balances,
            endpoint_cross_product_diagnostics=phase)
        arrays = {f"g{guitar}_frequency_q": np.array(modes)[:, :2],
                  f"g{guitar}_endpoint_residues": residues,
                  f"g{guitar}_target_endpoint": targets, f"g{guitar}_target_pair": derived,
                  f"g{guitar}_model_endpoint": models, f"g{guitar}_model_pair": paired(models)}
        return report, arrays
    raise ValueError(f"g{guitar}: all {len(candidates)} measured prefixes failed; last {trials[-1] if trials else None}")


def self_test() -> None:
    pair.self_test()
    modes = [(300.0, 4.0, 1.0), (900.0, 8.0, 1.0)]
    residues = np.array([0.002+0.001j, -0.003+0.005j])
    model = response(modes, residues)
    samples = np.arange(body.FFT_SIZE)
    impulse = np.zeros(body.FFT_SIZE)
    for (frequency, q, _), residue in zip(modes, residues):
        pole = np.exp(-np.pi*frequency/(q*body.SAMPLE_RATE) + 2j*np.pi*frequency/body.SAMPLE_RATE)
        impulse += 2*np.real(residue * pole**samples)
    if abs(impulse[0] - 2*residues.real.sum()) > 1e-14:
        raise AssertionError("modal impulse origin is incorrect")
    if np.linalg.norm(np.fft.rfft(impulse)-model)/np.linalg.norm(model) > 1e-12:
        raise AssertionError("complex residues and causal modal realization disagree")
    if not path_check(model, modes, residues)["passed"]:
        raise AssertionError("exact synthetic response failed unchanged gates")
    wrong = path_check(model, modes, -residues)
    if wrong["passed"] or wrong["complex_relative_l2"] < 1.99 or wrong["magnitude_abs_p90_db"] > 1e-10:
        raise AssertionError("a phase-inverted equal-magnitude model escaped the complex gate")
    bass, treble = residues, residues * np.array([0.8, -0.4])
    heave, rock = pair.force_pair(bass, treble)
    if (not np.allclose(response(modes, heave-rock), response(modes, bass), atol=1e-12, rtol=0)
        or not np.allclose(response(modes, heave+rock), response(modes, treble), atol=1e-12, rtol=0)):
        raise AssertionError("residue basis did not reconstruct measured endpoints")
    fixture = {}
    for guitar in (21, 34):
        fixture[f"g{guitar}_frequency_q"] = np.array([[300.0, 4.0]])
        fixture[f"g{guitar}_endpoint_residues"] = np.array([
            [[1+2j], [3+4j]], [[5+6j], [9+10j]], [[-1-2j], [-7-8j]]])
    expected = "    { 300.0f, 4.0f, 7.0f, 8.0f, -4.0f, -5.0f, 2.0f, 3.0f, 2.0f, 2.0f, -3.0f, -3.0f, 1.0f, 1.0f },"
    if render_header(fixture).count(expected) != 2:
        raise AssertionError("native export changed microphone order or force/moment polarity")
    steel_q = np.array([[100.0, 20.0], [600.0, 60.0], [700.0, 20.0], [2000.0, 90.0]])
    population_q = [np.array([[550.0, 30.0], [650.0, 40.0], [750.0, 50.0], [2000.0, 10.0]])]
    median = plate_q_correction(steel_q, population_q, "median", (300.0, 1500.0))
    ceiling = plate_q_correction(steel_q, population_q, "ceiling", (300.0, 1500.0))
    if (not np.array_equal(median[[0, 3]], steel_q[[0, 3]])
            or not np.array_equal(ceiling[[0, 3]], steel_q[[0, 3]])):
        raise AssertionError("the plate-mode Q correction reached a mode outside its band")
    if not np.allclose(median[1:3, 1], [60.0, 20.0]) or not np.allclose(ceiling[1:3, 1], [45.0, 20.0]):
        raise AssertionError("the plate-mode Q correction does not read the population's octave")
    low_q = np.array([[200.0, 40.0], [250.0, 20.0], [600.0, 60.0]])
    low_population = [np.array([[190.0, 10.0], [260.0, 10.0], [580.0, 30.0]])]
    full = plate_q_correction(low_q, low_population, "median", (150.0, 1500.0))
    none = plate_q_correction(low_q, low_population, "median", (150.0, 1500.0), 0.0)
    part = plate_q_correction(low_q, low_population, "median", (150.0, 1500.0), 0.7)
    if (not np.array_equal(plate_q_correction(low_q, low_population, "median", (150.0, 1500.0), 1.0), full)
            or not np.array_equal(none[:2], low_q[:2]) or none[2, 1] != full[2, 1]
            or part[2, 1] != full[2, 1]
            or not np.allclose(part[:2, 1], (low_q[:2, 1] ** 0.3 * full[:2, 1] ** 0.7).astype(np.float32),
                               rtol=1e-6, atol=0)):
        raise AssertionError("the T1 weight is not a log-Q share of the rule below 300 Hz")
    if abs(steel_blend_t1_weight() - 0.5) > 0.5:
        raise AssertionError("SteelBodyBlend.h's D weight is not a share")
    print("Body force-pair generator self-test passed")


def run(raw: Path, output: Path, g21_keep: int = 12000, steel: int = 21,
        steel_keep: int | None = None, room_free: bool = False,
        plate_q: str | None = None, plate_q_band: tuple = PLATE_Q_BAND_HZ,
        t1_weight: float = 1.0) -> None:
    if output.exists() or not output.parent.is_dir():
        raise ValueError("output must be a new directory inside an existing parent")
    if steel != 21 and steel not in body.ANECHOIC_GUITARS:
        raise ValueError(f"g{steel}: only g21 or an anechoically measured guitar may be steel's body")
    if steel_keep is None:
        steel_keep = g21_keep if steel == 21 else body.converged_keep_samples(raw, steel)[0]
    values = spatial.bridge.load_matrix(raw)
    responses, quality = spatial.extract(values, (steel, 34))
    room = None
    if room_free:
        if steel != 21:
            raise ValueError("--room-free applies to g21, the one body measured in a room")
        # The music room's reverberant tail is removed from every g21 path the
        # fit reads, with the room measured on the other eleven guitars
        # recorded in it (RemoveRoomTail.py).
        room = RemoveRoomTail.fit_room(values, held_out=21)
        for impact in RemoveRoomTail.IMPACTS:
            for channel in RemoveRoomTail.MICROPHONES:
                responses[21, impact, channel] = RemoveRoomTail.remove(responses[21, impact, channel], room)
    population_responses = spatial.extract(values, PLATE_Q_POPULATION)[0] if plate_q else None
    banks, arrays = [], {"frequency": FREQUENCY}
    for guitar, keep in ((steel, steel_keep), (34, 12000)):
        bank, values = fit_bank(guitar, keep, responses)
        banks.append(bank)
        arrays.update(values)
        print(f"g{guitar}: first passing measured prefix {bank['mode_count']}/{bank['candidate_count']}", flush=True)
    population = None
    if plate_q:
        if steel != 21:
            raise ValueError("--plate-q applies to g21, whose plate modes are read against the anechoic flamencas")
        # Each population guitar is fitted as steel's body would be: its own
        # converged window, the same candidate ordering and gates.
        population = {}
        for guitar in PLATE_Q_POPULATION:
            keep = body.converged_keep_samples(raw, guitar)[0]
            try:
                _, fitted = fit_bank(guitar, keep, population_responses)
            except ValueError as error:
                print(f"g{guitar}: left out of the population ({str(error)[:60]})", flush=True)
                continue
            population[guitar] = dict(keep_samples=keep, frequency_q=fitted[f"g{guitar}_frequency_q"].tolist())
            print(f"g{guitar}: population bank {len(fitted[f'g{guitar}_frequency_q'])} modes at {keep} samples", flush=True)
        arrays["g21_measured_frequency_q"] = arrays["g21_frequency_q"]
        arrays["g21_frequency_q"] = plate_q_correction(
            arrays["g21_frequency_q"], [entry["frequency_q"] for entry in population.values()],
            plate_q, tuple(plate_q_band), t1_weight)
    report = dict(protocol=__doc__, source=str(raw.resolve()),
        source_url="https://zenodo.org/records/4604577", source_license="CC BY 4.0",
        raw_md5=spatial.bridge.digest(raw), raw_sha256=spatial.sha256(raw),
        tool_sha256=spatial.sha256(Path(__file__)),
        helper_sha256={name: spatial.sha256(Path(__file__).with_name(name)) for name in
            ("GenerateMeasuredBody.py", "GenerateMeasuredBridge.py", "AuditBridgeSpatialMap.py",
             "AuditBodyForcePair.py", "RemoveRoomTail.py")},
        versions=dict(python=sys.version.split()[0], numpy=np.__version__, scipy=scipy.__version__),
        sample_rate=body.SAMPLE_RATE, fft_size=body.FFT_SIZE,
        response_units="Pa/N for endpoint forces and normalized moment T=M/a",
        realization="H(z)=sum R/(1-p*z^-1)+conj(R)/(1-conj(p)*z^-1); p=exp(-pi*f/(Q*Fs)+2j*pi*f/Fs); no added delay",
        rounding="frequency/Q and residue components float32, pole exponential and gate evaluation float64",
        array_axes=dict(endpoint=["microphone:upper,treble,bass", "impact:bass,treble", "frequency"],
                        pair=["microphone:upper,treble,bass", "input:heave,rock", "frequency"],
                        endpoint_residues=["microphone:upper,treble,bass", "impact:bass,treble", "mode"],
                        frequency_q=["mode", "frequency_Hz,Q"]),
        gates=dict(complex_relative_l2=body.MAX_COMPLEX_RELATIVE_ERROR,
            magnitude_median_db=body.MAX_MEDIAN_MAGNITUDE_ERROR_DB,
            magnitude_p90_db=body.MAX_P90_MAGNITUDE_ERROR_DB,
            endpoint_mic_balance_p90_db=body.MAX_STEREO_RATIO_P90_ERROR_DB,
            erb_5k_to_10k_level_abs_db=body.MAX_BAND_MAGNITUDE_ERROR_DB),
        force_quality=quality, banks=banks,
        plate_q=None if population is None else dict(rule=plate_q, band_hz=list(plate_q_band),
            t1_upper_hz=PLATE_Q_T1_UPPER_HZ, t1_weight=t1_weight, population=population,
            measured_frequency_q=arrays["g21_measured_frequency_q"].tolist(),
            corrected_frequency_q=arrays["g21_frequency_q"].tolist()),
        room_removed=None if room is None else dict(
            band_centres_hz=RemoveRoomTail.CENTRES.tolist(),
            t60_s=(3.0 * np.log(10.0) / room["delta"]).tolist(),
            coupling=room["kappa"].tolist(), room_coupling=room["room_kappa"].tolist(),
            anechoic_coupling=room["anechoic_kappa"].tolist(), mann_whitney_p=room["p"].tolist()))
    json.dumps(report, allow_nan=False)
    output.mkdir()
    np.savez(output / "body-force-pair.npz", **arrays)
    report["coefficient_response_npz_sha256"] = spatial.sha256(output / "body-force-pair.npz")
    (output / "MeasuredBodyData.h").write_text(render_header(arrays, steel,
        None if plate_q is None else (plate_q, *plate_q_band),
        None if plate_q is None or steel != 21 else t1_weight))
    report["native_header_sha256"] = spatial.sha256(output / "MeasuredBodyData.h")
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")


def reweight(source: Path, output: Path, t1_weight: float) -> None:
    """Rewrite an earlier --plate-q median run's header at another T1 weight."""
    if output.exists() or not output.parent.is_dir():
        raise ValueError("output must be a new directory inside an existing parent")
    report = json.loads((source / "report.json").read_text())
    plate_q = report.get("plate_q")
    with np.load(source / "body-force-pair.npz") as data:
        arrays = {key: data[key] for key in data.files}
    if not plate_q or plate_q["rule"] != "median" or "g21_measured_frequency_q" not in arrays:
        raise ValueError(f"{source}: not a --plate-q median run of g21")
    band = tuple(plate_q["band_hz"])
    arrays["g21_frequency_q"] = plate_q_correction(
        arrays["g21_measured_frequency_q"],
        [entry["frequency_q"] for entry in plate_q["population"].values()],
        "median", band, t1_weight)
    output.mkdir()
    np.savez(output / "body-force-pair.npz", **arrays)
    report["reweighted_from"] = dict(directory=str(source.resolve()),
        native_header_sha256=report.get("native_header_sha256"),
        tool_sha256=report.get("tool_sha256"))
    report["tool_sha256"] = spatial.sha256(Path(__file__))
    plate_q.update(t1_upper_hz=PLATE_Q_T1_UPPER_HZ, t1_weight=t1_weight,
                   measured_frequency_q=arrays["g21_measured_frequency_q"].tolist(),
                   corrected_frequency_q=arrays["g21_frequency_q"].tolist())
    report["coefficient_response_npz_sha256"] = spatial.sha256(output / "body-force-pair.npz")
    (output / "MeasuredBodyData.h").write_text(render_header(arrays, 21, ("median", *band), t1_weight))
    report["native_header_sha256"] = spatial.sha256(output / "MeasuredBodyData.h")
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False)+"\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--raw-mat", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--g21-keep", type=int, default=12000,
                        help="samples of the g21 responses kept before the taper")
    parser.add_argument("--steel-guitar", type=int, default=21,
                        help="archive guitar steel's body is fitted from: g21, or an "
                             "anechoically measured one at its converged window")
    parser.add_argument("--room-free", action="store_true",
                        help="remove the music room's reverberant tail from g21 before "
                             "fitting (Tools/RemoveRoomTail.py)")
    parser.add_argument("--plate-q", choices=("median", "ceiling"),
                        help="read g21's plate-mode Q against the anechoic flamencas "
                             "over the octave round each mode: their median over g21's, "
                             "or a ceiling at their upper quartile")
    parser.add_argument("--plate-q-band", type=float, nargs=2, default=PLATE_Q_BAND_HZ,
                        metavar=("LOW_HZ", "HIGH_HZ"), help="band --plate-q corrects (default 150 10000)")
    parser.add_argument("--reweight", type=Path, metavar="DIR",
                        help="rewrite an earlier --plate-q median run's header at this "
                             "--plate-q-t1-weight, without refitting")
    parser.add_argument("--plate-q-t1-weight", type=float, default=None,
                        help="share of the way, in log Q, the modes below 300 Hz move under "
                             "--plate-q (default: ACUSTRA_STEEL_BLEND_T1_PLATE_Q in "
                             "Source/DSP/SteelBodyBlend.h)")
    args = parser.parse_args()
    try:
        if args.self_test:
            if args.raw_mat or args.output:
                parser.error("--self-test does not take input/output paths")
            self_test()
        elif args.reweight is not None:
            if args.output is None:
                parser.error("--reweight needs --output")
            t1_weight = (steel_blend_t1_weight() if args.plate_q_t1_weight is None
                         else args.plate_q_t1_weight)
            if not 0.0 <= t1_weight <= 1.0:
                parser.error("--plate-q-t1-weight is a share between 0 and 1")
            reweight(args.reweight, args.output, t1_weight)
        else:
            if args.raw_mat is None or args.output is None:
                parser.error("--raw-mat and --output are required")
            t1_weight = (steel_blend_t1_weight() if args.plate_q_t1_weight is None
                         else args.plate_q_t1_weight)
            if not 0.0 <= t1_weight <= 1.0:
                parser.error("--plate-q-t1-weight is a share between 0 and 1")
            run(args.raw_mat, args.output, args.g21_keep, args.steel_guitar, room_free=args.room_free,
                plate_q=args.plate_q, plate_q_band=tuple(args.plate_q_band), t1_weight=t1_weight)
        return 0
    except (OSError, ValueError, AssertionError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
