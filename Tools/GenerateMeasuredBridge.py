#!/usr/bin/env python3
"""Generate Acustra's passive spatial bridge approximation, steel's own bridge.

The input is Robert Mores' ``qualified_selected_impulses.mat``.  Method.pdf
section 2b describes three hammer positions on the bridge inlay and two
accelerometers, "the two signals at the bass and the treble side taken
together allow to trace two of the degrees of freedom of the bridge".  This
tool uses both of them: segment 1 with channel 3 is the bass impact read at
the bass end, segment 3 with channel 2 the treble impact read at the treble
end. Figure 3 locates the accelerometers behind the saddle on the tie block,
not at the hammer impacts; their exact spacing is unreported. The two
cross-side records (segment 1/channel 2 and segment 3/channel 3) are averaged
as a model constraint, not as an experimentally verified reciprocal pair.
Acceleration/force is converted to velocity/force
exactly as the archive's MATLAB script does, by differentiating the hammer.

The model treats these responses as collocated heave/rock ports. This is a
spatial approximation; a positive-semidefinite fit makes the model passive,
but cannot establish collocation of the original measurements. Distance
along the saddle is measured in units of the impacts'
half-separation, so the impacts are at u = -1 (bass) and u = +1 (treble) and
a string at u sees the driving-point mobility

    Y(u) = R_hh + 2 u R_hr + u^2 R_rr   summed over the modes,

which fits the treble-side response at u = +1 and bass-side at u = -1. Each mode
carries one pole pair and the residue matrix [[R_hh, R_hr], [R_hr, R_rr]];
constraining that matrix to be positive semidefinite makes Y(u) a positive
real sum for every u, so the waveguide termination can still use the passive
reflectance construction in Bank and Karjalainen, DAFx-10, Eq. (17):
https://www.dafx.de/paper-archive/2010/DAFx10/BankKarjalainen_DAFx10_P60.pdf

The cross-side records' disagreement sets a heuristic corner in third-octave
bands. Sensor offsets and setup differences can contribute to it; this is
not a unique measurement of the frequency where bridge rigidity is lost.
Below the corner the target is the symmetrized spatial approximation;
above it every string gets the mean of the two side responses, and no mode
carries a cross or rocking residue.

One measured guitar is emitted, for the Original model: the g21 DeVoe
flamenco, adapted for steel; it was not steel-strung. The archive's
physical-measures table lists Savarez Tomatito strings for g21:
https://www.savarez.com/tomatito-normal-tension-t50r (nylon/KF trebles).
The fit must meet relative-complex and median-magnitude limits pinned on the
g21 scalar fit.

NumPy and SciPy are required (the command lines are at the end).

Steel's own bridge plays on its radiation's poles. The same modal body gives
mode k's mobility residue phi_k(bridge)^2/m_k and its radiation residue
phi_k(bridge) psi_k(mic)/m_k on one pole, so every steel bridge mode that is
the same resonance as a radiation mode of MeasuredBodyData.h (--radiation-
header) is given that mode as its twin: the nearest radiation mode k, taken
only when the bridge mode lies inside k's as-fitted half-power band,
|f_b - f_k| < f_k/(2 Q_k), and is itself one resolved resonance, f_b/Q_b <
(f_k+1 - f_k-1)/2. The as-fitted Qs (before the plate-Q rule) and the
anechoic population come from GenerateBodyForcePair.py's report
(--body-report). A mode without a twin is damped as the radiation's plate
modes were: GenerateBodyForcePair.plate_q_factor at its own frequency inside
the report's band, else 1, raised below 300 Hz to the report's T1 weight (the
steel blend's D weight, --plate-q-t1-weight) as the radiation's modes there
were. The engine gives a twinned mode its twin's engine
pole (AcustraEngine.cpp, steelOwnBridgePole).

The flamenca's top is far more compliant than a steel-string guitar's.
steelTopMobilityRatio is the geometric mean over 1000 log-spaced frequencies
in 80 Hz-4 kHz of |Y_Fylde(f)| / |Y_steel(f, u=-1)|, both modal sums with no
anchor and no plate floor: the Fylde Falstaff (Carcagno et
al., JASA 144 (2018) 3533, https://doi.org/10.1121/1.5084735, data
https://osf.io/f4pqa/, CC BY 4.0), fitted by FyldeBridgeReference.py from
--fylde-mat, was measured between strings 5 and 6, which is the flamenca's
bass-side point u = -1. The run prints its spread over other bands and over
conductance.

    python3 Tools/GenerateBodyForcePair.py --raw-mat /path/qualified_selected_impulses.mat \
        --plate-q median --output /tmp/body
    python3 Tools/GenerateMeasuredBridge.py --raw-mat /path/qualified_selected_impulses.mat \
        --body-report /tmp/body/report.json --fylde-mat /path/bridge_admittance_all.mat --check
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import sys
import textwrap

import numpy as np
from scipy.io import loadmat
from scipy.ndimage import gaussian_filter1d
from scipy.signal import find_peaks


SAMPLE_RATE = 48_000.0
FFT_SIZE = 65_536
RECORD_SAMPLES = 48_000
# Method.pdf section 4: signal 1:144000 is bass/centre/treble impact, and the
# channels are 1 hammer, 2 sensor treble side, 3 sensor bass side.
BASS_IMPACT_INDEX = 0
TREBLE_IMPACT_INDEX = 2
IMPACT_INDEX = TREBLE_IMPACT_INDEX
FORCE_CHANNEL = 0
TREBLE_ACCELEROMETER_CHANNEL = 1
BASS_ACCELEROMETER_CHANNEL = 2
# Distance along the saddle in units of the impacts' half-separation. The
# treble impact is between B3 and E4 and the bass impact between E2 and A2
# (Method.pdf section 2b), so they are the midpoints of string pairs (4,5)
# and (0,1) and the six strings sit at (i - 2.5) / 2. Only the ratio enters,
# so the set-up spacing at the saddle cancels; the engine's
# saddleLeverArm() carries the same expression.
STRING_LEVER_ARMS = tuple((index - 2.5) / 2.0 for index in range(6))
MINIMUM_FREQUENCY = 60.0
MAXIMUM_FREQUENCY = 10_000.0
# This is the smallest prominence pool that retains 50 positive-real modes
# from g21. The DAFx-26 DeVoe model uses 50--200 bridge modes.
CANDIDATE_COUNT = 65
PEAK_PROMINENCE_DB = 0.5
Q_MINIMUM = 2.0
Q_MAXIMUM = 80.0
# Standard tuning at A = 440 Hz; the guitars were measured strung.
OPEN_STRING_HZ = (82.40689, 110.0, 146.83238, 195.99772, 246.94165, 329.62756)
MAX_COMPLEX_RELATIVE_ERROR = 0.24
MAX_MEDIAN_MAGNITUDE_ERROR_DB = 1.6
RAW_MD5 = "733cb10baf5ce36d8bf333610ffbb260"
HAMMER_NEWTONS_PER_FULL_SCALE = (10_000.0 / 92.90) * 4.4482
ACCELERATION_MPS2_PER_FULL_SCALE = (10_000.0 / 10.64) * 9.80665
# Provenance from the archive's List_of_guitars_description.pdf.
GUITAR_DESCRIPTION = {
    21: "a 2018 Lester DeVoe flamenca blanca, spruce/cypress, measured in a "
        "school music room in Freiburg",
    34: "a 1971 Manuel Contreras classical Spanish, cedar/Rio palisander, "
        "measured anechoic in the class-1 free-field laboratory of the "
        "Hamburg University of Applied Sciences",
    35: "a 1978 Manuel Lopez Bellido classical Spanish, cedar/Rio palisander, "
        "measured anechoic in the class-1 free-field laboratory of the "
        "Hamburg University of Applied Sciences",
    36: "a 2001 Jose Lopez Bellido classical Spanish, spruce/Indian "
        "palisander, measured anechoic in the class-1 free-field laboratory "
        "of the Hamburg University of Applied Sciences",
    39: "a 1971 German Perez Barranco flamenca blanca (Nr. 11), cedar/"
        "cypress, measured anechoic in the class-1 free-field laboratory of "
        "the Hamburg University of Applied Sciences",
}
DEFAULT_STEEL_GUITAR = 21
DEFAULT_OUTPUT = (
    Path(__file__).resolve().parents[1] / "Source" / "DSP" / "MeasuredBridgeData.h"
)


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "md5").hexdigest()


def load_matrix(path: Path) -> np.ndarray:
    actual_digest = digest(path)
    if actual_digest != RAW_MD5:
        raise ValueError(f"{path}: MD5 {actual_digest}, expected {RAW_MD5}")

    values = loadmat(path, variable_names=["qualified_selected_impulses"])[
        "qualified_selected_impulses"
    ]
    if values.shape != (65, 144_000, 6):
        raise ValueError(f"{path}: unexpected source matrix shape {values.shape}")
    return values


def transfer(
    values: np.ndarray, guitar: int, impact: int, sensor: int
) -> np.ndarray:
    start = impact * RECORD_SAMPLES
    record = values[guitar - 1, start : start + RECORD_SAMPLES, :]
    if not np.all(np.isfinite(record)):
        raise ValueError(f"g{guitar} impact record {impact} is non-finite")

    taper = 0.5 * np.cos(
        np.arange(1, RECORD_SAMPLES + 1) * np.pi / RECORD_SAMPLES
    ) + 0.5
    force = record[:, FORCE_CHANNEL] * taper * HAMMER_NEWTONS_PER_FULL_SCALE
    differentiated_force = np.diff(force) * SAMPLE_RATE
    force_spectrum = np.fft.rfft(
        np.pad(differentiated_force, (0, FFT_SIZE - len(differentiated_force)))
    )
    acceleration = record[:, sensor] * taper * ACCELERATION_MPS2_PER_FULL_SCALE
    acceleration_spectrum = np.fft.rfft(acceleration, FFT_SIZE)
    denominator = np.abs(force_spectrum) ** 2
    denominator += np.max(denominator) * 1.0e-14
    return acceleration_spectrum * np.conj(force_spectrum) / denominator


def extract_mobility(
    path: Path, guitar: int = 21
) -> tuple[np.ndarray, np.ndarray]:
    values = load_matrix(path)
    frequency = np.fft.rfftfreq(FFT_SIZE, 1.0 / SAMPLE_RATE)
    return frequency, transfer(
        values, guitar, TREBLE_IMPACT_INDEX, TREBLE_ACCELEROMETER_CHANNEL
    )


def extract_two_point(
    path: Path, guitar: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Return frequency and the treble, bass and reciprocal cross mobilities."""
    values = load_matrix(path)
    frequency = np.fft.rfftfreq(FFT_SIZE, 1.0 / SAMPLE_RATE)
    treble = transfer(
        values, guitar, TREBLE_IMPACT_INDEX, TREBLE_ACCELEROMETER_CHANNEL
    )
    bass = transfer(
        values, guitar, BASS_IMPACT_INDEX, BASS_ACCELEROMETER_CHANNEL
    )
    # The source's sensors are offset from its impacts (Method.pdf Fig. 3),
    # so these are not a verified reciprocal pair. Symmetrization is a model
    # constraint; their disagreement sets the heuristic corner below.
    cross_forward = transfer(
        values, guitar, BASS_IMPACT_INDEX, TREBLE_ACCELEROMETER_CHANNEL
    )
    cross_reverse = transfer(
        values, guitar, TREBLE_IMPACT_INDEX, BASS_ACCELEROMETER_CHANNEL
    )
    return frequency, treble, bass, 0.5 * (cross_forward + cross_reverse)


