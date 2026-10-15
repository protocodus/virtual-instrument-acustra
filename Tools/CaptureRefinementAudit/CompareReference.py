import sys,json
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import FitCaptureVoicing as f
root=Path(sys.argv[1]).resolve()
manifest=json.loads((root/'corpus/flattop.json').read_text());notes=manifest['examples'];results={}
for v in [0,1,5,6]:
 pooled=f.Spectra('flat_top',1.0);rows=[]
 for row in notes:
  target=f._read_channels(row['target'],root/'corpus')
  model=np.fromfile(root/f"notes/m{row['midi']}-v{v}.f32",dtype='<f4').reshape(-1,2).astype(float)
  if v==0:
   reference=f._read_channels(row['model'],root/'corpus')
   if not np.array_equal(model,reference): raise Exception(f"baseline parity failed {row['midi']} {np.max(abs(model-reference))}")
  pooled.add(target,model,manifest['model_controls'],row['id'])
  a=f._bands(f._note_segment(target,row['id']));b=f._bands(f._note_segment(model,row['id']));d=10*np.log10(a/b)
  baseline=f._read_channels(row['model'],root/'corpus');base_power=f._bands(f._note_segment(baseline,row['id']))
  band=(f.CENTRES>=800)&(f.CENTRES<=3200)&(a>=a.max()*1e-4)&(base_power>=base_power.max()*1e-4)
  rows.append({'midi':row['midi'],'occupied_presence_bands_hz':f.CENTRES[band].tolist(),'presence_rms_db':float(np.sqrt(np.mean(d[band]**2))),'target_minus_model_db':d.tolist()})
 d=pooled.difference('channels','stereo');band=(f.CENTRES>=800)&(f.CENTRES<=3200)
 results[v]={'pooled_presence_rms_db':float(np.sqrt(np.mean(d[band]**2))),'pooled_target_minus_model_db':d.tolist(),'median_note_presence_rms_db':float(np.median([r['presence_rms_db'] for r in rows])),'rows':rows}
report={'scope':'Fresh baseline bank flat-top8rows;native candidate engine render; dry pair per-channel power; first0.03–1s following independent detected onsets; equal per-note spectral power; individual note score uses only0.8–3.2k bands where both reference and baseline are within40dB of own maximum third-octave power (same mask for every candidate); pooled0.8–3.2k score includes all6bands; this source is development evidence and cannot establish listener preference or generalisation','centres_hz':f.CENTRES.tolist(),'results':results}
(root/'reference-report.json').write_text(json.dumps(report,indent=2))
for v,r in results.items(): print(v,{k:val for k,val in r.items() if not isinstance(val,list)});print([(round(freq),round(d,2)) for freq,d in zip(f.CENTRES,r['pooled_target_minus_model_db']) if 700<freq<4000])
