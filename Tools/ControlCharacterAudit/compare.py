#!/usr/bin/env python3
"""Clean held-note option contrasts; fixed source-relative windows, stereo power.
No recording targets, feature fitting or perceptual-realism claim.
"""
import argparse, concurrent.futures, hashlib, itertools, json, shutil, subprocess
from pathlib import Path
import numpy as np
from scipy.io import wavfile

RATE=48000; NOTES=[40,55,64,74]; VELOCITIES=[91,127]
BANDS=np.array([60,120,250,500,1000,2000,4000,8000,12000])
WINDOWS={'attack':[0,.05],'early':[0,.1],'sustain':[.1,.5]}
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

def features(audio):
    rms=np.sqrt(np.mean(audio[:24000]**2));result={'rms_0_500':float(rms),'peak':float(np.max(np.abs(audio)))}
    for name,(begin,end) in WINDOWS.items():
        x=audio[int(begin*RATE):int(end*RATE)];win=np.hanning(len(x));nfft=1<<(len(x)-1).bit_length()
        power=np.mean(np.abs(np.fft.rfft(x*win[:,None],n=nfft,axis=0))**2,axis=1)/(nfft*np.sum(win**2));power[1:-1]*=2
        freq=np.fft.rfftfreq(nfft,1/RATE);valid=(freq>=60)&(freq<12000);total=np.sum(power[valid])
        bands=np.array([power[(freq>=lo)&(freq<hi)].sum() for lo,hi in zip(BANDS[:-1],BANDS[1:])])
        result[name]={'centroid_hz':float(np.sum(power[valid]*freq[valid])/max(total,1e-30)),
                      'high_fraction_db':float(10*np.log10(max(power[(freq>=2000)&(freq<12000)].sum()/max(total,1e-30),1e-20))),
                      'normalized_band_db':(10*np.log10(np.maximum(bands/max(rms*rms,1e-30),1e-8))).tolist(),
                      'band_fraction':(bands/max(total,1e-30)).tolist()}
    energies=[np.mean(audio[int(a*RATE):int(b*RATE)]**2) for a,b in [(0,.012),(.04,.1)]]
    result['late_vs_early_db']=float(10*np.log10(max(energies[1],1e-30)/max(energies[0],1e-30)))
    return result