def coherence_corner(
    path: Path, guitar: int
) -> tuple[float, list[tuple[float, float]]]:
    """Heuristic corner from disagreement between the cross-side records.

    In third-octave bands, the median disagreement between those two
    records is divided by the median of the transfer itself; the corner is
    the top edge of the last band below which that ratio stays under one,
    that is, the last band in which the cross term is larger than the spread
    of the two records. Above it the model uses the mean of the two ends.
    Sensor offsets/setup differences can contribute to this discrepancy;
    the threshold does not uniquely diagnose loss of bridge rigidity.
    """
    frequency, _, _, _ = extract_two_point(path, guitar)
    values = load_matrix(path)
    forward = transfer(
        values, guitar, BASS_IMPACT_INDEX, TREBLE_ACCELEROMETER_CHANNEL
    )
    reverse = transfer(
        values, guitar, TREBLE_IMPACT_INDEX, BASS_ACCELEROMETER_CHANNEL
    )
    corner = MINIMUM_FREQUENCY
    bands: list[tuple[float, float]] = []
    edge = 2.0 ** (1.0 / 6.0)
    for exponent in range(-24, 14):
        centre = 1000.0 * 2.0 ** (exponent / 3.0)
        if centre < MINIMUM_FREQUENCY or centre > MAXIMUM_FREQUENCY:
            continue
        band = (frequency >= centre / edge) & (frequency < centre * edge)
        if int(np.count_nonzero(band)) < 3:
            continue
        error = float(
            np.median(np.abs(forward[band] - reverse[band]))
            / np.median(np.abs(0.5 * (forward[band] + reverse[band])))
        )
        bands.append((centre, error))
        if error >= 1.0:
            break
        corner = centre * edge
    return corner, bands


