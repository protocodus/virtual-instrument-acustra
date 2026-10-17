#!/usr/bin/env python3
"""Short exact-centre auditions from existing renders; no further DSP runs."""
from pathlib import Path
import hashlib,json
import numpy as np
from scipy.io import wavfile
HERE=Path(__file__).resolve().parent
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    source=HERE/'comparison-final/comparison.json';report=json.loads(source.read_text())
    cases=json.loads((HERE/'plan.json').read_text())['cases']
    out=HERE/'comparison-final/audition/centres';out.mkdir(exist_ok=False)
    files=[]
    for c in cases:
        if c['group']!='primary' or c['master_tune_cents']!=0:continue
        clips=[];segments=[];inputs=[]
        for version in ['before','after']:
            path=HERE/(('normalized-after' if version=='after' else version)+'-renders')/'audio'/(c['key']+'.f32')
            gain=report['common_headroom_gain_linear'][c['capture']]
            if version=='after':gain*=report['matched_after_gain_linear'][c['capture']]
            audio=(np.fromfile(path,dtype='<f4').reshape(-1,2)*gain).astype(np.float32)
            audio[-960:]*=np.linspace(1,0,960,dtype=np.float32)[:,None]
            clips.append(audio);inputs.append(dict(path=str(path),sha256=sha(path)))
            start=0. if version=='before' else 4.35
            segments.append(dict(version=version,start_seconds=start,end_seconds=start+4.,gain_linear=gain))
        output=np.concatenate([clips[0],np.zeros((16800,2),np.float32),clips[1]])
        path=out/f"{c['shape']}-{c['wood']}-{c['capture']}-{c['reason_note']}-centre-ab.wav"
        wavfile.write(path,48000,output)
        files.append(dict(path=str(path),sha256=sha(path),shape=c['shape'],wood=c['wood'],capture=c['capture'],
            reason_note=c['reason_note'],midi=c['midi'],nominal_frequency_hz=c['expected_fundamental_hz'],
            segments=segments,inputs=inputs))
    assert len(files)==16
    (out/'manifest.json').write_text(json.dumps(dict(policy='Before0–4s; silence0.35s; after4.35–8.35s. Same per-capture global gains as full sweep auditions; no new gain normalization. Finger96, zero cents.',
        comparison_sha256=sha(source),script_sha256=sha(Path(__file__)),format='48kHz stereo IEEE float32 WAV',
        tail_fade_seconds=.02,listened=False,files=files),indent=2)+'\n')
    print(out)
if __name__=='__main__':main()
