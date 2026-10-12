from pathlib import Path
import hashlib
import json
import sys
import numpy as np

# Same analysis as the scratch run; locate the repository after evidence copying.
root = next(p for p in Path(__file__).resolve().parents
            if (p / 'Tools/FitCaptureVoicing.py').is_file())
sys.path.insert(0, str(root / 'Tools'))
import FitCaptureVoicing as fit

folder = Path(__file__).resolve().parent
report_path = folder / 'capture-residuals.json'
report = json.loads(report_path.read_text())
source = {name: np.asarray(value) for name, value in report['source_residual_db'].items()}
weights = {'eastman-pick': .5, 'eastman-finger': .5, 'bank-flattop': 1., 'martin-hd28': 1., 'guitarset': 1.}
current = report['header_before']['sections']
offset = np.asarray(report['authored_balance']['band_offset_db'])
mask, band_weights = fit._weights()
def rms(value):
    return float(np.sqrt(np.average(value[mask] ** 2, weights=band_weights[mask])))

def view(active):
    differences = {name: (source[name], weight) for name, weight in active.items()}
    result = fit._fit_report(differences, current, offset)
    consensus = fit.consensus(differences)
    return {'source_weights': active,
            'observed_consensus_rms_db': rms(consensus),
            'consensus_residual_db': consensus.tolist(),
            'suggested_sections': result['sections'],
            'fixed_structure_fit_residual_rms_db': result['residual_rms_db']}

views = {'declared_protocol': view(weights),
         'without_duplicate_eastman_bank': view({k:v for k,v in weights.items() if k != 'bank-flattop'})}
for label, excluded in [('Eastman', ('eastman-pick', 'eastman-finger', 'bank-flattop')),
                        ('Martin', ('martin-hd28',)), ('GuitarSet', ('guitarset',))]:
    views['without_' + label] = view({k:v for k,v in weights.items() if k not in excluded})

independent = {'Eastman': .5 * (source['eastman-pick'] + source['eastman-finger']),
               'Martin': source['martin-hd28'], 'GuitarSet': source['guitarset']}
bands = []
for index, hz in enumerate(report['band_centres_hz']):
    if not mask[index]:
        continue
    values = {name: float(value[index]) for name,value in independent.items()}
    same_sign = all(v > 0 for v in values.values()) or all(v < 0 for v in values.values())
    bands.append({'hz': hz, 'recording_minus_model_db': values,
                  'same_sign_all_three_groups': same_sign,
                  'minimum_absolute_difference_db': min(abs(v) for v in values.values()),
                  'group_span_db': max(values.values()) - min(values.values())})

result = {'purpose': 'Final-model diagnostic only; no header writes or voicing adoption.',
          'capture_report_sha256': hashlib.sha256(report_path.read_bytes()).hexdigest(),
          'residual_sign': 'Positive: recording has more power than model after source-specific level removal.',
          'observed_source_rms_db': {k:rms(v) for k,v in source.items()},
          'weight_sensitivity': views, 'independent_group_bands': bands,
          'limits': ['Bank flat-top repeats eight Eastman finger performances: default Eastman weight is one half, versus one third after removing that duplicate.',
                     'Different instruments, pickups/microphone locations, playing and recording processing remain confounded with model response.',
                     'A fixed-structure fit residual is a spectral descriptor, not a perceptual realism score or proof that its suggested contour should ship.',
                     'The fit retains the separate authored MicrophoneBalance refinement; this diagnostic does not change either runtime header.']}
(folder / 'capture-assessment.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({'observed_source_rms_db': result['observed_source_rms_db'],
 'observed_consensus_rms_db': {k:v['observed_consensus_rms_db'] for k,v in views.items()},
 'all_group_same_sign_over_2db': [b for b in bands if b['same_sign_all_three_groups'] and b['minimum_absolute_difference_db'] >= 2]}, indent=2))
