#!/usr/bin/env python3
"""Package frozen measurements and fair, global-gain listening comparisons."""
from pathlib import Path
import hashlib,json,math
import numpy as np
from scipy.io import wavfile
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
OUT=HERE/'presentation'
OUT.mkdir(exist_ok=True)
NAMES=['baseline','measured_bridge','neck30_literal264','offhole45_literal264','free_neck30','measured_blend']
LABELS=['Current','Measured bridge','Constrained neck 30 cm','Constrained off-hole 45 cm','Measured-phase neck 30 cm','Measured microphone mix']
COLORS=['#263238','#889099','#1881ad','#d47623','#7a50b1','#269b73']
SHAPES=['parlor','auditorium','jumbo']
TARGETS={'parlor':[47],'auditorium':[45],'jumbo':[40,41]}
PLAYS={'parlor':[(40,1),(42,1),(47,1),(59,5)],
       'auditorium':[(40,1),(45,1),(46,1),(56,4)],
       'jumbo':[(40,1),(41,1),(51,3),(53,3)]}
reports={n:json.loads((HERE/(n+'-extended')/'report.json').read_text()) for n in NAMES}
data={n:{(r['shape'],r['wood'],r['capture'],r['midi'],r['channel']):r for r in reports[n]['renders']}for n in NAMES}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def balance(r,window):
    h=r['windows'][window]['partial_power_dbfs']
    return h[0]-10*math.log10((10**(h[1]/10)+10**(h[2]/10))/2)
