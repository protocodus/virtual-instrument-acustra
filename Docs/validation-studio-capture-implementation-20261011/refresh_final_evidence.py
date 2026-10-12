#!/usr/bin/env python3
"""Refresh hashes of this compact evidence package, excluding the map itself."""
from pathlib import Path
import hashlib,json
BASE=Path(__file__).resolve().parent
ROOT=BASE.parents[1]
OUTPUT=BASE/'final-evidence-sha256.json'
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    paths=sorted(path for path in BASE.rglob('*') if path.is_file() and path!=OUTPUT and '__pycache__' not in path.parts)
    paths += [ROOT/'Docs/studio-capture-implementation-2026-10-11.md']
    result=dict(scope='Compact final capture evidence and canonical implementation report. Historical phase1 receipts keep original input hashes; native-validation records final test-only repair. No self-hash or cyclic receipt dependency.',
        runtime_revision='edba92084e36bac4ab6962fdcec1a36f9a0b3b6e',
        runtime_library_sha256='282ae53ffe4a0f977bb9c11ec4c06858303dd118aa00a03bc8315e021da7ca00',
        files={str(path.relative_to(ROOT)):dict(sha256=sha(path),bytes=path.stat().st_size) for path in paths})
    OUTPUT.write_text(json.dumps(result,indent=2)+'\n')
    print('Hashed',len(paths),'files; map excludes itself.')
if __name__=='__main__':main()
