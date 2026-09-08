# Fixed control-character comparison

These are the exact small renderer and analysis sources used for the September
2026 picking/body feedback comparison. They are offline audit tools, not plugin
code. Each note starts a fresh engine with the same seed. MIDI 40/55/64/74 and
velocities 91/127 cover steel and nylon, the three picking styles, four shapes,
normal panel position and actual MPE CC74=0.3. Shape comparisons include Stereo
mic and Piezo. The full fixed matrix has 152 renders per executable.

Build an adapter against each source revision being compared:

```sh
clang++ -std=c++20 -O3 -I Source/DSP \
  Tools/ControlCharacterAudit/OptionRenderer.cpp Source/DSP/AcustraEngine.cpp \
  -o /tmp/acustra-control-options
```

Keep before/after executables separately, then run with Python, NumPy and SciPy:

```sh
python3 Tools/ControlCharacterAudit/compare.py \
  --renderer before=/path/to/before-options \
  --renderer after=/path/to/after-options --output /tmp/new-control-comparison
```

The output directory must be new. Use simple distinct renderer labels. The
adapter accepts only the fixed protocol's arguments; it is not a general
performance-file renderer. Ordinary cases were cross-checked byte for byte
against `AcustraPerformanceRenderer` before interpreting the results.

Each render lasts two seconds without note-off. Features use fixed source-time
windows: 0–50 ms attack, 0–100 ms early, and 100–500 ms sustain. Stereo power
avoids phase cancellation from mono summing. Per-note 0–500 ms RMS normalization
removes overall volume differences; changes in relative attack/sustain level
remain meaningful. Power-weighted band differences reduce the contribution of
weak spectral bins. Reported distances are engineering descriptors, not
perceptual thresholds or realism scores. Body/picking parameters remain authored
variations; no recordings are loaded and no feature or parameter is fitted.

WAVs use a common 0–500 ms stereo RMS of 0.035, with a checked peak below 0.99;
raw renders and gains remain in the report. The sources also retain the observed
limit: fixed MPE position can make high-velocity steel Finger/Pick very similar,
because both contacts are narrow. The normal panel comparison must not be used
to claim separation at every possible control setting.

See [the change record](../../Docs/control-character-feedback-2026-09-08.md)
and its companion JSON for retained summaries, source hashes and validation.
