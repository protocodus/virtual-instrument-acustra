#!/usr/bin/env python3
"""Run only after collaborators explicitly confirm builds/tests/renders are idle."""
import datetime
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent
build = root / 'build'
manifest = json.loads((build / 'manifest.json').read_text())
if not manifest['complete']:
    raise SystemExit('Benchmark build is incomplete')
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
for version in ('baseline','candidate'):
    src = Path(manifest[f'{version}_source'])
    now = {str(p.relative_to(src)):sha(p) for p in sorted((src/'Source'/'DSP').rglob('*')) if p.is_file()}
    if now != manifest[f'{version}_files_sha256']:
        raise SystemExit(f'{version} changed after freeze')
exe = build / 'AcustraSoundRefinementBenchmark'
if sha(exe) != manifest['executable_sha256']:
    raise SystemExit('Executable changed')
index = int(sys.argv[1]) if len(sys.argv) > 1 else 1
prefix = root / f'run-{index}'
if any(prefix.with_suffix(s).exists() for s in ('.json','.csv','.run.json')):
    raise SystemExit('Refusing to overwrite an existing final run')
allowed = sorted(os.sched_getaffinity(0))
chosen = allowed[0]
command = [str(exe), str(prefix), str(index)]
metadata = {'command':command, 'source_manifest_sha256':sha(build/'manifest.json'),
            'executable_sha256':sha(exe), 'cpu_affinity_before':allowed,
            'cpu_affinity_timed':[chosen],
            'quiet_period':'Parent explicitly confirms builds, tests, and renders idle before launch',
            'processes_before':subprocess.check_output(['ps','-eo','pid,ppid,stat,comm,args'],text=True),
            'start_utc':datetime.datetime.now(datetime.timezone.utc).isoformat()}
os.sched_setaffinity(0, {chosen})
completed = subprocess.run(command)
metadata['exit_code'] = completed.returncode
metadata['end_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
metadata['processes_after'] = subprocess.check_output(['ps','-eo','pid,ppid,stat,comm,args'],text=True)
metadata['output_sha256'] = {p.name:sha(p) for p in (prefix.with_suffix('.json'), prefix.with_suffix('.csv')) if p.exists()}
prefix.with_suffix('.run.json').write_text(json.dumps(metadata,indent=2)+'\n')
raise SystemExit(completed.returncode)
