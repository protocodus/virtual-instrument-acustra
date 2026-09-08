#!/usr/bin/env python3
"""Calibrate or evaluate Acustra against two named, recorded acoustic guitars.

The first two filename-ordered finger/thumb takes at fixed disjoint pitches
form training/development splits. The archive hash and selection precede audio
analysis. The optional bounded search reads only training scores. Validation
is evaluated once after its winner is selected; it never selects a replacement.
This is exploratory because source capture, velocity and fingering are unknown.
No recording or candidate audio is committed. See Docs/realism-work.md.
"""
from pathlib import Path
import concurrent.futures as cf
import argparse, hashlib, json, subprocess, sys, zipfile
import numpy as np
import scipy

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'Tools'))
import BenchmarkTechniqueNotes as data
import FitPhysicalModel as features
from OptimizePhysicalModel import NAMES, LOWER, UPPER

# Freeze code bytes at startup, before another build can change the workspace.
SOURCE_BYTES = {path.name: path.read_bytes() for path in (
    Path(__file__), Path(features.__file__), Path(data.__file__),
    ROOT / 'Tools/OptimizePhysicalModel.py', ROOT / 'Tools/BenchmarkPerformances.py')}

OUT = RENDERER = ARCHIVE = None
TRAIN = (40,45,50,55,59,64,69,74,79)
VALID = (42,47,52,57,62,67,72,77,80)
# Source timing, exact tuning and dynamics are unknown. Fit only normalized
# spectral shape and decay, using the existing scorer's scales and penalties.
WEIGHTS = {'attack': .15, 'harmonics': .35, 'decay': .35, 'body': .15}
BASE = {}
TARGETS = {}

def loss(target, model):
    residual = features._example_residuals(target, model)
    residual['attack'] = residual['attack'][1:]  # not unknown source pre-roll
    return {name: float(np.mean(features._huber_squared(np.asarray(residual[name]))))
            for name in WEIGHTS}

def evaluate(job):
    material, name, values, split = job
    directory = OUT/material/name/split
    directory.mkdir(parents=True, exist_ok=False)
    calibration = directory/'calibration.txt'
    calibration.write_text(''.join(f'{key} {value:.10g}\n' for key,value in values.items()))
    terms = []
    for midi in TRAIN if split == 'train' else VALID:
        fingering = data.fingering(midi)
        event = directory/f'{midi}.events'
        model = directory/f'{midi}.f32'
        data.write_events(event, [(0,fingering['channel'],midi,91,0.)], 96000)
        subprocess.run([str(RENDERER),str(event),str(model),'stereo_mic','finger','original',
                        '--string-material',material,'--tuning',fingering['tuning'],
                        '--body-shape','auditorium','--body-material',
                        'mahogany' if material == 'steel' else 'spruce',
                        '--calibration',str(calibration)],check=True,capture_output=True)
        audio = np.fromfile(model,dtype='<f4').reshape(-1,2).mean(axis=1).astype(float)
        if not np.isfinite(audio).all() or not np.any(audio):
            raise ValueError('invalid render')
        model_features = features.extract_features(audio,48000,midi)
        for target in TARGETS[material,midi]:
            terms.append(loss(target,model_features))
    average = {key:float(np.mean([term[key] for term in terms])) for key in WEIGHTS}
    result = {'material':material,'candidate':name,'split':split,'values':values,
              'score':sum(WEIGHTS[key]*average[key] for key in WEIGHTS),'terms':average}
    (directory/'score.json').write_text(json.dumps(result,indent=2)+'\n')
    print(material,name,split,round(result['score'],6),flush=True)
    return result

