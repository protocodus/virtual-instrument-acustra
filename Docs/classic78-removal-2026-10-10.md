# Bellido 1978 (Classical 78) removal — 2026-10-10

The user asked to remove the Classical 78 model completely and to clean up
the code and parameters only it used. The baseline is canonical commit
`bc81e95a2e0cf9ceeb4982502fd97f78fd8b0995`, whose tree is identical to
`ae25d1e15941ccb9a8aa96d6f505ce483537e829`, the revision the Rack Extension
pins.

## Removed

- **The model.** The `GuitarModel` enum and `EngineParameters::guitarModel`,
  with the model-selection and model-fade bookkeeping. Shape and Body
  Material keep their own fades and request coalescing.
- **The g35 measurement and its adaptations.** The Mores archive g35 body
  and bridge banks (`BellidoData.h`) and the per-model data in
  `GuitarModelData.h` (microphone trims, identities). Also the bridge-radiation
  twins, the named-mode pole rule, the classical anchor and the cedar wood
  reference.
- **Its microphone contour.** The two Classical capture peaks, with the
  float-stored coefficient path and the unity-gain branch that only it used.
  The Original's six-section contour is unchanged.
- **Classic-only playing policies.** The pluck-polarisation, release and
  sustain choices of 2026-10-09 (`PluckPolarisationData.h`,
  `ReleasePolicyData.h`, `SustainPolicyData.h`), together with:
  - the broad-loss corner scale and fresh-energy matching that only they used;
  - the player-contact body loading only the Bellido enabled
    (`PlayerBodyLoading.h`, `PerformanceRealism::playerBodyLoading`).
- **The Bellido half of the 2026-10-02 model convergence.** The Original
  keeps its half, a 0.4375 dB/octave brightness tilt on the microphones, now
  `microphoneTiltGain` in `MicrophoneBalanceData.h`.
- **The model dimension of the construction loudness tables.** They fall from
  72 to 36 cells, and the Original's values are unchanged.
  `CalibrateConstructionLoudness.py` writes the header byte for byte.
- **Tests.** The PlayerBodyLoading, PluckPolarisation, ReleasePolicy and
  SustainPolicy suites and the FitModelConvergence check are deleted, along
  with Bellido-only checks elsewhere. Checks that exchanged models now
  exchange Shape and Wood.
- **Tools.** `FitModelConvergence.py` and `GenerateBellidoBody.py` are deleted.
  `--guitar-model`, `--models` and the repertoire's `model=` are now refused.
  - Renderer manifests keep `"guitar_model": "original"`, so new runs still
    pair with earlier evidence. The natural-performance manifest moves to
    schema 3, without the "body" variant; schemas 1 and 2 are still read.
  - The CPU comparison matrices halve: 96 cases and 40 partitions, and
    515 audible-realism cases.
- **Demos and repertoire.** The two Tárrega demos and the repertoire's
  Bellido parts now play an Auditorium in Mahogany, the construction and
  controls the Bellido preset used. The Dowland quartet's cantus plays a
  Dreadnought in Maple, so that ensemble keeps four different guitars.

## Compatibility

The desktop `guitarModel` parameter remains a hidden, non-automatable
legacy slot. It keeps its ID, AU version hint 7, index 14 and both choices.
The editor loses its MODEL switch and its Bellido construction preset. Hosts
that address parameters by index keep every later parameter, and nothing
reads the slot: a session saved on the Bellido reloads every other setting
on the Original. Active parameters fall from 16 to 15, and the published
count stays 19.

The Rack Extension keeps its released `guitar_model` property inactive in
its own change.

Historical reports, measurements and listening packs stay unchanged; paths
in them may name removed source, which the baseline revision still holds.

## Validation

**Byte parity.** A native harness compiled each tree's
`AcustraEngine.cpp` and `AcustraPerformer.cpp` with GCC 13.3, `-O3`, on
x86-64 Linux. All 158 render files (335,368,748 bytes of raw float output)
are identical between baseline and candidate. They cover:

- 108 construction cases at 48 kHz: every Shape, Wood, technique and capture.
  Each plays chords, a re-pluck, a slide, a bend, vibrato and palm muting.
  Shape, Wood, Tuning and Capture also change live, including a return to the
  sounding construction during a fade.
- Every construction's radiation poles and wood factors.
- 21 rate cases from 8 to 384 kHz and the Performer at 44.1 and 96 kHz, then
  the extended layout's 24 rate cases up to 768 kHz.

The tool renderers were compared the same way against the baseline build:

- These match byte for byte: the natural-performance renders, the
  audible-realism renders, the physical-fit bank corpus (198 files), the
  external-corpus and construction-strum renders, and the CPU comparison
  matrices (96 configurations and 40 partitions; 515 of 515 hashes).
- Eight of the ten demos produce identical WAVs.
- Changed by design: demos 09 and 10, and the repertoire's former Bellido
  parts.
- Realism song 05 also changes, from 4.1 s on, by at most about 51 dB below
  its peak. Its old score asked for the Bellido and back on one sample, and
  the old engine kept a trace of that request. With the request removed,
  identical scores render identically.

**Tests and builds.**

- The JUCE-free Release build passes all 70 applicable CTest suites,
  including 28 Python tool checks. The count falls from 75 by the five
  deleted checks.
- `AcustraEngine.cpp` and `AcustraPerformer.cpp` compile as C++17 with
  `-Wall -Wextra -Wshadow -Wpedantic -Werror` under Clang 18 and GCC 13, in
  both the default and extended layouts.
- Linux VST3 and Standalone build against the pinned JUCE 8.0.14
  (`2cdfca8feb300fb424002ba2c2751569e5bacb64`). The PluginProcessor and
  PluginTempoRelease suites pass under Xvfb.
- macOS AU and Windows builds are left to CI.
- `sizeof(AcustraEngine)` falls by only 128 bytes. This change makes no
  memory or CPU claim.

## Limits of this record

- **A lost independent check.** GuitarModels' independent
  radiation-recurrence and fractional-delay checks ran only on the Bellido
  bank, so they left with it. The Original's host-rate radiation is still
  checked for level across rates (within 0.12 dB), its continuation (within
  0.3 dB) and the steel blend's poles, but no longer against an independent
  complex recurrence.
- **Weaker cancellation checks.** A Shape or Wood change keeps the modal
  state where a model exchange reset it, so the converted cancellation and
  bridge-step checks catch less than before. They now also assert that a
  cancelled request starts no fade.
- **Piezo trim not re-run.** `CalibratePiezo.py` now takes its median over
  three construction presets. A re-run could move the shipped trim; it was
  not re-run.
- **Platform-specific figures.**
  - The README's demo peak rows 09 and 10 were measured with GCC 13 on Linux.
    The other rows stand, since those demos render unchanged on one
    toolchain.
  - The README's editor screenshot was re-rendered by the PluginProcessor
    suite on Linux under Xvfb. Its system fonts differ slightly from the
    macOS render that CI produces.
- **No listening.** The changed demos and repertoire have not been listened
  to.
