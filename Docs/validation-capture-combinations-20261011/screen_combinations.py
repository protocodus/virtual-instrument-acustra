#!/usr/bin/env python3
"""Phase-aware screening of constant observation mixtures; no product edits.

All inputs share the same frozen instrument and alignment. The dry output stays
below the limiter knee. Each cross-power matrix retains the cross terms lost
by averaging magnitude or decibel spectra. This is a tradeoff search on a known
grid, not independent validation or measured new microphone placement.
"""
from pathlib import Path
import argparse, concurrent.futures, hashlib, itertools, json, time
import numpy as np
from scipy.signal.windows import hann

HERE=Path(__file__).resolve().parent; ROOT=HERE.parents[1]
PREV=ROOT/'build-audit/studio-mic-variants'
NAMES=['baseline','measured_bridge','neck30_literal264','offhole45_literal264','free_neck30','measured_blend']
GAINS=np.array([1,1,3,4.5,3,1.],float)
WINDOWS={'attack':(.02,.20),'early':(.15,.65),'held':(.20,2.80),'late':(1.90,2.80)}
FEATURES=['H1','H2','H3','rms','low80_300','mid80_300']
RATE=48000
TARGETS={'parlor':[47],'auditorium':[45],'jumbo':[40,41],'dreadnought':[]}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def reports():return {n:json.loads((PREV/(n+'-extended')/'report.json').read_text()) for n in NAMES}
def cross_spectral(X,mask,n,win):
    # X: basis x bin x stereo channel. Positive frequencies are doubled.
    Z=X[:,mask,:].reshape(6,-1)
    return np.real(Z.conj()@Z.T)/(np.sum(win*win)*n)
def build():
    out=HERE/'cross-powers.npz'
    if out.exists():raise RuntimeError('Refusing existing '+str(out))
    rp=reports(); maps={n:{r['key']:r for r in rp[n]['renders']}for n in NAMES}
    cases=[r for r in rp['baseline']['renders'] if r['capture']!='piezo']
    def run(r):
        xx=[]
        for n,g in zip(NAMES,GAINS):
            p=PREV/(n+'-extended')/'audio'/(r['key']+'.f32')
            b=p.read_bytes(); assert hashlib.sha256(b).hexdigest()==maps[n][r['key']]['audio_sha256']
            x=np.frombuffer(b,dtype='<f4').reshape(-1,2).astype(float)*g
            assert np.isfinite(x).all() and np.max(abs(x))<.89125094
            xx.append(x)
        full=np.stack(xx);mat=[]; errors=[];f0=r['nominal_fundamental_hz']
        for name,(a,b) in WINDOWS.items():
            x=full[:,round(a*RATE):round(b*RATE),:]; count=x.shape[1]
            n=1<<(count-1).bit_length(); win=hann(count,sym=False)
            X=np.fft.rfft(x*win[None,:,None],n=n,axis=1);f=np.fft.rfftfreq(n,1/RATE)
            q=[]
            for h in range(1,4):
                half=max(2*RATE/count,f0*h*.025)
                q.append(cross_spectral(X,(f>=f0*h-half)&(f<f0*h+half),n,win))
            flat=x.reshape(6,-1);q.append(flat@flat.T/flat.shape[1])
            mask=(f>=80)&(f<300);q.append(cross_spectral(X,mask,n,win))
            # For the stereo mean, compensate for the doubled-channel convention.
            mono=X.mean(axis=2,keepdims=True)
            q.append(cross_spectral(mono,mask,n,win)*2)
            q=np.stack(q);mat.append(q)
            for j,namebasis in enumerate(NAMES):
                source=maps[namebasis][r['key']]['windows'][name]
                expected=np.array(source['partial_power_dbfs'][:3]+[source['rms_dbfs']])+20*np.log10(GAINS[j])
                actual=10*np.log10(np.maximum(q[:4,j,j],1e-30)); errors.append(np.max(abs(expected-actual)))
        return np.stack(mat),max(errors)
    start=time.monotonic(); mats=[]; errors=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        for i,(m,e) in enumerate(pool.map(run,cases),1):
            mats.append(m);errors.append(e)
            if i%32==0:print(i,len(cases),round(time.monotonic()-start,1),'s',flush=True)
    assert max(errors)<1e-5,max(errors)
    np.savez_compressed(out,powers=np.stack(mats))
    receipt={'basis':NAMES,'nominal_gain_compensation':GAINS.tolist(),'windows':WINDOWS,'features':FEATURES,
      'cases':[{k:r[k] for k in ('key','shape','wood','capture','midi','channel')}for r in cases],
      'diagonal_vs_previous_metrics_max_db_error':max(errors),'all_source_audio_hashes_verified':True,
      'report_hashes':{n:sha(PREV/(n+'-extended')/'report.json')for n in NAMES},'matrix_sha256':sha(out),'script_sha256':sha(Path(__file__))}
    (HERE/'cross-powers.json').write_text(json.dumps(receipt,indent=2)+'\n');print('cross powers complete',max(errors),flush=True)
