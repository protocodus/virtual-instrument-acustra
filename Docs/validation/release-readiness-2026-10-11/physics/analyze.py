from pathlib import Path
import importlib.util,json,hashlib
import numpy as np
from scipy.optimize import minimize_scalar,brentq
R=Path.cwd();D=R/'build-audit/release-model-audit-20261011/physics'
p=R/'build-audit/studio-mic-variants/scoring/score_variants.py'
spec=importlib.util.spec_from_file_location('score',p);s=importlib.util.module_from_spec(spec);spec.loader.exec_module(s)
curpath=R/'Docs/validation-studio-capture-implementation-20261011/physics-final/production-coefficients.csv'
basepath=R/'Docs/validation-studio-capture-implementation-20261011/physics-final/baseline-coefficients.csv'
cur=s.read_coeff(curpath);base=s.read_coeff(basepath)
def tf(cell,f,ratio,ch=2):
 h=s.response(cell,np.atleast_1d(f));h=h[:,:2]+np.asarray(ratio)[...,None]*h[:,2:]
 return h.mean(axis=1) if ch==2 else h[:,ch]
def db(x):return 20*np.log10(np.maximum(abs(x),1e-30))
fr=[]
for wood,seed in [(0,254.10636044698046),(1,244.69109141829477)]:
 for ratio in [-1.25,-1.,-.75,0.]:
  cell=cur[(0,wood)]
  for ch,name in [(0,'left'),(1,'right_mono'),(2,'stereo_mid')]:
   # Refine a local minimum in the identified 10Hz neighborhood, not arbitrary full-band worst points.
   f=np.linspace(seed-5,seed+5,1001);v=db(tf(cell,f,ratio,ch));k=int(np.argmin(v))
   opt=minimize_scalar(lambda hz:float(db(tf(cell,[hz],ratio,ch))[0]),bounds=(f[max(0,k-1)],f[min(len(f)-1,k+1)]),method='bounded')
   fc=float(opt.x);abc=fc*2**(np.array([-2,0,2])/12);vv=db(tf(cell,abc,ratio,ch))
   old=db(tf(base[(0,wood)],abc,ratio,ch))
   fr.append(dict(wood=wood,normalized_moment=ratio,channel=name,local_center_hz=fc,at_search_boundary=k in (0,len(f)-1),local_notch_db=float(vv[1]-.5*(vv[0]+vv[2])),previous_local_notch_db=float(old[1]-.5*(old[0]+old[2])),raw_change_db=float(vv[1]-old[1])))
tr=[]
for r in json.loads((D/'trace-manifest.json').read_text()):
 x=np.fromfile(R/r['file'],dtype='<f4').reshape(-1,4).astype(float);cell=cur[(0,r['wood'])];b=base[(0,r['wood'])]
 for win,lo,hi in [('early',.15,.65),('held',1.,5.)]:
  y=x[int(lo*48000):int(hi*48000)];w=np.hanning(len(y));t=np.arange(len(y))/48000
  hz=np.linspace(r['nominal_h3']-3,r['nominal_h3']+3,121)
  # Local maximum of the actual heave input: all spectral ratios use this same bin.
  def spectra(f):return np.exp(-2j*np.pi*f*t)@(w[:,None]*y)
  amp=np.array([abs(spectra(f)[0]) for f in hz]);k=int(np.argmax(amp))
  opt=minimize_scalar(lambda f:-abs(spectra(f)[0]),bounds=(hz[max(0,k-1)],hz[min(len(hz)-1,k+1)]),method='bounded')
  f=float(opt.x);a=spectra(f);ratio=a[1]/a[0]
  responses={}
  for ch,name in [(0,'left'),(1,'right_mono'),(2,'stereo_mid')]:
   fratio=float(db(tf(cell,[f],ratio,ch))[0]-db(tf(b,[f],ratio,ch))[0]);around=f*2**(np.array([-2,0,2])/12);v=db(tf(cell,around,ratio,ch))
   responses[name]=dict(current_local_notch_db=float(v[1]-.5*(v[0]+v[2])),raw_observer_change_db=fratio)
  tr.append(dict(wood=r['wood'],picking=r['picking'],window=win,h3_heave_peak_hz=f,nominal_h3=r['nominal_h3'],moment_over_force=[float(ratio.real),float(ratio.imag)],moment_magnitude=float(abs(ratio)),moment_phase_degrees=float(np.angle(ratio,deg=True)),responses=responses,fold_h3_dbfs=float(db((a[2]+a[3])/len(y))[()])))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
result=dict(scope='Current production only: bounded static pressure transfer and four actual held low-E input traces; no runtime edits.',source_hashes={str(p.relative_to(R)):sha(p) for p in [curpath,basepath,R/'Source/DSP/AcustraEngine.cpp',R/'build-audit/libAcustraDSP.a',D/'force_trace.cpp',D/'analyze.py']},static_local_notches=fr,actual_drive_samples=tr,limits=['Force/moment ratio estimated from windowed Fourier components is not constant in time or frequency.','Actual-drive FRF values are a stationary observer diagnostic, not replacement audio or complete transient prediction.','No baseline re-render: historical pressure coefficients are retained only to attribute transfer changes.','Late weak partial levels may be masked; no listening preference or audibility threshold asserted.','Static transfer CSV omits flat output gain, which cannot change local notch depth.'])
(D/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(dict(static_local_notches=fr,actual_drive_samples=tr),indent=2))