def crossing(
    frequency: np.ndarray,
    magnitude_db: np.ndarray,
    peak: int,
    direction: int,
) -> float:
    threshold = magnitude_db[peak] - 3.0
    limit = frequency[peak] * (0.85 if direction < 0 else 1.15)
    index = peak
    while 0 <= index + direction < len(magnitude_db):
        following = index + direction
        if direction < 0 and frequency[following] < limit:
            break
        if direction > 0 and frequency[following] > limit:
            break
        if magnitude_db[following] <= threshold:
            x0, x1 = frequency[index], frequency[following]
            y0, y1 = magnitude_db[index], magnitude_db[following]
            if y1 == y0:
                return float(x1)
            return float(x0 + (threshold - y0) * (x1 - x0) / (y1 - y0))
        index = following
    return float(limit)


def candidate_modes(
    frequency: np.ndarray, mobility: np.ndarray,
    candidate_count: int = CANDIDATE_COUNT,
) -> list[tuple[float, float, float]]:
    useful = np.flatnonzero(
        (frequency >= MINIMUM_FREQUENCY) & (frequency <= MAXIMUM_FREQUENCY)
    )
    magnitude_db = gaussian_filter1d(
        20.0 * np.log10(np.maximum(np.abs(mobility), 1.0e-30)), 1.5
    )
    peaks, properties = find_peaks(
        magnitude_db[useful], prominence=PEAK_PROMINENCE_DB, distance=3
    )
    modes: list[tuple[float, float, float]] = []
    for local_peak, prominence in zip(peaks, properties["prominences"]):
        peak = int(useful[local_peak])
        left = crossing(frequency, magnitude_db, peak, -1)
        right = crossing(frequency, magnitude_db, peak, 1)
        q = np.clip(
            frequency[peak] / max(right - left, frequency[1]),
            Q_MINIMUM,
            Q_MAXIMUM,
        )
        modes.append((float(frequency[peak]), float(q), float(prominence)))

    modes.sort(key=lambda item: (-item[2], item[0]))
    if len(modes) < candidate_count:
        raise ValueError(
            f"only {len(modes)} modal candidates, expected {candidate_count}"
        )
    return sorted(modes[:candidate_count], key=lambda item: item[0])


