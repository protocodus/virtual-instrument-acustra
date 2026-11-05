# Acustra goal follow-up — 2026-11-02

> Written on a branch off 1.9.13 that numbered this work 1.9.14 and 1.9.15.
> Those versions reached main on 2026-11-05 as 1.9.20 and 1.9.21, after
> main's own 1.9.14 to 1.9.19; the figures here are from that branch.

This follows the researched 1.9.14 sound candidate (`9ecc7db`) and its
[original validation](fret-upper-refinement-2026-11-02.md). Production DSP,
calibration, parameters, CMake inputs and the audio renderer are unchanged.
The compiled DSP library still has the SHA-256 recorded with the 234-case
audio comparison and native CPU measurements.

## Resolve the two timeouts without weakening the checks

Both original CTest gates now pass serially in Release:

| Gate | Unchanged limit | Completed run |
| --- | ---: | ---: |
| Acustra.BodyShape | 90 s | 45.22 s |
| Acustra.VelocityRealism | 60 s | 20.92 s |

The prior timeouts, baseline controls and longer completion diagnostics are
retained. Those diagnostics were not counted as gate passes; the new results
above come from CTest enforcing the original limits.

VelocityRealism repeatedly prepared the same engine configurations, rebuilding
the piezo table and body configurations. It now returns independent value
copies of pristine prepared snapshots, keyed by exact rate, technique,
capture and setup order. All **1,467 original fixture instances**, all 771
dense float-velocity probes, every original assertion and every signal length
remain. Both original setup orders are preserved. The 12 original reported
descriptor lines match the pre-optimization candidate byte for byte.

An additional 27-key proof compares genuinely fresh setup with a snapshot
copy: finite release fields and stereo sample bytes must match exactly,
including signed-zero bits. It first renders, bends and reconfigures another
copy to expose shared mutable state or snapshot contamination. No reset/RNG
shortcut substitutes for the pristine initial state.

BodyShape keeps all distinct startup cases, including the 144
rate/Q/shape/wood configurations and 36 joint-bank configurations. Only
identical silent live-start states and the 45-note sweep's identical setup
are shared as pristine snapshots. All original renders, signal lengths,
frequency-search points and assertions remain. Added fresh/copy witnesses
compare **144,000 stereo frames byte for byte**.

Its Fourier observer prepares its Hann weights once and advances the complex
oscillator by a recurrence, re-anchored every 256 samples. An independent
direct-trigonometric sum checks offsets, rates, near-Nyquist cancellation and
partial final blocks. Worst L1-normalized complex-coefficient error is
3.32e-13, below the new 1e-10 observer-equivalence bound. Original reported
descriptors match apart from approximately 0.000001 cent of pitch-observer
roundoff; the original pitch tolerance is unchanged.

Independent cross-review of both test changes found no lost cases, weakened
tolerances, setup-order mismatch or mutable sharing. These are test-work
reductions, not a claimed production-DSP speedup. The wall times above are
gate evidence, not a matched before/after CPU benchmark.

## Final validation coverage

All **115 registered native gates have passing final results**: the prior
113 passes have unchanged code/build inputs, and the two modified suites
were rebuilt and rerun above. This is cumulative validation, not a claim
that all 115 were rerun in one new invocation. No test is disabled, skipped,
given a larger timeout, shortened or assigned a looser numeric tolerance.

The same-DSP eight-stage Rack suite and native performance results remain
applicable. The Rack source pin is advanced only after publishing this
canonical revision; its pin/version/source-ownership validator is rerun.
The 1.9.14d1 patch values, version artwork, property IDs and latency contracts
are unchanged. No local45/universal45, installed Reason or portal result is
inferred from native evidence.

The prior 234-case and RMS-matched A/B audio is preserved, since no synthesis
input changed. Source-tree and library hashes establish that reuse; the
original review archive and failed-run records remain intact.

## Upper-register research follow-up

The [bounded audit](../research/upper-register-audit-2026-11-02.md) checks
sampled endpoint moments and release phase rather than adding an unsupported
transient or extra damping. A 66-case neighboring-fret/position audit finds
no cross-term sign discontinuity in the tested Pick path. Arbitrarily forcing
the opposite energy root would break zero-share continuity, so no such phase
change is adopted.

The audit also corrects a reference label: Shinyguitar is an archtop per its
first-party description. The separate Eastman E1D recordings are the
identified dreadnought reference. Historical measurements are not recast as
independent flat-top validation.

The [bounded reference study](../research/upper-register-reference-audit-2026-11-02.md)
adds 67 identified reference recordings and 25 dry model renders. Attack
differences change with layer and corpus; selected high-note early partial
decay is broadly comparable and late model decay is faster. Its changed
estimator, preset and register do not reproduce or universally contradict
the historical global decay estimate. Floor, capture, force and fingering
limits are explicit. It supplies no defensible additional gain or damping
change for this iteration.

The current sound improvements remain the exact soft-contact cutoff and
the audible wound-string shift source. The broader snap/mellow gap is still
an explicit modelling limit. Further attack-envelope work requires an
independently checked initial-state/contact experiment and reference evidence;
this follow-up does not claim that a small spectral correction closes it.

Evidence lives outside Git under `diagnostics/acustra-goal-20261102/`:
`timeout-gates.log`, original test/binary snapshots, fresh/copy equivalence
results, reference provenance, the moment audit and the negative phase audit.
No PR was found for either task branch when checked, and none was merged.

## Rack defaults through the output converter

A separate SDK-free `CAcustra::RenderBatch` host-shim probe retains fresh
Rack defaults at 48 kHz: 2x quality, Room 10%, mixed Piezo 20%, Release Noise
10% and Output 0 dB. All 28 native stereo-float captures are finite and below
full scale; the largest peak is -1.998 dBFS. Public display packets confirm
the five six-string barre positions and B/high-E dyads at frets 12/16/20,
velocities 40/100. Events reach the wrapper as normal note-state diffs.

Explicitly setting both research switches true matches an untouched instance
byte for byte in barre and upper-fret witnesses. The arrival-only ablation
is also byte-identical for all six first-contact upper dyads. The barre
candidate minus legacy-arrival null is -44.232 dB relative to the whole
candidate phrase, with +0.000699 dB phrase RMS change. Upper candidate minus
legacy-pad nulls are -31.362 to -27.219 dB relative to each dyad. These are
controlled differences, not isolated total-noise recordings or listener scores.

The additional 4-second fret and 7.2-second upper lettered pairs preserve
both pre-iteration mechanisms in A and change only the reviewed mechanism in
B, using the offline ablations. They are not recordings from an old installed
binary. B is whole-file stereo-RMS matched to A: -0.000702501 dB for fret,
-0.044169194 dB for upper; mismatch is below 1.3e-9 dB. No common attenuation
is needed. Separate keys exist and no listening preference is asserted.
The full two-change candidate and unscaled controls are retained separately.

Source, object/input hashes, fixed events, raw captures, analysis and pairs
are in `diagnostics/acustra-goal-20261102/rack-default-audio/`. This confirms
the effects through the actual wrapper and output converter at defaults;
it remains host-shim evidence rather than installed Reason playback.
