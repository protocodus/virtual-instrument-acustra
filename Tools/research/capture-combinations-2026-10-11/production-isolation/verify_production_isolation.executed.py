#!/usr/bin/env python3
"""Run the same no-inject trace TU against separately linked old/new production libs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
HARNESS = ROOT/'build-audit/studio-mic-variants/harness'

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage',required=True,choices=['before','after'])
    parser.add_argument('--binary',type=Path,required=True)
    parser.add_argument('--library',type=Path,required=True)
    parser.add_argument('--headers',type=Path,required=True)
    args = parser.parse_args()
    out = HERE/args.stage
    out.mkdir(exist_ok=False)
    env = {k:v for k,v in os.environ.items() if not k.startswith('ACUSTRA_STUDIO_')}
    cases = [(rate,shape,wood,12000) for rate in [44100,48000,96000]
             for shape in range(4) for wood in range(3)]
    cases.append((48000,0,0,512))
    rows=[]
    for rate,shape,wood,length in cases:
        key=f'{rate}-s{shape}-w{wood}-n{length}'
        prefix=out/key
        testenv=env.copy()
        testenv['ACUSTRA_STUDIO_TRACE_FILE']=str(prefix)+'.trace.csv'
        if length==512:
            testenv['ACUSTRA_STUDIO_TRACE_STATES_FILE']=str(prefix)+'.states.f64'
        command=[str(args.binary.resolve()),str(prefix),str(rate),str(shape),str(wood),str(length)]
        subprocess.run(command,env=testenv,check=True,capture_output=True)
        files={suffix:Path(str(prefix)+suffix) for suffix in ['.mic.f32','.piezo.f32','.poles.u32','.trace.csv']}
        if length==512:
            files['.states.f64']=Path(str(prefix)+'.states.f64')
        row={'key':key,'rate':rate,'shape':shape,'wood':wood,'frames':round(length*rate/48000),
             'sha256':{suffix:sha(p) for suffix,p in files.items()}}
        if args.stage=='after':
            same={suffix:p.read_bytes()==(HERE/'before'/(key+suffix)).read_bytes()
                  for suffix,p in files.items()}
            row['byte_identity_to_before']=same
            row['pass']=all(value for suffix,value in same.items() if suffix!='.mic.f32')
            row['microphone_changed']=not same['.mic.f32']
        rows.append(row)
        print(args.stage,key,row.get('pass','recorded'),flush=True)
    report={'stage':args.stage,'binary':str(args.binary.resolve()),'binary_sha256':sha(args.binary),
            'library':str(args.library.resolve()),'library_sha256':sha(args.library),
            'matching_header_root':str(args.headers.resolve()),
            'engine_header_sha256':sha(args.headers/'DSP/AcustraEngine.h'),
            'probe_source_sha256':sha(HERE/'ProductionObserverTrace.cpp'),
            'trace_helper_sha256':sha(HARNESS/'StudioObserverHarness.h'),
            'runner_sha256':sha(Path(__file__)),
            'cases':rows,'ordinary_cases':36,'full_bridge_body_state_witness_samples':512,
            'scope':'Separate old/new production binaries. No observation CSV is loaded or injected. Each engine computes its own native body observation.',
            'limits':['Before/after audit fixture, not a permanent old-version product path.',
                      'Per-sample modal hashes supplemented by exact 512-sample full bridge/body modal-state witness.',
                      'Waveguide delay buffers are not dumped; bridge/reaction/body dynamics and all piezo probe fields are compared every sample.',
                      'Short sounding/release/repluck/chord fixture; very late inaudible idle-reset timing is not asserted.']}
    if args.stage=='after':
        report['all_pass']=all(r['pass'] and r['microphone_changed'] for r in rows)
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    if args.stage=='after' and not report['all_pass']:
        raise SystemExit('Production observation isolation failed; inspect report')

if __name__=='__main__':main()
