# Acustra realism audit — 2026-11-09 (work in progress)

This work starts from shipping `8e195a9` (1.9.34-build.90). The environment
initially exposed 1.9.28 (`25c3d64`); those preliminary binaries and partial
results are explicitly retained as stale and are not the comparison baseline.
Frozen source, renderers and raw evidence are outside the checkout under
`/workspace/scratch/acustra-20261109/`.

## Scope and evidence standard

Focus: physical attack/contact, sustain and velocity response, overall model
consistency, full MIDI-key classification and outliers, then CPU profiling.
The supplied external-library comparison is a set of diagnostic hypotheses;
its source recordings and scoring script are not available here. This audit
does not reproduce those measurements or claim a listening preference.
Nylon reference timbre is not a target for this steel-string instrument.

## Confirmed findings

1. The plectrum unloading calculation used the bent characteristic-impedance
   ratio as the wave-speed ratio. With stretched mass per length
   `mu = mu0/(1+strain)`, `Z/Z0 = sqrt((T/T0)/(1+strain))`, while
   `c/c0 = sqrt((T/T0)*(1+strain))`. The candidate caches the distinct speed
   ratio. An independent force/impedance oracle fails the shipping binary
   with a maximum 0.722636% release-time discrepancy and passes the correction.
   Unbent arithmetic is retained. Existing expected-value tests that repeated
   the same incorrect substitution are corrected using `c=T/Z`.

2. Shipping fret-crown grazing clips one outgoing travelling displacement
   potential at the bridge. A constant potential is physically silent because
   local string displacement is the difference of the two folded travelling
   waves. At a common potential of -0.5 the shipping clip writes a +0.378 kink
   despite zero physical displacement. The reaction therefore depends on an
   arbitrary potential reference and is not located at a fret.

   A first replacement experiment projects the local next-fret displacement
   onto a unilateral soft bound using coalesced fractional gather/scatter
   weights. It passes gauge, locality and fixed-port increment-contraction
   checks. That proof alone does **not** establish the energy behaviour of a
   connected waveguide whose overlapping historical cells are rewritten.
   Independent review is investigating the connected ring; this prototype is
   not approved as a physical-energy-conserving replacement.

## Validation in progress

- Pristine 1.9.34 per-string/fret baseline completed: all eight factory
  constructions, six strings, frets 0–20, MIDI velocities 48/112, 48 kHz,
  2-second renders with 1.2-second holds (2,016 notes).
- Full public-Performer MIDI 0–127 scanner added. It distinguishes fretted
  notes, six intentional gesture noises, silent keyswitches and out-of-range
  silence; default matrix uses Finger/Thumb/Pick plus DADGAD/Open G at
  velocities 16/64/112 (1,920 independently reset notes).
- The fretboard tool's numerous diagnostic flags require investigation;
  finite-window decay fits, stereo cancellation, partial leakage and physical
  construction changes are not themselves evidence for flattening the model.

## Limitations to retain in the final receipt

Signal analysis cannot establish listener preference or market-leading
realism. The external sample-library corpus is absent. CPU measurements will
be native DSP measurements, not Reason/DAW CPU claims. The final paired WAVs
must use one identical shipping performance path and whole-file RMS matching,
with raw gain information and an identification key kept separately.

## Subsequent evidence and decisions

The bridge-potential clip is removed rather than replaced by an unvalidated
moving contact. Its setup flag remains an explicitly retired no-op for source
compatibility; it is not a saved/UI parameter. The new regression exercises
54 rate/fret/common-reference cases, including both live and retained planes.
A shipping-library ablation of 108 individual factory notes (Finger, Thumb,
Pick; nine pitches; velocities 16/64/112/127) is sample-identical with the clip
enabled/disabled. This removal is therefore a correctness/cleanup result,
not an asserted audible improvement for those ordinary notes.

A further confirmed excitation-coordinate issue affects the actual release:
physical hand position was transformed into the shorter delay-line frame,
then clipped again to 0.05–0.60. MPE neckward contacts on stiff upper frets can
need a numerical position above 0.60; the authored normal-plane offset can
put bridgeward contacts below 0.05. Those numerical limits moved the released
kink away from the physical hand and also changed the released saddle force.
Only numerical triangle endpoint guards become 0.01–0.95, in the main shape
and Pick reference-grid paths. The physical hand maps are unchanged.

An independent curvature-centroid oracle recovers the kink from the emitted
waveform, without reading the stored release-position diagnostic. Across
1,616 resolved construction/string/fret/MPE/bend cases at 44.1/48/96 kHz,
shipping error reaches 0.1261 of the string; candidate error is at most
0.0001042. This covers 88 upper-bound and 48 lower-bound cases. A further 112
very short, bridgeward cases have overlapping endpoint/contact curvature and
are explicitly outside this separated-centroid estimator; audio/rate checks
cover their finite output and continuity instead.

