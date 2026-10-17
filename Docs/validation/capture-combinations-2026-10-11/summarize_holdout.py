#!/usr/bin/env python3
"""Report the fixed holdout without selecting weights or making new renders."""
from pathlib import Path
import hashlib
import itertools
import json
import numpy as np

HERE=Path(__file__).resolve().parent
WINDOWS=('attack','early','held','late')

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    full=json.loads((HERE/'comparison.json').read_text())
    reports={name:json.loads((HERE/name/'report.json').read_text())['renders']
             for name in ('baseline','balanced_all','measured_continuity')}
    baseline={r['key']:r for r in reports['baseline']}
    summary={'comparison_sha256':sha(HERE/'comparison.json'),
             'summary_script_sha256':sha(Path(__file__)), 'variants':{}}
    lines=['# Fixed excitation holdout of two selected microphone mixtures','',
      '256 native clips per version; 768 total. Frozen weights were selected before this holdout and were not adjusted afterwards. '
      'All four shapes, Spruce/Mahogany, Stereo/Mono, pick velocity120 and thumb velocity56, '
      'MIDI40/41/42/45/47/51/53/59 on first-position strings; 48kHz, 3s held, 4s total. '
      'Native output with no post-render gain. Frozen metric and copied input/binary hashes are in manifest.json; exact commands and file hashes are in each report.json.','',
      'Target notes are Parlor47, Auditorium45 and Jumbo40/41. H1 balance means H1 power relative to the mean H2/H3 power. '
      'A negative balance change does not itself establish a healthier note: absolute H1 and upper-partial losses are reported separately.','']
    for variant, data in full['variants'].items():
        changes=data['changes']
        tables=[]
        for shape,window in itertools.product(('parlor','auditorium','jumbo'),WINDOWS):
            rows=[r for r in changes if r['target'] and r['shape']==shape and r['window']==window]
            tables.append(dict(shape=shape,window=window,
                balance_min=min(r['h1_h2h3_balance_change_db'] for r in rows),
                balance_median=float(np.median([r['h1_h2h3_balance_change_db'] for r in rows])),
                balance_max=max(r['h1_h2h3_balance_change_db'] for r in rows),
                h1_min=min(r['h1_change_db'] for r in rows),h1_max=max(r['h1_change_db'] for r in rows)))
        partials=[]
        folds=[]
        by_key={r['key']:r for r in reports[variant]}
        for key,row in by_key.items():
            b=baseline[key]
            for window in WINDOWS:
                for h in (2,3,4):
                    partials.append(dict(key=key,window=window,harmonic=h,
                        change_db=row['windows'][window]['partial_power_dbfs'][h-1]-b['windows'][window]['partial_power_dbfs'][h-1],
                        original_power_dbfs=b['windows'][window]['partial_power_dbfs'][h-1],
                        candidate_power_dbfs=row['windows'][window]['partial_power_dbfs'][h-1]))
                if row['capture']=='stereo_mic':
                    folds.append(dict(key=key,window=window,
                        h1_retention_db=row['stereo_fold_windows'][window]['partial_power_dbfs'][0]-row['windows'][window]['partial_power_dbfs'][0],
                        h1_retention_change_db=row['stereo_fold_windows'][window]['partial_power_dbfs'][0]-row['windows'][window]['partial_power_dbfs'][0]
                            -b['stereo_fold_windows'][window]['partial_power_dbfs'][0]+b['windows'][window]['partial_power_dbfs'][0]))
        target=[r for r in changes if r['target']]
        lower_by_window=[]
        for window in WINDOWS:
            worst=min([r for r in changes if r['midi']<=47 and r['window']==window],key=lambda r:r['h1_change_db'])
            key=worst['key']
            lower_by_window.append({**worst,
                'baseline_h1_dbfs':baseline[key]['windows'][window]['partial_power_dbfs'][0],
                'candidate_h1_dbfs':by_key[key]['windows'][window]['partial_power_dbfs'][0]})
        worst_upper=data['upper_balance_extrema']['maximum']
        upper_key,upper_window=worst_upper['key'],worst_upper['window']
        upper_partial_context=dict(key=upper_key,window=upper_window,
            baseline_partial_power_dbfs=baseline[upper_key]['windows'][upper_window]['partial_power_dbfs'],
            candidate_partial_power_dbfs=by_key[upper_key]['windows'][upper_window]['partial_power_dbfs'])
        retained={k:v for k,v in data.items() if k not in ('changes','target_summary')}
        retained.update(target_summary=tables,
            worst_lower_h1_by_window=lower_by_window,largest_upper_balance_partial_context=upper_partial_context,
            target_balance_increase_count=sum(r['h1_h2h3_balance_change_db']>0 for r in target),
            target_window_case_count=len(target),
            worst_higher_partial=min(partials,key=lambda r:r['change_db']),
            worst_fold_h1=min(folds,key=lambda r:r['h1_retention_db']),
            worst_fold_h1_change=min(folds,key=lambda r:r['h1_retention_change_db']),
            highest_fold_h1_change=max(folds,key=lambda r:r['h1_retention_change_db']))
        summary['variants'][variant]=retained
        lines += [f'## {variant}','',
          '| Shape | Window | H1 balance change range | Median | Absolute H1 change range |',
          '|---|---|---:|---:|---:|']
        for r in tables:
            lines.append(f"| {r['shape']} | {r['window']} | {r['balance_min']:+.2f} to {r['balance_max']:+.2f} dB | {r['balance_median']:+.2f} dB | {r['h1_min']:+.2f} to {r['h1_max']:+.2f} dB |")
        lines += ['',f"Target balance increases in {retained['target_balance_increase_count']}/{len(target)} target/window cases. "
          f"Maximum output peak {data['maximum_peak_dbfs']:.2f}dBFS; full-scale samples {data['full_scale_samples']}; Mono channel difference {data['mono_channel_max_difference']:.3g}.",'']
        for label,field,direction,metric in [
            ('Worst lower-note H1','lower_h1_extrema','minimum','h1_change_db'),
            ('Largest upper-note H1 gain','upper_h1_extrema','maximum','h1_change_db'),
            ('Largest upper-note balance increase','upper_balance_extrema','maximum','h1_h2h3_balance_change_db')]:
            r=data[field][direction]
            lines.append(f"- {label}: {r[metric]:+.2f}dB, {r['key']}, {r['window']}.")
        r=retained['worst_higher_partial']
        lines.append(f"- Largest H2–H4 power loss: {r['change_db']:+.2f}dB, H{r['harmonic']}, {r['key']}, {r['window']} (baseline {r['original_power_dbfs']:.2f}dBFS, candidate {r['candidate_power_dbfs']:.2f}dBFS).")
        r=retained['worst_fold_h1']
        lines.append(f"- Lowest stereo fold-down H1 retention relative to stereo channel-average power: {r['h1_retention_db']:.2f}dB, {r['key']}, {r['window']}.")
        r=retained['worst_fold_h1_change']
        lines.append(f"- Worst change of fold-down H1 retention versus baseline: {r['h1_retention_change_db']:+.3f}dB, {r['key']}, {r['window']}.")
        lines += ['', '| Window | Worst lower-note H1 change | Baseline H1 | Candidate H1 | Case |',
                  '|---|---:|---:|---:|---|']
        for r in lower_by_window:
            lines.append(f"| {r['window']} | {r['h1_change_db']:+.2f}dB | {r['baseline_h1_dbfs']:.2f}dBFS | {r['candidate_h1_dbfs']:.2f}dBFS | {r['key']} |")
        bh=upper_partial_context['baseline_partial_power_dbfs']
        ch=upper_partial_context['candidate_partial_power_dbfs']
        lines += ['', f"The largest upper-note balance increase is {upper_key}, {upper_window}: "
            f"H1 changes {ch[0]-bh[0]:+.2f}dB, while H2 changes {ch[1]-bh[1]:+.2f}dB "
            f"({bh[1]:.2f} to {ch[1]:.2f}dBFS). Thus the roughly +8dB balance change mainly reflects H2 weakening.", '']
    lines += ['## Limits','',
      'The holdout tests new excitation strengths/techniques and some new string routes, while sharing notes and the same modeled instrument with selection. '
      'It is not an independent studio recording or a generalization test of measured body radiation. No listening acceptance is claimed. '
      'There is no Maple render or continuous force/moment sweep. Therefore this cannot dismiss the separately identified Maple220.63Hz FRF notch. '
      'Parlor/Mahogany low-E H3 and B1 H2 appear in these native clips, but partial-band integration can hide a narrower notch or a different force/moment trajectory. '
      'H1 balance, absolute harmonic power, attack and later windows remain separate conclusions.','',
      'Run sequence (from repository root; output directories must be fresh):','',
      '```sh','OPENBLAS_NUM_THREADS=1 VECLIB_MAXIMUM_THREADS=1 python3 build-audit/studio-mic-combinations-20261011/holdout/run_holdout.py',
      'python3 build-audit/studio-mic-combinations-20261011/holdout/summarize_holdout.py','```','']
    (HERE/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
    (HERE/'REPORT.md').write_text('\n'.join(lines))
    print(HERE/'REPORT.md')

if __name__=='__main__':main()
