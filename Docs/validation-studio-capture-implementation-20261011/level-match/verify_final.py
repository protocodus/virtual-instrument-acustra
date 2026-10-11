#!/usr/bin/env python3
"""Verify generated flat gains using final native phrase and hardest-case audio."""
from pathlib import Path
import concurrent.futures,hashlib,json,os,subprocess,sys,tempfile,time
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'Tools'))
import CalibrateConstructionLoudness as protocol

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    report=json.loads((HERE/'report.json').read_text())
    ab=HERE.parent/'ab';binary=ab/'normalized-after/TunedPerformanceRenderer'
    manifest=ab/'normalized-after-manifest.json'
    initial_sha=sha(binary)
    env={k:v for k,v in os.environ.items() if not k.startswith('ACUSTRA_STUDIO_')}
    start=time.monotonic()
    with tempfile.TemporaryDirectory(prefix='acustra-final-level-match-') as temp:
        work=Path(temp)
        for name,score in report['scores'].items():
            path=work/(name+'.txt');path.write_text(score['text']);assert sha(path)==score['sha256']
        def one(row):
            key='-'.join(row[k] for k in ['shape','wood','picking','capture']);measurements={}
            for name in ['phrase','hardest']:
                output=work/(key+'-'+name+'.f32')
                cmd=[str(binary),str(work/(name+'.txt')),str(output),row['capture'],row['picking'],'--body-shape',row['shape'],'--body-material',row['wood'],'--room','0']
                if name=='hardest':cmd+=['--touch','1','--pluck-position','0']
                p=subprocess.run(cmd,capture_output=True,env=env)
                if p.returncode:raise RuntimeError(p.stderr.decode())
                audio=np.fromfile(output,dtype='<f4').reshape(-1,2)
                assert np.isfinite(audio).all() and np.any(audio)
                measurements[name]=dict(audio_sha256=sha(output),frames=len(audio),
                    peak_dbfs=float(20*np.log10(np.max(abs(audio)))),
                    prelimiter_peak_dbfs=float(20*np.log10(np.max(protocol.unlimited(audio)))))
                if name=='phrase':measurements[name]['lufs']=protocol.meter.integrated_loudness(audio)
                measurements[name]['limiter_active_samples']=int(np.sum(abs(audio)>protocol.LIMIT_KNEE))
                output.unlink()
            delta=measurements['phrase']['lufs']-row['versions']['baseline']['phrase']['lufs']
            peak_delta=measurements['hardest']['prelimiter_peak_dbfs']-row['predicted_hardest_prelimiter_peak_dbfs']
            return {**{k:row[k] for k in ['shape','wood','picking','capture','cell','level_match_gain']},
                'final':measurements,'phrase_error_vs_baseline_lu':delta,
                'hardest_peak_error_vs_prediction_db':peak_delta,
                'passes':abs(delta)<.001 and measurements['phrase']['limiter_active_samples']==0 and measurements['hardest']['prelimiter_peak_dbfs']<=-2.0}
        rows=[]
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            for row in pool.map(one,report['rows']):
                rows.append(row)
                if len(rows)%12==0:print('Verified',len(rows),'/72 in',round(time.monotonic()-start,1),'s',flush=True)
    assert sha(binary)==initial_sha
    out=dict(scope='Final compiled engine: 72 phrase +72 hardest-case native renders. Frozen scores and reference measurements reused; no fitting.',
        renderer_sha256=initial_sha,manifest_sha256=sha(manifest),reference_report_sha256=sha(HERE/'report.json'),
        source_sha256={str(path):sha(path) for path in [Path(__file__),Path(protocol.__file__),Path(protocol.meter.__file__)]},
        rows=rows,summary=dict(all_pass=all(r['passes'] for r in rows),
            maximum_absolute_phrase_error_lu=max(abs(r['phrase_error_vs_baseline_lu']) for r in rows),
            maximum_absolute_peak_prediction_error_db=max(abs(r['hardest_peak_error_vs_prediction_db']) for r in rows),
            maximum_hardest_peak_dbfs=max(r['final']['hardest']['prelimiter_peak_dbfs'] for r in rows),
            phrase_limiter_active_samples=sum(r['final']['phrase']['limiter_active_samples'] for r in rows),
            hardest_limiter_active_samples=sum(r['final']['hardest']['limiter_active_samples'] for r in rows)),
        jobs=2,wall_seconds=time.monotonic()-start,native_renders=144)
    (HERE/'final-verification.json').write_text(json.dumps(out,indent=2,allow_nan=False)+'\n')
    print(json.dumps(out['summary'],indent=2),flush=True)
    assert out['summary']['all_pass']
if __name__=='__main__':main()