def positive_semidefinite_fit(
    frequency: np.ndarray,
    treble: np.ndarray,
    bass: np.ndarray,
    cross: np.ndarray,
    corner: float,
    modes: list[tuple[float, float, float]],
) -> tuple[list[tuple[float, float, float, float, float]], int, float, list[float]]:
    """Fit one residue matrix per mode to the six strings' own mobilities.

    The target for a string at lever arm u is the two-point reduction of the
    measured matrix below the corner and the mean of the two ends above it.
    The model is a common pole set with a residue matrix per mode, held
    positive semidefinite, which is what makes every string's mobility a
    positive real sum.  The constrained least squares is convex, so it is
    solved by accelerated projected gradient with the exact 2x2 projection.
    """
    fit_indices = np.flatnonzero(
        (frequency >= MINIMUM_FREQUENCY) & (frequency <= MAXIMUM_FREQUENCY)
    )[::3]
    fit_frequency = frequency[fit_indices]
    s_plane = 2j * SAMPLE_RATE * np.tan(np.pi * fit_frequency / SAMPLE_RATE)
    basis = np.array([
        s_plane / (s_plane * s_plane + 2.0 * damping * s_plane + omega * omega)
        for omega, damping in (
            (
                2.0 * SAMPLE_RATE * np.tan(np.pi * mode[0] / SAMPLE_RATE),
                SAMPLE_RATE * np.tan(np.pi * mode[0] / SAMPLE_RATE) / mode[1],
            )
            for mode in modes
        )
    ])
    arms = np.array(STRING_LEVER_ARMS)
    # Y(u) = [1, 2u, u^2] . (R_hh, R_hr, R_rr)
    shape = np.stack((np.ones_like(arms), 2.0 * arms, arms * arms), axis=1)
    below = fit_frequency < corner

    def targets(advance: float) -> np.ndarray:
        rotation = np.exp(2j * np.pi * fit_frequency * advance / SAMPLE_RATE)
        tt, bb, tb = treble[fit_indices] * rotation, bass[fit_indices] * rotation, cross[fit_indices] * rotation
        mean = 0.5 * (tt + bb)
        rows = []
        for arm in arms:
            two_point = (
                (1.0 + arm) ** 2 * tt
                + (1.0 - arm) ** 2 * bb
                + 2.0 * (1.0 - arm * arm) * tb
            ) / 4.0
            rows.append(np.where(below, two_point, mean))
        return np.array(rows)

    rocking = np.array([mode[0] < corner for mode in modes])
    mask = np.ones((len(modes), 3))
    mask[~rocking, 1:] = 0.0
    gram = np.kron(np.real(np.conj(basis) @ basis.T), shape.T @ shape)
    step = 1.0 / float(np.linalg.eigvalsh(gram).max())

    def solve(target: np.ndarray, iterations: int) -> np.ndarray:
        linear = (np.real(np.conj(basis) @ target.T) @ shape).reshape(-1)
        residues = np.zeros((len(modes), 3))
        look = residues.copy()
        momentum = 1.0
        for _ in range(iterations):
            gradient = (gram @ look.reshape(-1) - linear).reshape(-1, 3)
            trial = (look - step * gradient) * mask
            matrix = np.zeros((len(modes), 2, 2))
            matrix[:, 0, 0] = trial[:, 0]
            matrix[:, 0, 1] = matrix[:, 1, 0] = trial[:, 1]
            matrix[:, 1, 1] = trial[:, 2]
            eigenvalues, vectors = np.linalg.eigh(matrix)
            eigenvalues = np.maximum(eigenvalues, 0.0)
            matrix = (vectors * eigenvalues[:, None, :]) @ np.swapaxes(
                vectors, 1, 2)
            trial[:, 0] = matrix[:, 0, 0]
            trial[:, 1] = matrix[:, 0, 1]
            trial[:, 2] = matrix[:, 1, 1]
            trial *= mask
            next_momentum = 0.5 * (1.0 + np.sqrt(1.0 + 4.0 * momentum ** 2))
            look = trial + ((momentum - 1.0) / next_momentum) * (trial - residues)
            residues, momentum = trial, next_momentum
        return residues

    best: tuple[float, int] | None = None
    # The accelerometer peak follows the differentiated hammer by two samples.
    # Select the integer alignment by the constrained complex residual rather
    # than embedding a non-passive measurement delay in the termination. A
    # short solve is enough to rank the alignments; the winner is then solved
    # out.
    for phase_advance in range(-4, 9):
        target = targets(phase_advance)
        residues = solve(target, 500)
        model = (shape @ residues.T) @ basis
        relative_error = float(
            np.linalg.norm(model - target) / np.linalg.norm(target))
        if best is None or relative_error < best[0]:
            best = (relative_error, phase_advance)
    assert best is not None

    phase_advance = best[1]
    target = targets(phase_advance)
    residues = solve(target, 20_000)
    model = (shape @ residues.T) @ basis
    relative_error = float(
        np.linalg.norm(model - target) / np.linalg.norm(target))
    keep = np.abs(residues).sum(axis=1) > np.max(residues[:, 0]) * 1.0e-8
    fitted_modes = [
        (mode[0], mode[1], float(row[0]), float(row[1]), float(row[2]))
        for mode, row, retained in zip(modes, residues, keep)
        if retained
    ]
    model = (shape @ residues.T) @ basis
    magnitude_errors = [
        float(np.median(np.abs(20.0 * np.log10(
            np.maximum(np.abs(model[index]), 1.0e-30)
            / np.maximum(np.abs(target[index]), 1.0e-30)))))
        for index in range(len(arms))
    ]
    if phase_advance != 2:
        raise ValueError(f"unexpected fitted instrumentation delay {phase_advance}")
    if (relative_error > MAX_COMPLEX_RELATIVE_ERROR
            or max(magnitude_errors) > MAX_MEDIAN_MAGNITUDE_ERROR_DB):
        raise ValueError(
            "passive bridge fit missed its regression limits: "
            f"complex={relative_error:.6f}, "
            f"magnitude={max(magnitude_errors):.6f} dB"
        )
    for _, _, heave, cross_residue, rock in fitted_modes:
        # Positive semidefinite is what keeps every string's Y(u) positive
        # real; the projection above enforces it and this is the assertion.
        if heave < 0.0 or rock < 0.0:
            raise ValueError("bridge fit contains a negative direct residue")
        if cross_residue * cross_residue > heave * rock + 1.0e-12:
            raise ValueError("bridge fit contains an indefinite residue matrix")
    # The archive's setup photographs show installed, undamped strings, so a
    # retained mode sitting on an open string could be that string rather than
    # the body. A guitar's low air and top modes have Q in the tens while an
    # open string's is near a thousand, and this estimator clips at
    # Q_MAXIMUM = 80, so a mode near an open string is cleared only if its Q is
    # resolved below that clip. (2026-08-30: g21's one such candidate, 82.764
    # Hz, reads Q 16.6 and is a body mode.)
    for mode_frequency, q, *_ in fitted_modes:
        for open_frequency in OPEN_STRING_HZ:
            cents = 1200.0 * np.log2(mode_frequency / open_frequency)
            if abs(cents) < 25.0 and q >= Q_MAXIMUM:
                raise ValueError(
                    f"retained mode {mode_frequency:.3f} Hz sits {cents:+.1f} "
                    f"cents from an open string with an unresolved Q {q:.1f}"
                )
    return fitted_modes, phase_advance, relative_error, magnitude_errors


