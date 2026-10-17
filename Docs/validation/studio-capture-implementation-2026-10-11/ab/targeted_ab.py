#!/usr/bin/env python3
"""Fixed, geometry-preserving pitch sweeps of native before/after captures."""
from pathlib import Path
import argparse,concurrent.futures,hashlib,importlib.util,itertools,json,os,shutil,subprocess,time
import numpy as np
from scipy.signal import periodogram

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
RATE=48000
CENTRES=[('parlor',47,2,'B1'),('auditorium',45,2,'A1'),('jumbo',40,1,'E1'),('jumbo',41,1,'F1')]
WOODS=('spruce','mahogany')
CAPTURES=('stereo_mic','mono_mic')

def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,obj):p.write_text(json.dumps(obj,indent=2,allow_nan=False)+'\n')
def frequency(midi):return 440*2**((midi-69)/12)

def prepare():
    (HERE/'events').mkdir(exist_ok=False)
    frozen_metric=HERE/'MeasureMicrophoneBoom.py'
    shutil.copy2(ROOT/'Docs/validation-microphone-boom-20261011/MeasureMicrophoneBoom.py',frozen_metric)
    cases=[]
    def add(group,shape,note,channel,label,wood,capture,technique,velocity,cents,partial_hz=None):
        key=f'{group}-{shape}-{wood}-{capture}-{technique}-v{velocity}-m{note}-s{channel}-c{cents:+.6f}'
        cases.append(dict(key=key,group=group,shape=shape,wood=wood,capture=capture,
            technique=technique,velocity=velocity,midi=note,channel=channel,reason_note=label,
            master_tune_cents=cents,expected_fundamental_hz=frequency(note+cents/100),
            requested_third_partial_hz=partial_hz))
    for (shape,note,channel,label),wood,capture in itertools.product(CENTRES,WOODS,CAPTURES):
        for cents in range(-100,101,20):
            add('primary',shape,note,channel,label,wood,capture,'finger',96,cents)
        for (technique,velocity),cents in itertools.product([('pick',120),('thumb',56)],[-50,0,50]):
            add('excitation',shape,note,channel,label,wood,capture,technique,velocity,cents)
    for wood,capture,(technique,velocity),partial in itertools.product(WOODS,CAPTURES,
            [('finger',96),('pick',120),('thumb',56)],[245.,254.]):
        cents=1200*np.log2(partial/(3*frequency(40)))
        add('partial','parlor',40,1,'E1',wood,capture,technique,velocity,float(cents),partial)
    assert len(cases)==296 and len({c['key'] for c in cases})==296
    for c in cases:
        event=HERE/'events'/(c['key']+'.events')
        event.write_text(f"ACUSTRA_PERFORMANCE_V1 {RATE} {RATE*4}\n0 {c['channel']} {c['midi']} {c['velocity']} 0\n{RATE*3} {c['channel']} {c['midi']} 0 0\n")
        c['event_sha256']=sha(event)
    manifest=dict(status='Frozen test plan; no weight selection, optimization or per-note normalization.',
        cases=cases,cases_per_version=len(cases),primary=176,bounded_excitation=96,partial_witness=24,
        before_revision='d54efb2800a12130cd1027a8d64c1c0970e0ae9b',
        tune_policy='Scratch CLI calls setMasterTuneCents before notes. All open/stopped speaking geometry is retained; all strings retension with the global reference, while body and microphone poles/calibration remain fixed. No slide wheel bend is sent.',
        controls=dict(rate=RATE,block_frames=127,touch=.58,pluck_position=.28,body_amount=.82,
            stereo_width=.62,output_linear=.42,room=0,string_age=.15,piezo_mix=0,release_noise=0,
            hold_seconds=3,duration_seconds=4,fresh_engine_per_case=True,tuning='standard',pitch_bend_semitones=0),
        metric_policy='Frozen MeasureMicrophoneBoom.metric, evaluated at nominal midi+cents/100; attack/early/held/late/release windows unchanged. Additional settled H1 spectral-peak estimate is descriptive, not a pass condition.',
        audition_policy='One AFTER scalar per capture matches pooled RMS over the complete primary Finger96 sweep (all target shapes/woods/pitches), using0.02–2.8s. That scalar applies unchanged to every after audition in that capture. A common pair headroom gain is applied only if required for−1dBFS. No per-model, note, technique or window gain.',
        limitations=['Master tuning shifts all string references and sympathetic pitches; it is a physically consistent excitation sweep, not a pure external force-frequency sweep.',
            'Body/microphone poles are fixed for each construction. Existing attack pitch and body pulling can shift measured peaks slightly from nominal excitation.',
            'Two visible woods at48kHz. No Maple, sample-rate matrix, new model tuning or listening acceptance.',
            'Early-window harmonic integration cannot resolve arbitrarily narrow cancellation. H1–H4 powers and partial witness cases remain separate from target balance.'],
        sha256={str(p.relative_to(HERE)):sha(p) for p in [Path(__file__),frozen_metric,HERE/'TunedPerformanceRenderer.cpp',HERE/'before-manifest.json',HERE/'preflight/receipt.json']})
    save(HERE/'plan.json',manifest)
    print('Frozen296 cases/version; partial tuning cents:',sorted({c['master_tune_cents'] for c in cases if c['group']=='partial'}))

