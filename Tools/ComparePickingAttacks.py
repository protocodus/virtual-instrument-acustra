#!/usr/bin/env python3
"""Compare level-normalized picking attacks with a frozen real-note selection.

The source's 'f' label merges finger and thumb; this cannot identify a separate
Thumb profile, recorded velocity, fingering, or contact width. This is an
exploratory matched-pitch contrast, not a controlled perceptual experiment.
Uses the existing attack windows/bands/onset detector without fitting them.
Example: --archive /tmp/acustra-technique-guitar-notes-v3.zip
         --renderer before=/tmp/before --renderer after=/tmp/after --output /tmp/new
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile

import numpy as np

import BenchmarkTechniqueNotes as data
import FitPhysicalModel as features

PITCHES = (40, 42, 45, 47, 50, 52, 55, 57, 59, 62, 64, 67, 69, 72, 74, 77, 79, 80)
VELOCITIES = (32, 64, 91, 127)
ANALYSIS_HASHES = {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
                   for path in (Path(__file__), Path(data.__file__), Path(features.__file__))}


def attack(audio):
    onset = features._onset(audio, 48000)
    return features._normalise_levels(np.concatenate([
        features._band_levels(audio, 48000, onset, begin, end, features.ATTACK_BANDS)
        for begin, end in features.ATTACK_WINDOWS]))


def contrast(first, second):
    difference = np.asarray(second) - np.asarray(first)
    return {'mean_absolute_db': float(np.mean(np.abs(difference))),
            'difference_db': difference.tolist()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--renderer', action='append', default=[], metavar='LABEL=PATH')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    archive_hash = hashlib.sha256(args.archive.read_bytes()).hexdigest()
    if archive_hash != data.ARCHIVE_SHA256:
        raise ValueError('source archive differs from the frozen v3 download')
    renderers = {}
    for item in args.renderer:
        label, path = item.split('=', 1)
        if not label.isidentifier() or label in renderers:
            raise ValueError('renderer labels must be distinct identifiers')
        renderers[label] = Path(path).resolve(strict=True)
    out = args.output.resolve()
    out.mkdir()
    references, models = [], []
    with zipfile.ZipFile(args.archive) as archive:
        selected = [row for row in data.select_targets(archive.namelist()) if row['midi'] in PITCHES]
        (out / 'selection.json').write_text(json.dumps(selected, indent=2) + '\n')
        for row in selected:
            raw = archive.read(row['source_member'])
            references.append({**row, 'sha256': hashlib.sha256(raw).hexdigest(),
                               'attack': attack(data.read_reference(raw, row['source_member'])).tolist()})
    for label, renderer in renderers.items():
        directory = out / label
        directory.mkdir()
        for material in ('steel', 'nylon'):
            for midi in PITCHES:
                fingering = data.fingering(midi)
                for velocity in VELOCITIES:
                    name = f'{material}-{midi}-{velocity}'
                    events = directory / f'{name}.events'
                    data.write_events(events, [(0, fingering['channel'], midi, velocity, 0.)], 9600)
                    for picking in ('finger', 'pick', 'thumb'):
                        output = directory / f'{name}-{picking}.f32'
                        subprocess.run([str(renderer), str(events), str(output), 'stereo_mic', picking,
                                        'original', '--string-material', material,
                                        '--body-shape', 'auditorium', '--body-material',
                                        'mahogany' if material == 'steel' else 'spruce'],
                                       check=True, capture_output=True)
                        raw = output.read_bytes()
                        audio = np.frombuffer(raw, dtype='<f4').reshape(-1, 2).mean(axis=1).astype(float)
                        if not np.isfinite(audio).all() or not np.any(audio):
                            raise ValueError('nonfinite or silent model attack')
                        models.append({'renderer': label, 'material': material, 'midi': midi,
                                       'velocity': velocity, 'picking': picking,
                                       'sha256': hashlib.sha256(raw).hexdigest(),
                                       'attack': attack(audio).tolist()})
    reference_contrasts = []
    for material in ('steel', 'nylon'):
        for midi in PITCHES:
            means = {picking: np.mean([row['attack'] for row in references
                     if row['material'] == material and row['midi'] == midi
                     and row['picking'] == picking], axis=0) for picking in ('finger', 'pick')}
            reference_contrasts.append({'material': material, 'midi': midi,
                                       **contrast(means['finger'], means['pick'])})
    summaries = {}
    for label in renderers:
        groups = {}
        for material in ('steel', 'nylon'):
            target = {row['midi']: row for row in reference_contrasts if row['material'] == material}
            for velocity in VELOCITIES:
                contrasts = {name: [] for name in ('finger_pick', 'finger_thumb', 'pick_thumb')}
                target_errors = []
                identical_finger_pick = 0
                for midi in PITCHES:
                    rows = {row['picking']: row for row in models if row['renderer'] == label
                            and row['material'] == material and row['midi'] == midi and row['velocity'] == velocity}
                    identical_finger_pick += rows['finger']['sha256'] == rows['pick']['sha256']
                    for name, first, second in (('finger_pick', 'finger', 'pick'),
                                                ('finger_thumb', 'finger', 'thumb'),
                                                ('pick_thumb', 'thumb', 'pick')):
                        value = contrast(rows[first]['attack'], rows[second]['attack'])
                        contrasts[name].append(value['mean_absolute_db'])
                        if name == 'finger_pick':
                            target_errors.append(float(np.mean(np.abs(np.asarray(value['difference_db'])
                                                                      - target[midi]['difference_db']))))
                groups[f'{material}_v{velocity}'] = {
                    'attack_contrast_mean_absolute_db': {key: {'median': float(np.median(values)),
                                                              'minimum': float(np.min(values))}
                                                         for key, values in contrasts.items()},
                    'exactly_identical_finger_pick_notes': int(identical_finger_pick),
                    'reference_pick_minus_merged_finger_contrast_mae_db': float(np.mean(target_errors))}
        summaries[label] = groups
    finger_preservation = {}
    if renderers:
        baseline = next(iter(renderers))
        baseline_hashes = {(row['material'], row['midi'], row['velocity']): row['sha256']
                           for row in models if row['renderer'] == baseline and row['picking'] == 'finger'}
        for label in renderers:
            rows = [row for row in models if row['renderer'] == label and row['picking'] == 'finger']
            finger_preservation[label] = {'baseline': baseline, 'tested': len(rows),
                'byte_identical': sum(row['sha256'] == baseline_hashes[row['material'], row['midi'], row['velocity']]
                                      for row in rows)}
    report = {'protocol': __doc__, 'source': data.SOURCE, 'archive_sha256': archive_hash,
              'analysis_sha256': ANALYSIS_HASHES,
              'finger_preservation_vs_first_renderer': finger_preservation,
              'pitches': PITCHES, 'velocities': VELOCITIES,
              'windows_seconds': features.ATTACK_WINDOWS, 'band_edges_hz': features.ATTACK_BANDS.tolist(),
              'renderers': {label: {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
                            for label, path in renderers.items()},
              'reference_contrast_median_db': {material: float(np.median([row['mean_absolute_db']
                  for row in reference_contrasts if row['material'] == material])) for material in ('steel', 'nylon')},
              'summary': summaries, 'reference_contrasts': reference_contrasts,
              'references': references, 'models': models}
    (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'reference': report['reference_contrast_median_db'], 'models': summaries}, indent=2))


if __name__ == '__main__':
    main()
