#!/usr/bin/env python3
"""Fit one pole set to g21's bridge accelerometers and its microphones.

The archive measured both halves of steel's body from the same hammer
impacts: two accelerometers behind the saddle (the bridge's mobility) and
three microphones (its radiation). Fitted apart, the two banks disagree about
the modes they share (591 Hz: bridge Q 73, radiation Q 53.4), and 29 of the
bridge's 47 modes had no radiation twin at all. A joint fit with common poles
is the measured statement of reciprocity: every mode that radiates also loads
the string, at its own frequency and Q (Maestre, Scavone and Smith, "Joint
modeling of bridge admittance and body radiativity for efficient synthesis
of string instrument sound by digital waveguides", IEEE/ACM TASLP 25(5)
(2017) 1128-1139, which fits one pole set to a violin's admittance and its
radiativity).

Inputs, all from the verified archive (MD5-checked):
  - the six impact-to-microphone paths of GenerateBodyForcePair.py (bass and
    treble impact x upper, treble and bass microphone), raw complex H1, no
    minimum phase, kept for 12000 samples (250 ms) at 48 kHz with the final
    tenth cosine-tapered, the window the shipping radiation was fitted at;
  - the four impact-to-accelerometer paths (bass and treble impact x bass and
    treble accelerometer), converted to velocity/force exactly as
    GenerateMeasuredBridge.py does (the hammer differentiated) and advanced
    by its 2-sample instrumentation alignment, over the whole record as that
    generator reads them: their impulse responses keep 39-43% of their
    energy past 12000 samples, so that window would change them by 116-177%
    (relative complex, 60 Hz-10 kHz) rather than trim a room.

Poles. The body generator's candidates (peaks of the six microphone paths,
177 on g21) and the bridge generator's (peaks of the accelerometer trace, 65)
are pooled: a bridge candidate merges into the nearest radiation candidate
closer than |df| < f/(2Q), with frequency and log Q averaged by prominence
per path (on g21 all 65 find one). The pool is refined by variable
projection: for fixed poles every path's residues are linear (a complex
residue per microphone path on the radiation's discrete pole, a real one per
accelerometer path on the bridge's s/(s^2+2ds+w^2) section), so they are
solved out and the poles alone are moved by Levenberg-Marquardt (Kaufman's
Jacobian) on the concatenated residuals of all ten paths, each weighted by
one over its smoothed magnitude as the body generator weights its residue
fit. The refinement is local and stops at the bridge's cross-side corner
(2245 Hz on g21): each pole stays inside its starting half-power band and
within a quarter of the gap to each neighbour, its Q within a factor 2, and
the poles above the corner, where the bridge is the mean of its two ends and
the modes overlap, keep the candidates' values. Refined freely, or above the
corner, neighbouring poles converge into cancelling pairs (residue
cancellation ratio 22-387 against the shipping bank's 3.9) and the radiation
moves 1.6 dB and more from the approved one.

Selection. The bridge orders the poles itself: its mobility is fitted on all
of them by GenerateMeasuredBridge.py's positive-semidefinite projection
(u = +-1 two-point targets below the corner, their mean above), the poles it
leaves empty and those carrying the least mobility (heave + rock) are
dropped and the fit repeated. The set kept is the smallest no worse than the
committed bridge fit on both of its measures, or failing that the largest
within the 56 bridge slots (ACUSTRA_BRIDGE_MODE_COUNT) that passes that
generator's gates (relative complex error 0.24, median magnitude error
1.6 dB); every other mode's mobility residue is exactly zero. The radiation
is then the smallest prefix of the pooled prominence ordering which, with the
bridge's poles added so that every mode that loads the string also radiates,
passes every gate of GenerateBodyForcePair.py (six endpoint paths, six
force-pair paths, the microphone balance).

Damping. --plate-q median then applies GenerateBodyForcePair.py's rule to the
common Q (300 Hz-10 kHz by default, the band of the candidate Blind Set 18
heard as E, before that generator's default moved to 150 Hz): both the
radiation and the bridge ring at the damped Q the listener approved for the
radiation.

In the steel blend (Source/DSP/SteelBodyBlend.h) this bank is played as a
parallel copy at steelBlendJointBodyWeight, beside g21's own bank and bridge;
its level constant is named steelJointTopMobilityRatio there so it stands
beside MeasuredBridgeData.h's steelTopMobilityRatio. The committed
MeasuredJointBodyData.h is the one cand/body-joint-pole-body (1813a88) wrote,
with only that name changed.

Level. steelJointTopMobilityRatio is the Fylde Falstaff's measured bridge
mobility (Carcagno et al. 2018, between strings 5 and 6; FyldeBridgeReference.py
fits it from --fylde-mat) over this bank's at the bass impact, u = -1, as the
RMS of |Y| on a log-frequency grid over 80 Hz-4 kHz: what the steel presets
scale the flamenca's measured bridge by, so its modes are g21's own and its
level a steel-string top's.

    python3 Tools/GenerateJointBody.py --self-test
    python3 Tools/GenerateJointBody.py --raw-mat /path/qualified_selected_impulses.mat \
        --fylde-mat /path/bridge_admittance_all.mat --output /new/dir [--plate-q median]
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
import textwrap
import time

import numpy as np
import scipy
import scipy.linalg
from scipy.ndimage import gaussian_filter1d

import GenerateBodyForcePair as gen

spatial, body, bridge = gen.spatial, gen.body, gen.spatial.bridge
FS = body.SAMPLE_RATE
FREQUENCY = gen.FREQUENCY
KEEP = 12_000
ALIGNMENT = 2  # GenerateMeasuredBridge.py's fitted instrumentation alignment
MICROPHONE_CHANNELS = (3, 4, 5)  # upper, treble, bass (GenerateBodyForcePair order)
IMPACTS = (bridge.BASS_IMPACT_INDEX, bridge.TREBLE_IMPACT_INDEX)
# (impact, accelerometer): bass/bass, treble/treble, bass->treble, treble->bass
ACCELEROMETER_PATHS = ((0, bridge.BASS_ACCELEROMETER_CHANNEL),
                       (2, bridge.TREBLE_ACCELEROMETER_CHANNEL),
                       (0, bridge.TREBLE_ACCELEROMETER_CHANNEL),
                       (2, bridge.BASS_ACCELEROMETER_CHANNEL))
STRIDE = 5  # 3.662 Hz, the body generator's residue-fit grid
Q_BOUNDS = (2.0, 200.0)
LOCAL_Q = 2.0
LOCAL_BAND = 1.0
REFINE_UPPER_HZ = np.inf
MAX_ACTIVE_BRIDGE_MODES = 56
RATIO_BAND_HZ = (80.0, 4000.0)
DEFAULT_OUTPUT_HEADER = "MeasuredJointBodyData.h"
# The plate-Q band the committed bank was damped over (see Damping above).
JOINT_PLATE_Q_BAND_HZ = (300.0, 10_000.0)


# ----------------------------------------------------------------- targets

def advance(response: np.ndarray, samples: int) -> np.ndarray:
    return response * np.exp(2j * np.pi * FREQUENCY * samples / FS)


def extract(values: np.ndarray, guitar: int, keep: int = KEEP) -> dict:
    """Microphone (3, 2, bins) and accelerometer (4, bins) targets, windowed,
    plus the bridge generator's own full-record two-point mobilities."""
    responses, quality = spatial.extract(values, (guitar,))
    microphones = np.array([[spatial.windowed(responses[guitar, impact, channel], keep)
                             for impact in IMPACTS] for channel in MICROPHONE_CHANNELS])
    # The accelerometers are read over the whole record, as the bridge
    # generator reads them: their velocity/force impulse responses keep
    # 39-43% of their energy past 12000 samples (the integrated low end), so
    # the radiation's window would change them by 116-177% (relative complex,
    # 60 Hz-10 kHz) instead of trimming a room.
    accelerometers = np.array([advance(bridge.transfer(values, guitar, impact, sensor), ALIGNMENT)
                               for impact, sensor in ACCELEROMETER_PATHS])
    full = np.array([bridge.transfer(values, guitar, impact, sensor)
                     for impact, sensor in ACCELEROMETER_PATHS])
    return dict(microphones=microphones, accelerometers=accelerometers, full_accelerometers=full,
                force_quality=quality)