def run(version,workers):
    plan=json.loads((HERE/'plan.json').read_text())
    for path,expected in plan['sha256'].items():assert sha(HERE/path)==expected,path
    binary=HERE/version/'TunedPerformanceRenderer'
    assert binary.is_file(),binary
    build_manifest=HERE/(version+'-manifest.json')
    assert build_manifest.is_file(),build_manifest
    out=HERE/(version+'-renders');out.mkdir(exist_ok=False);(out/'audio').mkdir()
    spec=importlib.util.spec_from_file_location('frozen_boom_metric',HERE/'MeasureMicrophoneBoom.py')
    metric=importlib.util.module_from_spec(spec);spec.loader.exec_module(metric)
    env={k:v for k,v in os.environ.items() if not k.startswith('ACUSTRA_STUDIO_')}
    def one(c):
        event=HERE/'events'/(c['key']+'.events');assert sha(event)==c['event_sha256']
        path=out/'audio'/(c['key']+'.f32')
        cmd=[str(binary),str(event),str(path),c['capture'],c['technique'],
            '--body-shape',c['shape'],'--body-material',c['wood'],'--room','0',
            '--master-tune-cents',format(c['master_tune_cents'],'.17g')]
        p=subprocess.run(cmd,env=env,capture_output=True,text=True)
        if p.returncode:raise RuntimeError(c['key']+': '+p.stderr)
        audio=np.fromfile(path,dtype='<f4').reshape(-1,2).astype(np.float64)
        assert audio.shape==(RATE*4,2) and np.isfinite(audio).all() and np.any(audio)
        tuned_midi=c['midi']+c['master_tune_cents']/100
        windows={name:metric.metric(audio[round(a*RATE):round(b*RATE)],tuned_midi)
            for name,(a,b) in metric.WINDOWS.items()}
        fold=audio.mean(axis=1,keepdims=True)
        folded={name:metric.metric(fold[round(a*RATE):round(b*RATE)],tuned_midi)
            for name,(a,b) in metric.WINDOWS.items()}
        f,power=periodogram(audio[24000:134400],fs=RATE,axis=0,window='hann',detrend=False,
                            scaling='density',nfft=131072)
        power=power.mean(axis=1);expected=c['expected_fundamental_hz']
        search=np.flatnonzero((f>=expected*.96)&(f<=expected*1.04))
        index=search[np.argmax(power[search])]
        y=np.log(np.maximum(power[index-1:index+2],1e-300))
        offset=.5*(y[0]-y[2])/(y[0]-2*y[1]+y[2])
        peak_hz=float(f[index]+np.clip(offset,-.5,.5)*(f[1]-f[0]))
        return {**c,'command':cmd,'audio_sha256':sha(path),'all_finite':True,
            'peak_dbfs':metric.db(np.max(abs(audio))**2),
            'full_scale_samples':int(np.sum(abs(audio)>=1)),
            'matching_reference_energy':float(np.sum(audio[960:134400]**2)),
            'estimated_settled_h1_hz':peak_hz,'estimated_h1_offset_cents':float(1200*np.log2(peak_hz/expected)),
            'windows':windows,'stereo_fold_windows':folded}
    start=time.monotonic();rows=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for row in pool.map(one,plan['cases']):
            rows.append(row)
            with (out/'progress.jsonl').open('a') as f:f.write(json.dumps(row,allow_nan=False)+'\n')
            if len(rows)%32==0:print(version,len(rows),'/',len(plan['cases']),round(time.monotonic()-start,1),'s',flush=True)
    save(out/'report.json',dict(version=version,renderer_sha256=sha(binary),
        build_manifest_sha256=sha(build_manifest),plan_sha256=sha(HERE/'plan.json'),
        render_seconds=time.monotonic()-start,max_workers=workers,renders=rows))
    print('Complete',version,len(rows),round(time.monotonic()-start,1),'s',flush=True)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('command',choices=['prepare','before','after'])
    ap.add_argument('--workers',type=int,choices=[1,2],default=1)
    a=ap.parse_args()
    if a.command=='prepare':prepare()
    else:run(a.command,a.workers)

if __name__=='__main__':main()
