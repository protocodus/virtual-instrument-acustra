# Acustra project cleanup — 2026-10-09

The user requested removal of code for removed or unavailable features,
including archived experimental implementations. The baseline is canonical
commit `65f4940bc3975056a95bbd8cb4d4f1c61867cd46`, which supplied the released
Acustra 1.1.3 build 1 and Rack Extension 1.2.2f1.

## Removed code

- Rejected axial/phantom resonators, bridge-local direct radiation, separate
  Pick impact burst, contact-noise/click generator and optional polarisation
  end correction, including their buffers, state, calibration and tests.
  The standard excitation burst, release noise, coupled polarisations,
  tension model and accepted Classic attack/sustain policies remain active.
- Thirteen inactive calibration fields. The remaining 24 values, order,
  bounds and listener-selected freezes are unchanged. Positional 37-value
  and nylon-era 48-value inputs are rejected; named historical optimizer
  resumes retain the surviving fields. Four empty fitting stages are removed.
- The obsolete measured-body header writer, which emitted an incompatible
  old header layout. Its mathematical fitting routines remain for current
  body/bridge generators. The retired axial column is also removed from the
  ringing trace header and values together.
- Fifteen archived source files: nonlinear-string and moving-endpoint
  prototypes and amendment patches, old body/contact/aperture/saddle/loss
  implementation patches, the retired Rau-guitar generator and the
  unavailable compliant-pick contact solver. Its CTest registration is removed.
- Dormant compile-time analysis interventions for excluding measured open
  string modes, normal-plane-only bending loss and fixed contact aperture.
- Unused desktop runtime parameter-cache slots and unreachable Rack panel
  rendering code. The Rack benchmark defaults now agree with the actual
  motherboard defaults, and its existing contract check enforces that match.

## Compatibility and retained evidence

All 19 published desktop parameter identities and their order remain,
including three nonautomatable compatibility slots. Sixteen active slots
drive runtime. Saved-session migration and supported MIDI, MPE, Gather,
capture, room, piezo and connected hammer/pull behavior are retained.
Hidden host controls and stored Rack values remain usable.

Current scientific references, fitting generators and the offline reference
bank remain. Historical measurements, JSON reports, documentation, listening
packs and released packages are preserved. Paths in dated reports may name
experimental source removed from the current tree; the baseline Git revision
still records that source. This cleanup adds no listening decision or
new CPU-performance claim.

## Validation

Native Release comparisons use separately compiled baseline/candidate
headers and libraries with matching Apple Clang flags. All 280 deterministic
cases match every raw float byte, including signed zeros: 216 construction
cases cover two models, four shapes, three woods, three techniques and three
active captures; 64 control cases cover 44.1, 96, 192 and 384 kHz. Eight
audio/observer columns contain 140,780,160 samples per version. Finite/nonzero
guards and both connected transitions pass. This is representative control
coverage, not every continuous combination or a cross-platform claim.

Removing dormant state reduces `sizeof(AcustraEngine)` by 407,600 bytes in
the default layout and 1,624,208 bytes in the extended-rate layout. This
measures object storage, not callback speed or host memory use.

The final JUCE-free Release build and all 75 applicable CTest suites pass,
including required Python checks. The count falls from 76 only because the
deleted compliant-pick prototype's selftest is no longer applicable. Strict
default and extended-layout C++17 checks retain `-Wall -Wextra -Wshadow -Werror`.
Native arm64 VST3, AU and Standalone formats build successfully, and both
PluginProcessor and PluginTempoRelease suites pass. Published parameter and
state compatibility are checked by the existing wrapper tests.

Rack renderer comparison reproduces all 104 generated files exactly,
including 101 PNG pixel comparisons. Runtime panels, patches, properties and
released artifacts remain unchanged. Further build/test results and source
hashes are recorded in
[the cleanup validation receipt](project-cleanup-validation-2026-10-09.json).

Local detailed evidence is retained under
`build-cleanup-20261009/evidence/` in the canonical checkout and
`Examples/Acustra/Output/cleanup-20261009/` in the Rack checkout. These are
validation builds; the delivered release packages are preserved.
