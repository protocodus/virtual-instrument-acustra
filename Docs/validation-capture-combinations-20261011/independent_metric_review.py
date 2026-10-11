"""Independent reporting/indexing audit, with direct waveform mixture controls.

Reads the completed screen. Never modifies its input matrices, source, or audio.
"""
from pathlib import Path
import hashlib,json
import numpy as np
from scipy.signal import periodogram

HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
PREV=ROOT/'build-audit/studio-mic-variants';SCORES=PREV/'scoring'
meta=json.loads((HERE/'cross-powers.json').read_text())
screen=json.loads((HERE/'screen.json').read_text())
M=np.load(HERE/'cross-powers.npz')['powers']
names=meta['basis'];gains=meta['nominal_gain_compensation'];windows=list(meta['windows'])
cases=meta['cases'];index={r['key']:i for i,r in enumerate(cases)}
assert names==screen['basis'] and gains==screen['compensations']
assert M.shape==(len(cases),4,6,6,6)
assert len(index)==len(cases)==464
all_weights=np.array([[r['weights'][n] for n in names] for r in screen['results']])
assert np.min(all_weights)>=0 and np.max(abs(all_weights.sum(axis=1)-1))<1e-12
assert np.isfinite(M).all()
symmetry=float(np.max(abs(M-M.swapaxes(-1,-2))))
eig=np.linalg.eigvalsh(M)
eig_floor=float(eig.min());max_power=float(np.max(np.diagonal(M,axis1=-2,axis2=-1)))
assert symmetry<1e-14 and eig_floor > -1e-12*max_power
pure={r['name']:r for r in screen['results'] if r['name'] in names}
prior={n:json.loads((SCORES/(n+'-extended.json')).read_text()) for n in names if n!='baseline'}
baseline_report=json.loads((PREV/'baseline-extended/report.json').read_text())
baseline_rows={r['key']:r for r in baseline_report['renders']}
all_reports={n:json.loads((PREV/(n+'-extended')/'report.json').read_text()) for n in names}
errors={};neighbors_checked=0
for j,n in enumerate(names):
    source={r['key']:r for r in prior[n]['native_audio']['cases']} if n!='baseline' else None
    expected=[];window_arrays=[];nb=[];groups={};targets=[];upper=[]
    err_h=err_bal=err_rms=err_neighbor=0.
    for ci,r in enumerate(cases):
        wr=[]
        for wi,w in enumerate(windows):
            actual=10*np.log10(np.maximum(M[ci,wi,:,j,j],1e-30))
            base=10*np.log10(np.maximum(M[ci,wi,:,0,0],1e-30))
            hd=float(actual[0]-base[0]);rd=float(actual[3]-base[3])
            bal=lambda x:x[0]-10*np.log10((10**(x[1]/10)+10**(x[2]/10))/2)
            bd=float(bal(actual)-bal(base))
            if source:
                old=source[r['key']]['windows'][w]
                eh=old['h1_change_after_distance_match_db'];er=old['rms_change_after_distance_match_db'];eb=old['h1_vs_h2h3_change_db']
            else:eh=er=eb=0
            err_h=max(err_h,abs(hd-eh));err_bal=max(err_bal,abs(bd-eb));err_rms=max(err_rms,abs(rd-er))
            wr.append({'h':eh,'r':er,'b':eb})
        expected.append(wr)
    lookup={(r['shape'],r['wood'],r['capture'],r['midi'],r['channel']):i for i,r in enumerate(cases)}
    for ci,r in enumerate(cases):
        stem=(r['shape'],r['wood'],r['capture']);lo=lookup.get((*stem,r['midi']-1,r['channel']));hi=lookup.get((*stem,r['midi']+1,r['channel']))
        if lo is not None and hi is not None:
            for wi,w in enumerate(windows):
                scalar=expected[ci][wi]['h']-(expected[lo][wi]['h']+expected[hi][wi]['h'])/2
                old=source[r['key']]['windows'][w]['h1_neighbor_excess_change_db'] if source else 0.
                err_neighbor=max(err_neighbor,abs(scalar-old));neighbors_checked+=1
                if wi<3:nb.append(scalar)
        target=r['midi'] in {'parlor':[47],'auditorium':[45],'jumbo':[40,41],'dreadnought':[]}[r['shape']]
        if target:
            targets.extend(expected[ci][wi]['b'] for wi in range(2))
            for wi,w in enumerate(windows):groups.setdefault((r['shape'],r['capture'],w),[]).append(expected[ci][wi]['b'])
    want={'worst_target_attack_early_balance_change_db':max(targets),
        'worst_group_target_attack_early_median_db':max(float(np.median(v)) for (s,c,w),v in groups.items() if w in ['attack','early']),
        'all_target_attack_early_median_db':float(np.median(targets)),
        'minimum_h1_change_db':min(x[wi]['h'] for x in expected for wi in range(3)),
        'maximum_upper_h1_change_db':max(expected[ci][wi]['h'] for ci,r in enumerate(cases) if r['midi']>=49 for wi in range(3)),
        'worst_neighbor_dip_change_db':min(nb),'worst_neighbor_peak_change_db':max(nb),
        'maximum_rms_loss_db':min(x[wi]['r'] for x in expected for wi in range(3))}
    metric_errors={k:abs(v-pure[n]['metrics'][k]) for k,v in want.items()}
    group_error=max(abs(float(f(vals))-pure[n]['target_groups'][s+'/'+c][w][label]) for (s,c,w),vals in groups.items() for label,f in [('min',min),('max',max),('median',np.median)])
    assert max([err_h,err_bal,err_rms,err_neighbor,group_error,*metric_errors.values()])<1e-10
    errors[n]={'all_case_window_h1_max_db_error':err_h,'harmonic_balance_max_db_error':err_bal,'rms_max_db_error':err_rms,
       'neighbor_scalar_vs_previous_scorer_max_db_error':err_neighbor,'aggregate_metric_max_db_error':max(metric_errors.values()),'group_max_db_error':group_error}

