# Steel contact travels from the picking point

Dated 2026-09-08. Ordinary steel notes now send the existing short contact burst
toward the bridge and nut from the physical picking point. Previously the burst
was written at the collapsed round-trip boundary, delaying its first bridge
arrival by approximately a string period. This change passed the predeclared
recording gates without fitting an amplitude, delay or calibration parameter.
Its descriptor gains are small; they do not establish an audible improvement
or a ranking against commercial instruments.

## Mechanism and scope

For picking position `p=x/L` measured from the bridge and string round-trip
duration `D=2L/c` in samples, the two contact paths have delays `pD/2` and
`(1-p/2)D`. Each carries `1/sqrt(2)` of the existing displacement-wave source.
Their difference enters the incident wave before the shared bridge solve:

```
incident += polarizationGain / sqrt(2)
          * [source(t-pD/2) - source(t-(1-p/2)D)]
```

The minus sign is displacement inversion at the fixed nut. Polarization gains
remain 0.76 and 0.51. The former boundary write is disabled for these voices,
so the source is emitted once. Subsequent round trips use the existing string
loop, losses and dispersion. The perpendicular plane drives the shared bridge;
the parallel plane contributes to the existing steel attack-pitch surrogate.
This change does not add a parallel bridge port.

The fractional paths use fixed allpass delays. The source remains an authored
displacement-noise burst, not a measured contact force or a friction/contact
solver. The equal split preserves the sum of the two emitted directional
wave-velocity energy norms. Coherent interference at the bridge, total stored
energy including existing waves, and microphone RMS need not be preserved.
There is no fitted compensation for the resulting amplitude change.

Ordinary steel notes use this routing. Nylon and natural-harmonic excitation
keep their established paths. Picking, body, capture, velocity, calibration and
public parameter mappings are unchanged. No additional feature or control is
introduced.

## Recording decision

The frozen protocol required lower steel attack loss on both dry and Walden
training recordings, with total loss no more than 0.5% worse on either. After
training passed, the candidate was frozen and the existing development sets
were scored once against the same gates. No amplitude or delay search was
performed. These development recordings have been inspected before; they are
not a blind test. The protected archtop test regions were not opened.

Baseline DSP is the sound in `dfa1002`; protocol commit `b958979` has the same
DSP. Baseline renders were independently authenticated against the retained
recording corpus. Lower scores are better:

| Recording and split | Total before | Total after | Attack before | Attack after |
|---|---:|---:|---:|---:|
| Dry steel, training | 6.091584 | 6.091710 | 7.147092 | 7.140397 |
| Walden, training | 9.132877 | 9.149220 | 8.765550 | 8.739105 |
| Dry steel, development | 6.739403 | 6.734326 | 9.087985 | 9.081581 |
| Walden, development | 8.383039 | 8.379880 | 8.628614 | 8.557665 |
| Flat-top monitoring set | 8.791977 | 8.690282 | 8.731260 | 8.518641 |

Training totals increase by 0.0021% and 0.1790%, within the declared tolerance;
both development totals decrease. Flat-top total loss decreases by about
1.16%. This is evidence for a modest attack-source correction under this
protocol, not uniformly better sound across every descriptor or note.

The six-player GuitarSet monitor is mixed: mean log-magnitude error changes
from 13.540526 to 13.547153, spectral convergence from 0.915735 to 0.915695,
and chroma distance from 0.305459 to 0.305620. These performances are not
matched in picking velocity, exact instrument or microphone placement; they
remain a generalization monitor rather than a fitted or blind listening test.
The 39 dry nylon renders are byte-identical to the pre-change baseline.

## Release and repicking qualification

The first held-note candidate exposed lifecycle problems that were fixed
before integration. An ordinary damping release now stops new contact
emission. Already emitted waves remain in transit and receive the relevant
plane's applied hand damping on arrival. Pedal-held notes continue until pedal
release; the existing legato path retains its separate handling.

Immediate or scheduled repicking retains the preceding vertical transport in
the captured tail, even when the current wave-level estimate is quiet. That
tail receives zeros as new input and its own captured hand damping on arrival.
A quiet tail cannot retire while a contact packet is pending. Panic, reset and
MPE ownership changes cannot replay queued contact into a new voice.

Transport retires only after delayed input is gone and all allpass states are
too small ever to produce a nonzero float. Its routing identity persists after
retirement, preventing fallback to the former boundary write. This avoids an
audible cutoff threshold.

