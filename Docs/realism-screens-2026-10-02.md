# Bounded realism experiments, 2026-10-02

None of these sound changes is selected for implementation. The experiments
test the remaining 165 Hz ringing and Finger brightness discrepancies after
the live-chord correction, using the separate
[current-model baselines](cross-model-recording-baseline-2026-10-02.md).
The production engine and listener-selected calibration stay unchanged.

## Finger contact width

An offline source copy multiplies the registered Gaussian aperture by 1.0,
1.25 or 1.5 for Finger alone. It preserves velocity-relative release share,
slip, burst, contact-noise corner and all shipping calibration values. In
particular, it does not change `contactSamples` or `contactWidthRatio`.
Pick and Thumb retain their existing aperture. Both actual model presets
are rendered on every bank, Eastman and Martin split. The 1.0 copy matches
all 123 baseline float-audio files per preset exactly; each candidate keeps
all 72 Pick files per preset byte-identical.

The declared gates require improvement on both models' Eastman Finger and
bank flat-top totals, no corpus total or body-term regression above 1%,
unchanged first nonzero sample, fitted onset within 1 ms, played-pitch
changes within 3 cents, and an 8–10 dB median velocity-brightness rise at
each of 44.1, 48 and 96 kHz. Native probes use MIDI 40/45/51/57/60/66/72/78
at velocities 16 and 112, on both presets: 96 probes per variant.

| Width | Preset | Eastman Finger total | Bank flat-top total | Martin total / body |
|---|---|---:|---:|---:|
| 1.25 | Original | −0.321% | −0.197% | +0.012% / +0.192% |
| 1.25 | Bellido | −0.295% | −0.454% | −0.082% / +0.158% |
| 1.5 | Original | −0.677% | −0.370% | +0.111% / +0.420% |
| 1.5 | Bellido | −0.729% | −0.817% | −0.237% / +0.261% |

Negative percentages mean less descriptor error. Both candidates pass the
corpus, onset and pitch guards but fail the velocity gate. That gate also
fails for several baseline model/rate cells; it is an aspirational criterion,
not evidence of a newly introduced baseline regression. It was not relaxed
after seeing the results.

| Width | Original rise, 44.1 / 48 / 96 kHz | Bellido rise, 44.1 / 48 / 96 kHz |
|---|---:|---:|
| 1.0 | 9.584 / 9.066 / 10.328 dB | 8.559 / 6.999 / 7.947 dB |
| 1.25 | 9.833 / 9.319 / 10.648 dB | 8.783 / 7.403 / 8.196 dB |
| 1.5 | 10.109 / 9.628 / 11.034 dB | 9.027 / 7.861 / 8.470 dB |

A separate friend probe reads each orthogonal plane's initialized delay
history immediately after the explicit pluck, before new-note processing.
Its periodic-grid DFT is normalized by grid length, with powers summed
between planes. The continuum-slope diagnostic weights harmonic powers by
harmonic index squared and compares summed H5–H12 with H1–H4. At width 1.0,
median velocity contrast is about 8.60/8.58/8.57 dB across the three rates
on both bodies. Maximum per-note rate spread is 0.0733 dB, and maximum
per-note difference between bodies is 0.0175 dB.

These observed initial histories do not show the larger audible spread.
Downstream dynamics and the acoustic observation need separation before
retuning the release law. This DFT omits fractional-delay and filter
histories; it is not an exact modal decomposition of the full string. The
recorded acoustic 8–10 dB criterion is not transferred to this source metric.

An audit of all 96 saved two-channel baseline probes reproduces the original
mono peak estimator within 7.1×10⁻¹⁵ dB. Integrating partial power within
±2/window-duration or ±4/window-duration retains the rate/body spread.
Separate left/right channels and incoherent stereo power also retain it.
For Original MIDI 78, the rate spread remains about 6.59 dB in integrated
mono and 6.44 dB in incoherent stereo. All partials remain in the results;
none fails a 12× local interharmonic-floor proxy. This proxy may include
overlapping deterministic string/body energy. Peak-bin sampling and mono
cancellation alone therefore do not remove the variation. No estimator
replaces the original eligibility gate.

A native ablation then zeroes only `excitationEnvelope` immediately after
the explicit fired note. The unchanged friend control reproduces all 96
baseline files byte-for-byte. All 96 ablations pass a whole-voice
single-field-edit guard; initial RNG state, initializer, slip, filters,
contact-noise zeros and all other state remain unchanged. With no burst,
its private RNG naturally stops drawing during subsequent processing;
there are no later plucks in this fixture. The frozen DSP library is unchanged.

| Preset | Peak-estimator median rise without burst, 44.1 / 48 / 96 kHz | Median rate span, baseline → absent |
|---|---:|---:|
| Original | 9.286 / 9.410 / 9.491 dB | 1.263 → 0.205 dB |
| Bellido | 7.735 / 7.742 / 7.928 dB | 1.561 → 0.194 dB |

Tracked integration confirms the reduction. Some per-note rate variation
and the body difference remain; Bellido's contrast remains below 8 dB.
This identifies a substantial burst-related contribution in these fixed
draws, not a systematic sample-rate defect. Only one deterministic draw
per note/rate was tested. Removing the burst also changes absolute
brightness and cannot authorize changing that listener-selected sound.
The width-screen gate remains failed and unchanged. All note/channel
comparisons, single-field guards, control hashes and commands are retained;
raw partial measurements and the 192 stereo renders remain local.

