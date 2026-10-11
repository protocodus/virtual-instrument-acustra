"""Summarize already-scored microphone experiments; never modifies model or audio."""
from pathlib import Path
import hashlib,json

HERE=Path(__file__).resolve().parent
NAMES=['measured_bridge','measured_blend','neck30_literal264','offhole45_literal264','free_neck30']
SHAPES=['parlor','auditorium','dreadnought','jumbo']
WINDOWS=['attack','early','held','late']
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def extent(rows,window,field):
    rows=[r for r in rows if field in r['windows'][window]]
    if not rows:return None
    lo=min(rows,key=lambda r:r['windows'][window][field]);hi=max(rows,key=lambda r:r['windows'][window][field])
    return {'minimum':{'key':lo['key'],'db':lo['windows'][window][field]},'maximum':{'key':hi['key'],'db':hi['windows'][window][field]}}
def fmt(x):return f"{x['minimum']['db']:+.2f} to {x['maximum']['db']:+.2f}"

out={'purpose':__doc__,'variants':[],'evidence_files':{},'limits':[
    'Research-only coefficient injection; no new production model or EQ section.',
    '600 cases per variant: original 408 plus 192 upper-note microphone cases. Piezo has 136 cases, not 200.',
    'Two native woods only: Spruce and Mahogany. The force-transfer sweep separately covers all three woods.',
    'Attack, early, held and late power are separate observations; held averages can conceal attack peaks.',
    'Spectral H1/H2-H3 reduction differs from reducing same-string note-neighbor excess.',
    'Only a declared single common distance gain is compensated in numerical reporting. Audio is untouched.',
    'No output spectral power is called internal modal energy or free decay; this is one pluck/control/rate grid.',
    'FRF M/F=-1,0,+1 brackets the normalized input axes; it is not a claim that every point occurs during every pluck.',
    'No scalar score establishes studio-position accuracy or listening preference.']}
for name in NAMES:
    path=HERE/(name+'-extended.json');d=json.loads(path.read_text());a=d['native_audio'];rows=[r for r in a['cases'] if r['capture']!='piezo']
    s={'name':name,'score_path':str(path),'score_sha256':sha(path),'scorer_sha256':d['scorer_sha256'],
       'baseline_report_sha256':a['baseline_report_sha256'],'candidate_report_sha256':a['candidate_report_sha256'],
       'override_sha256':d['force_transfer']['override_sha256'],'declared_common_microphone_gain':d['declared_common_microphone_gain'],
       'audio_case_count':len(a['cases']),'piezo_case_count':a['piezo_cases'],'piezo_bit_identical':a['all_piezo_bit_identical'],
       'windows_seconds':a['windows_seconds'],'held_flags':a['flagged_cases'],'targets':{},'low_note_metrics':{},'upper_note_metrics':{}}
    s['window_review_triggers']={}
    for w in WINDOWS:
        s['window_review_triggers'][w]={
            'reported_target_harmonic_balance_worse_over_1dB':[r['key'] for r in rows if r['reported_target'] and r['windows'][w]['h1_vs_h2h3_change_db']>1],
            'additional_neighbor_h1_dip_over_6dB':[r['key'] for r in rows if r['windows'][w].get('h1_neighbor_excess_change_db',0)<-6],
            'distance_matched_h1_loss_over_10dB':[r['key'] for r in rows if r['windows'][w]['h1_change_after_distance_match_db']<-10]}
    for shape in SHAPES:
        target=[r for r in rows if r['shape']==shape and r['reported_target']]
        if target:s['targets'][shape]={w:{field:extent(target,w,field) for field in [
            'h1_vs_h2h3_change_db','h1_neighbor_excess_before_db','h1_neighbor_excess_after_db','h1_neighbor_excess_change_db']} for w in WINDOWS}
    for label,predicate in [('low_note_metrics',lambda r:r['midi']<=48),('upper_note_metrics',lambda r:r['midi']>=49)]:
        subset=[r for r in rows if predicate(r)]
        s[label]={w:{field:extent(subset,w,field) for field in [
            'h1_vs_h2h3_change_db','h1_change_after_distance_match_db','rms_change_after_distance_match_db','h1_neighbor_excess_change_db']} for w in WINDOWS}
    fr=d['force_transfer']['rows'];s['force_transfer_case_count']=len(fr)
    s['largest_frf_distance_matched_loss']=min(fr,key=lambda r:r['maximum_loss_after_distance_match']['db'])
    s['largest_frf_added_local_dip']=min(fr,key=lambda r:r['largest_added_local_dip']['db'])
    out['variants'].append(s)
for p in [HERE/'score_variants.py',HERE/'check_scorer.py',HERE/'scorer-controls.json',Path(__file__),
          HERE.parents[1]/'studio-capture-research-20261011/physics/nbody-directivity.json']:
    out['evidence_files'][str(p)]={'sha256':sha(p),'bytes':p.stat().st_size}
