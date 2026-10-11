from pathlib import Path
import argparse, concurrent.futures, hashlib, itertools, json, shutil, subprocess, time
import numpy as np
from scipy.signal import periodogram, find_peaks
import scipy

HERE=None
RENDERER=None
RATE=48000
SHAPES=('parlor','auditorium','dreadnought','jumbo')
WOODS=('spruce','mahogany')
CAPTURES=('stereo_mic','mono_mic','piezo')
NOTES=[(m,1) for m in range(40,49)]+[(m,2) for m in range(45,49)]+[(52,3),(53,3),(57,4),(59,5)]
WINDOWS={'attack':(.02,.20),'early':(.15,.65),'held':(.20,2.80),'late':(1.90,2.80),'release':(3.05,3.90)}
BANDS={'low_65_160':(65,160),'top_160_350':(160,350),'mid_350_1500':(350,1500),'treble_1500_6000':(1500,6000)}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def db(x):return float(10*np.log10(max(float(x),1e-30)))
def metric(audio,midi):
 f0=440*2**((midi-69)/12)
 f,p=periodogram(audio,fs=RATE,axis=0,window='hann',detrend=False,scaling='density',nfft=1<<(len(audio)-1).bit_length())
 p=p.mean(axis=1); step=f[1]-f[0]
 def band(a,b):return float(np.sum(p[(f>=a)&(f<b)])*step)
 total=band(20,10000)
 partial=[]
 for h in range(1,5):
  half=max(2*RATE/len(audio),f0*h*.025)
  partial.append(band(f0*h-half,f0*h+half))
 peaks,_=find_peaks(p)
 peaks=[i for i in peaks if 65<=f[i]<=350]
 peaks=sorted(peaks,key=lambda i:p[i],reverse=True)[:4]
 return {'rms_dbfs':db(np.mean(audio*audio)),'peak_dbfs':db(np.max(np.abs(audio))**2),
  'band_power_dbfs':{name:db(band(*bounds)) for name,bounds in BANDS.items()},
  'partial_power_dbfs':[db(x) for x in partial], 'fundamental_fraction_db':db(partial[0]/max(total,1e-30)),
  'h2_to_h1_db':db(partial[1]/max(partial[0],1e-30)),
  'low_band_fraction_db':db(band(65,160)/max(total,1e-30)),
  'low_frequency_spectral_peaks':[{'hz':float(f[i]),'power_density_dbfs_per_hz':db(p[i])} for i in peaks]}
def run(case):
 shape,wood,capture,midi,ch=case
 key=f'{shape}-{wood}-{capture}-m{midi}-s{ch}'
 events=HERE/'events'/f'm{midi}-s{ch}.events'
 raw=HERE/'audio'/f'{key}.f32'
 cmd=[str(RENDERER),str(events),str(raw),capture,'finger','--body-shape',shape,'--body-material',wood,'--room','0']
 if raw.exists():raise RuntimeError('Refusing duplicate render: '+str(raw))
 process=subprocess.run(cmd,capture_output=True,text=True)
 if process.returncode:raise RuntimeError(key+': '+process.stderr)
 audio=np.fromfile(raw,dtype='<f4').reshape(-1,2).astype(np.float64)
 assert audio.shape==(RATE*4,2) and np.isfinite(audio).all() and np.any(audio)
 windows={name:metric(audio[round(a*RATE):round(b*RATE)],midi) for name,(a,b) in WINDOWS.items()}
 row={'key':key,'shape':shape,'wood':wood,'capture':capture,'midi':midi,'channel':ch,'fret':midi-(40,45,50,55,59,64)[ch-1],
  'nominal_fundamental_hz':440*2**((midi-69)/12),'command':cmd,'audio_sha256':sha(raw),'sample_count_per_channel':len(audio),
  'all_finite':True,'peak_dbfs':db(np.max(np.abs(audio))**2),'samples_at_full_scale':int(np.sum(np.abs(audio)>=1)),
  'windows':windows,'held_h1_apparent_decay_db_per_second':(windows['late']['partial_power_dbfs'][0]-windows['early']['partial_power_dbfs'][0])/(2.35-.40),
  'release_low_band_relative_to_late_db':windows['release']['band_power_dbfs']['low_65_160']-windows['late']['band_power_dbfs']['low_65_160']}
 return row