def fit_bank(path: Path, guitar: int) -> dict:
    frequency, treble, bass, cross = extract_two_point(path, guitar)
    corner, _ = coherence_corner(path, guitar)
    # One pole set serves all three transfers, so the candidates are taken
    # from the trace of the measured matrix, which is the same under the
    # change of coordinates and so shows both heaving and rocking modes.
    modes = candidate_modes(frequency, treble + bass)
    fitted, phase_advance, relative_error, magnitude_errors = (
        positive_semidefinite_fit(
            frequency, treble, bass, cross, corner, modes))
    return {
        "guitar": guitar,
        "modes": fitted,
        "corner": corner,
        "phase_advance": phase_advance,
        "relative_error": relative_error,
        "magnitude_error": max(magnitude_errors),
        "magnitude_errors": magnitude_errors,
        "rocking": sum(1 for mode in fitted if mode[4] > 0.0),
    }


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_RADIATION_HEADER = REPOSITORY / "Source" / "DSP" / "MeasuredBodyData.h"
MOBILITY_RATIO_BAND_HZ = (80.0, 4000.0)
MOBILITY_RATIO_POINTS = 1000
MOBILITY_RATIO_POSITION = -1.0


def header_rows(path: Path, name: str) -> np.ndarray:
    """The numeric rows of one constexpr std::array in a generated header."""
    text = path.read_text(encoding="utf-8")
    start = text.index(f" {name} {{{{")
    block = text[start:text.index("}};", start)]
    rows = [[float(value.strip().rstrip("f")) for value in match.split(",")]
            for match in re.findall(r"^\s*\{ ([^{}]*) \},$", block, flags=re.M)]
    if not rows:
        raise ValueError(f"{path}: no rows in {name}")
    return np.array(rows)


