#!/usr/bin/env python3
"""Package fixed-mixture native measurements and globally gain-matched auditions."""
from pathlib import Path
import hashlib,json,math
import numpy as np
from scipy.io import wavfile
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];OLD=ROOT/'build-audit/studio-mic-variants'
NAMES=['baseline','balanced_all','measured_continuity','equal_five']
LABELS=['Current','Balanced blend','Measured-response blend','Equal mix of five']
DIRS={n:(OLD/'baseline-extended' if n=='baseline' else HERE/n)for n in NAMES}
OUT=HERE/'presentation';OUT.mkdir(exist_ok=False)
SHAPES=['parlor','auditorium','jumbo'];TARGETS={'parlor':[47],'auditorium':[45],'jumbo':[40,41]}
PLAYS={'parlor':[(40,1),(42,1),(47,1),(59,5)],'auditorium':[(40,1),(45,1),(46,1),(56,4)],'jumbo':[(40,1),(41,1),(51,3),(53,3)]}
reports={n:json.loads((d/'report.json').read_text())for n,d in DIRS.items()}
data={n:{r['key']:r for r in rp['renders']}for n,rp in reports.items()}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def balance(r,w):
 h=r['windows'][w]['partial_power_dbfs'];return h[0]-10*np.log10((10**(h[1]/10)+10**(h[2]/10))/2)