# Direct SciPy periodograms, independent from the Gram-construction helper.
# One complete construction/capture cross-section; selected notes include each
# reported target region and upper-bass interference. Eight pure/mixed controls.
selected=[]
target_note={'parlor':47,'auditorium':45,'dreadnought':53,'jumbo':41}
for shape in ['parlor','auditorium','dreadnought','jumbo']:
    for wood in ['spruce','mahogany']:
        for capture in ['mono_mic','stereo_mic']:
            selected.append(next(i for i,r in enumerate(cases) if r['shape']==shape and r['wood']==wood and r['capture']==capture and r['midi']==target_note[shape]))
test_weights=[*np.eye(6),np.array([.6,.2,0,0,.1,.1]),np.array([0,.2,.2,.2,.2,.2])]
direct_err=0.;direct_count=0
for ci in selected:
    r=cases[ci];basis=[]
    for n,g in zip(names,gains):
        path=PREV/(n+'-extended')/'audio'/(r['key']+'.f32');raw=path.read_bytes()
        old=next(x for x in all_reports[n]['renders'] if x['key']==r['key'])
        assert hashlib.sha256(raw).hexdigest()==old['audio_sha256']
        basis.append(np.frombuffer(raw,dtype='<f4').reshape(-1,2).astype(float)*g)
    basis=np.stack(basis);f0=baseline_rows[r['key']]['nominal_fundamental_hz']
    for wi,w in enumerate(windows):
        a,b=meta['windows'][w];x=basis[:,round(a*48000):round(b*48000),:];count=x.shape[1];nfft=1<<(count-1).bit_length()
        for weights in test_weights:
            mixed=np.einsum('i,itc->tc',weights,x)
            f,p=periodogram(mixed,fs=48000,window='hann',nfft=nfft,detrend=False,axis=0,scaling='density')
            _,pm=periodogram(mixed.mean(axis=1),fs=48000,window='hann',nfft=nfft,detrend=False,scaling='density')
            df=f[1]-f[0];v=[]
            for harmonic in range(1,4):
                half=max(2*48000/count,f0*harmonic*.025);mask=(f>=f0*harmonic-half)&(f<f0*harmonic+half)
                v.append(float(np.mean(np.sum(p[mask],axis=0)*df)))
            v.append(float(np.mean(mixed*mixed)));mask=(f>=80)&(f<300)
            v.append(float(np.mean(np.sum(p[mask],axis=0)*df)));v.append(float(np.sum(pm[mask])*df))
            gram=np.array([weights@M[ci,wi,feature]@weights for feature in range(6)])
            error=np.max(abs(10*np.log10(np.maximum(gram,1e-30))-10*np.log10(np.maximum(v,1e-30))))
            direct_err=max(direct_err,float(error));direct_count+=6