def modal_mobility(modes: np.ndarray, frequency: np.ndarray, position: float) -> np.ndarray:
    """Y(f) = sum (heave + 2u cross + u^2 rock) s/(s^2 + (w/q) s + w^2)."""
    s = 2j * np.pi * frequency[:, None]
    omega = 2.0 * np.pi * modes[:, 0]
    residue = modes[:, 2] + 2.0 * position * modes[:, 3] + position**2 * modes[:, 4]
    return (residue * s / (s * s + omega / modes[:, 1] * s + omega * omega)).sum(axis=1)


def top_mobility_ratio(reference: np.ndarray, steel: np.ndarray) -> tuple[float, dict]:
    def ratio(high: float, statistic) -> float:
        frequency = np.geomspace(MOBILITY_RATIO_BAND_HZ[0], high, MOBILITY_RATIO_POINTS)
        return float(np.exp(np.mean(
            np.log(statistic(modal_mobility(reference, frequency, MOBILITY_RATIO_POSITION)))
            - np.log(statistic(modal_mobility(steel, frequency, MOBILITY_RATIO_POSITION))))))
    spread = {f"{name} 80-{high:.0f} Hz": ratio(high, statistic)
              for name, statistic in (("|Y|", np.abs), ("conductance", np.real))
              for high in (1000.0, 2000.0, 4000.0, 10000.0)}
    return ratio(MOBILITY_RATIO_BAND_HZ[1], np.abs), spread


def radiation_twins(bridge: np.ndarray, radiation: np.ndarray, fitted_q: np.ndarray) -> list:
    frequencies = radiation[:, 0]
    twins = []
    for frequency, q in bridge[:, :2]:
        k = int(np.argmin(np.abs(frequencies - frequency)))
        below = frequencies[k] - frequencies[k - 1] if k > 0 else np.inf
        above = frequencies[k + 1] - frequencies[k] if k + 1 < len(frequencies) else np.inf
        spacing = 0.5 * (below + above) if np.isfinite(below) and np.isfinite(above) \
            else min(below, above)
        inside = abs(frequency - frequencies[k]) < frequencies[k] / (2.0 * fitted_q[k])
        resolved = frequency / q < spacing
        twins.append(k if inside and resolved else -1)
    return twins


def radiation_poles(bank: dict, radiation_header: Path, body_report: Path,
                    fylde_mat: Path) -> dict:
    """Steel's twins, unpaired-mode Q ratios and top mobility ratio."""
    radiation = header_rows(radiation_header, "measuredSteelBodyModes")[:, :2]
    report = json.loads(body_report.read_text(encoding="utf-8"))
    plate_q = report.get("plate_q")
    if not plate_q or "measured_frequency_q" not in plate_q:
        raise ValueError(f"{body_report}: no as-fitted Q; rerun GenerateBodyForcePair.py --plate-q")
    fitted = np.array(plate_q["measured_frequency_q"])
    corrected = np.array(plate_q["corrected_frequency_q"])
    if (fitted.shape != radiation.shape
            or not np.array_equal(fitted[:, 0].astype(np.float32), radiation[:, 0].astype(np.float32))
            or not np.array_equal(corrected[:, 1].astype(np.float32), radiation[:, 1].astype(np.float32))):
        raise ValueError(f"{body_report} is not the report of {radiation_header}")
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from GenerateBodyForcePair import plate_q_factor
    population = [entry["frequency_q"] for entry in plate_q["population"].values()]
    low, high = plate_q["band_hz"]
    modes = np.array(bank["modes"])
    twins = radiation_twins(modes, radiation, fitted[:, 1])
    t1_upper = plate_q.get("t1_upper_hz", 0.0)
    t1_weight = plate_q.get("t1_weight", 1.0)
    ratios = [1.0 if twin >= 0 or not low <= frequency < high
              else plate_q_factor(frequency, fitted, population)
                  ** (t1_weight if frequency < t1_upper else 1.0)
              for frequency, twin in zip(modes[:, 0], twins)]
    from FyldeBridgeReference import reference_modes
    reference = reference_modes(fylde_mat)
    ratio, spread = top_mobility_ratio(reference, modes)
    return dict(twins=twins, q_ratios=ratios, mobility_ratio=ratio, spread=spread)