def contrast(a,b):
    out={'byte_identical':a['sha256']==b['sha256'],'raw_rms_delta_db':20*np.log10(b['features']['rms_0_500']/a['features']['rms_0_500'])}
    for win in WINDOWS:
        x=a['features'][win];y=b['features'][win];diff=np.array(y['normalized_band_db'])-x['normalized_band_db']
        weights=(np.array(x['band_fraction'])+y['band_fraction'])/2
        active=np.maximum(x['normalized_band_db'],y['normalized_band_db'])>-50
        out[win]={'active_band_mean_abs_db':float(np.mean(np.abs(diff[active]))),
                  'power_weighted_abs_db':float(np.sum(weights*np.abs(diff))),
                  'centroid_ratio':y['centroid_hz']/x['centroid_hz'],
                  'centroid_delta_hz':y['centroid_hz']-x['centroid_hz'],
                  'high_fraction_delta_db':y['high_fraction_db']-x['high_fraction_db'],
                  'normalized_band_delta_db':diff.tolist()}
    out['late_vs_early_delta_db']=b['features']['late_vs_early_db']-a['features']['late_vs_early_db']
    return out

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--renderer',action='append',required=True);ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();out=args.output;out.mkdir()
    specs=set()
    for material,note,velocity,timbre,style in itertools.product(['steel','nylon'],NOTES,VELOCITIES,[-1,.3],['finger','pick','thumb']):
        specs.add((material,'auditorium',style,note,velocity,timbre,'stereo_mic',.58))
    for material,note,shape,capture in itertools.product(['steel','nylon'],NOTES,['parlor','auditorium','dreadnought','jumbo'],['stereo_mic','piezo']):
        specs.add((material,shape,'finger',note,91,-1,capture,.58))
    renderers={};jobs=[]
    for item in args.renderer:
        label,path=item.split('=',1);directory=out/label;directory.mkdir();exe=directory/'renderer';shutil.copy2(path,exe);exe.chmod(0o700)
        renderers[label]={'path':str(Path(path).resolve()),'sha256':sha(exe)}
        for spec in sorted(specs):jobs.append((label,exe,spec))
    def render(job):
        label,exe,spec=job;mat,shape,style,note,velocity,timbre,capture,touch=spec
        name=f'{mat}-{shape}-{style}-n{note}-v{velocity}-p{timbre}-{capture}'
        raw=exe.parent/(name+'.f32');cmd=[str(exe.resolve()),str(raw.resolve()),*map(str,spec)]
        subprocess.run(cmd,check=True,capture_output=True)
        audio=np.fromfile(raw,'<f4').reshape(-1,2).astype(float);assert audio.shape==(96000,2) and np.isfinite(audio).all() and np.any(audio)
        f=features(audio);gain=.035/f['rms_0_500'];peak=np.max(np.abs(audio*gain));assert peak<.99,('audition peak exceeds fixed target',peak)
        wav=raw.with_suffix('.wav');wavfile.write(wav,RATE,(audio*gain).astype(np.float32))
        return {'label':label,'material':mat,'shape':shape,'picking':style,'note':note,'velocity':velocity,'timbre':timbre,'capture':capture,'touch':touch,'raw_path':str(raw),'wav_path':str(wav),'sha256':sha(raw),'command':cmd,'features':f,'audition_gain':gain}
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:models=list(pool.map(render,jobs))
    def get(label,mat,shape,style,note,vel,timbre,capture):
        return next(x for x in models if (x['label'],x['material'],x['shape'],x['picking'],x['note'],x['velocity'],x['timbre'],x['capture'])==(label,mat,shape,style,note,vel,timbre,capture))
    pairs=[]
    for label,mat,vel,timbre,note in itertools.product(renderers,['steel','nylon'],VELOCITIES,[-1,.3],NOTES):
        for first,second in [('finger','pick'),('finger','thumb'),('thumb','pick')]:
            a=get(label,mat,'auditorium',first,note,vel,timbre,'stereo_mic');b=get(label,mat,'auditorium',second,note,vel,timbre,'stereo_mic')
            pairs.append({'type':'picking','label':label,'material':mat,'velocity':vel,'timbre':timbre,'note':note,'pair':first+'_'+second,**contrast(a,b)})
    for label,mat,capture,note in itertools.product(renderers,['steel','nylon'],['stereo_mic','piezo'],NOTES):
        for first,second in itertools.combinations(['parlor','auditorium','dreadnought','jumbo'],2):
            a=get(label,mat,first,'finger',note,91,-1,capture);b=get(label,mat,second,'finger',note,91,-1,capture)
            pairs.append({'type':'shape','label':label,'material':mat,'capture':capture,'note':note,'pair':first+'_'+second,**contrast(a,b)})
    summaries={}
    for row in pairs:
        key='|'.join(str(row.get(k,'')) for k in ['type','label','material','velocity','timbre','capture','pair']);summaries.setdefault(key,[]).append(row)
    summaries={key:{'count':len(rows),'byte_identical':sum(x['byte_identical'] for x in rows),'raw_rms_delta_db_median':float(np.median([x['raw_rms_delta_db'] for x in rows])),
         **{w:{k:float(np.median([x[w][k] for x in rows])) for k in ['active_band_mean_abs_db','power_weighted_abs_db','centroid_ratio','centroid_delta_hz','high_fraction_delta_db']} for w in WINDOWS}} for key,rows in summaries.items()}
    preservation=[];labels=list(renderers)
    for label in labels[1:]:
        rows=[x for x in models if x['label']==labels[0] and x['picking']=='finger'];same=0
        for r in rows:
            b=get(label,r['material'],r['shape'],r['picking'],r['note'],r['velocity'],r['timbre'],r['capture']);same+=r['sha256']==b['sha256']
        preservation.append({'reference':labels[0],'label':label,'finger_count':len(rows),'finger_byte_identical':same})
    report={'status':'complete','protocol':__doc__,'script_sha256':sha(__file__),'renderers':renderers,'notes':NOTES,'velocities':VELOCITIES,'band_edges_hz':BANDS.tolist(),'windows_seconds':WINDOWS,
            'normalization':'Stereo average power, each note divided by its own0–500ms stereoRMS². WAVs match that RMS to0.035. Fixed note-on windows; no onset/time shift. Default Spruce/Original/Touch.58/Pluck.28/Body.82/Stereo.62, same seed per fresh engine.',
            'summary':summaries,'finger_preservation':preservation,'pairs':pairs,'models':models}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'models':len(models),'pairs':len(pairs),'finger_preservation':preservation,'report':str(out/'report.json')},indent=2))

if __name__=='__main__':main()
