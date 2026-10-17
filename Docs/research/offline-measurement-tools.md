# Offline fitting, benchmark and audit tools

How to run the offline fit, the recording benchmarks, the audits and the
table generators. Moved unchanged from the README's **Build** section on
2026-10-17; build the JUCE-free tools first ([README: Build](../../README.md#build)).

To run the bounded offline fit after building the JUCE-free tools:

```sh
python3 Tools/OptimizePhysicalModel.py \
  ./build-dsp/AcustraPhysicalFitRenderer /tmp/acustra-physical-fit
```

The output directory must not already exist. NumPy and SciPy are required.

To reproduce the fixed performance benchmark with the published GuitarSet
`annotation.zip` and `audio_mono-mic.zip` archives in one directory:

```sh
python3 Tools/BenchmarkPerformances.py \
  --dataset /path/to/guitarset \
  --renderer ./build-dsp/AcustraPerformanceRenderer \
  --output /tmp/acustra-performances
```

The output directory must be new. `--capture` and `--picking` can evaluate
explicit alternatives, and `--room 0..1` renders with the Room (default 0;
`Tools/BenchmarkOpenCorpora.py`, `AcustraPhysicalFitRenderer`,
`AcustraExternalCorpusRenderer` and `AcustraPerformanceRenderer` take it
too, and `AcustraRenderRepertoire` reads `room=` on a part's line). To compare with the simultaneous
magnetic recording, download `audio_mono-pickup_mix.zip` instead of the microphone
archive and add `--reference-capture magnetic_pickup --capture piezo`. This is a
cross-sensor comparison; the retired magnetic model is no longer available.
The tool verifies only the selected audio archive plus the annotations and
records both the reference transducer and model capture. Scores against a
microphone recording cannot validate a pickup's response.
`--self-test --renderer ./build-dsp/AcustraPerformanceRenderer`
checks the scoring, string scheduling and renderer without downloading audio.
For dry-note comparisons, `AcustraPhysicalFitRenderer` takes the optional
`--models-only`/`--smoke` mode, then `--shape`, `--body-material` and
`--archtop-picking`, the output directory and the 25 calibration arguments in
`OptimizePhysicalModel.NAMES` order (the obsolete 37-value and nylon-era
48-value vectors are refused). The selected shape, wood and archtop picking
tool are recorded in each model manifest, with `guitar_model` fixed at
`original` so new runs still pair with earlier ones; without them every row
renders at the default Dreadnought and the archtop rows with Finger.
`AcustraExternalCorpusRenderer` accepts the same `--shape` and
`--body-material` options; `BenchmarkOpenCorpora.py` forwards them and
records the returned controls.
For an explicit performance event file, `AcustraPerformanceRenderer` also
accepts a trailing `--tuning standard|drop_d|dadgad|open_g|half_step_down`
option (the plug-in's five tunings). String/fret validation uses the selected
tuning; omitting it preserves the Standard render.

The independent technique-note audit needs the published v3 archive
(`4563a70d5dd9a14919171d103f10f6731fd78e63f1498dd11047bfea471f6a6d` SHA-256):

```sh
python3 Tools/BenchmarkTechniqueNotes.py \
  --archive /path/to/guitar-notes-v3.zip \
  --renderer ./build-dsp/AcustraPerformanceRenderer \
  --output /tmp/acustra-technique-notes
```

The output must be new;
`--self-test` checks selection, fingering and missing-feature accounting without
the recordings. Keep the downloaded archive and emitted audio out of git.

The recording-only hammer-on audit uses the two Taylor high-E hammer-on WAVs
from AG-PT-set. Its tool docstring lists the exact names and protocol:

```sh
python3 Tools/BenchmarkHammerOnNotes.py \
  --audio-dir /path/to/agpt/audio \
  --output /tmp/acustra-hammer-report.json
```

`--self-test` checks octave ambiguity, plateau selection and level measurement
without downloading recordings. The report's inferred times are not authored
MIDI events; timing, finger dynamics and capture remain uncertain.

An optional host renders installed VST3 instruments from their normal saved
states. Enable it in a JUCE/plugin build and build its target:

```sh
cmake -S . -B build-reference -DCMAKE_BUILD_TYPE=Release \
  -DACUSTRA_BUILD_PLUGIN_REFERENCE_HOST=ON
cmake --build build-reference --config Release --target AcustraPluginReferenceHost
```

The executable is under `build-reference/AcustraPluginReferenceHost_artefacts/Release/`.
Its `editor PLUGIN OUTPUT.state [INPUT.state]` command opens the ordinary plugin
editor; closing the window saves a new state file. Then use
`render PLUGIN STATE MIDI OUTPUT.wav BPM SECONDS` for a fresh-process render.
The host uses 48 kHz stereo float audio, 128-frame blocks and explicit 4/4
tempo/PPQ transport. Existing outputs, silent renders and failed renders are
rejected. Its bounded loading wait is not a general sample-readiness guarantee;
verify repeated fresh-process renders for each reference instrument.

Two audits read a rendered fit corpus and compare the model with the recordings
where the fitted score cannot say where a difference sits: one along the
frequency axis for decay, one band by band for the attack. Both withhold any
band whose answer moves with the analysis window, printing that instead of a
number, and both carry a self-test:

```sh
python3 Tools/AuditDampingCurve.py /tmp/acustra-physical-fit
python3 Tools/AuditAttackBands.py /tmp/acustra-physical-fit
python3 Tools/AuditDampingCurve.py --self-test
python3 Tools/AuditAttackBands.py --self-test
```

For a generated-note bridge audit, the probe writes headerless 48 kHz
little-endian float32 stereo audio plus a companion with five interleaved
channels: main reaction force, body force, tail force, bridge velocity and a
fifth that is always zero (the engine's separate sympathetic force, removed;
idle strings reach the body through the junction). The analyser reports matched-window magnitudes
and each signal's intra-signal harmonic phase relative to its H1; those phases
are not cross-stage transfer phase:

```sh
./build-dsp/AcustraBridgeProbe /tmp/e4.f32 on 0.5 auditorium 64
python3 Tools/AnalyzeBridgeProbe.py --midi 64 \
  /tmp/e4.f32 /tmp/e4.f32.telemetry.f32
```

To refit the Original's capture voicing after a change that moves its
radiation, render the three sources with the new build and fit twice
(rebuilding between), then re-level the Original's constructions:

```sh
./build-dsp/AcustraPhysicalFitRenderer /tmp/bank <25 shipping values>
python3 Tools/BenchmarkOpenCorpora.py --renderer ./build-dsp/AcustraExternalCorpusRenderer \
  --output /tmp/open --keep eastman/rows.json martin-hd28/rows.json
python3 Tools/BenchmarkPerformances.py --dataset /path/to/guitarset \
  --renderer ./build-dsp/AcustraPerformanceRenderer --output /tmp/guitarset
python3 Tools/FitCaptureVoicing.py --bank /tmp/bank --open /tmp/open \
  --guitarset /tmp/guitarset --write-header [--level-db DB]
python3 Tools/CalibrateConstructionLoudness.py \
  --renderer ./build-dsp/AcustraPerformanceRenderer --write-header
```

`--level-db` is the level that keeps the default construction's loudness
(`CalibrateConstructionLoudness.py --json` reports it before and after);
`python3 Tools/FitCaptureVoicing.py --self-test` recovers a known voicing
from a synthetic corpus and downloads nothing.

To regenerate the offline reference bank from separately downloaded source
archives:

```sh
python3 Tools/GenerateSampleBank.py \
  --eastman /path/to/acoustic-guitar-eastman-e1d \
  --shiny /path/to/karoryfer.shinyguitar
```

To reproduce the measured g21 coefficient tables from the separately
downloaded CC BY 4.0 Mores archive, including the passive bridge and force-pair
body regression gates, use a new output directory for the body generator:

```sh
python3 Tools/GenerateBodyForcePair.py \
  --raw-mat /path/to/qualified_selected_impulses.mat --plate-q median \
  --output /tmp/acustra-body-force-pair-regeneration
cmp /tmp/acustra-body-force-pair-regeneration/MeasuredBodyData.h \
  Source/DSP/MeasuredBodyData.h
python3 Tools/GenerateMeasuredBridge.py \
  --raw-mat /path/to/qualified_selected_impulses.mat \
  --body-report /tmp/acustra-body-force-pair-regeneration/report.json --check
```

To preflight a commissioned calibration pilot or full standard-tuning delivery
without modifying it:

```sh
python3 Tools/ValidateCapture.py /path/to/pilot-delivery
python3 Tools/ValidateCapture.py --full /path/to/full-delivery
python3 Tools/ValidateCapture.py --self-test
```
