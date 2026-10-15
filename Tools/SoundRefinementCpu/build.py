#!/usr/bin/env python3
"""Build matched, isolated adapters. Preparation does not run timings.

python3 build.py prepare BASELINE_ROOT BUILD_DIRECTORY
python3 build.py finish CANDIDATE_ROOT BUILD_DIRECTORY

Preserve BUILD_DIRECTORY and all source snapshots with final evidence. For a
fresh reproduction, run prepare then finish with a new build directory.
"""
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys

FLAGS = ['-std=c++17', '-O3', '-DNDEBUG', '-fPIC', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
SCRIPT = Path(__file__).resolve()
HARNESS = SCRIPT.with_name('BenchmarkSoundRefinements.cpp')

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def source_tree(root):
    return {str(p.relative_to(root)): sha(p) for p in sorted((root / 'Source' / 'DSP').rglob('*')) if p.is_file()}

def compiler_version(compiler):
    return subprocess.check_output([compiler, '--version'], text=True).splitlines()[0]

def compile_one(compiler, source, output, extra=()):
    subprocess.run([compiler, *FLAGS, *extra, '-c', str(source), '-o', str(output)], check=True)

def compile_adapter(compiler, version, root, build):
    extra = [f'-Dacustra=acustra_{version}', f'-DACUSTRA_TIMING_ADAPTER={version}', '-I', str(root / 'Source')]
    compile_one(compiler, build / HARNESS.name, build / f'{version}-adapter.o', extra)
    for unit in ('Engine', 'Performer'):
        compile_one(compiler, root / 'Source' / 'DSP' / f'Acustra{unit}.cpp', build / f'{version}-{unit.lower()}.o', extra)

def verify_sources(manifest, version):
    if source_tree(Path(manifest[f'{version}_source'])) != manifest[f'{version}_files_sha256']:
        raise SystemExit(f'{version} source changed; start a new build directory from a frozen snapshot')

def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ('prepare', 'finish'):
        raise SystemExit(__doc__)
    action, root, build = sys.argv[1], Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
    compiler = shutil.which(os.environ.get('ACUSTRA_TIMING_COMPILER', 'c++'))
    if not compiler:
        raise SystemExit('Compiler unavailable')
    if action == 'prepare':
        build.mkdir(parents=True, exist_ok=False)
        shutil.copy2(HARNESS, build / HARNESS.name)
        shutil.copy2(SCRIPT, build / SCRIPT.name)
        manifest = {
            'complete': False,
            'baseline_source': str(root),
            'baseline_files_sha256': source_tree(root),
            'tool_files_sha256': {p.name: sha(p) for p in (build / HARNESS.name, build / SCRIPT.name)},
            'compiler': compiler_version(compiler),
            'compiler_path': compiler,
            'compiler_executable_sha256': sha(compiler),
            'flags': FLAGS,
            'namespace_definitions': ['acustra=acustra_baseline', 'acustra=acustra_candidate'],
            'platform': platform.platform(),
            'cpu_lscpu': subprocess.check_output(['lscpu'], text=True),
            'build_process_affinity': sorted(os.sched_getaffinity(0)),
            'cpu_quota': Path('/sys/fs/cgroup/cpu.max').read_text().strip() if Path('/sys/fs/cgroup/cpu.max').exists() else 'unavailable',
            'protocol': 'Performer callbacks except explicit direct force edges; alternate pair order; 250 ms preroll; construction, event authoring, observers, hashing, filesystem reporting excluded from timing; separate snapshot headers and namespaces; no audio parity requirement because sound intentionally changes',
        }
        (build / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        compile_one(compiler, build / HARNESS.name, build / 'main.o')
        compile_adapter(compiler, 'baseline', root, build)
        verify_sources(manifest, 'baseline')
        manifest['baseline_objects_sha256'] = {p.name: sha(p) for p in sorted(build.glob('*.o'))}
        (build / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print('Baseline and harness ready; candidate build and timings are still pending.')
    else:
        manifest = json.loads((build / 'manifest.json').read_text())
        if manifest['complete']:
            raise SystemExit('Completed evidence build exists; preserve it and choose a fresh directory')
        if manifest['compiler_executable_sha256'] != sha(compiler) or manifest['flags'] != FLAGS:
            raise SystemExit('Compiler or flags changed')
        verify_sources(manifest, 'baseline')
        for name, digest in {**manifest['tool_files_sha256'], **manifest['baseline_objects_sha256']}.items():
            if sha(build / name) != digest:
                raise SystemExit('Prepared harness/baseline artifact changed: ' + name)
        manifest['candidate_source'] = str(root)
        manifest['candidate_files_sha256'] = source_tree(root)
        (build / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        compile_adapter(compiler, 'candidate', root, build)
        objects = [build / 'main.o'] + [build / f'{v}-{u}.o' for v in ('baseline', 'candidate') for u in ('adapter', 'engine', 'performer')]
        executable = build / 'AcustraSoundRefinementBenchmark'
        subprocess.run([compiler, *FLAGS, *map(str, objects), '-o', str(executable)], check=True)
        for version in ('baseline', 'candidate'):
            verify_sources(manifest, version)
        manifest['objects_sha256'] = {p.name: sha(p) for p in objects}
        manifest['executable_sha256'] = sha(executable)
        manifest['complete'] = True
        (build / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        print(executable)

if __name__ == '__main__':
    main()
