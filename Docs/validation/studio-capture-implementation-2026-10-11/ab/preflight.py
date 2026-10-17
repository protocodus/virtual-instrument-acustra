#!/usr/bin/env python3
"""Small zero-cent equivalence proof for the scratch master-tune CLI."""
from pathlib import Path
import hashlib,json,os,subprocess

HERE=Path(__file__).resolve().parent
BEFORE=HERE/'before'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    out=HERE/'preflight';out.mkdir(exist_ok=False)
    provenance=json.loads((BEFORE/'library-original-provenance.json').read_text())
    for relative,expected in provenance['inputs'].items():
        path=BEFORE/relative
        if path.is_file():assert sha(path)==expected,relative
    assert sha(BEFORE/'libAcustraDSP.a')==provenance['inputs']['build-audit/libAcustraDSP.a']
    assert sha(BEFORE/'OriginalPerformanceRenderer')==provenance['inputs']['build-audit/AcustraPerformanceRenderer']
    env={k:v for k,v in os.environ.items() if not k.startswith('ACUSTRA_STUDIO_')}
    results=[]
    for shape,wood,capture,note,channel in [('parlor','mahogany','mono_mic',47,2),('jumbo','spruce','stereo_mic',40,1)]:
        key=f'{shape}-{wood}-{capture}-m{note}-s{channel}'
        event=out/(key+'.events')
        event.write_text(f'ACUSTRA_PERFORMANCE_V1 48000 192000\n0 {channel} {note} 96 0\n144000 {channel} {note} 0 0\n')
        commands=[];paths=[]
        for variant,binary in [('original','OriginalPerformanceRenderer'),('scratch','TunedPerformanceRenderer')]:
            audio=out/(key+'-'+variant+'.f32')
            cmd=[str(BEFORE/binary),str(event),str(audio),capture,'finger','--body-shape',shape,'--body-material',wood,'--room','0']
            if variant=='scratch':cmd+=['--master-tune-cents','0']
            subprocess.run(cmd,env=env,check=True,capture_output=True)
            commands.append(cmd);paths.append(audio)
        assert paths[0].read_bytes()==paths[1].read_bytes(),key
        results.append(dict(key=key,byte_identical=True,sha256=sha(paths[0]),commands=commands))
    (out/'receipt.json').write_text(json.dumps(dict(script_sha256=sha(Path(__file__)),results=results),indent=2)+'\n')
    print('PASS: both zero-cent scratch renders byte-identical to frozen d54 original.')
if __name__=='__main__':main()