def measure():
 for name in ['events','audio']:(HERE/name).mkdir(exist_ok=True)
 for midi,ch in NOTES:
  path=HERE/'events'/f'm{midi}-s{ch}.events'
  text=f'ACUSTRA_PERFORMANCE_V1 {RATE} {RATE*4}\n0 {ch} {midi} 96 0\n{RATE*3} {ch} {midi} 0 0\n'
  if path.exists() and path.read_text()!=text:raise RuntimeError('Event score changed')
  path.write_text(text)
 start=time.monotonic(); rows=[]
 cases=[(*c,m,ch) for c in itertools.product(SHAPES,WOODS,CAPTURES) for m,ch in NOTES]
 with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
  jobs={pool.submit(run,case):case for case in cases}
  for job in concurrent.futures.as_completed(jobs):
   row=job.result();rows.append(row)
   with (HERE/'progress.jsonl').open('a') as progress:progress.write(json.dumps(row,allow_nan=False)+'\n')
   if len(rows)%24==0:print(f'{len(rows)}/{len(cases)} done in {time.monotonic()-start:.1f}s',flush=True)
 rows.sort(key=lambda x:x['key'])
 indexed={(r['shape'],r['wood'],r['capture'],r['midi'],r['channel']):r for r in rows}
 comparisons=[]
 for shape,wood,capture in itertools.product(SHAPES,WOODS,CAPTURES):
  for midi,ch in [(41,1),(45,1),(47,1),(47,2)]:
   low,mid,high=[indexed[shape,wood,capture,n,ch] for n in [midi-1,midi,midi+1]]
   comparisons.append({'shape':shape,'wood':wood,'capture':capture,'midi':midi,'channel':ch,
    'held_rms_neighbor_excess_db':mid['windows']['held']['rms_dbfs']-.5*(low['windows']['held']['rms_dbfs']+high['windows']['held']['rms_dbfs']),
    'held_h1_neighbor_excess_db':mid['windows']['held']['partial_power_dbfs'][0]-.5*(low['windows']['held']['partial_power_dbfs'][0]+high['windows']['held']['partial_power_dbfs'][0])})
 result={'purpose':'Matched microphone-boom measurement; native binary output, no normalization or control changes.',
  'renderer_sha256':sha(RENDERER),
  'source_provenance':'Binary SHA only: source identity must be supplied by the corresponding frozen build receipt.',
  'script_sha256':sha(Path(__file__)),
  'numpy_version':np.__version__,'scipy_version':scipy.__version__,'render_seconds':time.monotonic()-start,
  'controls':{'sample_rate':RATE,'block_frames':127,'velocity':96,'picking':'finger','touch':.58,'pluck_position':.28,'body_amount':.82,'stereo_width':.62,'output_linear':.42,'room':0,'piezo_mix':0,'release_noise':0,'string_age':.15,'tuning':'standard','bend_semitones':0,'hold_seconds':3,'duration_seconds':4,'fresh_engine_per_clip':True},
  'grid':{'shapes':SHAPES,'woods':WOODS,'captures':CAPTURES,'notes_and_channels':NOTES,'cases':len(rows)},
  'observation':'Per-channel power averaged across L/R. Mono and Piezo identical channels remain single-signal powers. Native trim is retained; no cross-capture level normalization.',
  'spectral_method':'Hann periodogram, power-density average of channels; zero-padded to next power of two. H1-H4 integration half-width max(2/windowSeconds, 2.5% harmonic frequency); powers integrated over frequency bins.',
  'windows_seconds':WINDOWS,'bands_hz':BANDS,'limits':['User-confirmed Reason note labels: E1=MIDI40, A1=MIDI45, B1=MIDI47 (scientific pitches E2, A2, B2).','Output band powers and peaks are modal-response proxies, not a measurement of internal body energy or a free body modal Q.','Apparent H1 decay is the early/late spectral-power slope while held; it includes string/body coupling, beating, and interference.','This first pass uses one technique, velocity, host rate and native engine controls; it is not the Rack wrapper or a subjective boom threshold.'],
  'neighbor_comparisons':comparisons,'renders':rows}
 (HERE/'report.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
 print('Complete',len(rows),'clips',time.monotonic()-start,'seconds',flush=True)

def main():
 global HERE, RENDERER
 parser=argparse.ArgumentParser(description='Render the fixed 408-case microphone-boom grid for two explicit frozen binaries. Existing output directories are refused. No compilation or DSP/header changes.')
 parser.add_argument('--before-renderer',type=Path,required=True)
 parser.add_argument('--after-renderer',type=Path,required=True)
 parser.add_argument('--output',type=Path,required=True)
 args=parser.parse_args()
 for label in ['before','after']:
  if (args.output/label).exists():parser.error('Refusing existing output: '+str(args.output/label))
 for label,source in [('before',args.before_renderer),('after',args.after_renderer)]:
  source=source.resolve(strict=True);HERE=(args.output/label).resolve();HERE.mkdir(parents=True)
  RENDERER=HERE/'AcustraPerformanceRenderer';shutil.copy2(source,RENDERER)
  assert sha(source)==sha(RENDERER)
  print(label,sha(RENDERER),flush=True);measure()

if __name__=='__main__':main()
