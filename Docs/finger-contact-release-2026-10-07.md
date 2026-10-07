# Finger contact release

The Finger release burst now follows the contact's actual force-release time.
This reduces the excessive high-frequency bass attack without changing the
released string wave, held-force step, note timing or amplitude envelope.
Pick and Thumb keep their existing burst laws.

## Cause and selected law

Matched native ablations used the current production baseline, `9bf1cff`,
Original/Dreadnought/Spruce, Room 0 and velocity 91. The full E2 ablation render
is byte-identical to its frozen baseline corpus render. Removing the burst
reduces first-15-ms 2–12 kHz power by 8.12 dB on E2 and 10.57 dB on A2;
removing the static body-force release changes it by only 0.42/0.13 dB.
Removing direct radiation or the disabled optional contact-noise source has
no effect. The burst, rather than the static-force release, is the dominant
source of this bass attack excess. There is also a residual string/body gap.

The existing burst sends noise through two first-order filters. Its colour
corner previously depended on Touch and velocity, but did not follow the
actual contact's existing `r/u` release time: effective tip radius divided by
the released string's speed. The released string already uses that time.
The new coupling uses the same force/geometry pole `b` and preserves the
complete burst cascade's sampled mean time. A normalized first-order kernel
has mean `b/(1-b)` samples; two identical stages at `q` have mean
`2q/(1-q)`. Thus `q = b/(2-b)` and the stage coefficient is
`2(1-b)/(2-b)`. Each stage uses the smaller of this coefficient and the
existing colour coefficient, both designed at the actual host sample rate.
The existing colour bound preserves firm articulation when the release is
faster than that bound.

This is an authored coupling of an existing effective-contact model, not a
measured fingertip trajectory. The two stages retain unit DC gain; there is
no per-note gain compensation or added output equalizer. Their native noise
energy changes as their bandwidth changes. The corner is captured at actual
contact, including a scheduled note's then-current controls. A later style
edit does not rewrite the burst already in progress. Already launched waves
keep their captured contact-travel histories through tail/arrival retention.

No filter stage, allocation or output latency is added. The colour/rate
coefficient cache moves to attack initialization and its effective coefficient
is stored for the burst; this removes those cache comparisons from its sample
loop. Final CPU timings belong to the combined, frozen implementation.
`PerformanceRealism::contactRelease = false` is the setup-only ablation;
changing realism options resets the engine, rather than automating a live
contact. It restores the previous Finger corner.

## Recording evidence and tradeoffs

Baseline and candidate use fresh native engines at 48 kHz, 127-sample blocks,
4.2 seconds, fixed default calibration and velocity 91. Original uses
Dreadnought/Spruce, Classical 78 Auditorium/Mahogany; both use Room 0.
`BenchmarkOpenCorpora.py` and its scorer are unchanged. Both models use the
same 55 identified Eastman Finger, 49 identified Eastman Pick and 11
unknown-tool Martin rows. No nuisance control is fitted per reference.
The production candidate reproduces the scored offline implementation's
native arrays exactly.

The attack diagnostic uses each signal's frozen fitter onset, a periodic
Hann window over 15 ms, a 4096-point FFT, and the 2–12 kHz share of
80 Hz–12 kHz power. For the 20 Finger bass rows (MIDI 40–54):

| Measurement | Original | Classical 78 |
| --- | ---: | ---: |
| Median signed model-minus-real attack share, before | +21.04 dB | +24.46 dB |
| Median signed model-minus-real attack share, after | +14.53 dB | +17.90 dB |
| Median paired share change | −6.93 dB | −5.44 dB |
| Rows with smaller absolute gap | 19/20 | 20/20 |
| Median native first-15-ms RMS change | −0.105 dB | −0.292 dB |
| Median native 80–250-ms RMS change | −0.009 dB | −0.025 dB |
| Median native whole-render RMS change | −0.009 dB | −0.024 dB |

The 17 upper-register Finger rows are byte-identical: their existing colour
bound is already slower. All known Pick renders remain byte-identical. The
attack gap remains large, and this work does not claim to resolve it fully.

| Frozen descriptor score change | Original | Classical 78 |
| --- | ---: | ---: |
| Eastman Finger total | +0.12% | −1.20% |
| Eastman Finger harmonic term | −0.53% | −0.16% |
| Eastman Pick total | exact | exact |
| Martin unknown-tool total | +5.00% | +2.88% |

Lower descriptor error is better. Original's whole Finger score slightly
worsens despite the clear bass attack correction; Classical 78 improves.
Martin loses attack/body agreement. Its brighter, cropped, unknown-tool
notes cannot establish a Finger contact law, but their transfer regression
is retained. The same assumed Finger technique is used before and after.
These corpora have informed previous development, so they are not pristine
held-out data. Different real guitars, captures, assumed velocity and
fingering remain confounds. Neither the scores nor an audible difference
establish listener preference.

Two alternatives were rejected. Replacing the released wave's exponential
with a two-stage equal-mean shape worsens known Finger attack and harmonic
gaps. Putting the original force pole on both burst stages gives a total
mean twice the release time; it makes a stronger correction, but fails the
existing plate-conductance tail guard. The selected law matches the complete
burst's mean to the existing release and preserves that damping guard.

## Dynamics and verification

Native dynamics were inspected at velocities 16, 40, 64, 91, 112 and 127 on
E2, E3 and G4 in both models. First sounding samples remain unchanged.
Strong notes remain brighter overall; the initial 16→40 dip on the basses
also exists in the baseline. The released wave, held-force step, envelope,
decay and noise draws are exact before/after; only the burst filter changes.

The previous engine brightness fixture used one stochastic burst to require
more than 3 dB of 16→112 upper-partial contrast on each of eight notes.
Before examining the ensemble result, 16 seeds were fixed as
`0x6d2b79f5 * take`, with `take = 1..16`. Only `excitationNoiseState` changes
after initialization; geometry, force, angle and other random sources stay
fixed. The same seed is paired between velocity layers.

Baseline E2 fails the old single-draw threshold on 8/16 seeds. Its contrast
ranges 2.337–3.847 dB, median 3.092 dB; the candidate ranges 2.598–3.464 dB,
median 3.014 dB. Two unchanged upper notes also fail individual draws:
MIDI 72 on 5/16 and MIDI 78 on 2/16. All eight per-note ensemble medians
remain above the unchanged 3 dB guard. The fixture now tests those medians,
without altering the analysis window, partial bands or threshold. A focused
constant-velocity negative control fails all eight notes at exactly 0 dB.

`ContactRelease` independently recovers the force/geometry release time,
checks its sampled-mean corner at 8–384 kHz and the six velocities, verifies
the actual two-filter recurrence against independently observed white draws,
and checks unchanged waves/force, same-colour force revisions, scheduled
controls, in-flight histories, technique isolation and block partition.
`VelocityRealism`, `StringPitchRealism` and `RepeatedPluckRealism` retain
their existing guards. C++17 DSP compilation is also checked. Combined
validation and listener material are recorded in the accompanying natural
performance report.
