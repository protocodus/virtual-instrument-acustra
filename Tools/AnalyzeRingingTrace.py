#!/usr/bin/env python3
"""Attribute the 165 Hz discrepancy without removing reciprocal string ports.

Read TraceRinging's fixed-body, public Standard/DropD and passive added-loss captures.
Report source-domain damped-component fits after a broad 250 Hz low-pass,
window sensitivity, per-string quarter-point motion and individual low body
mode radiation. A body mode's radiated contribution is driven by the entire
junction: it does not identify the string that supplied that force. Retuning
or damping an idle string changes the coupled load, so difference audio alone
is not a causal decomposition. Bridge supplied-work checks and loss gains
bounded by one are diagnostic guards, not a complete string/body energy proof.

Reference audio remains observational: nearby body modes, string partials,
hand damping and room sound can overlap. No DSP recommendation is applied.
The 149--183 Hz component fit is specifically a 165-region fit; it does not
track DropD's shifted octave at 146.83 Hz. DropD is measured with a separate
per-string local spectral search and projection; it overlaps the D3 string.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from statistics import median
from typing import Any

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import butter, resample_poly, sosfiltfilt, zoom_fft

RATE = 2000
LOG1000 = math.log(1000.)
WINDOWS = [(.04, .40), (.12, .55), (.25, .75)]
OPEN = np.array([82.406889, 110., 146.832384, 195.997718, 246.941651])


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(x: np.ndarray, rate: int) -> np.ndarray:
    x = np.asarray(x, dtype=float)
    if x.ndim == 1:
        x = x[:, None]
    if (x.ndim != 2 or not x.shape[1] or len(x) < rate * .8
            or rate < 1000 or not np.all(np.isfinite(x))):
        raise ValueError('finite audio with at least 0.8 seconds and rate >=1000 is required')
    return x


def low_band(x: np.ndarray, rate: int) -> np.ndarray:
    divisor = math.gcd(rate, RATE)
    x = resample_poly(x, RATE // divisor, rate // divisor, axis=0)
    return sosfiltfilt(butter(8, 250., fs=RATE, output='sos'), x, axis=0)


def db(value: float, reference: float) -> float:
    return 20. * math.log10(max(value, 1e-20) / max(reference, 1e-20))


def rms(x: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.asarray(x) ** 2)))


def basis(time: np.ndarray, frequency: float, t60: float) -> np.ndarray:
    decay = np.exp(-LOG1000 * time / t60)
    phase = 2. * np.pi * frequency * time
    return np.column_stack((decay * np.cos(phase), decay * np.sin(phase)))


def component_fit(y: np.ndarray, onset: float, begin: float, end: float,
                  frequencies: np.ndarray, air_center: float) -> dict[str, Any]:
    time = np.arange(len(y)) / RATE - onset
    take = (time >= begin) & (time < end)
    t, target = time[take], y[take]
    nuisance = np.column_stack([np.ones(len(t)), *[
        basis(t, f, 4.) for f in frequencies], basis(t, air_center, .55)])
    # Eliminate the fixed nuisance columns once. Variable projection then
    # solves only two columns per nonlinear step instead of repeatedly
    # factoring the entire matrix (the same least-squares solution).
    q, _ = np.linalg.qr(nuisance, mode='reduced')
    residual_target = target - q @ (q.T @ target)
    null_error = float(np.sum(residual_target ** 2))
    total = float(np.sum((target - np.mean(target, axis=0)) ** 2))

    def solve(parameters: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        matrix = basis(t, float(parameters[0]), float(parameters[1]))
        matrix -= q @ (q.T @ matrix)
        coefficients = np.linalg.lstsq(matrix, residual_target, rcond=None)[0]
        return matrix, coefficients, matrix @ coefficients - residual_target

    if null_error < 1e-25 or null_error < .03 * total:
        return {'present': False, 'reason': 'no identifiable component above nuisance/silence'}
    scale = math.sqrt(null_error / target.size)
    fits = [least_squares(lambda p: solve(p)[2].ravel() / scale, [f, t60],
                          bounds=([149., .08], [183., 6.]),
                          max_nfev=80, ftol=1e-8, xtol=1e-8, gtol=1e-8)
            for f, t60 in [(155., .5), (165., .5), (175., .5), (165., 1.5)]]
    fit = min(fits, key=lambda item: float(np.sum(item.fun ** 2)))
    _, coefficients, residual = solve(fit.x)
    error = float(np.sum(residual ** 2))
    increment = max(null_error - error, 0.) / max(total, 1e-30)
    amplitude = float(np.sqrt(np.mean(np.sum(coefficients[:2] ** 2, axis=0))))
    frequency, t60 = map(float, fit.x)
    return {'present': increment > .03 and total > 1e-24,
            'frequency_hz': frequency, 'apparent_t60_seconds': t60,
            'component_incremental_lf_variance': increment,
            'lf_variance_explained': 1. - error / max(total, 1e-30),
            'amplitude_at_220ms': amplitude * math.exp(-LOG1000 * .22 / t60),
            'parameter_boundary': frequency < 149.2 or frequency > 182.8
                                  or t60 < .081 or t60 > 5.99,
            'begin': begin, 'end': end}


def spectral_peak(x: np.ndarray, rate: int, onset: float,
                  center: float = 165., half_width: float = 22.,
                  begin: float = .04, end: float = .40) -> dict[str, float]:
    time = np.arange(len(x)) / rate - onset
    take = x[(time >= begin) & (time < end)]
    if not len(take):
        raise ValueError('empty spectral window')
    window = np.hanning(len(take))[:, None]
    spectrum = np.abs(zoom_fft(take * window, [center - half_width, center + half_width],
                                2048, fs=rate, axis=0)) / max(float(window.sum()), 1e-20)
    amplitude = 2. * np.sqrt(np.mean(spectrum ** 2, axis=1))
    frequency = np.linspace(center - half_width, center + half_width, len(amplitude), endpoint=False)
    index = int(np.argmax(amplitude))
    far = np.abs(frequency - frequency[index]) > min(7., .7 * half_width)
    return {'frequency_hz': float(frequency[index]), 'amplitude': float(amplitude[index]),
            'peak_over_local_median_db': db(float(amplitude[index]),
                                           float(np.median(amplitude[far]))),
            'window_resolution_hz': rate / len(take)}


def projection(x: np.ndarray, rate: int, onset: float, frequency: float,
               begin: float = .04, end: float = .40) -> complex:
    time = np.arange(len(x)) / rate - onset
    take = (time >= begin) & (time < end)
    t = time[take]
    matrix = np.column_stack((np.cos(2. * np.pi * frequency * t),
                              np.sin(2. * np.pi * frequency * t), np.ones(len(t))))
    coefficients = np.linalg.lstsq(matrix, x[take], rcond=None)[0]
    return complex(coefficients[0], -coefficients[1])


def analyse_audio(x: np.ndarray, rate: int, onset: float, frequencies: np.ndarray,
                  air_center: float) -> dict[str, Any]:
    x = validate(x, rate)
    y = low_band(x, rate)
    time = np.arange(len(x)) / rate - onset
    normalization = rms(x[(time >= 0.) & (time < .1)])
    peak = spectral_peak(y, RATE, onset)
    fits = [component_fit(y, onset, a, b, frequencies, air_center) for a, b in WINDOWS]
    for fit in fits:
        if 'amplitude_at_220ms' in fit:
            fit['level_at_220ms_rel_early_rms_db'] = db(fit['amplitude_at_220ms'], normalization)
    present = [fit for fit in fits if fit['present'] and not fit.get('parameter_boundary')]
    return {'early_broadband_rms': normalization, 'spectral_peak': peak,
            'spectral_level_rel_early_rms_db': db(peak['amplitude'], normalization),
            'fits': fits, 'component_detected_in_windows': len(present),
            'fit_frequency_range_hz': ([min(f['frequency_hz'] for f in present),
                                       max(f['frequency_hz'] for f in present)] if present else None),
            'fit_t60_range_seconds': ([min(f['apparent_t60_seconds'] for f in present),
                                      max(f['apparent_t60_seconds'] for f in present)] if present else None)}


def analyse_capture(row: dict[str, Any], base: Path) -> dict[str, Any]:
    rate = row['sample_rate']
    audio_path, trace_path = base / row['audio'], base / row['trace']
    x = np.fromfile(audio_path, dtype='<f4').reshape(-1, 2)
    trace = np.fromfile(trace_path, dtype='<f4').reshape(-1, len(row['columns']))
    if len(x) != row['frames'] or len(trace) != row['frames']:
        raise ValueError('trace/audio frame count mismatch')
    if not row['observer_audio_bit_identical'] or row['reciprocal_ports'] != 6:
        raise ValueError('invalid observation or reciprocal-port guard')
    trace = validate(trace, rate)
    frequencies = OPEN.copy()
    frequencies[0] *= 2. ** (row['low_e_retune'] / 12.)
    audio = analyse_audio(x, rate, 7. / rate, frequencies, 99.)
    # Trace precedes the output pipeline. A real displacement at one quarter
    # of low E has a second-partial antinode; its midpoint has a node.
    y = low_band(trace, rate)
    columns = row['columns']
    string_index = columns.index('string_0_normal_quarter')
    string_peak = spectral_peak(y[:, string_index:string_index + 1], RATE, 0.,
                                2. * frequencies[0], 4.)
    # Attribute at the observed idle-string octave, even when a stronger D3
    # tone or a fast body transient is the largest microphone-band peak.
    # This avoids calling the maximum of a broad 147--183 Hz search "165".
    peak = string_peak['frequency_hz'] if row['condition'] != 'low-e-damped' else 2. * frequencies[0]
    audio_band = low_band(x, rate)
    octave_projections = []
    for begin, end in WINDOWS:
        coefficients = [projection(audio_band[:, channel], RATE, 7. / rate,
                                    peak, begin, end) for channel in range(2)]
        amplitude = math.sqrt(sum(abs(c) ** 2 for c in coefficients) / 2.)
        octave_projections.append({'begin': begin, 'end': end, 'frequency_hz': peak,
                                   'amplitude': amplitude,
                                   'level_rel_early_rms_db': db(amplitude, audio['early_broadband_rms'])})
    audio['tracked_low_e_octave_projections'] = octave_projections
    observations = {}
    for i, column in enumerate(columns):
        if column.endswith('_power') or column == 'junction_impedance':
            continue
        signal = y[:, i:i + 1]
        band_center = (2. * frequencies[0] if column.startswith('string_0_') else peak)
        measured = spectral_peak(signal, RATE, 0., band_center, 7.)
        coefficient = projection(y[:, i], RATE, 0., peak)
        observations[column] = {'peak': measured,
                                'projection_at_attribution_frequency': [coefficient.real, coefficient.imag]}
    body = [(name, np.hypot(*item['projection_at_attribution_frequency']))
            for name, item in observations.items() if name.startswith('body_')]
    total = sum(complex(*observations[name]['projection_at_attribution_frequency']) for name, _ in body)
    body.sort(key=lambda pair: pair[1], reverse=True)
    for name, amplitude in body:
        observations[name]['magnitude_rel_body_vector_sum_db'] = db(float(amplitude), abs(total))
    return {'key': row['key'], 'picking': row['picking'], 'midi': row['midi'],
            'condition': row['condition'], 'low_e_octave_nominal_hz': 2. * frequencies[0],
            'attribution_frequency_hz': peak,
            'audio': audio, 'observations': observations,
            'largest_body_projection': body[:5], 'body_modes': row['body_modes'],
            'states': row['states'], 'cumulative_work': row['cumulative_work'],
            'minimum_work': row['minimum_work'], 'extra_roundtrip_gain': row['extra_roundtrip_gain'],
            'observer_audio_bit_identical': True, 'reciprocal_ports': 6,
            'audio_sha256': digest(audio_path), 'trace_sha256': digest(trace_path)}


def summarize(captures: list[dict[str, Any]],
              references: list[dict[str, Any]]) -> dict[str, Any]:
    """Aggregate stored measurements only; no audio reads or new fits."""
    by_key = {(row['picking'], row['midi'], row['condition']): row for row in captures}
    wave_drops: list[float] = []
    microphone_drops: list[list[float]] = [[] for _ in WINDOWS]
    matches: dict[str, list[list[float]]] = {}
    coverage: dict[str, dict[str, Any]] = {}
    for (picking, midi, condition), baseline in by_key.items():
        damped = by_key.get((picking, midi, 'low-e-damped'))
        if condition != 'baseline' or damped is None:
            continue
        a = baseline['observations']['string_0_normal_quarter']['peak']['amplitude']
        b = damped['observations']['string_0_normal_quarter']['peak']['amplitude']
        wave_drops.append(db(a, b))
        for i in range(len(WINDOWS)):
            microphone_drops[i].append(
                baseline['audio']['tracked_low_e_octave_projections'][i]['level_rel_early_rms_db']
                - damped['audio']['tracked_low_e_octave_projections'][i]['level_rel_early_rms_db'])
    for row in references:
        picking, midi = row['picking'], row['midi']
        baseline = by_key.get((picking, midi, 'baseline'))
        if baseline is None:
            continue
        values = matches.setdefault(picking, [[] for _ in WINDOWS])
        for i in range(len(WINDOWS)):
            values[i].append(
                row['audio']['observed_165_region_projections'][i]['level_rel_early_rms_db']
                - baseline['audio']['tracked_low_e_octave_projections'][i]['level_rel_early_rms_db'])
        group = coverage.setdefault(picking, {'clips': 0, 'pitches': []})
        group['clips'] += 1
        group['pitches'].append(midi)
    for group in coverage.values():
        group['pitches'] = sorted(set(group['pitches']))

    def triplet(values: list[float]) -> list[float] | None:
        if not values:
            return None
        if not all(math.isfinite(value) for value in values):
            raise ValueError('summary requires finite measurements')
        return [min(values), median(values), max(values)]

    return {
        'value_order': ['minimum', 'median', 'maximum'],
        'windows_seconds': WINDOWS,
        'measurement_definitions': {
            'phasor': 'Unweighted source-domain least squares of cos(2*pi*f*t), sin(2*pi*f*t), and a constant per channel over each window, after broad 250Hz low-pass/downsample to 2000Hz; coefficients form c=cos_coeff-i*sin_coeff.',
            'channel_combination': 'Amplitude=sqrt(mean(abs(c_channel)^2)); stereo channels are combined by power, not a mono waveform average. String displacement is the single normal-plane quarter-point trace.',
            'normalization': '20*log10(phasor_amplitude/early_broadband_rms), where RMS is sqrt(mean(x^2)) over all original audio channels in the first100ms after onset. Reference onset=20ms; model audio onset=7/48000s; state traces onset=0.',
            'frequency': 'Per-row projection frequency is stored with every projection. Standard/DropD uses the idle low-E quarter-point local peak within nominal octave +/-4Hz; added-loss uses nominal164.813778Hz. Eastman uses its observed local spectral peak within165+/-4Hz. Peaks use a Hann window40-400ms; finite-window projections can include nearby transient leakage.',
            'wave_suppression': '20*log10(Standard quarter-point normal spectral peak amplitude / added-loss peak amplitude), each from40-400ms with Hann window and local nominal-octave +/-7Hz search.',
            'aggregation': 'Equal weight per model picking/pitch for loss comparisons; equal weight per accepted reference clip for real-minus-baseline comparisons matched by picking and MIDI. Repeated clips use the same model baseline. Triplets are minimum, median, maximum; no confidence intervals.'},
        'loss_comparison_notes': len(wave_drops),
        'reference_coverage': coverage,
        'low_e_wave_suppression_db': triplet(wave_drops),
        'microphone_projection_reduction_db_by_window': [triplet(values) for values in microphone_drops],
        'reference_minus_model_projection_db_by_window': {
            picking: [triplet(values) for values in windows] for picking, windows in matches.items()}}


def self_test() -> None:
    t = np.arange(3000) / RATE
    for f, t60 in [(155.56, .5), (165.04, .9), (174.61, 1.5)]:
        x = (np.exp(-LOG1000 * t / t60) * np.cos(2. * np.pi * f * t)
             + .04 * np.exp(-LOG1000 * t / .55) * np.cos(2. * np.pi * 94. * t)
             + .01 * np.exp(-LOG1000 * t / 4.) * np.cos(2. * np.pi * OPEN[0] * t))[:, None]
        report = analyse_audio(x, RATE, 0., OPEN, 94.)
        assert report['component_detected_in_windows'] == len(WINDOWS), report
        for fit in report['fits']:
            assert abs(fit['frequency_hz'] - f) < .10, fit
            assert abs(fit['apparent_t60_seconds'] / t60 - 1.) < .05, fit
    for x in [np.zeros((3000, 1)),
              np.exp(-LOG1000 * t / 4.)[:, None] * np.cos(2. * np.pi * OPEN[0] * t)[:, None],
              np.exp(-LOG1000 * t / .55)[:, None] * np.cos(2. * np.pi * 94. * t)[:, None]]:
        report = analyse_audio(x, RATE, 0., OPEN, 94.)
        assert not report['component_detected_in_windows'], report
    for value in [np.nan, np.inf]:
        x = np.zeros((3000, 1)); x[10] = value
        try:
            analyse_audio(x, RATE, 0., OPEN, 94.)
            raise AssertionError('non-finite audio accepted')
        except ValueError:
            pass
    print('Ringing analysis self-test passed: known poles, silence/nuisance rejection, finite input')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--eastman-rows', '--rows', dest='rows', type=Path,
                        help='PrepareEastmanCorpus rows.json only; uses take tuning and 94 Hz air nuisance')
    parser.add_argument('--probe', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--summary-from-report', type=Path,
                        help='Only aggregate an existing analysis JSON into --output; reads no audio and runs no fits')
    args = parser.parse_args()
    if args.self_test:
        self_test(); return 0
    if args.summary_from_report:
        if args.output is None:
            parser.error('--summary-from-report requires --output')
        source = json.loads(args.summary_from_report.read_text())
        summary = summarize(source['captures'], source['references'])
        summary['source_report'] = {'path': str(args.summary_from_report.resolve()),
                                    'sha256': digest(args.summary_from_report)}
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(summary, indent=2, allow_nan=False) + '\n')
        print(f'Stored measurements only -> {args.output}')
        return 0
    if args.manifest is None or args.output is None:
        parser.error('--manifest and --output are required')
    document = json.loads(args.manifest.read_text())
    captures = [analyse_capture(row, args.manifest.parent) for row in document['captures']]
    references = []
    provenance = [{'path': str(args.manifest.resolve()), 'sha256': digest(args.manifest)}]
    if args.rows:
        reference_document = json.loads(args.rows.read_text())
        if reference_document.get('corpus') != 'eastman':
            raise ValueError('--eastman-rows requires an Eastman prepared corpus')
        rows = reference_document['rows']
        analysis_path = args.rows.parent / 'analysis.json'
        takes = json.loads(analysis_path.read_text())['takes']
        for row in rows:
            if row['midi'] not in (64, 67, 72):
                continue
            path = args.rows.parent / row['target']['path']
            x = np.fromfile(path, dtype='<f4').reshape(-1, row['target']['channels'])
            rate = row['target']['sample_rate']
            take = takes['picked' if row['picking'] == 'pick' else 'plucked']
            tuning = float(take['tuning_offset_cents'])
            analysis = analyse_audio(x, rate, .02, OPEN * 2. ** (tuning / 1200.), 94.)
            band = low_band(x, rate)
            peak = spectral_peak(band, RATE, .02, 165., 4.)
            projections = []
            for begin, end in WINDOWS:
                coefficients = [projection(band[:, channel], RATE, .02,
                                            peak['frequency_hz'], begin, end)
                                for channel in range(band.shape[1])]
                amplitude = math.sqrt(sum(abs(c) ** 2 for c in coefficients) / len(coefficients))
                projections.append({'begin': begin, 'end': end, 'frequency_hz': peak['frequency_hz'],
                                    'amplitude': amplitude,
                                    'level_rel_early_rms_db': db(amplitude, analysis['early_broadband_rms'])})
            analysis['observed_165_region_peak'] = peak
            analysis['observed_165_region_projections'] = projections
            references.append({'id': row['id'], 'midi': row['midi'], 'picking': row['picking'],
                               'nuisance_open_tuning_cents': tuning,
                               'audio': analysis,
                               'target_sha256': digest(path)})
        provenance.append({'path': str(args.rows.resolve()), 'sha256': digest(args.rows)})
        provenance.append({'path': str(analysis_path.resolve()), 'sha256': digest(analysis_path)})
    if args.probe:
        provenance.append({'path': str(args.probe.resolve()), 'sha256': digest(args.probe)})
    report = {'protocol': __doc__, 'windows': WINDOWS, 'controls': document['controls'],
              'retune_protocol': document['retune_protocol'], 'work_guard': document['work_guard'],
              'measurement_limit': 'A single damped component is a window-sensitive diagnostic, not an identified structural pole; nearby poles and non-exponential losses remain possible.',
              'provenance': provenance, 'captures': captures, 'references': references,
              'summary': summarize(captures, references),
              'reference_coverage': sorted({f'{r["picking"]}-{r["midi"]}' for r in references})}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    for row in captures:
        print(row['key'], 'peak', round(row['audio']['spectral_peak']['frequency_hz'], 3),
              'Hz level', round(row['audio']['spectral_level_rel_early_rms_db'], 2),
              'dB body', row['largest_body_projection'][0][0])
    print(f'{len(captures)} captures and {len(references)} references -> {args.output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
