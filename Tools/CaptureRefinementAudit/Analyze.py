import json,sys,hashlib
from pathlib import Path
import numpy as np
from scipy import signal
from scipy.io import wavfile
root=Path(sys.argv[1]).resolve()
bands=[(0,35),(35,70),(70,200),(200,800),(800,1100),(1100,1400),(1400,1800),(1800,4000),(4000,8000),(8000,16000)]
def read(p): return np.fromfile(p,dtype='<f4').reshape(-1,2).astype(float)
def metrics(x):
 f,p=signal.welch(x,48000,nperseg=8192,axis=0);p=p.mean(axis=1)
 return dict(rms_db=float(10*np.log10(np.mean(x*x)+1e-30)),peak_db=float(20*np.log10(np.max(abs(x))+1e-30)),band_db=[float(10*np.log10(np.trapezoid(p[(f>=a)&(f<b)],f[(f>=a)&(f<b)])+1e-30)) for a,b in bands],centroid=float(np.sum(f*p)/p.sum()),mid_retention_db=float(10*np.log10(np.mean(x.mean(1)**2)/np.mean(x*x))),correlation=float(np.corrcoef(x.T)[0,1]))
rows=[]
for a in sorted((root/'out').glob('*-v0.f32')):
 x=read(a);base=metrics(x)
 for p in sorted(a.parent.glob(a.name.replace('v0','v*'))):
  y=read(p)
  if y.shape!=x.shape: continue
  m=metrics(y);d=m['rms_db']-base['rms_db'];match=y*10**(-d/20)
  rows.append(dict(case=a.stem[:-3],variant=int(p.stem[-1]),**m,level_change_db=d,matched_difference_db=float(10*np.log10(max(np.mean((match-x)**2)/np.mean(x*x),1e-30))),relative_band_change_db=(np.array(m['band_db'])-base['band_db']-d).tolist(),peak_change_db=m['peak_db']-base['peak_db']))
summary={}
for v in range(1,7):
 r=[x for x in rows if x['variant']==v]
 if r: summary[v]={key:[float(np.min([x[key] for x in r])),float(np.median([x[key] for x in r])),float(np.max([x[key] for x in r]))] for key in ['level_change_db','matched_difference_db','peak_change_db']};summary[v]['relative_band_change_median_db']=np.median([x['relative_band_change_db'] for x in r],axis=0).tolist()
report=dict(rate=48000,bands_hz=bands,variants={'0':'baseline','1':'1k -1.5dB and1.4k -2dB','2':'35Hz Butterworth HP','3':'4k -1.5dB shelf','4':'remove residue brightness tilt diagnostic','5':'1k -1.5dB;1.4k→1.8k centre, unchanged+6dB','6':'1k -1.5dB;1.4k→1.8k centre,+4dB'},summary=summary,rows=rows)
(root/'report.json').write_text(json.dumps(report,indent=2))
print(json.dumps(summary,indent=2))
