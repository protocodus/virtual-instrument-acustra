from pathlib import Path
import argparse,csv,hashlib,json
import numpy as np
from scipy.io import wavfile

parser=argparse.ArgumentParser(description='Compare two matching microphone-boom measurement directories and create native-gain before/after WAVs. No rendering, compilation, normalization or source changes.')
parser.add_argument('--before',type=Path,required=True)
parser.add_argument('--after',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
BASE=args.before.resolve();CAND=args.after.resolve();HERE=args.output.resolve()
HERE.mkdir(parents=True,exist_ok=True)
if (HERE/'comparison.json').exists():parser.error('Refusing existing comparison.json')
b=json.loads((BASE/'report.json').read_text()); c=json.loads((CAND/'report.json').read_text())
assert b['grid']==c['grid'] and b['controls']==c['controls'] and b['spectral_method']==c['spectral_method'] and b['windows_seconds']==c['windows_seconds']
lookup={x['key']:x for x in b['renders']};rows=[];unchanged_expected=[]
for new in c['renders']:
 old=lookup[new['key']]
 same=old['audio_sha256']==new['audio_sha256']
 expect=new['capture']=='piezo' or new['shape']=='dreadnought'
 if expect:unchanged_expected.append({'key':new['key'],'byte_exact':same})
 row={k:new[k] for k in ['key','shape','wood','capture','midi','channel','fret','nominal_fundamental_hz']}
 row.update({'byte_exact':same,'expected_byte_exact':expect,'baseline_peak_dbfs':old['peak_dbfs'],'candidate_peak_dbfs':new['peak_dbfs'],
  'baseline_full_scale_samples':old['samples_at_full_scale'],'candidate_full_scale_samples':new['samples_at_full_scale']})
 row['windows']={}
 for win in b['windows_seconds']:
  before=old['windows'][win];after=new['windows'][win]
  row['windows'][win]={'rms_delta_db':after['rms_dbfs']-before['rms_dbfs'],
   'h1_delta_db':after['partial_power_dbfs'][0]-before['partial_power_dbfs'][0],
   'h2_delta_db':after['partial_power_dbfs'][1]-before['partial_power_dbfs'][1],
   'h3_delta_db':after['partial_power_dbfs'][2]-before['partial_power_dbfs'][2],
   'h4_delta_db':after['partial_power_dbfs'][3]-before['partial_power_dbfs'][3],
   'low_band_delta_db':after['band_power_dbfs']['low_65_160']-before['band_power_dbfs']['low_65_160'],
   'top_band_delta_db':after['band_power_dbfs']['top_160_350']-before['band_power_dbfs']['top_160_350'],
   'baseline_h2_to_h1_db':before['h2_to_h1_db'],'candidate_h2_to_h1_db':after['h2_to_h1_db'],
   'baseline_h1_fraction_db':before['fundamental_fraction_db'],'candidate_h1_fraction_db':after['fundamental_fraction_db']}
 rows.append(row)
lookup_new={x['key']:x for x in rows}
targets=[r for r in rows if (r['shape'],r['midi'],r['channel']) in [('parlor',47,2),('auditorium',45,2),('jumbo',40,1),('jumbo',41,1)]]
mic_targets=[r for r in targets if r['capture']!='piezo']
summary={'purpose':'Paired same-input output measurement, no listening verdict or perceptual acceptance claim.',
 'baseline_renderer_sha256':b['renderer_sha256'],'candidate_renderer_sha256':c['renderer_sha256'],
 'controls_identical':True,'cases':len(rows),'all_finite':all(r['all_finite'] for r in b['renders']+c['renders']),
 'expected_unchanged_count':len(unchanged_expected),'all_piezo_and_dreadnought_byte_exact':all(r['byte_exact'] for r in unchanged_expected),
 'unexpected_unchanged_case_failures':[r for r in unchanged_expected if not r['byte_exact']],
 'all_output_samples_below_full_scale':all(r['baseline_full_scale_samples']==0 and r['candidate_full_scale_samples']==0 for r in rows),
 'maximum_baseline_peak_dbfs':max(r['baseline_peak_dbfs'] for r in rows),'maximum_candidate_peak_dbfs':max(r['candidate_peak_dbfs'] for r in rows),
 'microphone_targets_delta_ranges_db':{w:{m:[min(r['windows'][w][m] for r in mic_targets),max(r['windows'][w][m] for r in mic_targets)] for m in ['h1_delta_db','h2_delta_db','h3_delta_db','low_band_delta_db','top_band_delta_db']} for w in ['attack','early','held']},
 'reported_target_rows':targets,'comparisons':rows,
 'limits':['Natural microphone output powers only; internal energy is not directly observed.','Measured attenuation is not a listener preference or proof of absolute realism.','The velocity96/Finger/48k/dry default-controls grid does not replace wider tests, calibration or Rack validation.']}
(HERE/'comparison.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n')
with (HERE/'comparison.csv').open('w') as f:
 fields=['shape','wood','capture','midi','channel','baseline_peak_dbfs','candidate_peak_dbfs','byte_exact','attack_h1_delta_db','early_h1_delta_db','early_h2_delta_db','early_h3_delta_db','early_baseline_h2_to_h1_db','early_candidate_h2_to_h1_db']
 writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader()
 for r in rows:
  item={k:r[k] for k in fields if k in r}
  for w in ['attack','early']:
   for m,v in r['windows'][w].items():
    if w+'_'+m in fields:item[w+'_'+m]=v
  writer.writerow(item)
# No normalization, gain change or fades. First 1.5 s of each native held note;
# 0.35 s silence separates before/after. Segments and trims are explicit.
audio_dir=HERE/'audition';audio_dir.mkdir(exist_ok=True);auditions=[]
for shape in ['parlor','auditorium','jumbo']:
 notes={'parlor':[(47,2)],'auditorium':[(45,2)],'jumbo':[(40,1),(41,1)]}[shape]
 for wood in ['spruce','mahogany']:
  for cap in ['stereo_mic','mono_mic']:
   parts=[];segments=[];at=0
   for midi,ch in notes:
    key=f'{shape}-{wood}-{cap}-m{midi}-s{ch}'
    for label,folder in [('baseline',BASE),('candidate',CAND)]:
     raw=folder/'audio'/f'{key}.f32';audio=np.fromfile(raw,dtype='<f4').reshape(-1,2)[:72000]
     parts.append(audio);segments.append({'label':label,'key':key,'start_seconds':at/48000,'duration_seconds':1.5,'source_audio_sha256':hashlib.sha256(raw.read_bytes()).hexdigest()});at+=len(audio)
     silence=np.zeros((16800,2),dtype='<f4');parts.append(silence);at+=len(silence)
   combined=np.concatenate(parts).astype('<f4');name=f'{shape}-{wood}-{cap}-before-after.wav';file=audio_dir/name
   wavfile.write(file,48000,combined)
   auditions.append({'file':name,'sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'gain_linear':1.0,'normalization':'none','sample_format':'float32 stereo','sample_rate':48000,'peak':float(np.max(np.abs(combined))),'segments':segments})
(audio_dir/'manifest.json').write_text(json.dumps({'ordering':'For each note: baseline, then candidate; 0.35s silence between segments. Jumbo includes E1 then F1.','reason_note_names':{'m40':'E1 (82.41 Hz)','m41':'F1 (87.31 Hz)','m45':'A1 (110 Hz)','m47':'B1 (123.47 Hz)'},'native_gain_preserved':True,'trim':'First 1.5 s of each 4 s native held-note render; no fades. Full native float captures retained in baseline/audio and candidate/audio.','files':auditions},indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['reported_target_rows','comparisons']},indent=2))
print('Wrote',len(auditions),'native-level sequential A/B WAVs')
