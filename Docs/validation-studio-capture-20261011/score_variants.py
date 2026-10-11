"""Independent research scorer. Reads completed reports/audio and optional residue overrides.

No rendering, fitting, per-note normalization, or source/model edits. The flags
are review triggers; no numerical score certifies physical studio placement.
"""
from pathlib import Path
import argparse,csv,hashlib,json
import numpy as np
from scipy.signal import periodogram,find_peaks

ROOT=Path(__file__).resolve().parents[3]
SHAPES=['parlor','auditorium','dreadnought','jumbo']; WOODS=['spruce','mahogany','maple']
TARGETS={'parlor':[47],'auditorium':[45],'dreadnought':[],'jumbo':[40,41]}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def db10(x):return float(10*np.log10(max(float(x),1e-30)))
def band(x,sr,lo,hi):
 f,p=periodogram(x,fs=sr,window='hann',axis=0,scaling='density',detrend=False)
 return np.sum(p[(f>=lo)&(f<hi)],axis=0)*(f[1]-f[0])
def extra_audio(p,sr):
 x=np.fromfile(p,dtype='<f4')
 if len(x)%2:raise ValueError('Not stereo float32: '+str(p))
 x=x.reshape(-1,2).astype(float)
 if not np.all(np.isfinite(x)):raise ValueError('Nonfinite audio: '+str(p))
 a=x[round(.2*sr):round(2.8*sr)];powers=band(a,sr,80,300)
 mid=band(a.mean(axis=1),sr,80,300)
 return {'low80_300_lr_power_dbfs':[db10(v) for v in powers],
  'low80_300_mono_sum_loss_db':db10(mid/max(float(np.mean(powers)),1e-30)),
  'low80_300_lr_level_db':db10(powers[0]/max(powers[1],1e-30)),
  'exact_audio_sha256':sha(p),'samples_per_channel':len(x)}
def h1balance(row,window='held'):
 p=row['windows'][window]['partial_power_dbfs'];return p[0]-db10((10**(p[1]/10)+10**(p[2]/10))/2)
