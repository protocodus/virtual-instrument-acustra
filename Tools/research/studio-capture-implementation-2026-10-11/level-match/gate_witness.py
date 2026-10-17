#!/usr/bin/env python3
"""Explain the largest final LUFS gate jump; no gain refit."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
import numpy as np
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/'Tools'));import MeasureMaterialLoudness as meter

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def gates(audio):
    y=meter.k_weight(audio.astype(np.float64));block=19200;hop=4800
    power=np.array([np.mean(y[i*hop:i*hop+block]**2,axis=0).sum() for i in range(1+(len(y)-block)//hop)])
    db=-.691+10*np.log10(np.maximum(power,1e-30));absolute=db>-70
    threshold=-.691+10*np.log10(power[absolute].mean())-10
    included=absolute&(db>threshold)
    return dict(lufs=float(-.691+10*np.log10(power[included].mean())),absolute_blocks=int(absolute.sum()),
        relative_threshold_lufs=float(threshold),integrated_blocks=int(included.sum()),
        included_block_indices=np.flatnonzero(included).tolist(),block_lufs=db.tolist())
def main():
    final=json.loads((HERE/'final-verification.json').read_text());reference=json.loads((HERE/'report.json').read_text())
    worst=max(final['rows'],key=lambda r:abs(r['phrase_error_vs_baseline_lu']))
    ab=HERE.parent/'ab';audio={};files={}
    with tempfile.TemporaryDirectory(prefix='acustra-lufs-gate-') as temp:
        temp=Path(temp);score=temp/'phrase.txt';score.write_text(reference['scores']['phrase']['text'])
        for version in ['after','normalized-after']:
            binary=ab/version/'TunedPerformanceRenderer';output=temp/(version+'.f32')
            cmd=[str(binary),str(score),str(output),worst['capture'],worst['picking'],'--body-shape',worst['shape'],'--body-material',worst['wood'],'--room','0']
            subprocess.run(cmd,check=True,capture_output=True)
            audio[version]=np.fromfile(output,dtype='<f4').reshape(-1,2);files[version]=dict(binary_sha256=sha(binary),audio_sha256=sha(output))
    scaled=audio['after'].astype(np.float64)*worst['level_match_gain']
    measured=audio['normalized-after'].astype(np.float64)
    values={'raw_blend':gates(audio['after']),'postscaled_raw_blend':gates(scaled),'final_native':gates(measured)}
    result=dict(cell={k:worst[k] for k in ['shape','wood','picking','capture','level_match_gain']},files=files,
        maximum_sample_difference=float(np.max(abs(scaled-measured))),
        relative_rms_difference_db=float(10*np.log10(np.mean((scaled-measured)**2)/np.mean(measured**2))),
        levels=values,changed_relative_gate_blocks=sorted(set(values['raw_blend']['included_block_indices'])^set(values['postscaled_raw_blend']['included_block_indices'])),
        explanation='The fixed -70 LUFS absolute gate includes additional quiet blocks after gain; its updated mean changes the relative gate enough to admit one block. Ideal double-precision postscaling reproduces the final native integrated loudness, so this is meter gating, not a non-flat observer or failed level reference.',
        source_sha256=sha(Path(__file__)))
    assert abs(values['postscaled_raw_blend']['lufs']-values['final_native']['lufs'])<1e-5
    (HERE/'gate-witness.json').write_text(json.dumps(result,indent=2)+'\n')
    print({k:v for k,v in result.items() if k not in ['levels']})
    print({k:{x:v[x] for x in ['lufs','absolute_blocks','integrated_blocks','relative_threshold_lufs']} for k,v in values.items()})
if __name__=='__main__':main()