def two_point(accelerometers: np.ndarray) -> tuple:
    """treble, bass and the symmetrized cross mobility, the bridge generator's inputs."""
    bass, treble, forward, reverse = accelerometers
    return treble, bass, 0.5 * (forward + reverse)


# ----------------------------------------------------------------- poles

def merge(radiation: list, bridge_candidates: list) -> list:
    """Pool the two generators' candidates (f, Q, prominence).

    Each generator's own list is kept as it resolved it; a bridge candidate
    is merged into the nearest radiation candidate when |df| < f/(2Q), f the
    pair's mean and Q the pair's log mean weighted by prominence per path
    (the radiation's is summed over its six microphone paths, the bridge's
    read on the one accelerometer trace). A merged pole takes the weighted
    frequency and log Q.
    """
    poles = [dict(frequency=float(f), q=float(q), prominence=float(p),
                  radiation_prominence=float(p), bridge_prominence=0.0,
                  members=[[float(f), float(q), float(p), "radiation"]]) for f, q, p in radiation]
    added = []
    for f, q, p in bridge_candidates:
        best, distance = None, np.inf
        for index, pole in enumerate(poles):
            if pole["bridge_prominence"] > 0.0:
                continue
            gap = abs(pole["frequency"] - f)
            w = np.array([pole["radiation_prominence"] / 6.0, p])
            pair_q = float(np.exp(np.average(np.log([pole["q"], q]), weights=w)))
            if gap < 0.5 * (pole["frequency"] + f) / (2.0 * pair_q) and gap < distance:
                best, distance = index, gap
        entry = [float(f), float(q), float(p), "bridge"]
        if best is None:
            added.append(dict(frequency=float(f), q=float(q), prominence=float(p),
                              radiation_prominence=0.0, bridge_prominence=float(p), members=[entry]))
            continue
        pole = poles[best]
        w = np.array([pole["radiation_prominence"] / 6.0, p])
        pole["frequency"] = float(np.average([pole["frequency"], f], weights=w))
        pole["q"] = float(np.exp(np.average(np.log([pole["q"], q]), weights=w)))
        pole["bridge_prominence"] = float(p)
        pole["prominence"] = pole["radiation_prominence"] + float(p)
        pole["members"].append(entry)
    return sorted(poles + added, key=lambda pole: pole["frequency"])


