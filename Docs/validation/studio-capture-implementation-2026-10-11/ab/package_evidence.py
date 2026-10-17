#!/usr/bin/env python3
"""Package compact evidence; never copy WAVs, banks, libraries or raw renders."""
from pathlib import Path
import gzip,hashlib,json,shutil
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
DEST=ROOT/'Docs/validation-studio-capture-implementation-20261011/ab'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    DEST.mkdir(parents=True,exist_ok=False)
    selected=['TunedPerformanceRenderer.cpp','targeted_ab.py','compare_ab.py','package_centres.py',
        'preflight.py','green_series_review.py','package_evidence.py','MeasureMicrophoneBoom.py',
        'plan.json','before-manifest.json','after-manifest.json','green-series-review.json',
        'audio-verification.json','commands.md','preflight/receipt.json','before-build.log','after-build.log',
        'before-render.log','after-render.log','comparison.log','comparison/REPORT.md',
        'comparison/comparison.json','comparison/changes.csv','comparison/fine-pitch-ab.png',
        'comparison/audition/manifest.json','comparison/audition/centres/manifest.json']
    copies=[(HERE/p,Path(p)) for p in selected]
    copies += [(HERE/'before/library-original-provenance.json',Path('before-library-original-provenance.json'))]
    reference=ROOT/'build-audit/studio-mic-research'
    copies += [(reference/'prototype_observer.py',Path('references/prototype_observer.py')),
               (reference/'export_candidates.py',Path('references/export_candidates.py')),
               (reference/'runtime-free-candidates-v1/manifest.json',Path('references/free-source-fit-manifest.json'))]
    records=[]
    for source,relative in copies:
        dest=DEST/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
        assert sha(source)==sha(dest)
        records.append(dict(source=str(source),path=str(relative),sha256=sha(dest),bytes=dest.stat().st_size))
    for name in ['before','after']:
        source=HERE/(name+'-renders')/'report.json';relative=Path(name+'-renders')/'report.json.gz'
        dest=DEST/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(gzip.compress(source.read_bytes(),compresslevel=9,mtime=0))
        assert gzip.decompress(dest.read_bytes())==source.read_bytes()
        records.append(dict(source=str(source),path=str(relative),sha256=sha(dest),bytes=dest.stat().st_size,
            encoding='gzip; deterministicmtime0; exact original JSON bytes',uncompressed_sha256=sha(source),
            uncompressed_bytes=source.stat().st_size))
    receipt=dict(source_root=str(HERE),status='Fixed production A/B evidence; no listening approval.',
        render_cases_per_version=296,total_renders=592,audition_files_retained_ignored=36,
        binary_before_sha256=sha(HERE/'before/TunedPerformanceRenderer'),
        binary_after_sha256=sha(HERE/'after/TunedPerformanceRenderer'),
        untracked_artifacts='Native libraries/source snapshots, rawfloat32 renders and WAVs remain in source_root.',
        source_preparation='Before sources read from exactd54 Git revision; after copied from root-frozen production tree after exactfinal DSP rebuild. See both build receipts and commands.',
        relocation='Scripts execute under ignored build-audit/<run>/ab; do not execute the archived copies in Docs. See commands.md.',
        figure_visually_inspected=True,audio_format_verified=True,listened=False,files=records)
    (DEST/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(DEST)
    print('Tracked bytes:',sum(r['bytes'] for r in records))
if __name__=='__main__':main()
