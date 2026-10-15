# Capture refinements screen, 2026-10-15

These are offline experiments. None of their changes are enabled in production.
The white-box probes change only the prepared microphone bank/filter. They do
not change the body mechanics, piezo chain, UI, calibration or production source.

Freeze the source headers and matching Release `libAcustraDSP.a` before building.
The measured source is `90d2d8c`, GCC 14.2, `-O3 -DNDEBUG -std=c++20`.
With `FROZEN` pointing to the snapshot containing `Source/` and
`libAcustraDSP.a`, run from the repository root (choose an empty `AUDIT`):

```sh
FROZEN=/path/to/frozen-baseline
AUDIT=/path/to/capture-audit
mkdir -p "$AUDIT"
g++ -O3 -DNDEBUG -std=c++20 -I"$FROZEN/Source" Tools/CaptureRefinementAudit/Probe.cpp "$FROZEN/libAcustraDSP.a" -o "$AUDIT/probe"
g++ -O3 -DNDEBUG -std=c++20 -I"$FROZEN/Source" Tools/CaptureRefinementAudit/NoteProbe.cpp "$FROZEN/libAcustraDSP.a" -o "$AUDIT/note-probe"
"$AUDIT/probe" "$AUDIT/out" 0 7
"$AUDIT/note-probe" "$AUDIT/notes"
python3 Tools/CaptureRefinementAudit/Analyze.py "$AUDIT"
```

`Probe` produces 12-second musical scores through Performer: four Spruce Shapes,
Finger/Pick/Thumb, Room .5, Piezo Mix .2, Release Noise .1, other engine defaults.
Every variant has the same deterministic events and controls. Output is stereo
48 kHz float32, untrimmed. Variant 0 is baseline; 1 reduces the existing 1 kHz
peak by 1.5 dB and 1.4 kHz peak by 2 dB; 2 inserts a 35 Hz Butterworth high-pass
on microphone pressure; 3 a -1.5 dB shelf at 4 kHz; 4 removes the existing
per-mode brightness tilt as a diagnostic; 5 reduces the 1 kHz peak by 1.5 dB
and moves the +6 dB 1.4 kHz peak to 1.8 kHz; 6 also reduces that peak to +4 dB.
Candidates 1, 5 and 6 are alternatives within one presence-contour experiment.

`NoteProbe` produces variants 0, 1, 5 and 6 for the eight embedded flat-top
reference pitches, at velocity 91, dry stereo microphone, no Piezo Mix. Export
those references with the matching baseline `AcustraPhysicalFitRenderer`, giving
all 25 calibration values from that snapshot's `OptimizePhysicalModel.SHIPPING`:

```sh
# Use the snapshot's calibration values, not a later edited calibration.
"$FROZEN/AcustraPhysicalFitRenderer" "$AUDIT/corpus" CALIBRATION_VALUES...
python3 Tools/CaptureRefinementAudit/CompareReference.py "$AUDIT"
```

Reference comparison requires NumPy and SciPy and imports the repository's
`FitCaptureVoicing.py`. It verifies native baseline sample parity, uses the
existing 0.03–1 second onset-relative observation and per-channel powers, and
compares 0.8–3.2 kHz. Individual-note scores exclude bands below -40 dB of
maximum reference or baseline band power, with one common mask per note across
all candidates. Pooled spectra sum each note's normalized powers. Do not use
full-range spectral RMS on these sparse notes: unoccupied bands dominate it.
The eight notes come from one previously used Eastman take; they are development
evidence, not held-out or multi-instrument validation. A lower error does not
establish a listening preference.

The short-lived experiment outputs are under
`/workspace/acustra-capture-experiments/`; committed measurements and decisions
are in `Docs/validation/capture-refinements-2026-10-15/`.
