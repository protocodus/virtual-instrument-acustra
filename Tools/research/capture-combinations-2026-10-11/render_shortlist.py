#!/usr/bin/env python3
"""Native confirmation of fixed mixtures selected by a phase-aware scratch screen."""
from pathlib import Path
import concurrent.futures,csv,hashlib,importlib.util,json,os,shutil,time
import numpy as np
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];OLD=ROOT/'build-audit/studio-mic-variants'
HARNESS=OLD/'harness';RENDERER=HARNESS/'StudioPerformanceRenderer'
INPUTS=[HARNESS/'verification/baseline-observer.csv',OLD/'measured_bridge_coefficients.csv',ROOT/'build-audit/studio-mic-research/runtime-candidates-v2/neck30_literal264.csv',ROOT/'build-audit/studio-mic-research/runtime-candidates-v2/offhole45_literal264.csv',ROOT/'build-audit/studio-mic-research/runtime-free-candidates-v1/free_neck30.csv',OLD/'measured_blend_coefficients.csv']
NAMES=['baseline','measured_bridge','neck30_literal264','offhole45_literal264','free_neck30','measured_blend'];GAINS=np.array([1,1,3,4.5,3,1.])
SELECTION=[('balanced_all','grid_2701'),('measured_continuity','grid_2977'),('equal_five','equal_five')]
SCHEMA='shape,wood,mode,leftRe,leftIm,rightRe,rightIm,leftMomentRe,leftMomentIm,rightMomentRe,rightMomentIm'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return {tuple(map(int,r[:3])):np.array(r[3:],dtype=np.float32).astype(float) for r in csv.reader(p.open()) if r and r[0]!='shape' and not r[0].startswith('#')}
def main():
    allscreen=json.loads((HERE/'screen.json').read_text());screen={r['name']:r for r in allscreen['results']}
    tables=[read(p)for p in INPUTS];assert all(t.keys()==tables[0].keys()for t in tables)
    source=ROOT/'Docs/validation-microphone-boom-20261011/MeasureMicrophoneBoom.py'
    spec=importlib.util.spec_from_file_location('measure',source);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    base=json.loads((OLD/'baseline-extended/report.json').read_text())
    for name,key in SELECTION:
        out=HERE/name;out.mkdir(exist_ok=False);(out/'audio').mkdir();(out/'events').mkdir()
        choice=screen[key];weights=np.array([choice['weights'][n]for n in NAMES]);assert abs(sum(weights)-1)<1e-12
        coeff=out/'observation.csv'
        with coeff.open('w')as f:
            f.write(SCHEMA+'\n')
            for k in sorted(tables[0]):
                v=sum(w*g*t[k]for w,g,t in zip(weights,GAINS,tables)).astype(np.float32)
                f.write(','.join(map(str,k))+','+','.join(format(float(x),'.17g')for x in v)+'\n')
        manifest={'name':name,'selection_screen_name':key,'weights':dict(zip(NAMES,map(float,weights))),'nominal_compensation':dict(zip(NAMES,map(float,GAINS))),
          'scope':'Constant complex-observation mixture. Same weights for every construction, input, channel and mode. No EQ, phase rotation, timing offset, pole or mechanical changes.',
          'selection':choice,'inputs':[{'path':str(p),'sha256':sha(p)}for p in INPUTS], 'coefficient_sha256':sha(coeff),'screen_sha256':sha(HERE/'screen.json'),'runner_sha256':sha(Path(__file__))}
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
        binary=out/'AcustraStudioObserverRenderer';shutil.copy2(RENDERER,binary)
        for r in base['renders']:
            midi,ch=r['midi'],r['channel'];(out/'events'/f'm{midi}-s{ch}.events').write_text(f'ACUSTRA_PERFORMANCE_V1 48000 192000\n0 {ch} {midi} 96 0\n144000 {ch} {midi} 0 0\n')
        m.HERE=out;m.RENDERER=binary
        for e in list(os.environ):
            if e.startswith('ACUSTRA_STUDIO_'):del os.environ[e]
        os.environ['ACUSTRA_STUDIO_OBSERVER_FILE']=str(coeff)
        cases=[tuple(r[k]for k in ['shape','wood','capture','midi','channel'])for r in base['renders']]
        t=time.monotonic();rows=[]
        with concurrent.futures.ThreadPoolExecutor(max_workers=2)as pool:
            for i,r in enumerate(pool.map(m.run,cases),1):
                rows.append(r)
                if i%100==0:print(name,i,'/',len(cases),round(time.monotonic()-t,1),'s',flush=True)
        report={k:v for k,v in base.items()if k not in ['renders','extension','neighbor_comparisons','experiment']}
        report.update({'renders':rows,'neighbor_comparisons':[],'purpose':'Native confirmation of a fixed observation mixture on the same frozen current instrument.','renderer_sha256':sha(binary),'script_sha256':sha(Path(__file__)),'render_seconds':time.monotonic()-t,'experiment':manifest})
        (out/'report.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');print(name,'complete',flush=True)
if __name__=='__main__':main()