def poles_block(poles: dict, steel_guitar: int) -> str:
    twins = poles["twins"]
    paired = sum(1 for twin in twins if twin >= 0)
    # The citation is wrapped on its own, as the committed header has it.
    comment = textwrap.fill(
        f'Steel\'s own bridge on its radiation\'s poles. {paired} of the'
        f' {len(twins)} g{steel_guitar} bridge modes are the same resonance as a'
        f' mode of measuredSteelBodyModes (MeasuredBodyData.h): the nearest'
        f' one, inside its as-fitted half-power band, with the bridge mode'
        f' narrower than the radiation bank\'s local spacing; each takes that'
        f' mode\'s engine pole. The others (-1) keep their own frequency and'
        f' take the plate-Q rule\'s octave factor on their Q'
        f' (GenerateBodyForcePair.py, median of the anechoic flamencas over'
        f' g{steel_guitar}\'s own, never raising, 1 outside its band).'
        f' steelTopMobilityRatio brings the flamenca\'s mobility to a'
        f' steel-string guitar\'s: the geometric mean over 80 Hz-4 kHz of'
        f' |Y| of the Fylde Falstaff',
        width=76, initial_indent="// ", subsequent_indent="// ") + "\n" + textwrap.fill(
        f'(Carcagno, Bucknall, Woodhouse, Fritz and Plack, JASA 144 (2018) 3533,'
        f' https://doi.org/10.1121/1.5084735, data https://osf.io/f4pqa/, CC BY'
        f' 4.0) over g{steel_guitar}\'s at u = -1, {poles["mobility_ratio"]:.5f},'
        f' to three figures; over other bands and conductance it spans'
        f' {min(poles["spread"].values()):.3f}-{max(poles["spread"].values()):.3f}.',
        width=76, initial_indent="// ", subsequent_indent="// ")
    rows = ", ".join(str(twin) for twin in twins)
    ratios = "\n".join(f"    {cpp_float(value)}," for value in poles["q_ratios"])
    return f'''{comment}
inline constexpr float steelTopMobilityRatio = {round(poles["mobility_ratio"], 3)}f;
inline constexpr std::array<std::int16_t, {len(twins)}> steelBridgeRadiationTwins {{{{
{textwrap.fill(rows, width=76, initial_indent="    ", subsequent_indent="    ")}
}}}};
inline constexpr std::array<float, {len(twins)}> steelBridgeUnpairedQRatio {{{{
{ratios}
}}}};'''


def cpp_float(value: float) -> str:
    text = format(float(np.float32(value)), ".9g")
    if "." not in text and "e" not in text:
        text += ".0"
    return text + "f"


def bank_block(name: str, bank: dict) -> str:
    rows = [
        "    { " + ", ".join(cpp_float(value) for value in mode) + " },"
        for mode in bank["modes"]
    ]
    guitar = bank["guitar"]
    comment = textwrap.fill(
        f'g{guitar}, {GUITAR_DESCRIPTION[guitar]}. A positive-semidefinite'
        f' least-squares projection retained {len(bank["modes"])} of'
        f' {CANDIDATE_COUNT} measured modal candidates after a'
        f' {bank["phase_advance"]}-sample instrumentation alignment;'
        f' {bank["rocking"]} of them carry a rocking residue, the rest sitting'
        f' above the {bank["corner"]:.0f} Hz heuristic corner from the two'
        f' cross-side records\' disagreement. Over the six model string'
        f' positions and 60--10000 Hz,'
        f' relative complex error {bank["relative_error"]:.6f} and worst median'
        f' magnitude error {bank["magnitude_error"]:.6f} dB.',
        width=76, initial_indent="// ", subsequent_indent="// ")
    return f'''{comment}
inline constexpr std::array<MeasuredBridgeMode, {len(bank["modes"])}> {name} {{{{
{chr(10).join(rows)}
}}}};'''


