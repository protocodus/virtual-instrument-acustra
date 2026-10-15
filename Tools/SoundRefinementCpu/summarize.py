#!/usr/bin/env python3
"""Summarize one final run; preserve its native per-configuration statistics."""
import json
from pathlib import Path
import statistics
import sys

source = Path(sys.argv[1]).resolve()
d = json.loads(source.read_text())
if d.get('smoke_only'):
    raise SystemExit('Refusing to summarize smoke timing as final evidence')
rows = []
for c in d['configurations']:
    r = {k:c[k] for k in ['index','rate','block','technique','scenario','room','touch']}
    r['pairs'] = c['baseline']['n']
    for v in ('baseline','candidate'):
        for s in ('median_us','p95_us','p99_us','max_us'):
            r[f'{v}_{s}'] = c[v][s]
        r[f'{v}_median_callback_budget_percent'] = 100*c[v]['median_us']/(1e6*c['block']/c['rate'])
        r[f'{v}_p95_callback_budget_percent'] = 100*c[v]['p95_us']/(1e6*c['block']/c['rate'])
    r['median_change_percent'] = 100*(c['candidate']['median_us']/c['baseline']['median_us']-1)
    r['p95_change_percent'] = 100*(c['candidate']['p95_us']/c['baseline']['p95_us']-1)
    r['audio_changed'] = c['baseline_evidence']['audio_fnv64'] != c['candidate_evidence']['audio_fnv64']
    if c['attack_baseline']:
        r['attack_window_median_change_percent'] = 100*(c['attack_candidate']['median_us']/c['attack_baseline']['median_us']-1)
    rows.append(r)

med = statistics.median
def group(rs):
    return {
        'configurations':len(rs), 'pairs':sum(r['pairs'] for r in rs),
        'median_of_configuration_changes_percent':med(r['median_change_percent'] for r in rs),
        'median_change_range_percent':[min(r['median_change_percent'] for r in rs),max(r['median_change_percent'] for r in rs)],
        'baseline_median_us_range':[min(r['baseline_median_us'] for r in rs),max(r['baseline_median_us'] for r in rs)],
        'candidate_median_us_range':[min(r['candidate_median_us'] for r in rs),max(r['candidate_median_us'] for r in rs)],
        'median_of_p95_changes_percent':med(r['p95_change_percent'] for r in rs),
        'candidate_worst_p95_us':max(r['candidate_p95_us'] for r in rs),
        'candidate_worst_max_us':max(r['candidate_max_us'] for r in rs),
    }
summary = {'input':str(source), 'overall':group(rows),
           'by_scenario':{s:group([r for r in rows if r['scenario']==s]) for s in sorted({r['scenario'] for r in rows})},
           'by_rate_and_block':{f'{rate}/{block}':group([r for r in rows if r['rate']==rate and r['block']==block]) for rate in (48000,96000) for block in (64,128)},
           'configurations':rows,
           'limitations':'One isolated paired run on this cloud CPU. Sound intentionally differs. Timings do not establish preference or host deadline guarantees.'}
source.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n')
lines = [f"{len(rows)} configurations; {sum(r['pairs'] for r in rows)} paired callbacks; one quiet run."]
lines.append(f"Median across configuration callback changes: {summary['overall']['median_of_configuration_changes_percent']:+.2f}% (positive means slower).")
for s,g in summary['by_scenario'].items():
    lines.append(f"{s}: {g['median_of_configuration_changes_percent']:+.2f}% median change; baseline medians {g['baseline_median_us_range'][0]:.2f}–{g['baseline_median_us_range'][1]:.2f} us, candidate {g['candidate_median_us_range'][0]:.2f}–{g['candidate_median_us_range'][1]:.2f} us.")
lines += ['', 'rate/block technique scenario room touch | before median/p95 us | after median/p95 us | median change']
for r in rows:
    lines.append(f"{r['rate']}/{r['block']} {r['technique']} {r['scenario']} {r['room']:.1f} {r['touch']:.2f} | {r['baseline_median_us']:.3f}/{r['baseline_p95_us']:.3f} | {r['candidate_median_us']:.3f}/{r['candidate_p95_us']:.3f} | {r['median_change_percent']:+.2f}%")
text='\n'.join(lines)+'\n'
source.with_suffix('.summary.txt').write_text(text)
print(text)