(HERE/'comparison-summary.json').write_text(json.dumps(out,indent=2,allow_nan=False)+'\n')
md=['# Independent microphone-variant comparison','',
    'These are scratch experiments. The report retains audible tradeoffs rather than treating a reduced target-band ratio as acceptance. Each variant has 600 paired native clips; all 136 Piezo clips are byte-identical. The 192 extension clips are microphone-only. No per-note gain normalization was used.','',
    '## Target fundamental balance by time window','',
    'Changes are dB of H1 relative to mean power of H2 and H3; negative means less fundamental emphasis. Ranges span the reported target notes, both rendered woods, both microphone captures and available string routes. They do not measure same-note loudness or modal damping.','',
    '| Variant | Shape | Attack | Early | Held | Late |','|---|---|---:|---:|---:|---:|']
for s in out['variants']:
    for shape,windows in s['targets'].items():md.append('| '+s['name']+' | '+shape+' | '+' | '.join(fmt(windows[w]['h1_vs_h2h3_change_db']) for w in WINDOWS)+' |')
md+=['','Window bounds in seconds: '+json.dumps(out['variants'][0]['windows_seconds'])+'.',
     '', '## Interpreting resonance and cancellation','',
     'Parlor B1 has an attack H1 neighbor excess of about +5.6 to +5.8 dB in the baseline, followed by a much weaker held fundamental. A held-only metric cannot describe the initial boom. The windowed per-note JSON retains both facts.',
     '', 'The table below gives the worst distance-matched held H1 change among lower notes and the largest held spectral-balance increase among upper notes. A harmonic-ratio increase can be caused by higher-harmonic cancellation; it is not necessarily an equal increase in H1 level.',
     '', '| Variant | Worst low H1 change | Case | Largest upper H1/H2-H3 change | Case |', '|---|---:|---|---:|---|']
for s in out['variants']:
    low=s['low_note_metrics']['held']['h1_change_after_distance_match_db']['minimum'];hi=s['upper_note_metrics']['held']['h1_vs_h2h3_change_db']['maximum']
    md.append(f"| {s['name']} | {low['db']:+.2f} | {low['key']} | {hi['db']:+.2f} | {hi['key']} |")
md+=['','## Independent measured bound','',
    'NBody mic 4 changes the empirical low-air/plate-band power ratio relative to front mic 1 by about −2.1 to −6.7 dB across four classical-guitar strikes, recorder-reference choices and 100–200 ms windows. Mic 3 instead raises that ratio about +3.5 to +6.2 dB. This supports direction-dependent balance changes of several dB, but not a universal reduction, an identified neck-direction index, or a transfer function for the present g21 guitar. Source geometry is internally approximate, the records are short, and a license for shipping the raw NBody data was not found. No NBody coefficients are used in these variants.',
    '', '## Assessment','',
    '- Measured bridge is a real pressure-tap control, not a new studio position. It greatly lowers target fundamental balance but creates a Parlor F# low-note dip and large changes elsewhere.',
    '- Measured blend is a frequency-independent average of real pressure taps, not a new measured location. It lowers target balance but adds Jumbo upper-bass mono holes; its large harmonic-ratio increases sometimes reflect suppressed higher harmonics rather than equivalent fundamental gain.',
    '- Coupled neck30 gives moderate target-balance reduction but severely weakens Parlor open-bass fundamentals after distance matching. Its constrained A0 spatial relation does not reproduce the held-out measured tap well.',
    '- Coupled offhole45 has similar low-note cancellation plus stronger upper-bass emphasis. It is not a demonstrated solution.',
    '- Free neck30 fits the three measured taps more closely and has the smallest played low-note disruption. Yet its Parlor and Auditorium target balance is nearly unchanged in the held window and can increase on attack. It is the most defensible listening candidate of these projections, but not a demonstrated fix for the reported boom. A Parlor/Maple extreme force/moment FRF combination also develops a deep notch near 257 Hz; Maple was not part of the played grid.',
    '',
    'The free fit uses a rank-two source matrix with condition about 5.2–5.8. Training complex errors are about 2.1% for A0 heave and 6.0–6.5% for T1 heave; moment errors remain 15–23%. These are training residuals, not independent validation at 30 cm. The earlier constrained A0 held-out errors are roughly 60–92%. Neither model establishes the complete broadband studio field. All variants preserve existing source poles and mechanics by construction, separately checked by the parent runtime invariant harness.',
    '',
    'Recommendation: retain all five as documented listening experiments. None demonstrates both a consistent reduction of the reported attack/held boom and absence of relevant tradeoffs. A low flag count is not acceptance. The source model, missing studio measurements and user listening judgement remain separate questions.',
    '', '## Reproducibility and limits','',
    'comparison-summary.json includes exact score/report/override hashes, every target/window range, the most adverse low/upper-note cases and FRF extrema. The five *-extended.json files retain all individual native and force/moment cases. scorer-controls.json checks the independent rational transfer formula against the complex-residue form, an identity override and in-phase/anti-phase stereo controls.',
    '', *['- '+x for x in out['limits']], '']
(HERE/'comparison-summary.md').write_text('\n'.join(md))
print(json.dumps({'summary':str(HERE/'comparison-summary.json'),'variants':len(out['variants'])}))