def keytuple(r):return(r['shape'],r['wood'],r['capture'],r['midi'],r['channel'])
def audio_scores(basepath,candpath):
 b=json.loads(basepath.read_text());c=json.loads(candpath.read_text())
 if b['controls']!=c['controls']:raise ValueError('Controls differ; cannot use paired comparison')
 if b['windows_seconds']!=c['windows_seconds']:raise ValueError('Measurement windows differ')
 bi={r['key']:r for r in b['renders']};ci={r['key']:r for r in c['renders']}
 if bi.keys()!=ci.keys():raise ValueError('Render grids differ')
 rows=[];idx={};piezo=[]
 for key,br in bi.items():
  cr=ci[key]
  if keytuple(br)!=keytuple(cr):raise ValueError('Mismatched case metadata')
  sr=c['controls']['sample_rate'];bfile=basepath.parent/'audio'/(key+'.f32');cfile=candpath.parent/'audio'/(key+'.f32')
  aa=extra_audio(bfile,sr);bb=extra_audio(cfile,sr)
  if aa['exact_audio_sha256']!=br['audio_sha256'] or bb['exact_audio_sha256']!=cr['audio_sha256']:raise ValueError('Rendered audio no longer matches report: '+key)
  row={'key':key,**{k:br[k] for k in ('shape','wood','capture','midi','channel')},
   'reported_target':br['midi'] in TARGETS[br['shape']],
   'audio_bit_identical':aa['exact_audio_sha256']==bb['exact_audio_sha256'],
   'held_h1_vs_h2h3_before_db':h1balance(br),'held_h1_vs_h2h3_after_db':h1balance(cr),
   'held_h1_vs_h2h3_change_db':h1balance(cr)-h1balance(br),
   'held_h1_change_db':cr['windows']['held']['partial_power_dbfs'][0]-br['windows']['held']['partial_power_dbfs'][0],
   'held_rms_change_db':cr['windows']['held']['rms_dbfs']-br['windows']['held']['rms_dbfs'],
   'late_h1_vs_h2h3_change_db':h1balance(cr,'late')-h1balance(br,'late'),
   'low80_300_mono_sum_loss_before_db':aa['low80_300_mono_sum_loss_db'],
   'low80_300_mono_sum_loss_after_db':bb['low80_300_mono_sum_loss_db'],
   'low80_300_lr_level_before_db':aa['low80_300_lr_level_db'],
   'low80_300_lr_level_after_db':bb['low80_300_lr_level_db'],
   'full_scale_sample_count_after':cr['samples_at_full_scale']}
  row['windows']={}
  for window in ('attack','early','held','late'):
   row['windows'][window]={
    'h1_vs_h2h3_before_db':h1balance(br,window),'h1_vs_h2h3_after_db':h1balance(cr,window),
    'h1_vs_h2h3_change_db':h1balance(cr,window)-h1balance(br,window),
    'h1_before_dbfs':br['windows'][window]['partial_power_dbfs'][0],
    'h1_after_dbfs':cr['windows'][window]['partial_power_dbfs'][0],
    'h1_change_db':cr['windows'][window]['partial_power_dbfs'][0]-br['windows'][window]['partial_power_dbfs'][0],
    'rms_change_db':cr['windows'][window]['rms_dbfs']-br['windows'][window]['rms_dbfs']}
  row['flags']=[]
  if row['capture']=='piezo':
   piezo.append(row['audio_bit_identical'])
   if not row['audio_bit_identical']:row['flags'].append('PIEZO_CHANGED')
  else:
   if row['reported_target'] and row['held_h1_vs_h2h3_change_db']>1:row['flags'].append('target_balance_worsened_over_1dB')
   if row['low80_300_mono_sum_loss_after_db']<-6:row['flags'].append('mono_sum_loss_over_6dB')
   if row['full_scale_sample_count_after']:row['flags'].append('full_scale_samples')
  idx[keytuple(br)]=row;rows.append(row)
 for row in rows:
  stem=(row['shape'],row['wood'],row['capture']);n=row['midi'];ch=row['channel']
  low=idx.get((*stem,n-1,ch));high=idx.get((*stem,n+1,ch))
  if low is not None and high is not None:
   br=bi[row['key']];cr=ci[row['key']]
   for window,metrics in row['windows'].items():
    def window_excess(source):return source[row['key']]['windows'][window]['partial_power_dbfs'][0]-.5*(source[low['key']]['windows'][window]['partial_power_dbfs'][0]+source[high['key']]['windows'][window]['partial_power_dbfs'][0])
    metrics['h1_neighbor_excess_before_db']=window_excess(bi)
    metrics['h1_neighbor_excess_after_db']=window_excess(ci)
    metrics['h1_neighbor_excess_change_db']=window_excess(ci)-window_excess(bi)
   def excess(source):return source[row['key']]['windows']['held']['partial_power_dbfs'][0]-.5*(source[low['key']]['windows']['held']['partial_power_dbfs'][0]+source[high['key']]['windows']['held']['partial_power_dbfs'][0])
   row['h1_neighbor_excess_before_db']=excess(bi);row['h1_neighbor_excess_after_db']=excess(ci)
   row['h1_neighbor_excess_change_db']=excess(ci)-excess(bi)
   if row['capture']!='piezo' and row['h1_neighbor_excess_change_db']<-6:row['flags'].append('new_neighbor_dip_over_6dB')
 targets=[r for r in rows if r['reported_target'] and r['capture']!='piezo']
 return {'baseline_report_sha256':sha(basepath),'candidate_report_sha256':sha(candpath),
  'windows_seconds':c['windows_seconds'],
  'piezo_cases':len(piezo),'all_piezo_bit_identical':all(piezo) and bool(piezo),
  'reported_target_cases':len(targets),'target_h1_balance_change_range_db':[min(r['held_h1_vs_h2h3_change_db'] for r in targets),max(r['held_h1_vs_h2h3_change_db'] for r in targets)] if targets else None,
  'flagged_cases':[{'key':r['key'],'flags':r['flags']} for r in rows if r['flags']],
  'cases':rows,
  'limits':['Harmonic power is native output, not internal modal energy or free decay.','Jumbo open E has no lower neighbor on the standard low-E string; its neighborhood test remains one-sided and must be reviewed with F/F#/G.','No per-note normalization. Gain-invariant H1/(mean H2,H3) and local note excess are reported alongside absolute levels.','Flags are explicit listening/review triggers, not a fitted target or proof of good studio placement.']}
