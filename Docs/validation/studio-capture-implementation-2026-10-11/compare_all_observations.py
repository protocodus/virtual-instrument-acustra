#!/usr/bin/env python3
"""Independent full-bank comparison to the pre-implementation approved CSV."""
import csv
import hashlib
import json
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
APPROVED = ROOT / 'build-audit/studio-mic-combinations-20261011/balanced_all/observation.csv'
CURRENT = HERE / 'production-coefficients.csv'
BASELINE = ROOT / 'build-audit/mic-boom-20261011/physics/coefficients.csv'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def parse_export(path):
    observations, poles, filters = {}, {}, []
    for row in csv.reader(path.open()):
        if row[0] != 'M':
            filters.append(row)
            continue
        key = tuple(map(int, row[1:4]))
        values = np.array(list(map(float, row[4:])))
        poles[key] = values[:2]
        observations[key] = values[2::2] + 1j * values[3::2]
    return observations, poles, filters

def main():
    approved = {}
    with APPROVED.open() as f:
        rows = csv.reader(f)
        next(rows)
        for row in rows:
            values = np.array(list(map(float, row[3:])))
            approved[tuple(map(int, row[:3]))] = values[::2] + 1j * values[1::2]
    current, poles, filters = parse_export(CURRENT)
    baseline, old_poles, old_filters = parse_export(BASELINE)
    assert len(approved) == len(current) == len(baseline) == 1920
    assert approved.keys() == current.keys() == baseline.keys()
    rows = []
    for key in approved:
        diff = abs(current[key] - approved[key])
        reference = abs(approved[key])
        errors = diff / np.maximum(reference, 1e-30)
        rows.append(dict(shape=key[0], wood=key[1], mode=key[2],
                         max_complex_relative_error=float(max(errors)),
                         max_complex_absolute_error=float(max(diff)),
                         per_path_relative_error=errors.tolist()))
    worst = max(rows, key=lambda r: r['max_complex_relative_error'])
    zero_expected_exact = all(np.all(current[k][approved[k] == 0] == 0) for k in approved)
    poles_exact = all(np.array_equal(poles[k], old_poles[k]) for k in poles)
    filters_exact = filters == old_filters
    report = dict(rows=1920, complex_observations=7680,
        approved_sha256=sha(APPROVED), production_sha256=sha(CURRENT), baseline_sha256=sha(BASELINE),
        engine_source_sha256=sha(ROOT / 'Source/DSP/AcustraEngine.cpp'),
        observer_data_sha256=sha(ROOT / 'Source/DSP/StudioCaptureData.h'),
        comparison_source_sha256=sha(Path(__file__)),
        max_complex_relative_error=worst['max_complex_relative_error'], worst=worst,
        max_complex_absolute_error=max(r['max_complex_absolute_error'] for r in rows),
        zero_expected_exact=zero_expected_exact, all1920_poles_exact=poles_exact,
        capture_filters_exact=filters_exact,
        active_observations=sum(int(np.count_nonzero(approved[k])) for k in approved),
        construction_maxima=[dict(shape=s, wood=w, max_complex_relative_error=max(
            r['max_complex_relative_error'] for r in rows if r['shape']==s and r['wood']==w))
            for s in range(4) for w in range(3)],
        pass_all=worst['max_complex_relative_error'] < 2e-5 and zero_expected_exact and poles_exact and filters_exact)
    (HERE / 'all-observations-review.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    if not report['pass_all']:
        raise SystemExit(1)

if __name__ == '__main__':
    main()
