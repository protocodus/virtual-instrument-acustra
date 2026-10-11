#!/usr/bin/env python3
"""Freeze current-model DSP trees and build paired callbacks; never runs timing.
Use --run-parity only with the optimization-only candidate, never an audible revision.
"""
import argparse, hashlib, json, pathlib, shutil, subprocess, sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
HARNESS=pathlib.Path(__file__).with_name('FinalizationCallbackBenchmark.cpp')
p=argparse.ArgumentParser()
p.add_argument('--output',required=True,type=pathlib.Path)
p.add_argument('--baseline-revision',default='56d4678')
p.add_argument('--candidate-revision',required=True)
p.add_argument('--build',action='store_true')
p.add_argument('--run-parity',action='store_true')
a=p.parse_args()
if a.run_parity and not a.build:p.error('--run-parity requires --build')
out=a.output.resolve()
if out.exists():raise SystemExit(f'Output must be new: {out}')
out.mkdir(parents=True)
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args])
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
revisions={}
for label,revision in [('baseline',a.baseline_revision),('current',a.candidate_revision)]:
    full=git('rev-parse',revision).decode().strip();revisions[label]=full
    files=git('ls-tree','-r','--name-only',full,'Source/DSP').decode().splitlines()
    for relative in files:
        path=out/label/relative;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(git('show',full+':'+relative))
shutil.copy2(HARNESS,out/HARNESS.name)
source=out/HARNESS.name
flags=['/usr/bin/c++','-O3','-DNDEBUG','-std=c++20','-arch','arm64','-mmacosx-version-min=11.0','-Wall','-Wextra','-Wpedantic']
commands=[];objects=[]
for label in ['baseline','current']:
    common=flags+[f'-Dacustra=acustra_{label}','-I'+str(out/label/'Source')]
    for kind,src,extra in [('adapter',source,[f'-DACUSTRA_CALLBACK_ADAPTER={label}']),
            ('engine',out/label/'Source/DSP/AcustraEngine.cpp',[]),
            ('performer',out/label/'Source/DSP/AcustraPerformer.cpp',[])]:
        obj=out/f'{label}-{kind}.o';objects.append(str(obj));commands.append(common+extra+['-c',str(src),'-o',str(obj)])
obj=out/'main.o';objects.append(str(obj));commands.append(flags+['-c',str(source),'-o',str(obj)])
binary=out/'FinalCallbacks';commands.append(flags+objects+['-o',str(binary)])
manifest={'revisions':revisions,'harness_sha256':digest(source),'commands':commands,
    'sources':{str(f.relative_to(out)):digest(f) for tree in ['baseline','current'] for f in sorted((out/tree).rglob('*')) if f.is_file()},
    'protocol':'42 cases, 4 warmup and32 measured pairs; wall plus thread CPU timing; per-build deterministic hashes; Room0 plus untimed48k Room1 reference; only parity mode requires cross-build exact equality',
    'status':'prepared','run_timing_only_after_quiet_freeze':[str(binary),str(out/'callbacks-final.json'),'32','4']}
(out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if a.build:
    with (out/'build.log').open('w') as log:
        for command in commands:
            result=subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
            if result.returncode:raise SystemExit(f'Build failed; inspect {out / "build.log"}')
    manifest['status']='built';manifest['binary_sha256']=digest(binary)
    if a.run_parity:
        with (out/'parity.log').open('w') as log:
            result=subprocess.run([str(binary),'--parity-only',str(out/'parity.json')],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
        manifest['parity_exit_code']=result.returncode
        if result.returncode:
            (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            raise SystemExit(f'Parity failed; inspect {out / "parity.log"}')
        manifest['parity_sha256']=digest(out/'parity.json')
    (out/'build-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out/'build-manifest.json')