The final lifecycle implementation is byte-identical to the scored first
candidate for all **115 held-note renders**: 39 nylon, 32 steel and 8 flat-top
dry notes, plus 18 Yamaha and 18 Walden held notes. All 18 rerendered first-stage
Walden notes also match the earlier scored files. The **six complete GuitarSet
performances** are byte-identical between first and final candidates. Lifecycle
behavior is tested separately; held-note identity alone cannot validate it.

The new native contact test covers allpass energy and delay, the reflected
sign, independent plane damping, pedal release, immediate and scheduled
repicking, pending quiet tails, retirement and extreme MPE ownership changes.
Removing arrival damping produces three failures; removing pending-tail copy
produces 1,199 failures in the retained negative-control audit.

An independent source-only probe covers 54 rate, pitch and position cases.
Worst relative emitted derivative-energy error is `2.78243e-8`. Its low-E
contact-only bridge force starts at 0.729 ms at 48 kHz, and 0.625–0.742 ms over
8–384 kHz. These are thresholded discrete first-arrival observations, not an
exact broadband group-delay or acoustic onset measurement.

## Validation and retained evidence

The final native Engine, Release, Capture, GuitarModels and ContactTravel
tests all pass. JUCE processor contract tests pass, and the standalone app
builds successfully. Exact scores, source hashes, artifact hashes, lifecycle
checks and callback results are retained in the [machine-readable report](contact-travel-2026-09-08.json).

The historical [first-candidate patch](../Tools/ContactTravelExperiment.patch)
is retained for reconstructing the scored held-note experiment. It predates
the release/tail/retirement corrections; it is not the final implementation
and must not be applied on top of current source. Current `AcustraEngine`
source and `ContactTravelTests.cpp` are the integrated implementation.

The paired native callback diagnostic covers 108 configurations: six
material/model combinations, 48/96 kHz, 32/64/128 frames, and initial chord,
repick and eighth-harmonic events. Each case has 256 measured pairs and 16
warmup pairs, alternating baseline/current order. Timed work includes note-on
and processing; engine preparation and the repick's 50 ms pre-roll are outside
the interval. Both adapters use the same named guitar, material, shape and
wood. Other agent compilation and rendering jobs were paused for this run.

Across the 48 ordinary-steel configurations, the median of per-case paired
median time ratios is **1.02522**; across all 108 it is **1.02160**. These are
workstation measurements, not deterministic CPU guarantees. Worst per-case
p95 deadline occupancy is 78.075% for the candidate and 82.575% for baseline.

| Rate / frames | Worst baseline p95, µs | Worst candidate p95, µs | Baseline / candidate deadline misses |
|---|---:|---:|---:|
| 48 kHz / 32 | 206.500 | 212.209 | 1 / 0 |
| 48 kHz / 64 | 592.750 | 600.625 | 6 / 3 |
| 48 kHz / 128 | 395.333 | 365.416 | 2 / 1 |
| 96 kHz / 32 | 275.250 | 260.250 | 37 / 40 |
| 96 kHz / 64 | 279.083 | 291.291 | 6 / 4 |
| 96 kHz / 128 | 390.833 | 446.917 | 6 / 4 |

Each row has 18 configurations and 4,608 measured callbacks per version.
The displayed p95 values are the largest per-case p95 for that version, not
necessarily the same model/event on each side. Raw observations retain all
outliers: 58 baseline and 52 candidate deadline misses out of 27,648 callbacks
each. Candidate maxima reach 13.59 times a deadline. This run does not isolate
the cause of those spikes or guarantee glitch-free host audio, especially at
96 kHz/32 frames. It excludes JUCE and host overhead.

The benchmark previously hardcoded the baseline model label as `Original`,
even when the baseline adapter was compiled with named-model support. The
retained raw run preserves that reporting error. Build commands and adapter
calls establish the matching-model selection; the companion JSON explicitly
corrects those labels without changing timing samples. The tool now queries
each adapter's support before reporting the selection. A separate 108-case
smoke run verifies the corrected labels; its timings are not used above.

## Remaining physical limits

The first transit leg is lossless and nondispersive apart from the allpass's
frequency-dependent delay approximation. Existing string loss and dispersion
act on subsequent round trips. The paths freeze at the actual pluck, so a
rapid bend during the short contact burst does not update wave travel time.
Extreme bends are checked for finite output, not physical trajectory fidelity.
Natural harmonics and nylon have not been promoted to this contact route.

The retained tail is the existing approximate vertical hand-contact branch.
These fixes preserve pending waves and apply its damping consistently; they
do not establish a fully power-balanced moving fret, hand or contact-force
model. The nonlinear numerical experiments remain separate until a physical
bridge port, forced response and recording gains are demonstrated.
