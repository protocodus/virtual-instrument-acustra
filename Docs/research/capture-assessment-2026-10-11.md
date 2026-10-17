# Final capture assessment — 2026-10-11

The accepted capture voicing and microphone balance remain unchanged. Fresh
measurements of final DSP commit `2decfdfbe8fdf69ab8a0885de8fd095668d6f174`
confirm the matched observation and Mono recovery contract. They still show
recording-dependent spectral differences, but do not isolate a new capture
implementation fault or justify another automatic contour fit.

## Measurement and provenance

The [compact evidence](https://github.com/protocodus/virtual-instrument-acustra/tree/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/project-completion-2026-10-11/capture)
contains the complete fit report, source-weight sensitivity report, render
manifests, source notices, commands and a hash receipt. All 202 source hashes
in the original render-input receipt match the final commit and the inspected
working files; all four recorded renderer binary hashes match. This is a new
measurement of the final model, not an independent validation dataset: the
Eastman material and GuitarSet timing statistics have informed development.

`FitCaptureVoicing.py --observation matched` uses per-channel spectral power
for the Eastman stereo pair and the model Stereo output, and the model Mono
microphone for mono recordings. Room is zero. Note renders use 48 kHz,
127-frame blocks, Dreadnought/Spruce and the recorded default controls
(Width 0.62, Touch 0.58, Body 0.82); manifests retain the exact float values,
calibration and row-specific picking. The inputs are:

| Source | Current observations | Capture pairing |
| --- | ---: | --- |
| Eastman E1D picked / finger-plucked masters | 49 / 55 notes | Stereo per-channel power |
| Martin HD28 samples | 11 accepted notes | Mono microphone |
| Existing flat-top reference bank | 8 notes, repeated Eastman finger material | Stereo per-channel power |
| GuitarSet v1.1.0 | Six 12-second clips, one phrase and six players | Mono microphone |

GuitarSet was rendered once for Stereo and once for Mono, using finger picking,
velocity 91, annotated mean pitch and string-per-channel events. Its published
archive MD5s were verified. The directly rendered Mono signal matches the Mono
signal recovered from Stereo up to a constant trim: maximum relative residual
energy **4.552e-15**, below the existing **1e-10** limit, across all six clips.
This checks the observation algebra, not microphone realism.

## Residuals and sensitivity

The fixed-structure shared-contour fit reports **2.565 dB RMS** residual. It
suggests gains of −3.004, −2.144, −6, −6, −0.937 and +6 dB for the existing
120 Hz shelf and 125/250/500/1000/1400 Hz peaks. These gains were **not written**.
The fitter retains the separately authored microphone balance; its fitted
target and the measured centered recording-minus-model residual are different
quantities and are not a before/after quality comparison.

The protocol weights Eastman Pick and Finger by 0.5 each and the bank, Martin
and GuitarSet by 1 each. Because the bank repeats Eastman material, Eastman
supplies half the consensus. Removing that duplicate makes it one third and
moves individual suggested gains by as much as **0.81 dB**. The 1400 Hz peak
then leaves its +6 dB bound for +5.19 dB. Source omission also changes the
suggested gains; the sensitivity report preserves each result.

After level removal, the three source-group averages have a hotter model band
near 1 kHz (2.26–5.24 dB) and less model power near 2.52 kHz (2.07–9.10 dB).
The Eastman Finger take alone is only 0.23 dB model-hot at 1 kHz, versus
4.29 dB for Pick. These are useful unresolved spectral differences, not proof
of one universal correction. Guitar construction, microphone placement,
recording processing, unknown source velocities and playing all remain
confounded with the model. The manifests document the controls that are known.

A separate Mono contour improves its own fixed-structure residual from
2.964 to 2.941 dB RMS, **0.023 dB**, below the predeclared 0.25 dB indication
threshold. This does not call for splitting the shared capture contour.
GuitarSet's reported log-magnitude, convergence and chroma descriptors are
preserved separately; they are different metrics and neither perceptual
equivalence scores nor evidence of product rank.

The decision is therefore to retain both capture headers and their level
calibration. A future measured correction needs a repeatable discrepancy with
capture and playing conditions controlled, plus listening evidence. This
assessment establishes the final measured state and its limitations; it does
not declare that all acoustic observation differences have been solved.