def render_header(steel: dict, poles: dict) -> str:
    return f'''// Generated by Tools/GenerateMeasuredBridge.py; do not hand-edit.
// Passive spatial approximations fitted to each guitar's bass and treble
// impacts and two accelerometers behind the saddle (Method.pdf Fig. 3).
// Sensor spacing is unreported; treating the responses as collocated ports
// is a model assumption. The cross-record corner is heuristic, not a unique
// measurement of lost rigidity. Acceleration/
// force is converted to velocity/force by differentiating the hammer,
// following the archive script. The model uses heaving and normalized
// rocking displacement r=a*theta; distance along the saddle is in
// units of the impact half-separation a, so impacts are at u = -1 (bass) and
// u = +1 (treble) and a string at u sees
//     Y(u) = sum over modes of (heave + 2 u cross + u^2 rock)
//                              * s/(s^2 + 2 damping s + omega^2),
// which fits the treble-side response at u = +1 and bass-side at u = -1.
// Each mode's residue matrix [[heave, cross], [cross, rock]] is positive
// semidefinite, so the model's Y(u) is positive real. This constraint does
// not establish passivity or collocation of the measured response matrix.
// The Original model plays g{steel['guitar']}, a measured flamenco guitar adapted for steel
// strings.
// Adapted from Robert Mores, "Archive for the acoustical documentation of
// classical Spanish guitars, flamenco guitars and romantic guitars from
// private and public collections -- bridge mobility" (2021),
// https://doi.org/10.5281/zenodo.4604577, licensed CC BY 4.0; see
// THIRD_PARTY_NOTICES.md.

#pragma once

#include <array>
#include <cstdint>

namespace acustra::detail
{{
struct MeasuredBridgeMode
{{
    float frequency;
    float q;
    float heave;
    float cross;
    float rock;
}};

{bank_block("measuredSteelBridgeModes", steel)}

{poles_block(poles, steel["guitar"])}
}} // namespace acustra::detail
'''


def check_output(path: Path, expected: str) -> bool:
    try:
        actual = path.read_text(encoding="utf-8")
    except FileNotFoundError:
        print(f"error: generated header does not exist: {path}", file=sys.stderr)
        return False
    if actual == expected:
        print(f"verified generated header: {path}")
        return True
    sys.stderr.writelines(
        difflib.unified_diff(
            actual.splitlines(keepends=True),
            expected.splitlines(keepends=True),
            fromfile=str(path),
            tofile="generated",
        )
    )
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-mat", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument(
        "--steel-guitar", type=int, default=DEFAULT_STEEL_GUITAR,
        choices=(21, 39),
        help="archive record for the steel bank: g21, or g39, the anechoic "
             "flamenca whose radiation steel may play (default: %(default)s)",
    )
    parser.add_argument(
        "--body-report", required=True, type=Path,
        help="GenerateBodyForcePair.py --plate-q report.json of the radiation header")
    parser.add_argument(
        "--radiation-header", type=Path, default=DEFAULT_RADIATION_HEADER,
        help="MeasuredBodyData.h whose steel modes the bridge's twins index")
    parser.add_argument(
        "--fylde-mat", required=True, type=Path,
        help="bridge_admittance_all.mat (FyldeBridgeReference.py), the "
             "steel-string mobility level")
    parser.add_argument(
        "--check",
        action="store_true",
        help="compare generated content with --output without writing it",
    )
    arguments = parser.parse_args()

    steel = fit_bank(arguments.raw_mat, arguments.steel_guitar)
    poles = radiation_poles(steel, arguments.radiation_header,
                            arguments.body_report, arguments.fylde_mat)
    header = render_header(steel, poles)
    twins = poles["twins"]
    print(f"steel bridge on its radiation's poles: {sum(t >= 0 for t in twins)} of "
          f"{len(twins)} modes twinned at "
          + ", ".join(f"{mode[0]:.1f}" for mode, t in zip(steel["modes"], twins) if t >= 0)
          + " Hz")
    print(f"steel top mobility ratio {poles['mobility_ratio']:.5f} (80 Hz-4 kHz |Y|); spread "
          + ", ".join(f"{name} {value:.3f}" for name, value in poles["spread"].items()))
    if arguments.check:
        return 0 if check_output(arguments.output, header) else 1
    with arguments.output.open("w", encoding="utf-8", newline="\n") as stream:
        stream.write(header)
    print(
        f"generated steel bank g{steel['guitar']}: {len(steel['modes'])} "
        f"passive modes ({steel['rocking']} rocking below "
        f"{steel['corner']:.0f} Hz), complex error "
        f"{steel['relative_error']:.4f}, median magnitude error per string "
        + "/".join(f"{value:.3f}" for value in steel["magnitude_errors"])
        + " dB"
    )
    print(f"wrote {arguments.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