def pitch(m):return ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][m%12]+str(m//12-2)

# Independent of how loud a more distant microphone happens to be: a harmonic ratio.
fig,axes=plt.subplots(3,2,figsize=(13,10),sharex=True,sharey=True)
opens=(40,45,50,55,59,64)
for si,shape in enumerate(SHAPES):
    for wi,window in enumerate(['attack','held']):
        ax=axes[si,wi]
        ax.axhline(0,color=COLORS[0],linewidth=1.5,label=LABELS[0])
        for n,label,col in zip(NAMES[1:],LABELS[1:],COLORS[1:]):
            xs=[];ys=[]
            for midi in range(40,65):
                ch=max(c for c,o in enumerate(opens,1) if o<=midi)
                k=(shape,'spruce','mono_mic',midi,ch)
                if k not in data[n]:continue
                xs.append(midi);ys.append(balance(data[n][k],window)-balance(data['baseline'][k],window))
            ax.plot(xs,ys,'o-',markersize=3,linewidth=1.2,color=col,label=label,alpha=.9)
        for midi in TARGETS[shape]:ax.axvspan(midi-.20,midi+.20,color='#edc75b',alpha=.3)
        ax.set_title(shape.title()+' · '+('attack 20–200 ms' if window=='attack' else 'held 200–2800 ms'),fontsize=11,loc='left')
        ax.grid(axis='y',alpha=.2)
        ax.set_ylim(-22,22)
        ax.set_xlim(39.5,64.5)
        ax.spines[['top','right']].set_visible(False)
        if wi==0:ax.set_ylabel('Change in H1 / mean(H2,H3), dB')
for ax in axes[-1]:
    ax.set_xticks(list(range(40,65,2)),[pitch(n) for n in range(40,65,2)],rotation=35)
    ax.set_xlabel('Reason note labels · usual first-position string')
fig.suptitle('Capture variants change spectral balance; placement alone is not a demonstrated fix',fontsize=15,y=.99)
handles,labels=axes[0,0].get_legend_handles_labels()
fig.legend(handles,labels,loc='upper center',bbox_to_anchor=(.5,.956),ncol=3,frameon=False,fontsize=9)
fig.text(.03,.016,'Spruce / Mono mic / Finger v96 / 48 kHz / dry. Yellow = reported notes. Negative = less fundamental relative to H2–H3.\nNo per-note gain changes; ratios cancel nominal distance gain. These are partial spatial prototypes, not measured new studio positions.',fontsize=9)
fig.tight_layout(rect=[.015,.062,.99,.915])
fig.savefig(OUT/'resonance-comparison.png',dpi=160)
plt.close(fig)

# Audition-only trims: one RMS gain per variant over the entire fixed phrase set
# (both woods, all three shapes, both mic captures). No note-specific matching.
rate=48000;duration=1.0;gap=.18
phrases={};energies={}
for n in NAMES:
    power=0.;count=0
    for shape in SHAPES:
        for wood in ('spruce','mahogany'):
            for capture in ('stereo_mic','mono_mic'):
                pieces=[]
                for midi,ch in PLAYS[shape]:
                    r=data[n][shape,wood,capture,midi,ch]
                    p=HERE/(n+'-extended')/'audio'/(r['key']+'.f32')
                    assert sha(p)==r['audio_sha256']
                    x=np.fromfile(p,dtype='<f4').reshape(-1,2)[:rate].astype(float).copy()
                    # Identical audition tail fade; rendered analysis files are untouched.
                    fade=round(.02*rate);x[-fade:]*=np.linspace(1,0,fade)[:,None]
                    power+=float(np.sum(x*x));count+=x.size
                    pieces += [x,np.zeros((round(gap*rate),2))]
                phrases[n,shape,wood,capture]=np.concatenate(pieces)
    energies[n]=power/count
trims={n:math.sqrt(energies['baseline']/energies[n]) for n in NAMES}
maxpeak=max(float(np.max(np.abs(x)))*trims[n] for (n,*_),x in phrases.items())
headroom=min(1.,10**(-3/20)/maxpeak)
timings=[];files=[]
for shape in SHAPES:
    for wood in ('spruce','mahogany'):
        for capture in ('stereo_mic','mono_mic'):
            pieces=[];at=0.;times=[]
            for n,label in zip(NAMES,LABELS):
                x=phrases[n,shape,wood,capture]*trims[n]*headroom
                times.append({'variant':n,'label':label,'start_seconds':at,'end_seconds':at+len(x)/rate})
                pieces+=[x,np.zeros((rate,2))];at+=len(x)/rate+1
            x=np.concatenate(pieces).astype(np.float32)
            assert np.isfinite(x).all() and np.max(np.abs(x))<1
            p=OUT/f'{shape}-{wood}-{capture}-comparison.wav'
            if p.exists():raise RuntimeError('Refusing existing audition '+str(p))
            wavfile.write(p,rate,x)
            files.append({'file':str(p),'sha256':sha(p),'shape':shape,'wood':wood,'capture':capture,'notes':[[pitch(m),m,c] for m,c in PLAYS[shape]],'segments':times,'peak_dbfs':float(20*np.log10(np.max(np.abs(x))))})
manifest={'scope':'Listening comparisons, not metric inputs. No human listening acceptance claimed.',
          'global_rms_policy':'One scalar per variant calculated over all36? fixed phrases: three shapes × two woods × two microphone captures × four notes. Same scalar on every note, shape, wood and capture for that variant. No per-note normalization.',
          'phrase_count_per_variant':12,'notes_per_phrase':4,
          'variant_gains_db':{n:20*math.log10(g) for n,g in trims.items()},
          'common_headroom_gain_db':20*math.log10(headroom),'clip_seconds':duration,'note_gap_seconds':gap,'variant_gap_seconds':1,
          'tail_fade_seconds':.02,'format':'48kHz stereo IEEE float32 WAV','files':files,
          'source_report_sha256':{n:sha(HERE/(n+'-extended')/'report.json') for n in NAMES},'script_sha256':sha(Path(__file__))}
manifest['global_rms_policy']=manifest['global_rms_policy'].replace('all36? fixed phrases:','the fixed phrase set:')
(OUT/'audition-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps({'directory':str(OUT),'files':len(files),'variant_gains_db':manifest['variant_gains_db'],'headroom_db':manifest['common_headroom_gain_db']},indent=2))