class Problem:
    """Variable projection over the ten measured paths."""

    def __init__(self, microphones: np.ndarray, accelerometers: np.ndarray):
        self.mic_index = np.flatnonzero((FREQUENCY >= body.MINIMUM_FREQUENCY)
                                        & (FREQUENCY <= body.MAXIMUM_FREQUENCY))[::STRIDE]
        self.acc_index = np.flatnonzero((FREQUENCY >= bridge.MINIMUM_FREQUENCY)
                                        & (FREQUENCY <= bridge.MAXIMUM_FREQUENCY))[::STRIDE]
        self.mic_targets = microphones.reshape(6, -1)
        self.acc_targets = accelerometers

        def weight(target, index):
            envelope = gaussian_filter1d(np.abs(target), 128)
            return 1.0 / np.maximum(envelope, np.max(envelope) * 1.0e-3)[index]
        self.mic_weights = [weight(t, self.mic_index) for t in self.mic_targets]
        self.acc_weights = [weight(t, self.acc_index) for t in self.acc_targets]
        self.z_inverse = np.exp(-2j * np.pi * FREQUENCY[self.mic_index] / FS)
        self.s = 2j * FS * np.tan(np.pi * FREQUENCY[self.acc_index] / FS)
        self._cache: tuple | None = None

    # bases and their derivatives for x = (log f, log Q)
    def bases(self, x: np.ndarray, derivatives: bool = False):
        n = len(x) // 2
        f, q = np.exp(x[:n]), np.exp(x[n:])
        pole = np.exp(-np.pi * f / (q * FS) + 2j * np.pi * f / FS)
        zi = self.z_inverse[:, None]
        positive = 1.0 / (1.0 - pole[None, :] * zi)
        negative = 1.0 / (1.0 - np.conj(pole)[None, :] * zi)
        omega = 2.0 * FS * np.tan(np.pi * f / FS)
        s = self.s[:, None]
        denominator = s * s + (omega / q)[None, :] * s + (omega * omega)[None, :]
        section = s / denominator
        if not derivatives:
            return positive, negative, section
        dp_df = pole * f * (-np.pi / (q * FS) + 2j * np.pi / FS)
        dp_dq = pole * (np.pi * f / (q * FS))
        d_positive = zi * positive ** 2  # d/dp of 1/(1 - p z^-1)
        d_negative = zi * negative ** 2
        domega_df = f * 2.0 * np.pi / np.cos(np.pi * f / FS) ** 2
        dsection_domega = -s * (s / q[None, :] + 2.0 * omega[None, :]) / denominator ** 2
        dsection_dq = s * s * (omega / q)[None, :] / denominator ** 2  # times q (log Q)
        return (positive, negative, section,
                (d_positive * dp_df, d_negative * np.conj(dp_df)),
                (d_positive * dp_dq, d_negative * np.conj(dp_dq)),
                dsection_domega * domega_df, dsection_dq)

    @staticmethod
    def solve(matrix: np.ndarray, vector: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """Ridge least squares as body.fit_residues: the solution and the
        factor of the regularised normal matrix, which projects Kaufman's
        Jacobian onto the complement of the basis."""
        normal = matrix.T @ matrix
        penalty = body.RIDGE * np.trace(normal) / normal.shape[0]
        factor = scipy.linalg.cho_factor(normal + penalty * np.eye(normal.shape[0]))
        return scipy.linalg.cho_solve(factor, matrix.T @ vector), factor

    @staticmethod
    def project(matrix: np.ndarray, factor, raw: np.ndarray) -> np.ndarray:
        return raw - matrix @ scipy.linalg.cho_solve(factor, matrix.T @ raw)

    @staticmethod
    def stack(values: np.ndarray) -> np.ndarray:
        return np.concatenate((values.real, values.imag), axis=0)

    def mic_matrix(self, positive, negative, weight):
        columns = np.empty((positive.shape[0], 2 * positive.shape[1]), dtype=complex)
        columns[:, 0::2] = positive + negative
        columns[:, 1::2] = 1j * (positive - negative)
        return self.stack(columns * weight[:, None])

    def evaluate(self, x: np.ndarray, jacobian: bool = False):
        parts = self.bases(x, derivatives=jacobian)
        positive, negative, section = parts[:3]
        n = len(x) // 2
        residuals, blocks, residues = [], [], []
        for target, weight in zip(self.mic_targets, self.mic_weights):
            matrix = self.mic_matrix(positive, negative, weight)
            vector = self.stack(target[self.mic_index] * weight)
            solution, factor = self.solve(matrix, vector)
            residue = solution[0::2] + 1j * solution[1::2]
            residues.append(residue)
            residuals.append(matrix @ solution - vector)
            if jacobian:
                (dpf, dnf), (dpq, dnq) = parts[3], parts[4]
                # d model / d x_k = R_k dP_k + conj(R_k) dN_k, only mode k's columns
                columns = np.hstack((dpf * residue[None, :] + dnf * np.conj(residue)[None, :],
                                     dpq * residue[None, :] + dnq * np.conj(residue)[None, :]))
                raw = self.stack(columns * weight[:, None])
                blocks.append(self.project(matrix, factor, raw))
        for target, weight in zip(self.acc_targets, self.acc_weights):
            matrix = self.stack(section * weight[:, None])
            vector = self.stack(target[self.acc_index] * weight)
            solution, factor = self.solve(matrix, vector)
            residues.append(solution)
            residuals.append(matrix @ solution - vector)
            if jacobian:
                columns = np.hstack((parts[5] * solution[None, :], parts[6] * solution[None, :]))
                raw = self.stack(columns * weight[:, None])
                blocks.append(self.project(matrix, factor, raw))
        residual = np.concatenate(residuals)
        if jacobian:
            return residual, np.vstack(blocks), residues
        return residual, residues

    def cost_by_path(self, x: np.ndarray) -> list[float]:
        residual, _ = self.evaluate(x)
        sizes = [2 * len(self.mic_index)] * 6 + [2 * len(self.acc_index)] * 4
        out, start = [], 0
        for size in sizes:
            out.append(float(np.sqrt(np.mean(residual[start:start + size] ** 2))))
            start += size
        return out

    def refine(self, frequency: np.ndarray, q: np.ndarray, max_nfev: int = 60, verbose: int = 0):
        """Levenberg-Marquardt on the poles alone (Marquardt's diagonal
        scaling), in log frequency and log Q within the fit band and Q_BOUNDS."""
        n = len(frequency)
        # A local refinement: each pole stays inside its starting half-power
        # band and within a quarter of the gap to each starting neighbour, so
        # neighbours keep at least half their starting separation and cannot
        # converge into a cancelling pair; its Q stays within a factor
        # LOCAL_Q of where it started.
        q = np.clip(q, *Q_BOUNDS)
        order = np.argsort(frequency)
        gap_below = np.full(n, np.inf)
        gap_above = np.full(n, np.inf)
        sorted_frequency = frequency[order]
        gap_below[order[1:]] = np.diff(sorted_frequency)
        gap_above[order[:-1]] = np.diff(sorted_frequency)
        half = LOCAL_BAND * frequency / (2.0 * q)
        lower = np.concatenate((np.log(frequency - np.minimum(half, 0.25 * gap_below)),
                                np.log(np.maximum(q / LOCAL_Q, Q_BOUNDS[0]))))
        upper = np.concatenate((np.log(frequency + np.minimum(half, 0.25 * gap_above)),
                                np.log(np.minimum(q * LOCAL_Q, Q_BOUNDS[1]))))
        x = np.concatenate((np.log(frequency), np.log(q)))
        # Poles above the refinement band keep the candidates' values.
        fixed = np.concatenate((frequency > REFINE_UPPER_HZ, frequency > REFINE_UPPER_HZ))
        lower[fixed] = x[fixed]
        upper[fixed] = x[fixed]
        damping, evaluations, status = 1.0e-3, 0, 0
        residual, jac, _ = self.evaluate(x, jacobian=True)
        evaluations += 1
        cost = 0.5 * float(residual @ residual)
        for iteration in range(max_nfev):
            hessian = jac.T @ jac
            gradient = jac.T @ residual
            diagonal = np.maximum(np.diag(hessian), 1.0e-12 * np.max(np.diag(hessian)))
            improved = False
            while damping < 1.0e10:
                step = -np.linalg.solve(hessian + damping * np.diag(diagonal), gradient)
                trial = np.clip(x + step, lower, upper)
                trial_residual, _ = self.evaluate(trial)
                evaluations += 1
                trial_cost = 0.5 * float(trial_residual @ trial_residual)
                if trial_cost < cost:
                    improved = True
                    break
                damping *= 4.0
            if not improved:
                status = 2
                break
            change = (cost - trial_cost) / cost
            x, cost = trial, trial_cost
            damping = max(damping / 3.0, 1.0e-9)
            if verbose:
                print(f"  LM {iteration + 1}: cost {cost:.6g} (-{100 * change:.4f}%), damping {damping:.2g}", flush=True)
            if change < 1.0e-6:
                status = 1
                break
            residual, jac, _ = self.evaluate(x, jacobian=True)
            evaluations += 1

        class Result:
            pass
        result = Result()
        result.x, result.nfev, result.status, result.cost = x, evaluations, status, cost
        return np.exp(x[:n]), np.exp(x[n:]), result


def collapse(frequency: np.ndarray, q: np.ndarray, prominence: np.ndarray,
             bridge_prominence: np.ndarray, radiation_prominence: np.ndarray):
    """Merge refined poles that converged within f/(2Q) of each other."""
    order = np.argsort(frequency)
    candidates = [(float(frequency[i]), float(q[i]), float(prominence[i]), int(i)) for i in order]
    keep: list[list] = []
    for item in candidates:
        if keep:
            last = keep[-1]
            w = np.array([m[2] for m in last])
            f0 = float(np.average([m[0] for m in last], weights=w))
            q0 = float(np.exp(np.average(np.log([m[1] for m in last]), weights=w)))
            if abs(item[0] - f0) < 0.5 * (item[0] + f0) / (2.0 * max(q0, item[1])):
                last.append(item)
                continue
        keep.append([item])
    out = []
    for group in keep:
        w = np.array([m[2] for m in group])
        idx = [m[3] for m in group]
        out.append((float(np.average([m[0] for m in group], weights=w)),
                    float(np.exp(np.average(np.log([m[1] for m in group]), weights=w))),
                    float(w.sum()), float(bridge_prominence[idx].sum()),
                    float(radiation_prominence[idx].sum())))
    arr = np.array(out)
    return arr[:, 0], arr[:, 1], arr[:, 2], arr[:, 3], arr[:, 4]


# ----------------------------------------------------------------- checks

def cancellation(fq: np.ndarray, residues: np.ndarray) -> float:
    """Sum of per-mode energies over the energy of their sum (1 = none), median
    over the four heard paths (treble and upper microphone, both impacts)."""
    values = []
    for mic, impact in ((1, 0), (1, 1), (0, 0), (0, 1)):
        modes = [(f, q, 0.0) for f, q in fq]
        total = np.sum(np.abs(gen.response(modes, residues[mic, impact])) ** 2)
        parts = sum(np.sum(np.abs(gen.response([modes[k]], residues[mic, impact, k:k + 1])) ** 2)
                    for k in range(len(modes)))
        values.append(parts / total)
    return float(np.median(values))


def radiation_gates(targets: np.ndarray, modes: list) -> tuple[bool, np.ndarray, list, dict | None]:
    """GenerateBodyForcePair.fit_bank's gates for one fixed pole set."""
    from itertools import combinations
    derived = gen.paired(targets)
    residues = np.zeros((3, 2, len(modes)), dtype=complex)
    checks, failed = [], None
    for mic in range(3):
        for impact in range(2):
            residues[mic, impact] = gen.rounded(body.fit_residues(targets[mic, impact], modes))
            check = dict(basis="endpoint", microphone=gen.MICROPHONES[mic], input=("bass", "treble")[impact],
                         **gen.path_check(targets[mic, impact], modes, residues[mic, impact]))
            checks.append(check)
            if not check["passed"] and failed is None:
                failed = check
    pair_residues = gen.paired(residues)
    for mic in range(3):
        for axis in range(2):
            check = dict(basis="force_pair", microphone=gen.MICROPHONES[mic], input=("heave", "rock")[axis],
                         **gen.path_check(derived[mic, axis], modes, pair_residues[mic, axis]))
            checks.append(check)
            if not check["passed"] and failed is None:
                failed = check
    for impact in range(2):
        for first, second in combinations(range(3), 2):
            _, ratio, _ = body.fit_errors([targets[first, impact], targets[second, impact]], modes,
                                          [residues[first, impact], residues[second, impact]])
            check = dict(basis="endpoint_capture_balance", input=("bass", "treble")[impact],
                         microphones=[gen.MICROPHONES[first], gen.MICROPHONES[second]],
                         magnitude_ratio_p90_error_db=ratio,
                         passed=ratio <= body.MAX_STEREO_RATIO_P90_ERROR_DB)
            checks.append(check)
            if not check["passed"] and failed is None:
                failed = check
    return failed is None, residues, checks, failed


def committed_bridge_errors(repo: Path) -> tuple[float, float]:
    """The committed steel bridge fit's complex and median magnitude errors,
    as MeasuredBridgeData.h records them: the regression limit this bank's
    bridge must also meet."""
    text = (repo / "Source" / "DSP" / "MeasuredBridgeData.h").read_text()
    block = text[:text.index("measuredSteelBridgeModes")]
    block = block[block.rindex("// g"):]
    flat = " ".join(line.lstrip("/ ") for line in block.splitlines())
    match = re.search(r"relative complex error ([0-9.]+) and worst median magnitude error ([0-9.]+) dB", flat)
    if match is None:
        raise ValueError("MeasuredBridgeData.h does not record the steel fit's errors")
    return float(match.group(1)), float(match.group(2))


def bridge_elimination(targets: tuple, corner: float, modes: list) -> list:
    """The bridge's own ordering of the joint poles: fit the mobility on every
    pole with GenerateMeasuredBridge's positive-semidefinite projection,
    drop the poles it leaves empty and then those carrying the least mobility
    (heave + rock), refit, and repeat (a tenth at a time down to 56, then one
    by one). Returns [(pole indices, fit)] from the largest set down, each
    fit's errors on the generator's own full-record targets."""
    treble, bass, cross = targets
    limits = (bridge.MAX_COMPLEX_RELATIVE_ERROR, bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB)
    active = list(range(len(modes)))
    sequence = []
    try:
        # the gates are applied by the caller, so the generator reports all fits
        bridge.MAX_COMPLEX_RELATIVE_ERROR, bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB = np.inf, np.inf
        while len(active) >= 8:
            fitted, phase, relative, magnitude = bridge.positive_semidefinite_fit(
                FREQUENCY, treble, bass, cross, corner, [modes[k] for k in active])
            by_frequency = {round(row[0], 6): row for row in fitted}
            energy = {k: (by_frequency[round(modes[k][0], 6)][2] + by_frequency[round(modes[k][0], 6)][4]
                          if round(modes[k][0], 6) in by_frequency else 0.0) for k in active}
            kept = [k for k in active if energy[k] > 0.0]
            sequence.append((kept, dict(rows=list(fitted), phase_advance=phase, relative_error=relative,
                                        magnitude_errors=magnitude, magnitude_error=max(magnitude))))
            print(f"  bridge {len(kept)} modes: complex {relative:.4f}, magnitude {max(magnitude):.3f} dB", flush=True)
            if relative > limits[0] or max(magnitude) > limits[1]:
                break
            drop = max(1, min(len(kept) - MAX_ACTIVE_BRIDGE_MODES, len(kept) // 10)) \
                if len(kept) > MAX_ACTIVE_BRIDGE_MODES else 1
            active = sorted(sorted(kept, key=lambda k: -energy[k])[:len(kept) - drop])
    finally:
        bridge.MAX_COMPLEX_RELATIVE_ERROR, bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB = limits
    return sequence


def bridge_errors(rows: list, targets: tuple, corner: float, advance_samples: int = ALIGNMENT) -> tuple:
    """GenerateMeasuredBridge's regression measures for fixed rows: relative
    complex error over the six string positions and each string's median
    |dB| error, 60 Hz-10 kHz, on its two-point targets."""
    treble, bass, cross = targets
    index = np.flatnonzero((FREQUENCY >= bridge.MINIMUM_FREQUENCY) & (FREQUENCY <= bridge.MAXIMUM_FREQUENCY))[::3]
    frequency = FREQUENCY[index]
    rotation = np.exp(2j * np.pi * frequency * advance_samples / FS)
    tt, bb, tb = treble[index] * rotation, bass[index] * rotation, cross[index] * rotation
    model, target = [], []
    for arm in bridge.STRING_LEVER_ARMS:
        two = ((1 + arm) ** 2 * tt + (1 - arm) ** 2 * bb + 2 * (1 - arm * arm) * tb) / 4
        target.append(np.where(frequency < corner, two, 0.5 * (tt + bb)))
        model.append(string_mobility(rows, frequency, arm))
    model, target = np.array(model), np.array(target)
    relative = float(np.linalg.norm(model - target) / np.linalg.norm(target))
    magnitude = [float(np.median(np.abs(20 * np.log10(np.maximum(np.abs(m), 1e-30) / np.maximum(np.abs(t), 1e-30)))))
                 for m, t in zip(model, target)]
    return relative, magnitude


def string_mobility(rows: list, frequency: np.ndarray, arm: float) -> np.ndarray:
    s = 2j * FS * np.tan(np.pi * frequency / FS)
    total = np.zeros(len(frequency), dtype=complex)
    for f, q, heave, cross, rock in rows:
        residue = heave + 2.0 * arm * cross + arm * arm * rock
        if residue == 0.0:
            continue
        omega = 2.0 * FS * np.tan(np.pi * f / FS)
        total += residue * s / (s * s + omega / q * s + omega * omega)
    return total


def fylde_rows(fylde_mat: Path) -> list:
    """The Fylde Falstaff's float32 modal fit (FyldeBridgeReference.py)."""
    from FyldeBridgeReference import reference_modes
    return [tuple(row) for row in reference_modes(fylde_mat).tolist()]


def fylde_mobility(rows: list, frequency: np.ndarray) -> np.ndarray:
    """The Fylde fit's scalar mobility, the same at every string."""
    return string_mobility(rows, frequency, 0.0)


def top_mobility_ratio(joint_rows: list, fylde: list) -> float:
    grid = RATIO_BAND_HZ[0] * 2.0 ** (np.arange(0.0, np.log2(RATIO_BAND_HZ[1] / RATIO_BAND_HZ[0]), 1.0 / 96.0))
    ours = np.abs(string_mobility(joint_rows, grid, -1.0))
    theirs = np.abs(fylde_mobility(fylde, grid))
    return float(np.sqrt(np.mean(theirs ** 2) / np.mean(ours ** 2)))


# ----------------------------------------------------------------- header

def cpp(value: float) -> str:
    return body.cpp_float(value) if hasattr(body, "cpp_float") else bridge.cpp_float(value)


def render_header(guitar: int, fq: np.ndarray, residues: np.ndarray, rows: list, ratio: float,
                  summary: dict) -> str:
    pair = gen.paired(residues)
    active = sum(1 for row in rows if row[2] > 0.0 or row[4] > 0.0)
    rocking = sum(1 for row in rows if row[4] > 0.0)
    lines = [
        "// Generated by Tools/GenerateJointBody.py; do not hand-edit.",
        "// Robert Mores, https://zenodo.org/records/4604577, CC BY 4.0:",
        "// https://creativecommons.org/licenses/by/4.0/ ; see THIRD_PARTY_NOTICES.md.",
        *textwrap.wrap(f"g{guitar}, {bridge.GUITAR_DESCRIPTION[guitar]}, adapted for steel.",
                       width=76, initial_indent="// ", subsequent_indent="// "),
        "// One pole set fitted jointly to the bass and treble impacts' three",
        "// microphones (raw complex H1, 12000 samples, the final tenth tapered)",
        "// and two accelerometers behind the saddle (velocity/force, the hammer",
        "// differentiated, 2-sample alignment, the whole record), refined below",
        "// the bridge's cross-side corner (Maestre, Scavone and Smith, IEEE/ACM",
        "// TASLP 25(5) 2017: every mode that loads the string radiates on the",
        "// same pole). Radiation residues as MeasuredBodyData.h: left = treble",
        "// microphone, right = bass, upper = upper-bout; force F=Fb+Ft then",
        "// moment T=Ft-Fb. Mobility residues as MeasuredBridgeData.h: a string at",
        "// lever arm u sees heave + 2u cross + u^2 rock times",
        "// s/(s^2 + (omega/q) s + omega^2), positive semidefinite per mode; zero on",
        "// every mode outside the bridge's own set.",
        f"// {len(fq)} modes, {active} carrying mobility ({rocking} rocking below the",
        f"// {summary['corner']:.0f} Hz cross-side corner). Bridge fit over six string positions,",
        f"// 60 Hz-10 kHz: relative complex error {summary['bridge_relative_error']:.6f}, worst median",
        f"// magnitude error {summary['bridge_magnitude_error']:.6f} dB.",
    ]
    if summary.get("plate_q"):
        lines += [f"// Q from {summary['plate_q'][1]:.0f} to {summary['plate_q'][2]:.0f} Hz read against the anechoic "
                  "flamencas", f"// ({summary['plate_q'][0]} over the octave), for the radiation and the bridge alike."]
    lines += [
        "// steelJointTopMobilityRatio: the Fylde Falstaff's measured bridge mobility",
        "// (Carcagno et al. 2018) over this bank's at u = -1, RMS |Y| on a",
        "// log-frequency grid over 80 Hz-4 kHz.",
        "", "#pragma once", "#include <array>", "", "namespace acustra::detail", "{",
        "struct MeasuredJointBodyMode", "{", "    float frequency, q;",
        "    float leftReal, leftImaginary, rightReal, rightImaginary, upperReal, upperImaginary;",
        "    float leftMomentReal, leftMomentImaginary, rightMomentReal, rightMomentImaginary;",
        "    float upperMomentReal, upperMomentImaginary;",
        "    float heave, cross, rock;", "};", "",
        f"inline constexpr float steelJointTopMobilityRatio = {cpp(ratio)};", "",
        f"inline constexpr std::array<MeasuredJointBodyMode, {len(fq)}> measuredSteelJointBodyModes {{{{"]
    for index, (frequency, q) in enumerate(fq):
        values = [frequency, q]
        for axis in (0, 1):
            for mic in (1, 2, 0):
                residue = pair[mic, axis, index]
                values.extend((residue.real, residue.imag))
        values.extend(rows[index][2:])
        lines.append("    { " + ", ".join(cpp(value) for value in values) + " },")
    lines += ["}};", "", "} // namespace acustra::detail", ""]
    return "\n".join(lines)


# ----------------------------------------------------------------- run

def self_test() -> None:
    rng = np.random.default_rng(7)
    modes = np.array([[250.0, 30.0], [410.0, 20.0], [900.0, 45.0]])
    residues = rng.normal(size=(6, 3)) * 1e-3 + 1j * rng.normal(size=(6, 3)) * 1e-3
    mic = np.array([gen.response([(f, q, 0) for f, q in modes], r) for r in residues]).reshape(3, 2, -1)
    acc = np.array([string_mobility([(f, q, h, 0.0, 0.0) for (f, q), h in zip(modes, rng.uniform(0.1, 1, 3))],
                                    FREQUENCY, 0.0) for _ in range(4)])
    problem = Problem(mic, acc)
    x = np.concatenate((np.log(modes[:, 0]), np.log(modes[:, 1])))
    # Kaufman's Jacobian against a finite difference of the projected
    # residual, near the solution where the two agree to first order
    xp = x + 1.0e-5
    residual, jac, _ = problem.evaluate(xp, jacobian=True)
    step = 1e-7
    numeric = []
    for k in range(len(x)):
        d = np.zeros_like(x); d[k] = step
        numeric.append((problem.evaluate(xp + d)[0] - problem.evaluate(xp - d)[0]) / (2 * step))
    numeric = np.array(numeric).T
    if np.linalg.norm(numeric - jac) / np.linalg.norm(numeric) > 1.0e-2:
        raise AssertionError("variable-projection Jacobian disagrees with finite differences")
    frequency, q, _ = problem.refine(modes[:, 0] * [1.01, 0.99, 1.005], modes[:, 1] * [1.3, 0.8, 1.1])
    if np.max(np.abs(frequency / modes[:, 0] - 1)) > 1e-4 or np.max(np.abs(q / modes[:, 1] - 1)) > 1e-3:
        raise AssertionError(f"joint refinement did not recover the synthetic poles: {frequency} {q}")
    merged = merge([(100.0, 10.0, 6.0), (101.0, 10.0, 6.0)], [(102.0, 10.0, 3.0), (300.0, 50.0, 1.0)])
    if len(merged) != 3 or abs(merged[1]["frequency"] - 101.75) > 1e-9:
        raise AssertionError("pole pooling did not merge within half a bandwidth")
    print("Joint body generator self-test passed")


def run(raw: Path, output: Path, guitar: int, plate_q: str | None, plate_q_band: tuple,
        repo: Path, max_nfev: int, verbose: int, cache: Path | None = None,
        max_rounds: int = 1, population_file: Path | None = None,
        refine_upper: float | None = None, fylde_mat: Path | None = None) -> None:
    if output.exists() or not output.parent.is_dir():
        raise ValueError("output must be a new directory inside an existing parent")
    started = time.time()
    values = bridge.load_matrix(raw)
    data = extract(values, guitar)
    microphones, accelerometers = data["microphones"], data["accelerometers"]
    # candidates, as each generator takes them
    radiation = body.candidate_poles(list(microphones.reshape(6, -1)))
    frequency_axis, treble, bass, cross = bridge.extract_two_point(raw, guitar)
    bridge_candidates = bridge.candidate_modes(frequency_axis, treble + bass)
    print(f"g{guitar}: {len(radiation)} radiation and {len(bridge_candidates)} bridge candidates", flush=True)
    pooled = merge(radiation, bridge_candidates)
    print(f"pooled to {len(pooled)} poles", flush=True)
    f0 = np.array([p["frequency"] for p in pooled])
    q0 = np.array([p["q"] for p in pooled])
    prominence = np.array([p["prominence"] for p in pooled])
    bridge_prominence = np.array([p["bridge_prominence"] for p in pooled])
    radiation_prominence = np.array([p["radiation_prominence"] for p in pooled])

    corner, _ = bridge.coherence_corner(raw, guitar)
    global REFINE_UPPER_HZ
    REFINE_UPPER_HZ = corner if refine_upper is None else refine_upper
    print(f"poles refined below {REFINE_UPPER_HZ:.0f} Hz (the bridge's cross-side corner)", flush=True)
    problem = Problem(microphones, accelerometers)
    before = problem.cost_by_path(np.concatenate((np.log(f0), np.log(q0))))
    if cache is not None and cache.exists():
        stored = np.load(cache)
        f2, q2, prom2, bprom2, rprom2 = (stored[k] for k in ("f", "q", "prominence", "bridge", "radiation"))
        evaluations, rounds = int(stored["evaluations"]), int(stored["rounds"])
        print(f"refined poles read from {cache}", flush=True)
    else:
        f2, q2, prom2, bprom2, rprom2 = f0, q0, prominence, bridge_prominence, radiation_prominence
        evaluations, rounds = 0, 0
        # Refine, then merge poles that converged within f/(2Q) of a
        # neighbour (near-duplicate pairs cancel), until none do.
        while True:
            f1, q1, result = problem.refine(f2, q2, max_nfev=max_nfev, verbose=verbose)
            evaluations += result.nfev
            rounds += 1
            if rounds >= max_rounds:
                f2, q2 = f1, q1
                break
            f2, q2, prom2, bprom2, rprom2 = collapse(f1, q1, prom2, bprom2, rprom2)
            print(f"round {rounds}: {len(f1)} poles refined, {len(f1) - len(f2)} merged onto a neighbour "
                  f"({time.time() - started:.0f} s)", flush=True)
            if len(f2) == len(f1) or rounds >= max_rounds:
                f2, q2 = f1, q1
                break
        if cache is not None:
            np.savez(cache, f=f2, q=q2, prominence=prom2, bridge=bprom2, radiation=rprom2,
                     evaluations=evaluations, rounds=rounds)
    after = problem.cost_by_path(np.concatenate((np.log(f2), np.log(q2))))
    print(f"{len(f2)} poles after {rounds} rounds, {evaluations} evaluations: weighted RMS per path "
          f"{np.round(before, 3).tolist()} -> {np.round(after, 3).tolist()}", flush=True)

    class Result:
        pass
    result = Result()
    result.nfev, result.status = evaluations, rounds
    # float32 poles, as the header stores them
    f2 = f2.astype(np.float32).astype(float)
    q2 = q2.astype(np.float32).astype(float)

    bridge_targets = (treble, bass, cross)
    reference = committed_bridge_errors(repo)
    all_modes = [(float(f2[k]), float(q2[k]), float(prom2[k])) for k in range(len(f2))]
    sequence = bridge_elimination(bridge_targets, corner, all_modes)
    # The smallest set no worse than the committed bridge fit on both of its
    # measures; failing that, the largest within the bridge slots that passes
    # the generator's gates.
    bridge_set, fit = None, None
    for members, candidate in sequence:
        if (candidate["relative_error"] <= min(bridge.MAX_COMPLEX_RELATIVE_ERROR, reference[0])
                and candidate["magnitude_error"] <= min(bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB, reference[1])
                and len(members) <= MAX_ACTIVE_BRIDGE_MODES):
            bridge_set, fit = members, candidate
    if bridge_set is None:
        for members, candidate in sequence:
            if (candidate["relative_error"] <= bridge.MAX_COMPLEX_RELATIVE_ERROR
                    and candidate["magnitude_error"] <= bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB
                    and len(members) <= MAX_ACTIVE_BRIDGE_MODES):
                bridge_set, fit = members, candidate
                print("no set within the bridge slots is as close as the committed fit; "
                      "keeping the largest that passes the generator's gates", flush=True)
                break
    if bridge_set is None:
        raise ValueError("no bridge prefix of the joint poles passes the bridge gates within "
                         f"{MAX_ACTIVE_BRIDGE_MODES} modes")
    print(f"bridge: {len(bridge_set)} modes carry mobility, complex {fit['relative_error']:.4f}, "
          f"magnitude {fit['magnitude_error']:.3f} dB (committed {reference[0]:.4f}, {reference[1]:.3f})",
          flush=True)

    order = sorted(range(len(f2)), key=lambda k: (-prom2[k], f2[k]))
    trials, chosen = [], None
    for count in range(1, len(order) + 1):
        subset = sorted(set(order[:count]) | set(bridge_set), key=lambda k: f2[k])
        if trials and len(subset) == trials[-1]["modes"]:
            continue
        modes = [(float(f2[k]), float(q2[k]), float(prom2[k])) for k in subset]
        passed, residues, checks, failed = radiation_gates(microphones, modes)
        trials.append(dict(count=count, modes=len(subset), radiation_passed=passed,
                           first_failure=None if failed is None else
                           {k: failed[k] for k in ("basis", "microphone", "input") if k in failed}))
        if passed:
            chosen = (subset, modes, residues, checks)
            break
    if chosen is None:
        raise ValueError("no prominence prefix of the joint poles passes the radiation gates")
    subset, modes, residues, checks = chosen
    print(f"radiation passes at the {count}-pole prominence prefix with the bridge's poles: "
          f"{len(subset)} modes", flush=True)
    rows = [(m[0], m[1], 0.0, 0.0, 0.0) for m in modes]
    fitted = {round(row[0], 6): row for row in fit["rows"]}
    for position, k in enumerate(subset):
        if k in bridge_set:
            rows[position] = fitted[round(float(f2[k]), 6)]
    active = sum(1 for row in rows if row[2] > 0.0 or row[4] > 0.0)
    members = bridge_set

    fq_measured = np.array([(m[0], m[1]) for m in modes])
    fq = fq_measured.copy()
    population = None
    if plate_q and population_file is not None and population_file.exists():
        population = json.loads(population_file.read_text())
        print(f"population banks read from {population_file}", flush=True)
    elif plate_q:
        population_responses = spatial.extract(values, gen.PLATE_Q_POPULATION)[0]
        population = {}
        for other in gen.PLATE_Q_POPULATION:
            keep = body.converged_keep_samples(raw, other)[0]
            try:
                _, fitted = gen.fit_bank(other, keep, population_responses)
            except ValueError as error:
                print(f"g{other}: left out of the population ({str(error)[:60]})", flush=True)
                continue
            population[other] = dict(keep_samples=keep, frequency_q=fitted[f"g{other}_frequency_q"].tolist())
            print(f"g{other}: population bank {len(fitted[f'g{other}_frequency_q'])} modes", flush=True)
        if population_file is not None:
            population_file.write_text(json.dumps(population))
    if plate_q:
        fq = gen.plate_q_correction(fq_measured, [e["frequency_q"] for e in population.values()],
                                    plate_q, tuple(plate_q_band))
        rows = [(f, q, *row[2:]) for (f, q), row in zip(fq, rows)]

    damped_bridge = bridge_errors(rows, bridge_targets, corner)
    print(f"bridge at the common damped Q: complex {damped_bridge[0]:.4f}, magnitude "
          f"{max(damped_bridge[1]):.3f} dB", flush=True)
    ratio = top_mobility_ratio(rows, fylde_rows(fylde_mat))
    canc = cancellation(fq_measured, residues)
    summary = dict(corner=corner, bridge_relative_error=fit["relative_error"],
                   bridge_magnitude_error=fit["magnitude_error"],
                   plate_q=None if not plate_q else (plate_q, *plate_q_band))
    output.mkdir()
    header = render_header(guitar, fq, residues, rows, ratio, summary)
    (output / DEFAULT_OUTPUT_HEADER).write_text(header)
    np.savez(output / "joint-body.npz", frequency_q=fq, measured_frequency_q=fq_measured,
             residues=residues, mobility=np.array(rows), pooled_frequency=f0, pooled_q=q0,
             refined_frequency=f2, refined_q=q2, prominence=prom2, bridge_prominence=bprom2,
             microphones=microphones, accelerometers=accelerometers)
    report = dict(protocol=__doc__, guitar=guitar, source=str(raw.resolve()), raw_md5=bridge.digest(raw),
                  tool_sha256=spatial.sha256(Path(__file__)),
                  versions=dict(python=sys.version.split()[0], numpy=np.__version__, scipy=scipy.__version__),
                  candidates=dict(radiation=len(radiation), bridge=len(bridge_candidates), pooled=len(pooled),
                                  refined=len(f2)),
                  refinement=dict(evaluations=int(result.nfev), status=int(result.status),
                                  weighted_rms_before=before, weighted_rms_after=after),
                  radiation=dict(mode_count=len(modes), trials=trials, checks=checks, cancellation=canc),
                  bridge=dict(corner=corner, prefix=len(members), active=active,
                              rocking=sum(1 for row in rows if row[4] > 0.0),
                              relative_error=fit["relative_error"], magnitude_errors=fit["magnitude_errors"],
                              phase_advance=fit["phase_advance"], committed=reference,
                              elimination=[dict(modes=len(m), relative_error=c['relative_error'],
                                                magnitude_error=c['magnitude_error']) for m, c in sequence]),
                  bridge_at_common_q=dict(relative_error=damped_bridge[0], magnitude_errors=damped_bridge[1]),
                  steel_top_mobility_ratio=ratio,
                  plate_q=None if population is None else dict(rule=plate_q, band_hz=list(plate_q_band),
                                                               population=population),
                  seconds=time.time() - started)
    (output / "report.json").write_text(json.dumps(report, indent=1, default=float) + "\n")
    print(f"{len(modes)} modes, {active} with mobility; cancellation {canc:.2f}; "
          f"steelJointTopMobilityRatio {ratio:.5f}; wrote {output / DEFAULT_OUTPUT_HEADER}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--raw-mat", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--guitar", type=int, default=21)
    parser.add_argument("--plate-q", choices=("median", "ceiling"))
    parser.add_argument("--plate-q-band", type=float, nargs=2, default=JOINT_PLATE_Q_BAND_HZ)
    parser.add_argument("--max-evaluations", type=int, default=60)
    parser.add_argument("--verbose", type=int, default=0)
    parser.add_argument("--cache", type=Path, help="npz holding the refined poles (read if present, else written)")
    parser.add_argument("--refine-upper-hz", type=float,
                        help="refine poles below this frequency (default: the cross-side corner)")
    parser.add_argument("--population", type=Path,
                        help="json of the plate-Q population banks (read if present, else written)")
    parser.add_argument("--fylde-mat", type=Path,
                        help="bridge_admittance_all.mat, the Fylde Falstaff's measured mobility "
                             "that sets steelJointTopMobilityRatio (FyldeBridgeReference.py)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        else:
            if args.raw_mat is None or args.output is None or args.fylde_mat is None:
                parser.error("--raw-mat, --fylde-mat and --output are required")
            run(args.raw_mat, args.output, args.guitar, args.plate_q, tuple(args.plate_q_band),
                Path(__file__).resolve().parents[1], args.max_evaluations, args.verbose, args.cache,
                population_file=args.population, refine_upper=args.refine_upper_hz,
                fylde_mat=args.fylde_mat)
        return 0
    except (OSError, ValueError, AssertionError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