def read_coeff(path):
 cells={}
 for row in csv.reader(path.open()):
  kind=row[0];s,w=map(int,row[1:3]);cell=cells.setdefault((s,w),{'modes':{},'filters':{}})
  if kind=='M':cell['modes'][int(row[3])]=np.array(list(map(float,row[4:])))
  elif kind=='F':cell['filters'][int(row[3])]=np.array(list(map(float,row[4:])))
  elif kind=='G':cell['gain']=float(row[3])
  else:raise ValueError('Unknown coefficient record')
 return cells
def overrides(path,base):
 import copy
 c=copy.deepcopy(base)
 for row in csv.reader(path.open()):
  if not row or row[0].startswith('#'):continue
  # rows: shape index, wood index, slot, 8 real/imag residue fields; optional M prefix.
  if row[0]=='M':row=row[1:]
  if row[0].lower() in ('shape','shape_index'):continue
  s,w,k=map(int,row[:3]);v=np.array(list(map(float,row[3:])))
  if len(v)==10:
   if not np.array_equal(v[:2],base[(s,w)]['modes'][k][:2]):raise ValueError('Candidate changed physical pole')
   v=v[2:]
  if len(v)!=8 or not np.all(np.isfinite(v)):raise ValueError('Override must have eight finite residues')
  c[(s,w)]['modes'][k][2:]=v
 return c
def response(cell,f):
 # Independent real second-order numerator/denominator form of conjugate modal pair.
 q=np.exp(-2j*np.pi*f/48000);out=np.zeros((len(f),4),complex)
 for m in cell['modes'].values():
  pr,pi=m[:2];cr=m[2::2];ci=m[3::2]
  out+=(2*cr[None,:]-2*(cr*pr+ci*pi)[None,:]*q[:,None])/(1-2*pr*q[:,None]+(pr*pr+pi*pi)*q[:,None]**2)
 filt=np.full(len(f),cell['gain'],complex)
 for v in cell['filters'].values():
  b0,b1,b2,a1,a2=v;filt*=(b0+b1*q+b2*q*q)/(1+a1*q+a2*q*q)
 return out*filt[:,None]