def pitch(m):return ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][m%12]+str(m//12-2)
summary={}
for name in NAMES[1:]:
 score=json.loads((DIRS[name]/'score.json').read_text());rows=score['native_audio']['cases'];sr={r['key']:r for r in rows}
 vals={};peers={}
 for s in SHAPES:
  cases=[r for r in rows if r['shape']==s and r['reported_target'] and r['capture']!='piezo']
  vals[s]={}
  for w in ['attack','early','held','late']:
   vals[s][w]={}
   for metric in ['h1_vs_h2h3_change_db','h1_change_db','rms_change_db','h1_neighbor_excess_change_db']:
    v=[r['windows'][w][metric]for r in cases if metric in r['windows'][w]]
    vals[s][w][metric]={'min':min(v),'median':float(np.median(v)),'max':max(v)}if v else None
 collateral=[]
 for row in reports[name]['renders']:
  if row['capture']=='piezo':continue
  for window in ['attack','early','held']:
   for h in range(3):
    before=data['baseline'][row['key']]['windows'][window]['partial_power_dbfs'][h]
    after=row['windows'][window]['partial_power_dbfs'][h]
    collateral.append({'key':row['key'],'window':window,'harmonic':h+1,'before_dbfs':before,'after_dbfs':after,'change_db':after-before})
 summary[name]={'weights':reports[name]['experiment']['weights'],'target_changes':vals,'candidate_cases':len(rows),'piezo_cases':score['native_audio']['piezo_cases'],'all_piezo_bit_identical':score['native_audio']['all_piezo_bit_identical'],
  'all_finite':all(r['all_finite']for r in reports[name]['renders']),'full_scale_samples':sum(r['samples_at_full_scale']for r in reports[name]['renders']),
  'worst_harmonic_losses':{str(h):min([r for r in collateral if r['harmonic']==h],key=lambda r:r['change_db'])for h in range(1,4)},
  'largest_harmonic_gains':{str(h):max([r for r in collateral if r['harmonic']==h],key=lambda r:r['change_db'])for h in range(1,4)},
  'worst_added_frf_dip_by_wood':{wood:min([r for r in score['force_transfer']['rows']if r['wood']==wood],key=lambda r:r['largest_added_local_dip']['db'])for wood in ['spruce','mahogany','maple']},
  'report_sha256':sha(DIRS[name]/'report.json'),'score_sha256':sha(DIRS[name]/'score.json')}
(HERE/'native-summary.json').write_text(json.dumps(summary,indent=2)+'\n')

# Illustrate attack/held neighborhood, with all points on their original levels.
fig,ax=plt.subplots(3,2,figsize=(11,9),sharex=True,sharey=True)
colors=['#263238','#217c9b','#7b57b3','#b08b3a'];opens=[40,45,50,55,59,64]
for si,s in enumerate(SHAPES):
 for wi,w in enumerate(['attack','held']):
  a=ax[si,wi];a.axhline(0,color=colors[0],linewidth=1.4,label=LABELS[0])
  for n,l,c in zip(NAMES[1:],LABELS[1:],colors[1:]):
   xs=[];ys=[]
   for midi in range(40,65):
    ch=max(c for c,o in enumerate(opens,1)if o<=midi);key=f'{s}-spruce-mono_mic-m{midi}-s{ch}'
    if key in data[n]:xs.append(midi);ys.append(balance(data[n][key],w)-balance(data['baseline'][key],w))
   a.plot(xs,ys,'o-',color=c,linewidth=1.3,markersize=3,label=l)
  for midi in TARGETS[s]:a.axvspan(midi-.2,midi+.2,color='#edc75b',alpha=.25)
  a.set_title(s.title()+' · '+w,loc='left',fontsize=11);a.grid(axis='y',alpha=.2);a.spines[['top','right']].set_visible(False)
  if wi==0:a.set_ylabel('Change in H1 / mean(H2,H3), dB')
  a.set_xlim(39.5,64.5);a.set_ylim(-13,8)
for a in ax[-1]:a.set_xticks(range(40,65,2),[pitch(n)for n in range(40,65,2)],rotation=35);a.set_xlabel('Reason note labels · first-position string')
fig.suptitle('Fixed capture combinations: smaller tradeoffs than equal mixing',y=.99,fontsize=15)
h,l=ax[0,0].get_legend_handles_labels();fig.legend(h,l,loc='upper center',bbox_to_anchor=(.5,.955),ncol=2,frameon=False)
fig.text(.03,.015,'Spruce / Mono / Finger v96. Yellow = reported notes. Attack 20–200 ms; held 200–2800 ms.\nNegative = less fundamental relative to H2–H3; this does not measure removal of a narrow neighbor peak.',fontsize=9)
fig.tight_layout(rect=[.01,.06,.99,.905]);fig.savefig(OUT/'combination-resonances.png',dpi=160);plt.close(fig)

# A single gain for each variant, estimated from the same12phrases, not each note.
rate=48000;phrases={};energy={}
for n in NAMES:
 total=0.;count=0
 for s in SHAPES:
  for wood in ['spruce','mahogany']:
   for cap in ['mono_mic','stereo_mic']:
    pieces=[]
    for midi,ch in PLAYS[s]:
     key=f'{s}-{wood}-{cap}-m{midi}-s{ch}';p=DIRS[n]/'audio'/(key+'.f32');assert sha(p)==data[n][key]['audio_sha256']
     x=np.fromfile(p,dtype='<f4').reshape(-1,2)[:rate].astype(float).copy();x[-960:]*=np.linspace(1,0,960)[:,None]
     total+=np.sum(x*x);count+=x.size;pieces.extend([x,np.zeros((8640,2))])
    phrases[n,s,wood,cap]=np.concatenate(pieces)
 energy[n]=total/count
trims={n:math.sqrt(energy['baseline']/energy[n])for n in NAMES}
peak=max(np.max(abs(x))*trims[n]for(n,*_),x in phrases.items());common=min(1,10**(-3/20)/peak)
files=[]
for s in SHAPES:
 for wood in ['spruce','mahogany']:
  for cap in ['mono_mic','stereo_mic']:
   for order,suffix in [(NAMES,'all-combinations'),(NAMES[:2],'current-vs-balanced')]:
    pieces=[];segments=[];at=0.
    for n in order:
     x=phrases[n,s,wood,cap]*trims[n]*common;segments.append({'variant':n,'starts':at,'ends':at+len(x)/rate});at+=len(x)/rate+1
     pieces.extend([x,np.zeros((rate,2))])
    x=np.concatenate(pieces).astype(np.float32);assert np.isfinite(x).all()and np.max(abs(x))<1
    p=OUT/f'{s}-{wood}-{cap}-{suffix}.wav';wavfile.write(p,rate,x)
    files.append({'file':str(p),'sha256':sha(p),'shape':s,'wood':wood,'capture':cap,'order':order,'segments':segments,'peak_dbfs':float(20*np.log10(np.max(abs(x)))),'notes':[[pitch(m),m,ch]for m,ch in PLAYS[s]]})
manifest={'policy':'One scalar gain per variant across12phrases (3shapes x2woods x2captures), never per-note normalization. Identical20ms audition tail fade; measurement inputs unchanged. No listening acceptance claimed.',
 'variant_gains_db':{n:20*math.log10(v)for n,v in trims.items()},'common_gain_db':20*math.log10(common),'sample_rate':rate,'files':files,'source_reports':{n:sha(DIRS[n]/'report.json')for n in NAMES},'script_sha256':sha(Path(__file__))}
(OUT/'audition-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'audio_files':len(files),'gains_db':manifest['variant_gains_db'],'summary':str(HERE/'native-summary.json')},indent=2))
