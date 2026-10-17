#!/usr/bin/env python3
"""Add final normalized evidence without replacing phase1 observer-only receipts."""
from pathlib import Path
import gzip,hashlib,json,shutil
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
DEST=ROOT/'Docs/validation-studio-capture-implementation-20261011/ab'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    files=['freeze_normalized_after.py','compare_final_ab.py','package_final_centres.py','package_final_evidence.py',
        'normalized-after-manifest.json','normalized-after-build.log','final-audio-verification.json',
        'comparison-final/REPORT.md','comparison-final/comparison.json','comparison-final/changes.csv',
        'comparison-final/fine-pitch-ab.png','comparison-final/audition/manifest.json',
        'comparison-final/audition/centres/manifest.json','commands-final.md']
    records=[]
    for name in files:
        source=HERE/name;target=DEST/name;target.parent.mkdir(parents=True,exist_ok=True)
        assert not target.exists(),target
        shutil.copy2(source,target);assert sha(source)==sha(target)
        records.append(dict(path=name,source=str(source),sha256=sha(target),bytes=target.stat().st_size))
    source=HERE/'normalized-after-renders/report.json';target=DEST/'normalized-after-renders/report.json.gz'
    target.parent.mkdir(exist_ok=False);target.write_bytes(gzip.compress(source.read_bytes(),compresslevel=9,mtime=0))
    assert gzip.decompress(target.read_bytes())==source.read_bytes()
    records.append(dict(path=str(target.relative_to(DEST)),source=str(source),sha256=sha(target),bytes=target.stat().st_size,
        uncompressed_sha256=sha(source),encoding='gzip; exact original JSON bytes; mtime0'))
    receipt=dict(status='Final observer plus flat microphone level restoration; baseline and phase1 artifacts preserved.',
        cases_per_version=296,new_renders=296,baseline_reused=True,
        prior_phase1_receipt_sha256=sha(DEST/'receipt.json'),baseline_report_sha256=sha(HERE/'before-renders/report.json'),
        final_library_sha256=sha(HERE/'normalized-after/libAcustraDSP.a'),
        final_renderer_sha256=sha(HERE/'normalized-after/TunedPerformanceRenderer'),
        figure_visually_inspected=True,audio_format_verified=True,audition_files_ignored=36,listened=False,files=records)
    (DEST/'final-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    (DEST/'README.md').write_text('# Targeted capture A/B evidence\n\nThe final result is [comparison-final/REPORT.md](comparison-final/REPORT.md), with [final-receipt.json](final-receipt.json) and [commands-final.md](commands-final.md). It includes the approved observer and flat microphone-only level restoration. The baseline is reused unchanged.\n\nThe earlier [comparison/REPORT.md](comparison/REPORT.md) and [receipt.json](receipt.json) preserve the observer-only phase before the independent construction-loudness guard identified the need for flat reference restoration. They are not the final production level result.\n\nRaw renders, native libraries and float32 WAVs remain in the ignored source directory recorded by each receipt. No listening preference or approval is claimed.\n')
    print(DEST/'final-receipt.json')
if __name__=='__main__':main()
