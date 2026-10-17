#!/usr/bin/env python3
"""One fixed new-excitation holdout; no fitting, coefficient edits or rebuild."""
from pathlib import Path
import concurrent.futures
import hashlib
import importlib.util
import itertools
import json
import os
import shutil
import subprocess
import time
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
COMBINATIONS = HERE.parent
HARNESS = ROOT / 'build-audit/studio-mic-variants/harness'
MEASURE = ROOT / 'Docs/validation-microphone-boom-20261011/MeasureMicrophoneBoom.py'
RATE = 48000
NOTES = [(40,1),(41,1),(42,1),(45,2),(47,2),(51,3),(53,3),(59,5)]
EXCITATIONS = [('pick',120),('thumb',56)]
VARIANTS = ['baseline','balanced_all','measured_continuity']
TARGETS = {'parlor':[47], 'auditorium':[45], 'jumbo':[40,41]}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')


def balance(w):
    h = w['partial_power_dbfs']
    return h[0]-10*np.log10((10**(h[1]/10)+10**(h[2]/10))/2)


def main():
    frozen = HERE/'frozen'
    frozen.mkdir(exist_ok=False)
    binary = frozen/'StudioPerformanceRenderer'
    shutil.copy2(HARNESS/'StudioPerformanceRenderer',binary)
    shutil.copy2(MEASURE,frozen/MEASURE.name)
    spec = importlib.util.spec_from_file_location('frozen_boom_measurement',frozen/MEASURE.name)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    sources = {'baseline':HARNESS/'verification/baseline-observer.csv',
        **{name:COMBINATIONS/name/'observation.csv' for name in VARIANTS[1:]}}
    for name, source in sources.items():
        shutil.copy2(source,frozen/(name+'.csv'))
        if name != 'baseline':
            shutil.copy2(COMBINATIONS/name/'manifest.json',frozen/(name+'-selection-manifest.json'))
    cases = [(*cell,*excitation,midi,channel)
        for cell in itertools.product(m.SHAPES,m.WOODS,('stereo_mic','mono_mic'))
        for excitation in EXCITATIONS for midi,channel in NOTES]
    assert len(cases)==256
    manifest = dict(purpose='Fixed post-selection holdout, no subsequent tuning.',
        variants=VARIANTS,cases_per_variant=len(cases),
        case_fields=['shape','wood','capture','technique','velocity','midi','channel'],cases=cases,
        controls=dict(sample_rate=RATE,block_frames=127,touch=.58,pluck_position=.28,
            body_amount=.82,stereo_width=.62,output_gain=.42,room=0,piezo_mix=0,
            release_noise=0,string_age=.15,tuning='standard',bend_semitones=0,
            hold_seconds=3,duration_seconds=4,fresh_engine_per_clip=True),
        windows_seconds=m.WINDOWS,nominal_gain_compensation='Already fixed in mixture coefficients; no further audio gain or normalization.',
        first_position_note_channel_pairs=NOTES,max_workers=1,
        hashes={str(path):sha(path) for path in [Path(__file__),binary,frozen/MEASURE.name,
            *[frozen/(name+'.csv') for name in VARIANTS],
            *[frozen/(name+'-selection-manifest.json') for name in VARIANTS[1:]]]},
        limits=['New excitation settings and some string routes, not an independent measured guitar or studio response.',
            '48kHz, two visible woods, eight notes, isolated plucks. No listening acceptance.',
            'H1/H2-H3 is a balance ratio; same-note H1 power, clipping and fold-down are assessed separately.',
            'Only MIDI41 has both adjacent-note samples on the same string in this fixed sparse grid.'])
    write_json(HERE/'manifest.json',manifest)
    start = time.monotonic()
    reports = {}
    for variant in VARIANTS:
        out = HERE/variant
        out.mkdir(exist_ok=False)
        (out/'audio').mkdir()
        (out/'events').mkdir()
        for technique,velocity in EXCITATIONS:
            for midi,ch in NOTES:
                (out/'events'/f'{technique}-v{velocity}-m{midi}-s{ch}.events').write_text(
                    f'ACUSTRA_PERFORMANCE_V1 {RATE} {RATE*4}\n0 {ch} {midi} {velocity} 0\n{RATE*3} {ch} {midi} 0 0\n')
        environment = {key:value for key,value in os.environ.items() if not key.startswith('ACUSTRA_STUDIO_')}
        environment['ACUSTRA_STUDIO_OBSERVER_FILE']=str(frozen/(variant+'.csv'))

        def run(case):
            shape,wood,capture,technique,velocity,midi,ch = case
            key=f'{shape}-{wood}-{capture}-{technique}-v{velocity}-m{midi}-s{ch}'
            events=out/'events'/f'{technique}-v{velocity}-m{midi}-s{ch}.events'
            raw=out/'audio'/(key+'.f32')
            cmd=[str(binary),str(events),str(raw),capture,technique,'--body-shape',shape,
                '--body-material',wood,'--room','0']
            process=subprocess.run(cmd,env=environment,capture_output=True,text=True)
            if process.returncode:
                raise RuntimeError(key+': '+process.stderr)
            audio=np.fromfile(raw,dtype='<f4').reshape(-1,2).astype(np.float64)
            assert audio.shape==(RATE*4,2) and np.isfinite(audio).all() and np.any(audio)
            windows={name:m.metric(audio[round(a*RATE):round(b*RATE)],midi)
                     for name,(a,b) in m.WINDOWS.items()}
            fold=audio.mean(axis=1,keepdims=True)
            fold_windows={name:m.metric(fold[round(a*RATE):round(b*RATE)],midi)
                     for name,(a,b) in m.WINDOWS.items()}
            return dict(key=key,shape=shape,wood=wood,capture=capture,technique=technique,
                velocity=velocity,midi=midi,channel=ch,fret=midi-(40,45,50,55,59,64)[ch-1],
                command=cmd,audio_sha256=sha(raw),event_sha256=sha(events),
                sample_count_per_channel=len(audio),all_finite=True,
                peak_dbfs=m.db(np.max(abs(audio))**2),samples_at_full_scale=int(np.sum(abs(audio)>=1)),
                mono_channel_max_difference=float(np.max(abs(audio[:,0]-audio[:,1]))) if capture=='mono_mic' else None,
                windows=windows,stereo_fold_windows=fold_windows)
        variant_start=time.monotonic()
        rows=[]
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
            for row in pool.map(run,cases):
                rows.append(row)
                with (out/'progress.jsonl').open('a') as f:
                    f.write(json.dumps(row,allow_nan=False)+'\n')
                if len(rows)%32==0:
                    print(variant,len(rows),'/',len(cases),round(time.monotonic()-variant_start,1),'s',flush=True)
        reports[variant]=dict(renders=rows,render_seconds=time.monotonic()-variant_start,
                             renderer_sha256=sha(binary),coefficient_sha256=sha(frozen/(variant+'.csv')))
        write_json(out/'report.json',reports[variant])
    base={r['key']:r for r in reports['baseline']['renders']}
    comparisons={}
    for variant in VARIANTS[1:]:
        changes=[]
        for row in reports[variant]['renders']:
            b=base[row['key']]
            for window in ('attack','early','held','late'):
                w,bw=row['windows'][window],b['windows'][window]
                fw,bfw=row['stereo_fold_windows'][window],b['stereo_fold_windows'][window]
                changes.append({**{key:row[key] for key in ['key','shape','wood','capture','technique','velocity','midi','channel']},
                    'window':window,'target':row['midi'] in TARGETS.get(row['shape'],[]),
                    'h1_change_db':w['partial_power_dbfs'][0]-bw['partial_power_dbfs'][0],
                    'h1_h2h3_balance_change_db':float(balance(w)-balance(bw)),
                    'rms_change_db':w['rms_dbfs']-bw['rms_dbfs'],
                    'fold_retention_db':fw['rms_dbfs']-w['rms_dbfs'],
                    'fold_retention_change_db':fw['rms_dbfs']-w['rms_dbfs']-bfw['rms_dbfs']+bw['rms_dbfs'],
                    'fold_h1_change_db':fw['partial_power_dbfs'][0]-bfw['partial_power_dbfs'][0]})
        def extrema(rows,key):
            return dict(minimum=min(rows,key=lambda x:x[key]),maximum=max(rows,key=lambda x:x[key]))
        target_summary=[]
        for shape,technique,window in itertools.product(TARGETS,('pick','thumb'),('attack','early','held','late')):
            selected=[r for r in changes if r['target'] and r['shape']==shape and r['technique']==technique and r['window']==window]
            target_summary.append(dict(shape=shape,technique=technique,window=window,cases=len(selected),
                **{key:dict(min=min(r[key] for r in selected),median=float(np.median([r[key] for r in selected])),
                            max=max(r[key] for r in selected))
                    for key in ('h1_h2h3_balance_change_db','h1_change_db')}))
        comparisons[variant]=dict(target_summary=target_summary,
            all_h1_extrema=extrema(changes,'h1_change_db'),
            lower_h1_extrema=extrema([r for r in changes if r['midi']<=47],'h1_change_db'),
            upper_h1_extrema=extrema([r for r in changes if r['midi']>=51],'h1_change_db'),
            upper_balance_extrema=extrema([r for r in changes if r['midi']>=51],'h1_h2h3_balance_change_db'),
            fold_extrema=extrema([r for r in changes if r['capture']=='stereo_mic'],'fold_retention_db'),
            fold_change_extrema=extrema([r for r in changes if r['capture']=='stereo_mic'],'fold_retention_change_db'),
            peak_case=max(reports[variant]['renders'],key=lambda r:r['peak_dbfs'])['key'],
            maximum_peak_dbfs=max(r['peak_dbfs'] for r in reports[variant]['renders']),
            full_scale_samples=sum(r['samples_at_full_scale'] for r in reports[variant]['renders']),
            mono_channel_max_difference=max(r['mono_channel_max_difference'] for r in reports[variant]['renders'] if r['capture']=='mono_mic'),
            changes=changes)
    write_json(HERE/'comparison.json',dict(manifest_sha256=sha(HERE/'manifest.json'),
        total_seconds=time.monotonic()-start,baseline_maximum_peak_dbfs=max(r['peak_dbfs'] for r in reports['baseline']['renders']),
        report_hashes={name:sha(HERE/name/'report.json') for name in VARIANTS},variants=comparisons))
    print('COMPLETE',round(time.monotonic()-start,1),'s',flush=True)


if __name__=='__main__':
    main()