def main():
    global OUT, RENDERER, ARCHIVE, BASE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--initial-calibration', type=Path, required=True,
                        help='JSON object of the 29 named physical calibration values')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fit', action='store_true', help='run bounded training-only search')
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    OUT, RENDERER, ARCHIVE = (p.resolve() for p in (args.output,args.renderer,args.archive))
    BASE = json.loads(args.initial_calibration.read_text())
    if set(BASE) != set(NAMES) or any(not isinstance(BASE[k],(float,int))
            or not np.isfinite(BASE[k]) or not LOWER[i] <= BASE[k] <= UPPER[i]
            for i,k in enumerate(NAMES)):
        parser.error('calibration must contain exactly 29 finite, bounded named values')
    if not RENDERER.is_file() or not ARCHIVE.is_file():
        parser.error('renderer and archive must exist')
    OUT.mkdir(parents=True, exist_ok=False)
    source_renderer = RENDERER
    executable = source_renderer.read_bytes()
    RENDERER = OUT / 'renderer-snapshot'
    RENDERER.write_bytes(executable)
    RENDERER.chmod(0o700)
    source_directory = OUT / 'source-snapshot'
    source_directory.mkdir()
    for name, content in SOURCE_BYTES.items():
        (source_directory / name).write_bytes(content)
    provenance = {
        'source_renderer_path': str(source_renderer),
        'renderer_sha256': hashlib.sha256(executable).hexdigest(),
        'source_sha256': {name: hashlib.sha256(content).hexdigest()
                          for name, content in SOURCE_BYTES.items()},
        'numpy_version': np.__version__, 'scipy_version': scipy.__version__,
        'python_version': sys.version,
    }
    (OUT / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    if hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() != data.ARCHIVE_SHA256:
        raise ValueError('reference archive checksum mismatch')
    selection=[]
    with zipfile.ZipFile(ARCHIVE) as archive:
        for row in data.select_targets(archive.namelist()):
            if row['label'] not in ('sfn','nfn') or row['midi'] not in TRAIN+VALID:
                continue
            raw=archive.read(row['source_member'])
            target=features.extract_features(data.read_reference(raw,row['source_member']),48000,row['midi'])
            TARGETS.setdefault((row['material'],row['midi']),[]).append(target)
            selection.append({**row,'sha256':hashlib.sha256(raw).hexdigest(),
                              'split':'train' if row['midi'] in TRAIN else 'validation'})
    (OUT/'selection.json').write_text(json.dumps(selection,indent=2)+'\n')
    results=[]
    chosen={}
    # Thread pool: NumPy feature extraction and subprocess rendering release
    # the GIL; each job writes its own new directory and immutable controls.
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for material in ('nylon','steel'):
            current=BASE.copy()
            baseline=evaluate((material,'baseline',current,'train'))
            results.append(baseline)
            best=baseline
            for iteration in range(2 if args.fit else 0):
                jobs=[]
                for field in ('fundamentalT60Scale','frequencyLossScale','apertureScale','pluckDistanceScale'):
                    key=material+'.'+field
                    lower,upper=LOWER[NAMES.index(key)],UPPER[NAMES.index(key)]
                    for multiplier in (.8,1.2):
                        values=current.copy()
                        values[key]=float(np.clip(current[key]*multiplier,lower,upper))
                        jobs.append((material,f'i{iteration}-{field}-{multiplier}',values,'train'))
                evaluated=list(pool.map(evaluate,jobs))
                results.extend(evaluated)
                winner=min([best]+evaluated,key=lambda row:row['score'])
                if winner['score'] >= best['score']:
                    break
                best=winner
                current=best['values'].copy()
            chosen[material]=best
            # Validation is first read after selecting the training winner;
            # its result never selects a different candidate.
            validation_jobs=[(material,'baseline',BASE,'validation')]
            if args.fit:
                validation_jobs.append((material,'selected',current,'validation'))
            results.extend(pool.map(evaluate,validation_jobs))
            (OUT/f'{material}-selected.json').write_text(json.dumps(best,indent=2)+'\n')
    report={'protocol':'fixed disjoint pitches; two author-labeled finger/thumb takes each; training-only two-step bounded coordinate poll; no source latency/pitch/dynamics inference',
            'fitting_enabled':args.fit,'source':data.SOURCE,'archive_sha256':data.ARCHIVE_SHA256,
            **provenance,
            'instruments':{'steel':'Walden G551E grand auditorium, mahogany top',
                           'nylon':'Yamaha CM-40 classical; auditorium model proxy'},
            'render_controls':{'sample_rate':48000,'frames':96000,'velocity':91,
                               'picking':'finger','capture':'stereo_mic',
                               'analysis_mix':'arithmetic mean of left and right',
                               'bridge_model':'original','body_shape':'auditorium',
                               'body_material':{'steel':'mahogany','nylon':'spruce'},
                               'bend_semitones':0,'note_off':False,
                               'fingering':{str(midi):data.fingering(midi) for midi in TRAIN+VALID},
                               'other_controls':'compiled defaults pinned in renderer-snapshot',
                               'calibration':'all 29 values explicitly supplied for every candidate'},
            'train_midi':TRAIN,'validation_midi':VALID,'weights':WEIGHTS,
            'selected':chosen,'results':results,
            'limitations':['microphone/pickup and pluck velocity not documented',
                           'source fingering not documented; least-fret channel inferred',
                           'both model bodies are proxies, not independently identified target instruments',
                           'training has six open-string model pitches; validation has none (open-to-fretted distribution shift)',
                           'no perceptual claim; cross-instrument validation required before promotion']}
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__ == '__main__':main()
