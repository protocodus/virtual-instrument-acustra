# Bounded upper-register reference audit — 2026-11-02

> Written on a branch off 1.9.13 that numbered this work 1.9.14 and 1.9.15.
> Those versions reached main on 2026-11-05 as 1.9.20 and 1.9.21, after
> main's own 1.9.14 to 1.9.19; the figures here are from that branch.

This audit supplies no defensible direction for another blanket gain, phase or
damping change. The model has less normalized between-partial attack energy
than the louder Shinyguitar layers, but more than its softest layer. The selected
high notes do **not** reproduce the historical global claim that model upper
partials decay at about 35% of the recorded rate. This is a new, bounded
descriptor study, not a reproduction or universal contradiction of that claim.
The current validated 1.9.14 pad-corner and fret-arrival improvements stand.

The selection is 64 historical Shinyguitar archtop recordings (MIDI 72/75/78/84,
four layers and four round robins), three Eastman Finger recordings (71/77/83),
and 25 new serial renders from the existing `AcustraExternalCorpusRenderer`:
Dreadnought/Spruce, shipping calibration, default controls, dry microphones,
no piezo; Pick velocities 20/50/80/112 and Finger 40/91/112. Shinyguitar is
[an archtop per its first-party README](https://github.com/sfzinstruments/karoryfer.shinyguitar).
The [Eastman source](https://github.com/0x4D44/ferrosintesis/tree/810318c92e33e31b36638b0ffa7ffc834a2ae6a2/samples/acoustic-guitar-eastman-e1d)
is separately identified as finger-plucked in the pinned export manifest.
These are recovered calibration references, not held-out evidence.

## Observed attack and decay

Between-partial energy is measured over third-octave centres 2.5–12.5 kHz,
relative to each signal's own first-100-ms power. Positive differences mean
more model energy. The difference is paired by pitch against the median of
its four reference round robins. Decay columns are separate medians of valid
H9–H12 slopes, in dB/s; they are not differences of paired medians.

| Shiny layer; model velocity | Model minus reference attack, dB | Reference early / late decay | Model early / late decay | Valid late partials, reference / model |
| --- | ---: | ---: | ---: | ---: |
| 1–32; 20 | +9.82 | 76.0 / 17.0 | 67.2 / 43.5 | 55/64; 16/16 |
| 33–64; 50 | −3.08 | 87.2 / 14.6 | 89.1 / 42.0 | 61/64; 16/16 |
| 65–96; 80 | −10.42 | 100.8 / 20.0 | 95.4 / 41.8 | 62/64; 16/16 |
| 97–127; 112 | −10.01 | 92.9 / 20.3 | 99.7 / 41.5 | 64/64; 16/16 |

All early cells have 64/64 reference and 16/16 model partials supported. The
recordings' attack between-partial levels are 37–73 dB above their measured
pre-fade tail floor, so the loud-layer deficit is not explained by that floor
alone. Between-partial energy can also contain rapid harmonic modulation and
window leakage; these figures do not identify a pure noise source or its cause.
Despite that loud attack deficit, the model's 30–330-ms share above 2.5 kHz is
higher than the references' in every selected layer. Simply adding treble would
not distinguish those two observations.

Eastman has no recorded velocity label. Bracketing model velocities gives this
pitch-dependent attack difference, rather than a shared missing-transient gain:

| Eastman pitch | Model v40 | Model v91 | Model v112 |
| --- | ---: | ---: | ---: |
| B4, MIDI 71 | +2.31 dB | +4.18 dB | +5.25 dB |
| F5, MIDI 77 | −0.31 dB | +1.22 dB | +1.76 dB |
| B5, MIDI 83 | −9.99 dB | −8.71 dB | −8.16 dB |

Those reference attacks are 24–39 dB above the measured tail floor. Only 4/12
Eastman late H9–H12 slopes pass the floor guard, so their late median is not a
reliable general target. No model velocity was selected as a best fit.

## Method and limits

`analyze.py` reuses canonical `FitPhysicalModel` onset/spectra and
`MeasureAttackTransient` nine-period Blackman frames and between-band masks.
The full-spectrum running median was too slow and its initial run was stopped;
the off-tree series search uses a local mean baseline on the same bounded
f0/inharmonicity grid. It is a changed estimator, not direct historical
reproduction. No fit hits either search boundary. Detected reference onsets
remain within 0.69 ms of manifest onsets. Nine-period windows span approximately
8.6–17.2 ms here: the nominal 0–12-ms descriptor cannot resolve a brief contact
instant independently of neighboring samples.

All signals are resampled to 48 kHz. Stereo uses channel power, not a waveform
fold; the mono Shiny comparison recovers the model's dry upper-bout Mono mic.
Partial envelopes use zero-phase narrow-band filters, 10-ms samples and linear
fits over 30–300 and 300–1500 ms, with more than eight samples at least 15 dB
above both the historical first-two-seconds percentile floor and an independent
pre-fade native tail floor. Counts, individual frequencies and non-monotone
slopes remain in `measurements.json`; these are envelope descriptors, not
intrinsic loss constants.

Unknown pluck force, fingering, pluck position, microphone placement and room,
plus archtop-versus-Dreadnought construction, prevent causal attribution. The
old global figures used another register aggregation, Jumbo and the old
estimator. Four pitches with nested RRs are not 64 independent instruments.
Historical `compare_reference_takes.py` at `7b52fd1` selects only `bank == "0"`
for its four-layer comparisons: the labeled picked/layered reference there is
Shinyguitar, while the separate eight Eastman flat-top rows remain flat-top.

Stop this iteration at evidence. The loud Pick attack deficit is a useful
future target for technique- and register-matched release measurements; it does
not justify increasing Finger/Thumb noise or adding blanket upper damping.
No physical mechanism, calibration value or production DSP was changed.
`provenance.json`, `renderer.log`, `jobs.txt`, `analyze.py`, `summarize.py`,
`measurements.json` and `summary.json` preserve the complete bounded protocol.

Reproduction artifacts are outside Git in the workspace root at
`diagnostics/acustra-goal-20261102/upper-reference-audit/`; recorded source
audio is excluded from the review bundle and canonical repository.