The existing fretboard stiffness scorer also had a verified mode-identity
bug: its sixth-partial search window included the seventh harmonic. A stronger
seventh could be labelled H6, giving low-E velocity-48 B about 0.0093 instead
of the corrected 0.00017758. Search cells now do not overlap, peaks must be
resolved, and a poor six-mode stiff-string fit returns unavailable. Synthetic
known-mode tests cover a seventh 100 times stronger than the sixth, a missing
sixth, and a spectrum too stretched to label safely. All 2,016 baseline raw
renders were rescored to a separate corrected report; the original report is
retained as a measurement-provenance record, not DSP evidence.

The full public MIDI baseline completed 1,920 keys: 675 pitched, 90 intentional
noise gestures, 90 silent keyswitches and 1,065 intentional out-of-range silent
keys. No non-finite output, missing expected sound, unexpected silent-key
output, or lower sustained level at the next tested velocity was found. The
loudest pitched sample is -2.03035 dBFS. Three MIDI-35 gesture noises reach the
-1.5 dBFS output ceiling; raw/guarded comparisons are still required before
asserting even internal levels. The 377 level-neighbour flags remain leads
for body/string/capture attribution, not automatic defects.

The upper-loss retention helper contained another numerical-domain mistake:
`f_open/f` was clipped to 0.25–4, so crossing 24 effective semitones changed
the chosen per-second loss law, even within the supported 20-fret plus
12-semitone bend range. No replacement octave limit is needed: validated
frequencies are positive/finite and the base retention is in [0.05,1], so its
positive power is bounded. The existing final loss-mix ceiling remains for
extreme downward virtual pitches. Ordinary ratios inside the former range
retain the same arithmetic, including the exact open-string fast path.

The independent retention test compares configured live and retained loss
parameters against a double-precision logarithmic reference. Its 8,424 cases
cover 8/22.05/44.1/48/96/384 kHz, master tune +/-100 cents, four tunings, all
six strings, open/20th fret, global/member wheel corners through +/-96
semitones, and a continuous sweep through the old two-octave boundary.
Shipping fails 3,665 cases. The candidate passes; maximum loss-mix error is
1.645e-7 and normal-range per-second retention-rate error is 2.364e-6.
This verifies the shelf's retention parameter, not an exact decay fit of the
whole frequency-dependent, coupled guitar. It chiefly changes extended bends.

Cleanup removes the unused `attackPitchDecay` member, its six inert writes
and the unused test-snapshot member. Actual attack pitch already follows the
inferred slope-energy envelope. The retired crown flag is intentionally kept
for setup-source compatibility; neither change creates an audio-thread cost.

Stage-one validation passed all 119 unit tests other than the large Engine
contract, which was separately deferred after the shared-machine wall-time
limit. That batch took 1,098.7 seconds under concurrent team load. No signal
thresholds were changed. Final gates and CPU evidence remain pending.

The retention correction initially failed the existing high-slide solve-count
contract: the bounded dispersion fit still accepted the actual-pitch shelf,
which now changed beyond the old cap. The complete fit tuple now uses the
shelf at its bounded fit frequency, as its bending and constant-loss sections
already do. The actual running shelf remains in the actual-frequency delay
phase calculation. The original solve-count gate passes again; 48 uncached
recomputations reproduce all fitted coefficients exactly and independently
check the fit-frequency shelf. The bounded fit remains an approximation
outside the documented physical pitch band, not a claim of exact high-partial
tuning there.

Independent dependency review found one additional incomplete configuration
cache key: the contact tuning frequency was absent, although dispersion order
and polarization tuning use it. A re-pluck after a held slide can establish a
new contact reference without moving the current sounding pitch. The key now
includes that exact frequency. Eighteen public note/controller re-pluck cases
compare memoized execution with an otherwise identical engine whose cache is
invalidated before contact. The pre-fix maximum sample disagreement was
0.0727242; the correction is sample-identical to the fresh configuration.
The isolating fixture disables fretting-pitch randomness and settles the prior
attack: ordinary random contact offsets can otherwise invalidate the cache
through another field and mask this missing dependency. This is a correctness
fix; broad default-phrase audibility is not inferred from that witness.

The current targeted batch passes StringPitchRealism, RetuneContinuity,
BentRetention, PluckFrameGeometry and ContactReferenceCache with unchanged
existing tolerances. A second full public-keyboard survey before the contact
cache fix again produced exactly 377 diagnostic neighbour flags and no new
support/finite/velocity problems. A concurrent renderer rebuild interrupted
the velocity-1/2 supplement (executable was temporarily unavailable); that
partial run is retained, and subsequent surveys use a frozen executable.
