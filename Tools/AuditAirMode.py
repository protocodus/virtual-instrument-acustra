#!/usr/bin/env python3
"""Audit low air-mode decay and pre-existing open-string ringing.

This is a diagnostic, not a corpus fitter. A broad 150 Hz low-pass removes
the played note's higher partials; a source-domain damped sinusoid is fitted
alongside slow open-string tones. No narrow bandpass envelope sets the decay.
Repeat with different fit windows and broad low-pass corners to expose room,
noise and nearby-mode contamination. The model's 84.7 Hz mode is especially
hard to separate from low E at 82.4 Hz, so its known pole is the stronger
reference for intrinsic free decay.
The reciprocal bridge/anchor system can move the dominant audible transient
away from that radiation pole; the default model search therefore covers
79--109 Hz rather than assuming the audible peak is at 84.7 Hz.

--rows reads PrepareEastmanCorpus/PrepareMartinCorpus output. --manifest
reads BenchmarkOpenCorpora render manifests. --eastman-source optionally
reads the verified original takes to measure tones at open-string frequencies BEFORE each
onset; broadband isolation alone does not screen these quiet narrow tones.
All levels are relative to the same note's first 100 ms broadband RMS.
Fits report apparent output-component T60, not a room-free structural Q.
Tone energy near an open-string frequency does not by itself identify an
idle string: a nearby body mode, muted partial or other signal can overlap.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
from typing import Any

import numpy as np
from scipy.optimize import least_squares
from scipy.signal import butter, hilbert, resample_poly, sosfilt, sosfiltfilt

OPEN_HZ = np.array([82.406889, 110., 146.832384, 164.813778,
                    195.997718, 220., 246.941651])
LOG1000 = math.log(1000.)
ANALYSIS_RATE = 1000
SETTINGS = [(150., .04, .30), (150., .04, .40), (150., .06, .50),
            (140., .04, .40), (170., .04, .40)]
STABLE_SCREEN = ('No parameter-bound hits, T60 sensitivity range <0.25s, '
                 'LF variance explained >0.65, air incremental variance >0.15, '
                 'non-silent early RMS, and fitted center farther than 30 cents '
                 '(at least 1Hz) from any open tone. Diagnostic screen only; '
                 'these are not confidence intervals.')


def read_audio(spec: dict[str, Any], base: Path) -> tuple[np.ndarray, int]:
    path = Path(spec['path'])
    if not path.is_absolute():
        path = base / path
    rate, channels = int(spec['sample_rate']), int(spec['channels'])
    return np.fromfile(path, dtype='<f4').astype(float).reshape(-1, channels), rate


def downsample(x: np.ndarray, rate: int) -> np.ndarray:
    divisor = math.gcd(rate, ANALYSIS_RATE)
    return resample_poly(x, ANALYSIS_RATE // divisor, rate // divisor, axis=0)


def air_fit(y: np.ndarray, onset: float, frequencies: np.ndarray,
            center: float, half_width: float, cutoff: float,
            begin: float, end: float) -> dict[str, Any]:
    filtered = sosfiltfilt(butter(8, cutoff, fs=ANALYSIS_RATE, output='sos'),
                           y, axis=0)
    time = np.arange(len(y)) / ANALYSIS_RATE - onset
    take = (time >= begin) & (time < end)
    t, signal = time[take], filtered[take]
    if len(t) < 100:
        raise ValueError('fit window is shorter than 100 ms')
    slow = np.exp(-LOG1000 * t / 4.)
    nuisance = [slow * trig(2. * np.pi * f * t)
                for f in frequencies for trig in (np.cos, np.sin)]
    nuisance += [np.ones(len(t)), t]

    def matrix(parameters: np.ndarray) -> np.ndarray:
        f, t60 = parameters
        envelope = np.exp(-LOG1000 * t / t60)
        return np.column_stack([envelope * np.cos(2. * np.pi * f * t),
                                envelope * np.sin(2. * np.pi * f * t),
                                *nuisance])

    scale = max(float(np.sqrt(np.mean(signal ** 2))), 1e-15)

    def residual(parameters: np.ndarray) -> np.ndarray:
        design = matrix(parameters)
        coefficients = np.linalg.lstsq(design, signal, rcond=None)[0]
        return ((design @ coefficients - signal) / scale).ravel()

    fits = [least_squares(residual, [center + offset, .65],
                          bounds=([center - half_width, .15],
                                  [center + half_width, 1.8]),
                          x_scale=[half_width, .5], max_nfev=80)
            for offset in (-.55 * half_width, 0., .55 * half_width)]
    fit = min(fits, key=lambda item: float(np.sum(item.fun ** 2)))
    design = matrix(fit.x)
    coefficients, _, _, singular = np.linalg.lstsq(design, signal, rcond=None)
    prediction = design @ coefficients
    air = design[:, :2] @ coefficients[:2]
    baseline = np.column_stack(nuisance)
    baseline_error = signal - baseline @ np.linalg.lstsq(baseline, signal, rcond=None)[0]
    error = signal - prediction
    variance = max(float(np.sum((signal - signal.mean(axis=0)) ** 2)), 1e-30)
    return {'frequency_hz': float(fit.x[0]), 't60_seconds': float(fit.x[1]),
            'q_equivalent': float(fit.x[1] * np.pi * fit.x[0] / LOG1000),
            'lf_variance_explained': 1. - float(np.sum(error ** 2)) / variance,
            'air_fraction_of_lf_variance': float(np.sum(baseline_error ** 2)
                                                - np.sum(error ** 2)) / variance,
            'air_component_rms': float(np.sqrt(np.mean(air ** 2))),
            'air_extrapolated_onset_rms': float(np.sqrt(np.mean(
                np.sum(coefficients[:2] ** 2, axis=0) / 2.))),
            'design_condition': float(singular[0] / singular[-1]),
            'at_search_bound': bool(abs(fit.x[0] - center) > .98 * half_width
                                    or fit.x[1] < .151 or fit.x[1] > 1.79),
            'lowpass_hz': cutoff, 'window_seconds': [begin, end]}


def analyse_air(x: np.ndarray, rate: int, frequencies: np.ndarray,
                center: float, half_width: float, onset: float = .020) -> dict[str, Any]:
    if x.ndim != 2 or len(x) < rate * .6 or x.shape[1] < 1:
        raise ValueError('audio must have channels and at least 0.6 seconds')
    if not np.all(np.isfinite(x)) or rate < 1000:
        raise ValueError('audio must be finite and sampled at least 1000Hz')
    y = downsample(x, rate)
    fits = [air_fit(y, onset, frequencies, center, half_width, *setting)
            for setting in SETTINGS]
    primary = fits[1]
    early = x[round(onset * rate):round((onset + .100) * rate)]
    early_rms = float(np.sqrt(np.mean(early ** 2)))
    primary['air_onset_db_re_early_rms'] = db(primary['air_extrapolated_onset_rms'], early_rms)
    primary['air_window_db_re_early_rms'] = db(primary['air_component_rms'], early_rms)
    t60 = np.array([fit['t60_seconds'] for fit in fits])
    separation = np.min(abs(frequencies - primary['frequency_hz']))
    ambiguity_hz = max(1., primary['frequency_hz'] * (2. ** (30. / 1200.) - 1.))
    present = early_rms > 1e-12 and primary['air_fraction_of_lf_variance'] > .15
    distinct = separation > ambiguity_hz
    return {'primary': primary, 'sensitivity_fits': fits,
            't60_sensitivity_range_seconds': [float(t60.min()), float(t60.max())],
            'air_component_present': bool(present),
            'open_tone_ambiguity': bool(not distinct),
            'nearest_open_tone_distance_hz': float(separation),
            'stable': bool(not any(fit['at_search_bound'] for fit in fits)
                           and t60.max() - t60.min() < .25
                           and primary['lf_variance_explained'] > .65
                           and present and distinct),
            'early_rms': early_rms}


def db(value: float, reference: float) -> float:
    return 20. * math.log10(max(value, 1e-15) / max(reference, 1e-15))


def tone_projection(x: np.ndarray, rate: int, origin: float, begin: float,
                    end: float, frequencies: np.ndarray, air: dict[str, Any],
                    post: bool, played_hz: float | None = None) -> np.ndarray:
    first, last = round((origin + begin) * rate), round((origin + end) * rate)
    sample = downsample(x[max(first, 0):last], rate)
    t = np.arange(len(sample)) / ANALYSIS_RATE + begin
    middle = .5 * (begin + end)
    slow = np.exp(-LOG1000 * (t - middle) / 4.)
    columns = [slow * trig(2. * np.pi * f * t)
               for f in frequencies for trig in (np.cos, np.sin)]
    envelope = np.exp(-LOG1000 * (t - middle) / air['t60_seconds']) if post else np.ones(len(t))
    columns.extend(envelope * trig(2. * np.pi * air['frequency_hz'] * t)
                   for trig in (np.cos, np.sin))
    if played_hz:
        columns.extend(slow * trig(2. * np.pi * played_hz * harmonic * t)
                       for harmonic in range(1, 5) if played_hz * harmonic < 450.
                       for trig in (np.cos, np.sin))
    columns.extend([np.ones(len(t)), t - middle])
    coefficients = np.linalg.lstsq(np.column_stack(columns), sample, rcond=None)[0]
    # RMS over channels, sinusoid power = (cos^2 + sin^2) / 2.
    return np.sqrt(np.mean(coefficients[:2 * len(frequencies)].reshape(
        len(frequencies), 2, sample.shape[1]) ** 2, axis=(1, 2)))


def open_tones(x: np.ndarray, rate: int, onset: float, frequencies: np.ndarray,
               played_hz: float, air: dict[str, Any]) -> dict[str, Any]:
    harmonics = played_hz * np.arange(1, 5)
    frequencies = frequencies[np.min(abs(frequencies[:, None] - harmonics), axis=1) > 1.]
    amplitude = tone_projection(x, rate, onset, .04, .40, frequencies,
                                air['primary'], True, played_hz)
    return {'frequencies_hz': frequencies.tolist(), 'rms_at_220ms': amplitude.tolist(),
            'db_re_early_rms_at_220ms': [db(value, air['early_rms']) for value in amplitude],
            'total_db_re_early_rms_at_220ms': db(float(np.sqrt(np.sum(amplitude ** 2))),
                                                  air['early_rms']),
            'attribution': 'Energy projected at open-string frequencies; this is not direct idle-string wave energy. Nearby body modes and rapidly muted partials can overlap, and the fixed 4s basis is a measurement convention.'}


def residual_ringing(x: np.ndarray, rate: int, row: dict[str, Any],
                     frequencies: np.ndarray, air: dict[str, Any],
                     previous: dict[str, Any] | None) -> dict[str, Any]:
    onset = float(row['onset_seconds_in_source'])
    if onset < .5:
        return {'available': False}
    # Remove any frequency that is also one of the played note's harmonics.
    played = float(row['measured_f0_hz'])
    harmonics = played * np.arange(1, 5)
    distinguishable = np.min(abs(frequencies[:, None] - harmonics), axis=1) > 1.
    frequencies = frequencies[distinguishable]
    pre = tone_projection(x, rate, onset, -.45, -.025, frequencies, air, False)
    post = tone_projection(x, rate, onset, .04, .40, frequencies, air, True, played)
    ratio = np.minimum(pre / np.maximum(post, 1e-15), 10.)
    tone_share = post ** 2 / max(float(np.sum(post ** 2)), 1e-30)
    return {'available': True, 'frequencies_hz': frequencies.tolist(),
            'pre_to_post_db': [db(a, b) for a, b in zip(pre, post)],
            'pre_rms': pre.tolist(), 'post_rms': post.tolist(),
            'pre_energy_fraction_without_decay_correction':
                float(np.sum(pre ** 2) / max(float(np.sum(post ** 2)), 1e-30)),
            'tones_with_pre_at_least_half_post_amplitude': int(np.sum(ratio >= .5)),
            'substantial_tones_with_pre_at_least_half_post_amplitude':
                int(np.sum((ratio >= .5) & (tone_share >= .1))),
            'previous_event_seconds_before': (onset - previous['onset_seconds']) if previous else None,
            'previous_event_midi': previous.get('pitch', {}).get('midi') if previous else None,
            'interpretation': 'Pre/post are separate sinusoid projections; noise and unresolved nearby tones can contribute. This is evidence for carry-over, not an energy subtraction or proof of new sympathetic excitation.'}


def summary(rows: list[dict[str, Any]]) -> dict[str, Any]:
    result = {}
    for label in sorted({row['label'] for row in rows}):
        group = [row for row in rows if row['label'] == label]
        stable = [row for row in group if row['air']['stable']]
        def stats(items: list[dict[str, Any]]) -> dict[str, Any]:
            if not items:
                return {'count': 0}
            values = np.array([item['air']['primary']['t60_seconds'] for item in items])
            return {'count': len(items), 'median_frequency_hz': float(np.median([
                item['air']['primary']['frequency_hz'] for item in items])),
                'median_t60_seconds': float(np.median(values)),
                't60_interquartile_seconds': np.quantile(values, [.25, .75]).tolist(),
                'median_lf_variance_explained': float(np.median([
                    item['air']['primary']['lf_variance_explained'] for item in items])),
                'median_air_onset_db_re_early_rms': float(np.median([
                    item['air']['primary']['air_onset_db_re_early_rms'] for item in items])),
                'median_open_tones_db_re_early_rms_at_220ms': float(np.median([
                    item['open_tones']['total_db_re_early_rms_at_220ms'] for item in items]))}
        ringing = [row['residual_ringing'] for row in group
                   if row.get('residual_ringing', {}).get('available')]
        result[label] = {'all_rows': stats(group), 'stable_rows': stats(stable)}
        if ringing:
            result[label]['residual_ringing'] = {
                'rows': len(ringing),
                'rows_with_one_or_more_tone_pre_amplitude_at_least_half_post': sum(
                    item['tones_with_pre_at_least_half_post_amplitude'] > 0 for item in ringing),
                'rows_with_substantial_tone_pre_amplitude_at_least_half_post': sum(
                    item['substantial_tones_with_pre_at_least_half_post_amplitude'] > 0 for item in ringing),
                'median_pre_to_post_projected_energy_ratio': float(np.median([
                    item['pre_energy_fraction_without_decay_correction'] for item in ringing]))}
    return result


def self_test() -> None:
    rate = 48000
    t = np.arange(round(1.5 * rate)) / rate - .020
    for frequency, t60 in [(94., .87), (96., .61), (84.6785, .50146)]:
        x = np.where(t >= 0., np.exp(-LOG1000 * np.maximum(t, 0.) / t60)
                     * np.cos(2. * np.pi * frequency * t + .3), 0.)
        air_only = x.copy()
        # Strong adjacent low E, plus A2, are nuisance tones, not air decay.
        x += np.where(t >= 0., 2. * np.exp(-LOG1000 * np.maximum(t, 0.) / 4.)
                      * np.cos(2. * np.pi * OPEN_HZ[0] * t), 0.)
        x += np.where(t >= 0., .2 * np.exp(-LOG1000 * np.maximum(t, 0.) / 4.)
                      * np.sin(2. * np.pi * OPEN_HZ[1] * t), 0.)
        result = analyse_air(x[:, None], rate, OPEN_HZ, frequency, 2.)
        for fit in result['sensitivity_fits']:
            assert abs(fit['frequency_hz'] - frequency) < .10, fit
            assert abs(fit['t60_seconds'] / t60 - 1.) < .06, fit
        # The strong-nuisance fixture can recover a weak air component even
        # when its contribution is below the stability screen's presence
        # floor. An air-dominant mixture must also pass that screen.
        dominant = air_only + .05 * (x - air_only)
        positive = analyse_air(dominant[:, None], rate, OPEN_HZ, frequency, 2.)
        assert positive['stable'], positive
    # A narrow band whose lower edge excludes the pole adds its own ring.
    # The same intrinsic .50146s pole measures >8% longer by this envelope
    # rule; the source-domain fits above recover the intended decay.
    t = np.arange(1500) / ANALYSIS_RATE
    raw = np.exp(-LOG1000 * t / .50146) * np.cos(2. * np.pi * 84.6785 * t + .3)
    narrow = sosfilt(butter(4, [85., 105.], btype='bandpass',
                            fs=ANALYSIS_RATE, output='sos'), raw)
    envelope = abs(hilbert(narrow))
    take = (t >= .08) & (t < .40)
    narrow_t60 = -LOG1000 / np.polyfit(t[take], np.log(envelope[take]), 1)[0]
    assert narrow_t60 > .50146 * 1.08, narrow_t60
    assert not analyse_air(np.zeros((72000, 1)), rate, OPEN_HZ, 94., 15.)['stable']
    t = np.arange(72000) / rate - .020
    for t60 in (.61, .87, 1.2, 4.):
        tone = np.where(t >= 0., np.exp(-LOG1000 * np.maximum(t, 0.) / t60)
                        * np.cos(2. * np.pi * OPEN_HZ[0] * t), 0.)[:, None]
        result = analyse_air(tone, rate, OPEN_HZ, 94., 15.)
        assert not result['stable'], (t60, result)
        if t60 < 4.:
            assert result['open_tone_ambiguity'], (t60, result)
    print('AuditAirMode self-test: known decays, nearby tones, filter bias and silence/open-tone rejection pass')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rows', type=Path, action='append', default=[])
    parser.add_argument('--manifest', type=Path, action='append', default=[])
    parser.add_argument('--eastman-source', type=Path)
    parser.add_argument('--model-center', type=float, default=94.)
    parser.add_argument('--model-half-width', type=float, default=15.)
    parser.add_argument('--min-midi', type=int, default=58)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        if not args.rows and not args.manifest:
            return 0
    if not args.output or not (args.rows or args.manifest):
        parser.error('--output and --rows or --manifest are required')
    records, provenance = [], []
    for path in args.rows:
        document = json.loads(path.read_text())
        corpus = document['corpus']
        takes, events, tunings = {}, {}, {}
        if corpus == 'eastman':
            import PrepareEastmanCorpus as preparation
            analysis = json.loads((path.parent / 'analysis.json').read_text())
            for name, take in analysis['takes'].items():
                events[name] = take['events']
                tunings[name] = take['tuning_offset_cents']
                if args.eastman_source:
                    # Same preparation decoder and verified source hashes.
                    source = args.eastman_source / take['file']
                    if hashlib.sha256(source.read_bytes()).hexdigest() != take['sha256']:
                        raise ValueError(f'{source}: source hash mismatch')
                    takes[name] = preparation.decode('ffmpeg', source)
        for row in document['rows']:
            if row['midi'] < args.min_midi:
                continue
            name = 'picked' if row.get('picking') == 'pick' else 'plucked'
            tuning = tunings.get(name, -12.40 if corpus == 'martin-hd28' else 0.)
            frequencies = OPEN_HZ * 2. ** (tuning / 1200.)
            x, rate = read_audio(row['target'], path.parent)
            air = analyse_air(x, rate, frequencies, 96. if corpus == 'martin-hd28' else 94., 8.)
            record = {'id': row['id'], 'midi': row['midi'],
                      'label': f'{corpus}.{row["split"]}.reference', 'air': air}
            record['open_tones'] = open_tones(x, rate, .020, frequencies,
                                               row['measured_f0_hz'], air)
            if name in takes:
                preceding = [event for event in events[name]
                             if event['onset_seconds'] < row['onset_seconds_in_source'] - .01]
                record['residual_ringing'] = residual_ringing(takes[name], preparation.RATE,
                    row, frequencies, air['primary'], preceding[-1] if preceding else None)
            records.append(record)
        provenance.append({'path': str(path.resolve()),
                           'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    for path in args.manifest:
        document = json.loads(path.read_text())
        seen = set()
        for row in document['examples']:
            if row['midi'] < args.min_midi or row['model']['path'] in seen:
                continue
            seen.add(row['model']['path'])
            x, rate = read_audio(row['model'], path.parent)
            # Renderer noteOn is before frame zero; only the 7-sample capture
            # pipeline follows it. Targets instead have 20 ms of pre-roll.
            air = analyse_air(x, rate, OPEN_HZ, args.model_center,
                              args.model_half_width, 7. / rate)
            records.append({'id': row['id'], 'midi': row['midi'],
                            'label': f'{path.parent.name}.{path.stem}.model', 'air': air,
                            'open_tones': open_tones(x, rate, 7. / rate, OPEN_HZ,
                                440. * 2. ** ((row['midi'] - 69.) / 12.), air)})
        provenance.append({'path': str(path.resolve()),
                           'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                           'model_controls': document.get('model_controls'),
                           'renderer': document.get('provenance', {}).get('renderer_sha256')})
    report = {'protocol': __doc__, 'settings': SETTINGS, 'minimum_midi': args.min_midi,
              'stable_screen': STABLE_SCREEN,
              'provenance': provenance, 'summary': summary(records), 'rows': records}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['summary'], indent=2))
    print(f'{len(records)} diagnostic rows -> {args.output}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
