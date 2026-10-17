#!/usr/bin/env python3
"""Run only after the parent confirms the normalized production library is frozen."""
from pathlib import Path
import hashlib,json,shutil,subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    dest=HERE/'normalized-after';dest.mkdir(exist_ok=False)
    source=ROOT/'Source/DSP'
    for path in list(source.rglob('*.h'))+[source/'AcustraEngine.cpp',source/'AcustraPerformer.cpp']:
        target=dest/'Source/DSP'/path.relative_to(source)
        target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(path,target)
    for name in ['CalibrationFile.h','GenerateStudioCapture.py','CalibrateStudioCaptureLoudness.py']:
        target=dest/'Tools'/name;target.parent.mkdir(exist_ok=True);shutil.copy2(ROOT/'Tools'/name,target)
    shutil.copy2(ROOT/'build-audit/libAcustraDSP.a',dest/'libAcustraDSP.a')
    cmd=['/usr/bin/clang++','-std=c++20','-O3','-DNDEBUG','-I'+str(dest/'Source'),'-I'+str(dest/'Tools'),str(HERE/'TunedPerformanceRenderer.cpp'),str(dest/'libAcustraDSP.a'),'-o',str(dest/'TunedPerformanceRenderer')]
    proc=subprocess.run(cmd,capture_output=True,text=True)
    (HERE/'normalized-after-build.log').write_text(proc.stdout+proc.stderr);proc.check_returncode()
    paths=sorted(p for p in dest.rglob('*') if p.is_file())+[HERE/'TunedPerformanceRenderer.cpp',ROOT/'Tools/CalibrateStudioCaptureLoudness.py',ROOT/'build-audit/capture-implementation-20261011/level-match/report.json']
    report=dict(command=cmd,revision_base=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        source_policy='Parent-declared frozen production library after approved observer plus flat prior-level restoration; no coefficient injection. Phase1 raw-blend snapshots/reports retained.',
        git_status=subprocess.check_output(['git','status','--short'],cwd=ROOT,text=True),
        sha256={str(p.relative_to(HERE) if p.is_relative_to(HERE) else p.relative_to(ROOT)):sha(p) for p in paths})
    (HERE/'normalized-after-manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Frozen normalized renderer',sha(dest/'TunedPerformanceRenderer'),flush=True)
if __name__=='__main__':main()