assert direct_err<1e-9,direct_err
mixture_checks=[]
for name in ['grid_2461','baseline0.75_bridge','equal_five','equal_six']:
    candidate=next(r for r in screen['results'] if r['name']==name)
    weights=np.array([candidate['weights'][n] for n in names])
    # Direct indexed contraction, independent of the screen's flatten/reshape.
    power=np.einsum('i,cwfij,j->cwf',weights,M,weights,optimize=True)
    db=10*np.log10(np.maximum(power,1e-30));bdb=10*np.log10(np.maximum(M[...,0,0],1e-30))
    change=db-bdb;hc={r['key']:change[i,:,0] for i,r in enumerate(cases)}
    bchange=(db[:,:,0]-10*np.log10((power[:,:,1]+power[:,:,2])/2))-(bdb[:,:,0]-10*np.log10((10**(bdb[:,:,1]/10)+10**(bdb[:,:,2]/10))/2))
    neighbors=[];target=[];groups={};upper=[]
    lookup={(r['shape'],r['wood'],r['capture'],r['midi'],r['channel']):r['key'] for r in cases}
    for i,r in enumerate(cases):
        stem=(r['shape'],r['wood'],r['capture']);lo=lookup.get((*stem,r['midi']-1,r['channel']));hi=lookup.get((*stem,r['midi']+1,r['channel']))
        if lo and hi:
            for wi in range(3):neighbors.append((float(hc[r['key']][wi]-(hc[lo][wi]+hc[hi][wi])/2),r['key'],windows[wi]))
        if r['midi']>=49:upper.extend(change[i,:3,0])
        if r['midi'] in {'parlor':[47],'auditorium':[45],'jumbo':[40,41],'dreadnought':[]}[r['shape']]:
            target.extend(bchange[i,:2])
            for wi in range(2):groups.setdefault((r['shape'],r['capture'],wi),[]).append(bchange[i,wi])
    expected={'worst_target_attack_early_balance_change_db':max(target),
        'worst_group_target_attack_early_median_db':max(float(np.median(v)) for v in groups.values()),
        'all_target_attack_early_median_db':float(np.median(target)),
        'minimum_h1_change_db':float(np.min(change[:,:3,0])),
        'maximum_upper_h1_change_db':max(upper),
        'worst_neighbor_dip_change_db':min(x[0] for x in neighbors),
        'worst_neighbor_peak_change_db':max(x[0] for x in neighbors),
        'minimum_mono_retention_db':float(np.min(db[:,:3,5]-db[:,:3,4])),
        'worst_mono_retention_change_db':float(np.min(change[:,:3,5]-change[:,:3,4])),
        'maximum_rms_loss_db':float(np.min(change[:,:3,3]))}
    err=max(abs(float(v)-candidate['metrics'][k]) for k,v in expected.items());assert err<1e-10,err
    mixture_checks.append({'name':name,'all_aggregate_metrics_maximum_db_error':err,
        'worst_neighbor_dip':min(neighbors),'worst_neighbor_peak':max(neighbors)})
receipt={'purpose':__doc__,'basis_cases':errors,'scalar_neighbor_comparisons':neighbors_checked,
    'matrix_maximum_asymmetry':symmetry,'minimum_eigenvalue':eig_floor,'largest_diagonal_power':max_power,
    'all_weights_nonnegative_and_sum_one':True,'direct_waveform_cases':[cases[i]['key'] for i in selected],
    'direct_controls_per_case':len(test_weights),'direct_power_comparisons':direct_count,'direct_scipy_vs_gram_max_db_error':direct_err,
    'independent_mixture_contraction_checks':mixture_checks,
    'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'screen_sha256':hashlib.sha256((HERE/'screen.json').read_bytes()).hexdigest(),
    'cross_power_sha256':hashlib.sha256((HERE/'cross-powers.npz').read_bytes()).hexdigest()}
(HERE/'independent-metric-review.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
