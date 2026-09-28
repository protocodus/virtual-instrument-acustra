#!/usr/bin/env python3
"""Rebuild steel's g21 radiation above 1 kHz on a decay-Q pole grid.

Above about 800 Hz g21's modes overlap (modal overlap 1.1-1.4), so the
peak-width rule of GenerateBodyForcePair.py reads each cluster of modes as one
broad mode: Q 42-60 where the anechoic flamencas' band decay implies 61-145,
and 0.009-0.018 modes/Hz where a mean-value spruce top has 0.027-0.039. The
bank's magnitude is right but its top band dies in about 30 ms. Above the
overlap frequency the physically consistent parameters are the decay Q and the
measured-bandwidth density (Skudrzyk, JASA 67 (1980) 1105; Schroeder and
Kuttruff 1962; Elie et al., JASA 132 (2012) 4013), and a fixed-pole parallel
filter with least-squares residues reproduces a measured response on that
basis (B. Bank, "Direct design of parallel second-order filters for instrument
body modeling", ICMC 2007).

(a) Decay Q(f): per third octave from 250 Hz, each anechoic flamenca blanca
    (g37, g38, g39, g42, g43; g41 fails the generator's gates) gives the median,
    over the four heard paths (treble-bridge and upper-bout microphones x bass
    and treble impacts), of its Schroeder EDC T20 (-5 to -25 dB) with the
    noise floor (0.45-0.6 s) subtracted and the curve truncated where the band
    falls within 10 dB of it; Q = pi f T60 / ln(1000) of the median over the
    five guitars, interpolated in log frequency.
(b) Poles from 1000 Hz spaced one decay bandwidth f/Q(f) apart (modal
    overlap M = 1), each displaced by U(-0.25, 0.25) of that spacing (numpy
    default_rng(20260928)), Q = Q(f) there; float32. M = 1 and the jitter are
    basis choices, not measurements; the Q and the residues carry the physics.
    (Grids at M = 2 fail the anchor-detune gate.) The grid ends with the fit
    band at 10 kHz, the band GenerateMeasuredBody.py reads the archive over: a
    pole beyond it has no data to hold its residue, and poles run on to 10.5
    kHz took residues up to 0.82 (the bank's median is 0.0035) that cancel in
    the band and ring 23 dB over the measurement at 10-11 kHz, where the
    engine's steel anchor (frequencies x0.90) moves them into the heard band.
(c) The committed steel bank's modes below 1 kHz are kept byte for byte.
(d) Target: g21 with the music room's tail removed (RemoveRoomTail.py, the
    generator's --room-free path), 12000 samples with the final tenth tapered,
    minus the kept low modes' own transfer.
(e) Residues per path (3 microphones x 2 impacts): weighted complex least
    squares for the fixed poles, weight 1/(Gaussian-smoothed |target|, sigma
    128 bins), 800 Hz-10 kHz, every 5th bin, ridge 1e-7 trace/N; float32;
    the heave/moment pairs formed as GenerateBodyForcePair.py forms them.
(f) Gates, on the heard paths against the room-free target: median complex
    relative error (80 Hz-10 kHz) <= 0.15; third-octave level error (100 Hz-
    10 kHz) <= 1 dB rms and <= 2 dB max; residue cancellation (sum of the
    modes' energies over the energy of their sum) <= 3; the steel anchor's
    alternating 1.8%/sqrt(i+1) detune moves no third octave by more than 2 dB;
    a wood Q scale of 0.82 moves the mean third-octave level by <= 1 dB; the
    bank's level over 10-12 kHz, outside the fit, is not above the measured.

Blind Set 18 heard this grid as its C, in place of g21's fitted bank above
1 kHz. The steel blend (Source/DSP/SteelBodyBlend.h) plays it beside that
bank instead: the output is MeasuredBodyDecayGridData.h, the grid alone
(steelDecayGridBodyModes), with steelDecayGridFirstIndex the number of the
committed bank's modes below 1 kHz, so grid mode k takes the steel anchor's
alternating detune of bank index steelDecayGridFirstIndex + k as C played it.
The committed bank, MeasuredBodyData.h, is not rewritten; its modes below
1 kHz are read (their Q as the plate-Q rule and the blend's T1 weight left
them) and the grid's residues are fitted to the target less their transfer,
so the grid and g21's own top band reproduce the same measured response.

    python3 Tools/GenerateBodyDecayGrid.py --self-test
    python3 Tools/GenerateBodyDecayGrid.py --raw-mat /path/qualified_selected_impulses.mat \\
        --header Source/DSP/MeasuredBodyData.h --output /new/directory
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys

import numpy as np
from scipy.ndimage import gaussian_filter1d

import GenerateBodyForcePair as gen

spatial, body, RemoveRoomTail = gen.spatial, gen.body, gen.RemoveRoomTail
FS = body.SAMPLE_RATE
NFFT = body.FFT_SIZE
FREQUENCY = gen.FREQUENCY

DECAY_POPULATION = (37, 38, 39, 42, 43)
DECAY_LOWEST_BAND_HZ = 250.0
GRID_HZ = (1000.0, 10000.0)
GRID_OVERLAP = 1.0
GRID_JITTER = 0.25
GRID_SEED = 20260928
FIT_BAND_HZ = (800.0, 10000.0)
FIT_DECIMATION = 5
FIT_SMOOTHING_BINS = 128
FIT_RIDGE = 1e-7
TARGET_KEEP = 12000
# Heard paths: (microphone, impact) with microphone 0 upper-bout, 1 treble-bridge.
HEARD = ((1, 0), (1, 1), (0, 0), (0, 1))
GATES = dict(heard_complex_error=0.15, third_rms_db=1.0, third_max_db=2.0,
             cancellation=3.0, anchor_detune_db=2.0, wood_q_mean_db=1.0,
             above_band_excess_db=0.0)
ANCHOR_ASYMMETRY = 0.018   # AcustraEngine.cpp, the steel Original anchor
WOOD_Q_SCALE = 0.82        # the lowest woodSpecs Q scale (mahogany)


def taper_corrected(response: np.ndarray, upto_s: float = 0.6) -> np.ndarray:
    """Impulse response with the extraction's one-second half-cosine record taper
    (AuditBridgeSpatialMap.extract; the force lands near sample 100) undone
    over the first upto_s seconds, zero after."""
    ir = np.fft.irfft(response, NFFT)
    n = np.arange(len(ir))
    taper = 0.5 + 0.5 * np.cos(np.minimum(n + 100, 48000) * np.pi / 48000)
    limit = int(upto_s * FS)
    out = np.zeros_like(ir)
    out[:limit] = ir[:limit] / np.maximum(taper[:limit], 1e-3)
    return out


def band_t20(ir: np.ndarray) -> np.ndarray:
    """Schroeder T20 (s) per RemoveRoomTail third octave, noise subtracted and truncated."""
    frames = int(0.6 * FS) // 48
    out = np.full(len(RemoveRoomTail.CENTRES), np.nan)
    for band, signal in enumerate(RemoveRoomTail.band_signals(ir)):
        energy = (signal[:frames * 48] ** 2).reshape(-1, 48).mean(axis=1)
        floor = energy[450:].mean()
        smooth = np.convolve(energy, np.ones(10) / 10, mode="same")
        above = np.flatnonzero(smooth > 10 * floor)
        end = above[-1] + 1 if len(above) else 1
        energy = np.maximum(energy[:end] - floor, 0.0)
        edc = np.cumsum(energy[::-1])[::-1]
        if not edc[0] > 0:
            continue
        level = 10 * np.log10(np.maximum(edc / edc[0], 1e-30))
        t = np.arange(len(level)) * 1e-3
        fit = (level <= -5) & (level >= -25)
        if fit.sum() < 3 or not np.any(level <= -25):
            continue
        slope = np.polyfit(t[fit], level[fit], 1)[0]
        if slope < 0:
            out[band] = -60.0 / slope
    return out


def decay_t20(responses: dict) -> dict:
    per_guitar = {}
    for guitar in DECAY_POPULATION:
        rows = [band_t20(taper_corrected(responses[guitar, RemoveRoomTail.IMPACTS[impact],
                                                   RemoveRoomTail.MICROPHONES[mic]]))
                for mic, impact in HEARD]
        per_guitar[guitar] = np.nanmedian(rows, axis=0)
    return per_guitar


def decay_q_function(t20: np.ndarray):
    centres = RemoveRoomTail.CENTRES
    usable = np.isfinite(t20) & (centres >= DECAY_LOWEST_BAND_HZ)

    def q_of(frequency):
        f = np.atleast_1d(np.asarray(frequency, dtype=float))
        return np.pi * f * np.interp(np.log(f), np.log(centres[usable]), t20[usable]) / np.log(1e3)
    return q_of


def decay_grid(q_of) -> np.ndarray:
    rng = np.random.default_rng(GRID_SEED)
    frequency, poles = GRID_HZ[0], []
    while frequency < GRID_HZ[1]:
        spacing = GRID_OVERLAP * frequency / q_of(frequency)[0]
        poles.append(frequency + rng.uniform(-GRID_JITTER, GRID_JITTER) * spacing)
        frequency += spacing
    poles = np.array(poles)
    return np.column_stack([poles, q_of(poles)]).astype(np.float32).astype(float)


def transfer(frequency_q: np.ndarray, residues: np.ndarray) -> np.ndarray:
    """Modal transfer on the FFT grid; residues (..., modes)."""
    z = np.exp(-2j * np.pi * FREQUENCY / FS)
    out = np.zeros(residues.shape[:-1] + (len(z),), dtype=complex)
    for index, (f, q) in enumerate(frequency_q):
        pole = np.exp(-np.pi * f / (q * FS) + 2j * np.pi * f / FS)
        r = residues[..., index, None]
        out += r / (1 - pole * z) + np.conj(r) / (1 - np.conj(pole) * z)
    return out


def least_squares_residues(target: np.ndarray, frequency_q: np.ndarray) -> np.ndarray:
    """(e): complex residues for fixed poles, one path at a time."""
    bins = np.flatnonzero((FREQUENCY >= FIT_BAND_HZ[0]) & (FREQUENCY <= FIT_BAND_HZ[1]))[::FIT_DECIMATION]
    zi = np.exp(-2j * np.pi * FREQUENCY[bins] / FS)
    columns = []
    for f, q in frequency_q:
        pole = np.exp(-np.pi * f / (q * FS) + 2j * np.pi * f / FS)
        a, b = 1 / (1 - pole * zi), 1 / (1 - np.conj(pole) * zi)
        columns += [a + b, 1j * (a - b)]      # d/d(Re R), d/d(Im R)
    basis = np.column_stack(columns)
    out = np.zeros(target.shape[:-1] + (len(frequency_q),), dtype=complex)
    for mic in range(target.shape[0]):
        for impact in range(target.shape[1]):
            envelope = gaussian_filter1d(np.abs(target[mic, impact]), FIT_SMOOTHING_BINS)[bins]
            weight = 1 / np.maximum(envelope, envelope.max() * 1e-3)
            a = basis * weight[:, None]
            y = target[mic, impact][bins] * weight
            m = np.vstack([a.real, a.imag])
            v = np.concatenate([y.real, y.imag])
            normal = m.T @ m
            solution = np.linalg.solve(normal + FIT_RIDGE * np.trace(normal) / len(normal)
                                       * np.eye(len(normal)), m.T @ v)
            out[mic, impact] = gen.rounded(solution[0::2] + 1j * solution[1::2])
    return out


def third_levels(response: np.ndarray) -> np.ndarray:
    w = RemoveRoomTail.WEIGHTS
    return 10 * np.log10(np.maximum((w * np.abs(response)[None, :] ** 2).sum(axis=1) / w.sum(axis=1), 1e-30))


THIRDS = (RemoveRoomTail.CENTRES >= 100) & (RemoveRoomTail.CENTRES <= 10000)


def heard_levels(t: np.ndarray) -> np.ndarray:
    return np.array([third_levels(t[m, i])[THIRDS] for m, i in HEARD])


def gates(target: np.ndarray, frequency_q: np.ndarray, residues: np.ndarray) -> dict:
    model = transfer(frequency_q, residues)
    band = (FREQUENCY >= 80) & (FREQUENCY <= 10000)
    complex_error = float(np.median([np.linalg.norm((model[m, i] - target[m, i])[band])
                                     / np.linalg.norm(target[m, i][band]) for m, i in HEARD]))
    error = heard_levels(model) - heard_levels(target)
    third_rms = float(np.median(np.sqrt(np.mean(error ** 2, axis=1))))
    third_max = float(np.median(np.max(np.abs(error), axis=1)))
    parts = np.zeros(len(HEARD))
    for k in range(len(frequency_q)):
        single = transfer(frequency_q[k:k + 1], residues[:, :, k:k + 1])
        parts += [np.sum(np.abs(single[m, i]) ** 2) for m, i in HEARD]
    cancellation = float(np.median(parts / np.array([np.sum(np.abs(model[m, i]) ** 2) for m, i in HEARD])))
    index = np.arange(len(frequency_q))
    detuned = frequency_q.copy()
    detuned[:, 0] *= 1 + np.where(index % 2 == 0, 1.0, -1.0) * ANCHOR_ASYMMETRY / np.sqrt(index + 1)
    base = heard_levels(model)
    detune = float(np.max(np.abs(heard_levels(transfer(detuned, residues)) - base)))
    wood = frequency_q.copy()
    wood[:, 1] *= WOOD_Q_SCALE
    wood_mean = float(np.mean(heard_levels(transfer(wood, residues)) - base))
    above = (FREQUENCY >= 10000) & (FREQUENCY < 12000)
    excess = float(max(10 * np.log10(np.mean(np.abs(model[m, i][above]) ** 2)
                                      / np.mean(np.abs(target[m, i][above]) ** 2)) for m, i in HEARD))
    values = dict(heard_complex_error=complex_error, third_rms_db=third_rms, third_max_db=third_max,
                  cancellation=cancellation, anchor_detune_db=detune, wood_q_mean_db=wood_mean,
                  above_band_excess_db=excess)
    values["passed"] = bool(all(abs(values[k]) <= GATES[k] for k in GATES if k != "above_band_excess_db")
                            and excess <= GATES["above_band_excess_db"])
    return values


# ---------------------------------------------------------------- header
ROW = re.compile(r"^    \{ .* \},$")


def steel_block(text: str) -> tuple[int, int]:
    start = text.index("measuredSteelBodyModes {{")
    return text.rindex("\n", 0, start) + 1, text.index("}};", start) + 3


def parse_rows(lines: list[str]) -> tuple[np.ndarray, np.ndarray]:
    """frequency/Q and endpoint residues (microphone upper,treble,bass x impact bass,treble x mode)."""
    rows = np.array([[float(v.strip().rstrip("f")) for v in line.strip()[2:-2].split(",")] for line in lines])
    c = lambda col: rows[:, col] + 1j * rows[:, col + 1]
    heave = {"treble": c(2), "bass": c(4), "upper": c(6)}
    moment = {"treble": c(8), "bass": c(10), "upper": c(12)}
    residues = np.zeros((3, 2, len(rows)), dtype=complex)
    for m, name in enumerate(gen.MICROPHONES):
        residues[m, 0] = heave[name] - moment[name]
        residues[m, 1] = heave[name] + moment[name]
    return rows[:, :2], residues


def grid_rows(frequency_q: np.ndarray, residues: np.ndarray) -> list[str]:
    pairs = gen.paired(residues)
    lines = []
    for index, (frequency, q) in enumerate(frequency_q):
        values = [frequency, q]
        for axis in (0, 1):
            for mic in (1, 2, 0):
                r = pairs[mic, axis, index]
                values.extend((r.real, r.imag))
        lines.append("    { " + ", ".join(body.cpp_float(v) for v in values) + " },")
    return lines


OUTPUT_HEADER = "MeasuredBodyDecayGridData.h"


def render(low_count: int, grid_frequency_q, grid_residues, header_sha256: str) -> str:
    """The grid alone, beside the committed bank it was fitted with."""
    lines = [
        "// Generated by Tools/GenerateBodyDecayGrid.py; do not hand-edit.",
        "// Robert Mores, https://zenodo.org/records/4604577, CC BY 4.0:",
        "// https://creativecommons.org/licenses/by/4.0/ ; see THIRD_PARTY_NOTICES.md.",
        "// Steel's g21 radiation from 1 to 10 kHz on a decay-Q pole grid: one pole",
        "// per decay bandwidth f/Q(f) of the anechoic flamencas g37-g43 (their",
        f"// median third-octave T20), jittered +-{GRID_JITTER:g} of the spacing (seed {GRID_SEED}),",
        "// residues least-squares fitted to g21 with the music room's tail removed,",
        "// less the transfer of MeasuredBodyData.h's steel modes below 1 kHz",
        f"// (that header's sha256 {header_sha256[:16]}...). Blind Set 18's C; the",
        "// steel blend plays it beside g21's own top band (SteelBodyBlend.h).",
        "", "#pragma once", '#include "MeasuredBodyData.h"', "", "#include <array>", "",
        "namespace acustra::detail", "{",
        "// The committed steel bank's modes below 1 kHz: grid mode k is played as",
        "// that bank's index steelDecayGridFirstIndex + k (the anchor's alternating",
        "// detune), and the bank's modes from this index up are its top band.",
        f"inline constexpr int steelDecayGridFirstIndex = {low_count};",
        "",
        f"inline constexpr std::array<MeasuredBodyMode, {len(grid_frequency_q)}> steelDecayGridBodyModes {{{{",
        *grid_rows(grid_frequency_q, grid_residues), "}};", "",
        "} // namespace acustra::detail", ""]
    return "\n".join(lines)


def self_test() -> None:
    # A fixed-pole least-squares fit recovers residues of a response built on those poles.
    fq = np.array([[1500.0, 80.0], [1530.0, 80.0], [4000.0, 120.0]])
    truth = np.array([[[1e-3 + 2e-3j, -3e-3 + 1e-3j, 5e-4 - 2e-4j]] * 2] * 3)
    fitted = least_squares_residues(transfer(fq, truth), fq)
    if np.max(np.abs(fitted - truth)) > 1e-6:
        raise AssertionError("fixed-pole least squares does not recover exact residues")
    # A single mode's T20 read from its band equals its decay: T60 = Q ln(1000)/(pi f).
    f, q = 1000.0, 60.0
    ir = np.zeros(NFFT)
    n = np.arange(int(0.6 * FS))
    ir[:len(n)] = np.exp(-np.pi * f / q * n / FS) * np.sin(2 * np.pi * f * n / FS)
    ir[:len(n)] += 1e-9 * np.random.default_rng(0).standard_normal(len(n))
    t20 = band_t20(ir)[np.argmin(np.abs(RemoveRoomTail.CENTRES - f))]
    expected = q * np.log(1e3) / (np.pi * f)
    if abs(t20 / expected - 1) > 0.05:
        raise AssertionError(f"band T20 {t20:.4f} s for a {expected:.4f} s mode")
    # The header rows round-trip through the generator's pair basis.
    lines = grid_rows(fq, truth)
    back_fq, back_res = parse_rows(lines)
    if not np.allclose(back_res, truth, rtol=1e-6, atol=1e-12) or not np.allclose(back_fq, fq):
        raise AssertionError("header rows do not round-trip")
    print("Body decay-grid generator self-test passed")


def run(raw: Path, header: Path, output: Path) -> None:
    if output.exists() or not output.parent.is_dir():
        raise ValueError("output must be a new directory inside an existing parent")
    committed = header.read_text()
    begin, end = steel_block(committed)
    rows = [line for line in committed[begin:end].splitlines() if ROW.match(line)]
    fq_all, res_all = parse_rows(rows)
    low = fq_all[:, 0] < GRID_HZ[0]
    fq_low, res_low = fq_all[low], res_all[:, :, low]

    values = spatial.bridge.load_matrix(raw)
    responses, _ = spatial.extract(values, (21,) + DECAY_POPULATION)
    per_guitar = decay_t20(responses)
    t20 = np.nanmedian(np.array(list(per_guitar.values())), axis=0)
    q_of = decay_q_function(t20)
    grid = decay_grid(q_of)

    room = RemoveRoomTail.fit_room(values, held_out=21)
    target = np.array([[spatial.windowed(RemoveRoomTail.remove(responses[21, impact, channel], room), TARGET_KEEP)
                        for impact in RemoveRoomTail.IMPACTS] for channel in RemoveRoomTail.MICROPHONES])
    residues = least_squares_residues(target - transfer(fq_low, res_low), grid)
    fq_bank = np.vstack([fq_low, grid])
    res_bank = np.concatenate([res_low, residues], axis=2)
    checks = gates(target, fq_bank, res_bank)
    print(f"decay Q at 1/2.5/4/6.3 kHz: " + "/".join(f"{q_of(f)[0]:.0f}" for f in (1000, 2520, 4000, 6350)), flush=True)
    print(f"{len(fq_low)} kept + {len(grid)} grid poles; gates " + json.dumps(checks), flush=True)
    if not checks["passed"]:
        raise ValueError(f"gates failed: {checks}")
    if not np.all(fq_all[len(fq_low):, 0] >= GRID_HZ[0]):
        raise ValueError("the committed bank's modes below 1 kHz are not its first ones")
    text = render(len(fq_low), grid, residues, spatial.sha256(header))
    output.mkdir()
    (output / OUTPUT_HEADER).write_text(text)
    np.savez(output / "body-decay-grid.npz", frequency=FREQUENCY, target_endpoint=target,
             frequency_q=fq_bank, endpoint_residues=res_bank, grid_frequency_q=grid)
    report = dict(protocol=__doc__, source=str(raw.resolve()), source_url="https://zenodo.org/records/4604577",
                  source_license="CC BY 4.0", raw_md5=spatial.bridge.digest(raw),
                  tool_sha256=spatial.sha256(Path(__file__)), committed_header_sha256=spatial.sha256(header),
                  versions=dict(python=sys.version.split()[0], numpy=np.__version__),
                  band_centres_hz=RemoveRoomTail.CENTRES.tolist(),
                  t20_s={f"g{g}": finite(v) for g, v in per_guitar.items()}, t20_median_s=finite(t20),
                  grid=dict(band_hz=list(GRID_HZ), overlap=GRID_OVERLAP, jitter=GRID_JITTER, seed=GRID_SEED,
                            count=len(grid), frequency_q=grid.tolist()),
                  kept_low_modes=int(low.sum()), fit=dict(band_hz=list(FIT_BAND_HZ), decimation=FIT_DECIMATION,
                  smoothing_bins=FIT_SMOOTHING_BINS, ridge=FIT_RIDGE, target_keep=TARGET_KEEP),
                  gate_limits=GATES, gates=checks,
                  native_header_sha256=spatial.sha256(output / OUTPUT_HEADER))
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False, default=float) + "\n")


def finite(values) -> list:
    return [None if not np.isfinite(v) else float(v) for v in values]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--raw-mat", type=Path)
    parser.add_argument("--header", type=Path, default=Path(__file__).resolve().parents[1] / "Source/DSP/MeasuredBodyData.h",
                        help="committed MeasuredBodyData.h whose steel modes below 1 kHz are kept")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        if args.self_test:
            self_test()
        else:
            if args.raw_mat is None or args.output is None:
                parser.error("--raw-mat and --output are required")
            run(args.raw_mat, args.header, args.output)
        return 0
    except (OSError, ValueError, AssertionError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