def lattice(total,k,prefix=()):
    if k==1:yield prefix+(total,)
    else:
        for n in range(total+1):yield from lattice(total-n,k-1,prefix+(n,))
def screen():
    meta=json.loads((HERE/'cross-powers.json').read_text());M=np.load(HERE/'cross-powers.npz')['powers'];rows=meta['cases'];C=len(rows)
    identity=np.eye(6); weights=[*identity]; names=NAMES.copy()
    proposals={
      'equal_five':[0,.2,.2,.2,.2,.2],
      'free75_mix25':[0,0,0,0,.75,.25], 'free50_mix50':[0,0,0,0,.5,.5],
      'free75_bridge25':[0,.25,0,0,.75,0], 'mix75_bridge25':[0,.25,0,0,0,.75],
      'baseline50_mix50':[.5,0,0,0,0,.5],
      'baseline50_free25_bridge12_mix12':[.5,.125,0,0,.25,.125]}
    proposals['equal_six']=[1/6]*6
    for alpha in (.5,.625,.75,.875):
        for field,b in [('bridge',1),('measured_mix',5)]:
            w=np.zeros(6);w[0]=alpha;w[b]=1-alpha;proposals[f'baseline{alpha}_{field}']=w
        w=np.full(6,(1-alpha)/5);w[0]=alpha;proposals[f'baseline{alpha}_all5']=w
    for alpha in (.5,.625,.75):
        for frac in (1/3,2/3):
            w=np.zeros(6);w[0]=alpha;w[1]=(1-alpha)*frac;w[4]=(1-alpha)*(1-frac)
            proposals[f'baseline{alpha}_bridgefrac{frac}']=w
    for name,w in proposals.items():names.append(name);weights.append(w)
    for i,w in enumerate(lattice(10,6)):names.append('grid_'+str(i));weights.append(np.array(w)/10)
    W=np.array(weights);base=10*np.log10(np.maximum(M[...,0,0],1e-30))
    targ=np.array([r['midi'] in TARGETS[r['shape']] for r in rows]);upper=np.array([r['midi']>=49 for r in rows])
    idx={(r['shape'],r['wood'],r['capture'],r['midi'],r['channel']):i for i,r in enumerate(rows)}
    neighbors=[]
    for i,r in enumerate(rows):
        s=(r['shape'],r['wood'],r['capture']);n=r['midi'];ch=r['channel']
        if (*s,n-1,ch) in idx and (*s,n+1,ch) in idx:neighbors.append((i,idx[(*s,n-1,ch)],idx[(*s,n+1,ch)]))
    ii,ll,hh=np.array(neighbors).T
    groups={(s,c):np.array([r['shape']==s and r['capture']==c and t for r,t in zip(rows,targ)])for s in ['parlor','auditorium','jumbo']for c in ['stereo_mic','mono_mic']}
    output=[]
    flat=M.reshape(-1,36)
    for start in range(0,len(W),128):
        wb=W[start:start+128];outer=np.einsum('bi,bj->bij',wb,wb).reshape(-1,36)
        power=np.maximum((flat@outer.T).T.reshape(-1,C,4,6),1e-30);d=10*np.log10(power);delta=d-base
        balance=d[...,0]-10*np.log10((power[...,1]+power[...,2])/2)
        basebal=base[...,0]-10*np.log10((10**(base[...,1]/10)+10**(base[...,2]/10))/2)
        bd=balance-basebal
        hdelta=delta[...,0]
        neighbor=hdelta[:,ii,:]-.5*(hdelta[:,ll,:]+hdelta[:,hh,:])
        if start==0:
            assert np.max(abs(neighbor[0]))<1e-10, 'Identity mixture changed neighbor contrast'
        for j,w in enumerate(wb):
            groups_out={s+'/'+c:{window:{'median':float(np.median(bd[j,mask,wi])),'min':float(np.min(bd[j,mask,wi])),'max':float(np.max(bd[j,mask,wi]))}for wi,window in enumerate(WINDOWS)}for (s,c),mask in groups.items()}
            t=bd[j,targ,:2];n=neighbor[j,:,:3];h=delta[j,:,:3,0];ug=delta[j,upper,:3,0]
            worst_group=max(v[win]['median']for v in groups_out.values()for win in ['attack','early'])
            metrics={'worst_target_attack_early_balance_change_db':float(np.max(t)),
              'worst_group_target_attack_early_median_db':float(worst_group),
              'all_target_attack_early_median_db':float(np.median(t)),
              'minimum_h1_change_db':float(np.min(h)), 'maximum_upper_h1_change_db':float(np.max(ug)),
              'worst_neighbor_dip_change_db':float(np.min(n)), 'worst_neighbor_peak_change_db':float(np.max(n)),
              'minimum_mono_retention_db':float(np.min(d[j,:,:3,5]-d[j,:,:3,4])),
              'worst_mono_retention_change_db':float(np.min(delta[j,:,:3,5]-delta[j,:,:3,4])),
              'maximum_rms_loss_db':float(np.min(delta[j,:,:3,3]))}
            flags=[]
            if metrics['worst_target_attack_early_balance_change_db']>.5:flags.append('target_worse_over_0.5')
            if metrics['minimum_h1_change_db']< -6:flags.append('h1_loss_over6')
            if metrics['maximum_upper_h1_change_db']>3:flags.append('upper_h1_gain_over3')
            if metrics['worst_neighbor_dip_change_db']< -3:flags.append('neighbor_dip_over3')
            if metrics['worst_neighbor_peak_change_db']>3:flags.append('neighbor_peak_over3')
            if metrics['minimum_mono_retention_db']< -6:flags.append('mono_sum_loss_over6')
            if metrics['worst_mono_retention_change_db']< -3:flags.append('added_mono_sum_loss_over3')
            output.append({'name':names[start+j],'weights':dict(zip(NAMES,map(float,w))),'metrics':metrics,'flags':flags,'target_groups':groups_out})
    output.sort(key=lambda r:(bool(r['flags']),r['metrics']['worst_group_target_attack_early_median_db']))
    result={'scope':'In-sample phase-aware tradeoff screen, not physical placement or listening acceptance. All fixed weights sum one after declared nominal gain compensation. Native re-render required.',
       'basis':NAMES,'compensations':GAINS.tolist(),'grid':'All 3003 six-basis simplex weights in steps of 0.1 plus controls and declared proposals.',
       'screen_bounds':{'h1_loss_db':6,'upper_h1_gain_db':3,'neighbor_change_db':3,'target_worsening_db':.5,'mono_retention_db':-6},
       'gate_windows':'attack, early, held for collateral changes; attack and early for target aim; late reported only.',
       'aspirational_target':'At least 2dB median reduction in EACH reported shape/capture group during both attack and early, without excluded tradeoffs.',
       'count':len(output),'unflagged_count':sum(not r['flags']for r in output),'cross_power_sha256':sha(HERE/'cross-powers.npz'),'script_sha256':sha(Path(__file__)), 'results':output}
    (HERE/'screen.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'count':len(output),'unflagged':result['unflagged_count'],'best':[{k:r[k]for k in ['name','weights','metrics','flags']}for r in output[:10]],'named':[{k:r[k]for k in ['name','metrics','flags']}for r in output if not r['name'].startswith('grid_')]},indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('action',choices=['build','screen']);a=p.parse_args();globals()[a.action]()
