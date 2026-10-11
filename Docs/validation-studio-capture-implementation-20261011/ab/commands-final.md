# Final normalized capture A/B reproduction

Run from the repository root. Preserve the preceding observer-only `after` snapshot and its reports. The production final library used here is SHA256 `282ae53ffe4a0f977bb9c11ec4c06858303dd118aa00a03bc8315e021da7ca00`; exact source and compiler hashes are in `normalized-after-manifest.json`. This phase reuses the original 296-case frozen plan and baseline. It does not rebuild historical model variants or refit weights.

Scripts execute from the ignored `build-audit/capture-implementation-20261011/ab` directory, not from the archived `Docs` folder. They infer the repository root from that directory depth. A fresh replay requires the matching libraries/headers described by the receipts, the same frozen plan/score/metric inputs, and no existing output directories. Do not overwrite the original archived evidence.

After a matching production library has been built and frozen, snapshot and link the narrow tuning fixture:

```sh
python3 build-audit/capture-implementation-20261011/ab/freeze_normalized_after.py
```

Run the unchanged 296-case protocol against the new named snapshot. Calling the existing function preserves the frozen script and plan hashes:

```sh
python3 - <<'PY'
from pathlib import Path
import sys
p = Path('build-audit/capture-implementation-20261011/ab').resolve()
sys.path.insert(0, str(p))
import targeted_ab
targeted_ab.run('normalized-after', 1)
PY
python3 build-audit/capture-implementation-20261011/ab/compare_final_ab.py
python3 build-audit/capture-implementation-20261011/ab/package_final_centres.py
```

`compare_final_ab.py` is a path-adapted copy of the original comparator. It reads `normalized-after-renders`, writes `comparison-final`, and records that the runtime includes the flat microphone reference restoration. The scoring, fixed pitches, event hashes, windows, global audition matching and tail fade are unchanged. Per-construction level factors are part of the product's independently calibrated reference, not additional per-note/file audition normalization.

Verify the final generated references natively using the same frozen calibration scores:

```sh
python3 build-audit/capture-implementation-20261011/level-match/verify_final.py
```

That performs 72 phrase plus 72 hardest-case renders, reusing measured baseline values and recording a deliberately tight 0.001-LU diagnostic, no phrase limiter activity, and hardest-case peaks at or below −2 dBFS. The diagnostic exits nonzero for five Mono cells because of BS.1770 gate membership changes; the observed bound is 0.015770414 LU. That original result is retained and explained by the two-render `level-match/gate_witness.py`, not refitted or relabeled as a pass. The original construction-loudness test passed unchanged. Calibration evidence and source bytes are archived in the sibling `level-match` folder. The final field/kernel agreement, mechanical/Piezo invariance, full suite and callback checks are reported independently by the parent implementation receipt.

Thirty-six audition WAVs remain in `comparison-final/audition`: 16 pitch sweeps, four low-E partial witnesses, and 16 compact exact-centre A/B files under `centres`. Each is native stereo 48 kHz IEEE float32; the manifests list segment offsets, shared gains and file hashes. Numerical comparisons use unnormalized output. `listened:false`.