## Low body radiation and reciprocal poles

The radiation screen rescales the existing Original main and joint T1
microphone contributions, independently, over the grid 0/0.5/1/2/4/8. Its
36 outcomes preserve the mechanical load. Baseline recombination uses the
seven-sample microphone latency and a fixed fitted gain, with the existing
baseline residual retained. Native gain-zero/eight probes confirm unchanged
bridge force, bridge velocity and all six string-motion traces. This is a
radiation diagnostic, not a structural Q experiment or complete passivity
certification.

Discovery uses four Eastman MIDI-64 clips, with three MIDI-67/72 clips
reserved for other-note checking and one Martin MIDI-64 crosscheck. The
primary 155–175 Hz errors use 40–400, 120–550 and 250–750 ms windows;
800–1200 ms is a later guard. Other bands and first-100-ms RMS changes are
retained. Candidate normalization stays fixed to the original model RMS,
so added loudness cannot normalize itself away. The mono diagnostic cannot
replace full stereo benchmarks or listening.

The discovery-selected 8/8 radiation scales reduce early 165-region mean
absolute error from 20.18 to 8.40 dB, and other-note error from 24.62 to
9.44 dB. They worsen Martin's early error from 8.59 to 14.93 dB, worsen
other bands, and add 4.16–6.55 dB of early RMS. The exploratory regional
criterion requires neither aggregate early 165-region error nor aggregate
early other-band error to increase on any discovery, other-note or Martin
subset. This criterion was assessed after initial measurements, rather than
preregistered as an eligibility threshold. All nonbaseline radiation
outcomes fail it. Removing a fitted slow low-E octave
as a nuisance sensitivity does not rescue them; that subtraction can also
remove nearby body energy and is not an isolated string measurement.

A second screen changes the shared native `radiationPole` used by both
body radiation and reciprocal bridge construction. For Original's measured
177–180 Hz main/joint modes, frequency scales 0.96/1/1.04 and Q scales
0.5/1/2/4 produce 12 outcomes. Residues stay fixed; all six ports remain.
Six baseline captures match saved stereo audio exactly. Actual bridge
biquad roots and body poles confirm the common frequency/Q change within
float roundoff. Named Bellido is excluded and its audio remains identical.
A frozen-force denominator postfilter would not establish this result.

The discovery-selected unchanged-frequency Q×4 raises main/joint free T60
to approximately 0.794/0.925 s. It improves the early 165-region error to
11.71 dB and the other-note error to 14.54 dB, but fails the separate
exploratory regional criterion on other bands. Martin's later ringing also
increases. It was then
rendered on all recording splits, without redefining that earlier failure.

| Original recording set | Total error change | Body error change |
|---|---:|---:|
| Bank picked training | +2.081% | +4.029% |
| Bank picked development | +3.221% | +4.799% |
| Bank finger flat-top | +0.081% | +1.466% |
| Eastman Pick | +0.777% | +2.312% |
| Eastman Finger | −0.562% | +0.443% |
| Martin, assumed Finger | +1.695% | +4.482% |

Q×1 reproduces every baseline audio file and score before ranking Q×4.
Bellido's full files and scores remain identical at Q×4, proving scope
rather than a realism improvement. Q×4 fails the predeclared full-corpus
guard: no total or body error may worsen above 1% on any split/preset.
Further shipping certification and listening are unnecessary for this
rejected candidate. Native stationary work observations and fixed residue
matrices do not constitute a complete changing-contact energy proof.

## Evidence and next implication

The [machine-readable record](realism-screens-2026-10-02.json) preserves all
36 radiation outcomes, all 12 pole outcomes, nuisance sensitivities,
both-model full-corpus comparisons, all three width variants and the source
diagnostic. It includes protocols, input/tool/source hashes, recorded
commands, native pole checks and the exact offline engine patches. Raw
audio and laboratory source copies remain in ignored local folders.
The two rejected pole-check attempts used ill-conditioned float-radius/Q
inversion guards; they were corrected by checking actual complex
coefficients and bridge roots. They are recorded as numerical guard
failures, not physics successes or failures.

For reproduction, use an isolated checkout of the recorded engine/tool
commits and prepared reference corpora with matching hashes. The JSON's
`laboratory_tools` contains the exact diagnostic sources; its
`offline_hooks` contains the Finger aperture/CMake patches and the native
T1 patch. These belong in laboratory copies only. The saved Finger and
body reports include build/run commands and required directory layouts.
The executed Finger runner is preserved separately from its portable
rerun version; fresh output prefixes avoid replacing original results.
Establish and hash a same-toolchain baseline before comparing variants;
do not relax failed byte parity into a tolerance test. Saved protocols
and thresholds remain unchanged.

These are transfer benchmarks between different instruments and captures.
Eastman/Martin velocity is assumed and Martin's picking tool is unknown.
Development recordings have already informed the instrument; no untouched
test-set claim is made. Small score improvements alone do not establish a
more realistic sound. Previous listener choices remain authoritative.

The defensible next measurement is repeated-draw Finger velocity contrast,
with source, bridge/body transfer and acoustic observations kept separate.
The single-draw burst attribution is insufficient for a release-law retune.
The present evidence supports
neither a global sympathy boost, a longer T1 decay nor a new Finger width.
