# Final-source validation and reproduction

Run from the ACUSTRA repository root. The final computational source is
`2decfdfbe8fdf69ab8a0885de8fd095668d6f174`; subsequent completion edits are
only documentation/evidence. `receipt.json` verifies all 202 recorded inputs
against this Git commit and the working files. The original render receipt
was taken with the piezo change pending on `e7b3cc0`; its source hashes,
not that earlier base label, identify the final rendered source.

## Final native build (executed once)

```sh
BUILD_DIR=/Users/vojta/Dev/virtual-instrument-acustra-model-audit-20261011/build-final \
BUILD_UNIVERSAL=ON BUILD_JOBS=3 CTEST_PARALLEL_LEVEL=2 \
JUCE_PATH=/Users/vojta/Dev/virtual-instrument-acustra/build/_deps/juce-src \
PYTHON=/Library/Frameworks/Python.framework/Versions/3.11/bin/python3 \
ACUSTRA_EDITOR_SNAPSHOT=/Users/vojta/Dev/virtual-instrument-acustra-model-audit-20261011/build-audit/project-finalization/editor-final.png \
bash scripts/build-macos.sh
```

Exit 0; 88/88 tests, 220.65 seconds. The archived log removes only trailing
line whitespace; receipt.json retains the original and archived hashes.
The script builds and verifies ad-hoc signatures. `receipt.json` additionally records direct `lipo -archs` and
`codesign --verify --deep --strict --verbose=2` checks of the three products.
The saved snapshot was visually inspected: complete editor, legible controls
and no clipping in the default fixture. It is not a real-host interaction test.

## Level and strum protocol

The final-source `build-audit/` renderer binaries are identified by SHA-256 in
`render-inputs.json`. To reproduce the saved measurement protocols with those
renderers (choose new output files):

```sh
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer build-audit/AcustraPerformanceRenderer --jobs 3 \
  --json /tmp/acustra-final-loudness.json
build-audit/AcustraConstructionStrumLevels > /tmp/acustra-final-strums.json
```

The loudness command uses the existing default Pick-only hardest-case
protocol; it is not an all-technique peak assertion. No `--write-header`
was used. The executed final check reuses measured rows without rerendering:

```sh
python3 Tools/CalibrateConstructionLoudness.py \
  --measurements build-audit/project-finalization/loudness-measurements.json --check
```

The 180 native Finger strum rows retain rate, built gains, tuning, capture,
level and finite/peak values. `strum-summary.json` records their extrema.
The full suite independently applies the existing construction-strum guards.

## Final callback protocol

`callback-build.json` records all exact compiler argument arrays, two source
trees, object paths, binary hash and the executed timing command. The
archived `BuildFinalCallbackHarness.py` and `FinalizationCallbackBenchmark.cpp`
can recreate the harness from Git without changing the working source:

```sh
python3 Tools/research/project-completion-2026-10-11/BuildFinalCallbackHarness.py \
  --output /tmp/acustra-final-callbacks \
  --baseline-revision 56d4678 --candidate-revision 2decfdf --build
# Only after builds, tests, downloads and renders are idle:
/tmp/acustra-final-callbacks/FinalCallbacks /tmp/acustra-final-callbacks/trials.json 32 4
```

The final evidence is `callbacks-final.json`, `callbacks-final.log`, empty
`callbacks-final.stderr.log`, and `callback-summary.json`. This run is not
parity mode: intentional piezo/Room changes alter audio. The separate
`optimization-build.json` and `optimization-parity.json` describe the narrow
`ae0ba92` preservation proof; no full historical model matrix was rerun.

## Other evidence

- `capture/commands.txt`: final matched capture fit and source sensitivity;
  report manifests retain exact reference rows/controls and dataset hashes.
- `moving-stop/provenance.json`: independent oracle and both witness builds;
  final `2decfdf` relink exactly matches all twelve cases.
- `listening-manifest.json`: exact two-source compile/run arguments, event
  score, settings, native-level audio hashes and `listened: false`.
  `RenderAffectedPhrase.cpp` and `listening-events.tsv` preserve the harness
  inputs; WAVs remain in the local scratch directory recorded by the manifest.
- `body-coefficient-extraction.json`, `sparse-contact-receipt.json` and
  `sparse-contact-parity.log`: original optimization proofs.
- Rack commit `316db04605cafd7d325e5dc265bf5046cb871bbd`:
  `Examples/Acustra/Docs/validation/project-completion-2026-10-11/commands.md`
  records the explicit SDK path, source guard, seven contracts, module build
  and one serial 36-process timing matrix. The module consumes `2decfdf`.

Only compact source/log/JSON evidence is committed. Build products, downloaded
reference audio and generated listening WAVs are local artifacts. No install,
release packaging, host launch, publication or merge is part of this receipt.
