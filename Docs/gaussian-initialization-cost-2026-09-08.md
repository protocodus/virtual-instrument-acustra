# Gaussian initialization cost — 8 September 2026

The optimization preserves the existing continuous Gaussian contact and its
analytic tail bound. For radius below half a period, only the nearest image
can contribute, so that path avoids per-sample integer-image searches.
Ordinary pluck phases already lie in one period. Intervals outside all corner
supports use the same triangle directly; padded support edges still execute
the original radius test. Broad contacts retain the full image sum. A uniform
contact leaves the reset delay exactly zero after endpoint subtraction.

An isolated before/after build changes only this initialization block, using
the same frozen physical calibration, body banks and playing controls. All
36 callback output hashes are identical. Independent contact tests retain a
maximum complex Fourier error of 1.47e-8 and maximum broad-contact sample error
of 5.14e-8 across 40 cases, including 16 exact uniform cases. Natural-harmonic
checks also pass.

With 128 interleaved measured pairs and eight warmup pairs per case, paired
callback medians improve by 0.4%–13.2%. At 96 kHz / 32 frames, initial-chord
medians improve 5.7% for nylon and 6.8% for steel; repick medians improve 10.7%
and 13.2%, respectively. These are complete noteOn-plus-process intervals.
Scheduler outliers remain, so this isolated experiment supports the local
cost reduction and does not certify a deadline. Full combined guitar-bank
qualification is recorded separately in the final callback report.

The [raw timing and provenance report](gaussian-initialization-cost-2026-09-08.json)
retains all observations and frozen source hashes. The optimization is in
[AcustraEngine.cpp](../Source/DSP/AcustraEngine.cpp); the reusable probe is
[CallbackBenchmark.cpp](../Tools/CallbackBenchmark.cpp). No contact width,
pluck position, amplitude, tail bound or measured body coefficient is changed
by this optimization.
