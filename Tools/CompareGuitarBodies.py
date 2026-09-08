#!/usr/bin/env python3
"""Audition body controls with identical notes and level-independent spectra."""
from __future__ import annotations
import argparse
import hashlib
import itertools
import json
import shutil
import subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--baseline-renderer', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    rate = 48000
    notes = [40, 45, 50, 55, 59, 64, 69, 74]
    frames = len(notes) * rate
    events = args.output / 'notes.txt'
    rows = [f'ACUSTRA_PERFORMANCE_V1 {rate} {frames}']
    for i, note in enumerate(notes):
        channel = min(i + 1, 6)
        rows += [f'{i*rate} {channel} {note} 100 0',
                 f'{i*rate+int(.8*rate)} {channel} {note} 0 0']
    events.write_text('\n'.join(rows) + '\n')
    report = {'analysis': 'Identical eight notes, velocity100, 48kHz. Each note spectrum normalized independently before equal-weight band comparison. No perceptual realism claim.',
              'tool_sha256': digest(Path(__file__)), 'renderers': {}, 'renders': {}, 'shape_distances_db': {}}
    band_edges = np.geomspace(70, 10000, 44)
    freq = np.fft.rfftfreq(16384, 1/rate)
    for version, source in [('current', args.renderer), ('baseline', args.baseline_renderer)]:
        if source is None:
            continue
        frozen = args.output / f'{version}-renderer'
        shutil.copy2(source, frozen)
        frozen.chmod(frozen.stat().st_mode | 0o111)
        report['renderers'][version] = {'source': str(source.resolve()), 'sha256': digest(frozen)}
        cases = [(material, shape, 'original') for material in ['steel', 'nylon']
                 for shape in ['parlor', 'auditorium', 'dreadnought', 'jumbo']]
        if version == 'current':
            cases += [('nylon','auditorium','bellido1978'), ('steel','parlor','washburn1897'),
                      ('steel','auditorium','santacruz2022'), ('steel','dreadnought','martin2007')]
        spectra = {}
        audio = {}
        for material, shape, model in cases:
            key = f'{material}-{shape}-{model}'
            raw = args.output / f'{version}-{key}.f32'
            command = [str(frozen.resolve()), str(events.resolve()), str(raw.resolve()), 'stereo_mic', 'finger',
                       '--string-material', material, '--body-shape', shape,
                       '--body-material', 'cedar' if model == 'bellido1978' else 'spruce']
            if model != 'original':
                command += ['--guitar-model', model]
            subprocess.run(command, check=True, capture_output=True)
            signal = np.fromfile(raw, '<f4').reshape(-1, 2)
            assert len(signal) == frames and np.all(np.isfinite(signal))
            mono = signal.mean(axis=1)
            feature = []
            for i in range(len(notes)):
                # Overlapping spectra span attack and sustain, avoiding note-off.
                power = np.mean([abs(np.fft.rfft(mono[i*rate+offset:i*rate+offset+16384] * np.hanning(16384)))**2
                                 for offset in [0, 8192, 16384]], axis=0)
                power /= max(power.sum(), 1e-30)
                feature.append([10*np.log10(max(power[(freq>=lo)&(freq<hi)].mean(), 1e-14))
                                for lo, hi in zip(band_edges[:-1], band_edges[1:])])
            spectra[key] = np.array(feature)
            audio[key] = signal
            report['renders'][f'{version}-{key}'] = {'sha256': digest(raw), 'rms': float(np.sqrt(np.mean(signal**2))),
                                                     'peak': float(np.max(abs(signal))), 'command': command}
        for material in ['steel', 'nylon']:
            keys = [f'{material}-{s}-original' for s in ['parlor','auditorium','dreadnought','jumbo']]
            report['shape_distances_db'][f'{version}-{material}'] = {
                f'{a.split("-")[1]} vs {b.split("-")[1]}': float(np.mean(abs(spectra[a]-spectra[b])))
                for a, b in itertools.combinations(keys, 2)}
        for key, signal in audio.items():
            rms = np.sqrt(np.mean(signal**2))
            normalized = signal * (.055 / max(rms, 1e-12))
            # Common RMS for audition; any peak constraint is explicitly recorded.
            peak = np.max(abs(normalized))
            if peak > .9:
                normalized *= .9 / peak
            wavfile.write(args.output / f'{version}-{key}-listen.wav', rate, normalized.astype(np.float32))
            report['renders'][f'{version}-{key}']['audition_rms'] = float(np.sqrt(np.mean(normalized**2)))
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report['shape_distances_db'], indent=2))


if __name__ == '__main__':
    main()