def frf_scores(basepath,overridepath):
 base=read_coeff(basepath);cand=overrides(overridepath,base)
 # Wide sweep supports log-neighborhood references around 80--300Hz limits.
 f=np.geomspace(65,370,7001);inside=(f>=80)&(f<=300);rows=[]
 for (s,w),b in base.items():
  hb=response(b,f);hc=response(cand[(s,w)],f)
  for ratio in (-1.,0.,1.):
   a=hb[:,:2]+ratio*hb[:,2:];c=hc[:,:2]+ratio*hc[:,2:]
   a=np.column_stack((a,a.mean(axis=1)));c=np.column_stack((c,c.mean(axis=1)))
   for ch,name in enumerate(('left','right_mono','stereo_mid')):
    ad=20*np.log10(np.maximum(abs(a[:,ch]),1e-20));cd=20*np.log10(np.maximum(abs(c[:,ch]),1e-20));delta=cd-ad
    # Local shape relative to geometric mean response two semitones away.
    span=np.log(2)/6;lf=np.log(f)
    def shape(d):return d-.5*(np.interp(lf-span,lf,d)+np.interp(lf+span,lf,d))
    ash=shape(ad);csh=shape(cd);ix=np.flatnonzero(inside)
    def atmin(d):j=ix[np.argmin(d[ix])];return {'hz':float(f[j]),'db':float(d[j])}
    def atmax(d):j=ix[np.argmax(d[ix])];return {'hz':float(f[j]),'db':float(d[j])}
    rows.append({'shape':SHAPES[s],'wood':WOODS[w],'moment_to_force':ratio,'channel':name,
      'maximum_gain_change':atmax(delta),'maximum_loss':atmin(delta),
      'worst_local_notch_before':atmin(ash),'worst_local_notch_after':atmin(csh),
      'highest_local_peak_before':atmax(ash),'highest_local_peak_after':atmax(csh),
      'largest_added_local_dip':atmin(csh-ash),
      'candidate_response_dynamic_range_db':float(np.max(cd[ix])-np.min(cd[ix]))})
 return {'baseline_coefficients_sha256':sha(basepath),'override_sha256':sha(overridepath),
  'rate':48000,'sweep':'7001 logarithmic points65--370Hz; reported80--300Hz',
  'formula':'Conjugate modal pair real biquad numerator; F=1,M/F=-1,0,+1; existing capture cascade unchanged.',
  'local_reference':'Mean dB at +/-2 semitones; peaks/notches are interference/transfer measures, not source-Q estimates.',
  'rows':rows}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--baseline-report',type=Path,default=ROOT/'build-audit/mic-boom-20261011/baseline/report.json');ap.add_argument('--candidate-report',type=Path);ap.add_argument('--baseline-coefficients',type=Path,default=ROOT/'build-audit/mic-boom-20261011/physics/coefficients.csv');ap.add_argument('--overrides',type=Path);ap.add_argument('--output',required=True,type=Path);ap.add_argument('--nominal-gain',type=float,default=1.0,help='Declared common microphone propagation gain; reporting compensation only, never modifies audio.')
 a=ap.parse_args();result={'purpose':__doc__,'scorer_sha256':sha(Path(__file__))}
 if not np.isfinite(a.nominal_gain) or a.nominal_gain<=0:ap.error('nominal gain must be finite and positive')
 if a.candidate_report:result['native_audio']=audio_scores(a.baseline_report,a.candidate_report)
 if a.overrides:result['force_transfer']=frf_scores(a.baseline_coefficients,a.overrides)
 if len(result)==2:ap.error('Specify candidate report or overrides')
 gain_db=20*np.log10(a.nominal_gain);result['declared_common_microphone_gain']=a.nominal_gain
 result['gain_reporting_note']='After-distance-match fields subtract only the declared common gain; no fitted/per-note normalization and no audio edits.'
 for r in result.get('native_audio',{}).get('cases',[]):
  correction=0 if r['capture']=='piezo' else gain_db
  r['held_h1_change_after_distance_match_db']=r['held_h1_change_db']-correction
  r['held_rms_change_after_distance_match_db']=r['held_rms_change_db']-correction
  for window in r['windows'].values():
   window['h1_change_after_distance_match_db']=window['h1_change_db']-correction
   window['rms_change_after_distance_match_db']=window['rms_change_db']-correction
 for r in result.get('force_transfer',{}).get('rows',[]):
  for k in ('maximum_gain_change','maximum_loss'):
   r[k+'_after_distance_match']={'hz':r[k]['hz'],'db':r[k]['db']-gain_db}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
 print(json.dumps({'output':str(a.output),'audio_cases':len(result.get('native_audio',{}).get('cases',[])),'frf_cases':len(result.get('force_transfer',{}).get('rows',[])),'flags':len(result.get('native_audio',{}).get('flagged_cases',[]))}))
if __name__=='__main__':main()
