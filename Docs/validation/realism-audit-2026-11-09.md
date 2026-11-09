# Acustra physical realism and CPU audit — 2026-11-09

Final DSP is **5553984, version 1.9.35**, with acoustic checkpoint **0b835e8**,
compared with shipping
source **8e195a9, version 1.9.34-build.90**. The initially supplied checkout was
older; it was fast-forwarded before implementation and the old measurements
were labelled stale. No release, push, reference-library reproduction or
listening preference is claimed.

Evidence is retained under `/workspace/scratch/acustra-20261109/`. The
`baseline-1.9.34` and `pre-optimization` directories contain independent
source/header/library snapshots and immutable renderers. DSP and Performer
remain framework-free; parameter ranges, saved controls, voice counts,
sample rates, modal counts and RNG chronology are unchanged.

## Accepted model corrections

**Physical pluck coordinates.** The physical hand fraction is transformed by
`contactPeriodSamples / writtenLineLength` before the released triangle is
written. The old second clamp, 0.05–0.60 in that numerical coordinate, moved
valid bridgeward/neckward contacts. The four triangle/reference-grid endpoint
guards now use 0.01–0.95; the physical panel and MPE hand ranges remain
0.05–0.36 and 0.05–0.46. A curvature-centroid oracle independently recovers
the kink from emitted samples: 1,616 resolved cases at 44.1/48/96 kHz, with
maximum position error reduced from 0.1261 to 0.0001042 of string length.
It covers 88 upper-bound and 48 lower-bound failures. The 112 cases whose
endpoint and contact curvature overlap are unsuitable for that estimator;
separate extended-rate renders cover their finite operation.

**Bent pick unloading.** Wave speed `c = sqrt(T/mu)` and characteristic
impedance `Z = sqrt(T*mu)` differ when stretching changes linear density.
The release had reused `Z/Z0` for `c/c0`. It now caches
`c/c0 = (Z/Z0) * stretchedMass`, consistent with `c Z = T` and the two outgoing
string impedances' release velocity `F/(2Z)`. Independent force/impedance
oracles expose up to 0.7226% release-time error in shipping. Unbent arithmetic
keeps its existing fast path; this chiefly changes pre-bent contacts.

**Upper-loss retention.** A former 0.25–4 clamp on `f_open/f` changed the
chosen per-second loss law beyond 24 effective semitones, inside the
20-fret plus 12-semitone bend range. Positive validated frequencies and a
bounded base retention need no extra octave cap. The existing final loss-mix
ceiling still protects extreme downward virtual pitches. Live and retained
waves use the same law. The bounded dispersion fit evaluates its complete
loss tuple at its bounded fit frequency, while actual-frequency delay tuning
uses the actual running shelf. This preserves the original high-slide
solve-count contract instead of repeatedly fitting beyond its useful band.
The existing upper-partial approximation outside that band remains explicit.

**Contact-reference cache dependency.** A re-pluck after a held slide can
establish a new physical stopping reference without changing its current
sounding pitch. The exact configuration key now includes that reference
frequency, which controls dispersion order and polarization tuning. Eighteen
public re-pluck cases match an otherwise identical engine with memoization
invalidated before contact, sample for sample. Shipping differed by up to
0.0727242 per sample. The isolating fixture disables fretting-pitch randomness
and settles the previous attack: ordinary random contact offsets can mask
this missing dependency by changing another key field.

**Invalid fret-crown clip removed.** Clipping an absolute traveling-wave
potential at the bridge is not a contact law: physical string displacement
is a difference of the two traveling potentials and is invariant under a
common constant offset. Shipping could turn a constant, physically motionless
field into an impulse. The clip is removed; its setup flag is a retired no-op
for source compatibility, not a saved/UI control. Fifty-four live/retained
rate/fret/common-reference cases pass. A 108-note factory ablation is
sample-identical with the old clip enabled/disabled, so its removal is not
presented as an audible improvement to those ordinary notes.

The unused `attackPitchDecay` member, its six inert writes and its unused test
snapshot member are removed. Actual attack pitch already follows inferred
slope energy.

## Audible scope and keyboard results

The main three-velocity keyboard and per-string surveys use the frozen
acoustic checkpoint; the remaining-bank supplement uses final DSP. The CPU
transform is qualified separately by exact audio/state comparisons.
The review performance uses the shipping Performer, Jumbo Steel Pick, 48 kHz,
256-frame blocks, Room 0 and identical deterministic MIDI. Before RMS
matching, its ordinary
0–6-second phrase changes by about −102 dB relative RMS. Neckward upper-fret
MPE contacts differ by about −20 dB; upper-fret octave bends by about −11 dB.
These differences establish where the correction is substantial, not a
listener preference. The lettered files and whole-file gain metadata
are in `review/A.wav`, `review/B.wav`, `review/key.md` and `review/levels.json`.
Both matched files have stereo RMS 0.05302517; their four-times-oversampled
peaks are at most 0.79. No per-note normalization, EQ or extra dynamics is
used. A final-source
render is byte-identical to the acoustic B render, so the same matched
lettered files represent final DSP.

| Evaluation | Coverage | Result |
| --- | --- | --- |
| Shipping public MIDI | 0–127, five representative factory presets, velocities 16/64/112: 1,920 keys | 675 pitched, 90 noise gestures, 90 silent keyswitches, 1,065 intentional silent keys; no missing/unexpected sound or non-finite output |
| Remaining actual bank | Parlor/Palm/Piezo, 0–127 at velocity 64: 384 keys | 135 pitched, 18 noise, 18 silent keyswitches, 213 intentional silences; all finite and all 33 flag locations already present in shipping |
| Quiet MIDI edge | Finger/Pick, 0–127, velocities 1/2: 512 keys | 180 pitched notes emit finite sound; peaks −77.595 to −52.803 dBFS; velocity 2 raises sustain by 5.993–12.260 dB |
| Every string and fret | Eight preset-derived configurations, six strings, frets 0–20, velocities 48/112: 2,016 notes | Maximum changes: 0.198 dB attack, 0.111 dB sustain, 0.436 dB peak, 0.00956 s finite fundamental T60, 0.00321 cents settled pitch |
| Extended geometry | Three picking techniques, four shapes, six strings at fret 20, MPE position extremes, 0/+12 member bends, 8–384 kHz: 2,016 cases | Main and initial/final traveling-wave storage finite; maxima 0.784606 and 0.227590 respectively |

The extended-rate check establishes finite operation, not exact pitch at every
extreme: the existing shortest-delay limit still bounds very high notes at
8 kHz. Ordinary keyboard level and decay comparisons use 48 kHz.

Together the shipping-path scans cover all eight factory presets across
MIDI 0–127 at velocity 64, plus five presets at 16/112: 2,304 key cases.
The three remaining presets are not claimed to have the same three-velocity
coverage. Fresh isolated notes supply no CC2 palm gesture, including the
preset named Palm Muted Steel Pick.

Attack, sustain and stereo peak increase strictly with velocity throughout the
five-preset, three-velocity subset, independently checked rather than relying
only on the scorer's 1 dB reversal-warning threshold. All 410 public neighbour
flags (377 main plus 33 supplemental) already exist in shipping. They include
documented measured-body/capture
structure around C5/C#5 and Auditorium C#5/D5, with holes also present in
stereo power. No clear new model inconsistency justified arbitrary level EQ.

The per-string diagnostic sets shape, material, tuning, technique, capture,
Touch and pluck position from the bank; other controls remain at renderer
defaults. It does not restore complete factory patches or supply CC2 gestures.
The separate public MIDI scan uses the actual factory Performer path.

The per-string scan adds one threshold flag at the unchanged open A in the
configuration derived from Palm Muted Steel Pick (without a CC2 mute gesture).
Its peak remains exactly −13.645 dBFS; its first two
fretted neighbours peak 0.290/0.111 dB lower, moving its neighbour deviation
from 2.892 to 3.093 dB across the arbitrary 3 dB threshold. Attack and sustain
remain continuous there. This is not a new loud-note spike.

**Output guard accounted for.** An offline diagnostic bypasses only Main's
compressor/limiter and aligns its 96-sample lookahead at 48 kHz. Across all
810 pitched and 108 noise cases, raw pitched peak is at most −1.67074 dBFS;
154 pitched cases engage more than 0.01 dB gain reduction, with maximum
2.47014 dB. The loudest intentional noise, MIDI 35, peaks at +0.06006 dBFS raw
and −1.5 dBFS guarded, with 1.56006 dB reduction; 11 noise cases engage.
Reconstruction using one common stereo gain leaves less than 3.1e−8 sample
error. The ceiling is not hiding a runaway pitched note in this matrix;
this does not mean the guard is inactive or physical body levels are flat.
See `raw-guard/report.json`, `raw-guard-bank-supplement/report.json` and their
`notes.csv` files. The supplemental source is verified to differ from the
acoustic checkpoint only at the two Main guard calls.

## Measurement fixes and reference limits

The existing stiffness scorer could label a strong seventh partial as the
sixth, giving low-E velocity-48 B about 0.0093 instead of 0.00017758. Search
cells now do not overlap, peaks must be resolved, and an inconsistent six-mode
fit returns unavailable. Synthetic known-mode tests include a seventh 100
times stronger than the sixth, a missing sixth and ambiguous stiff spectra.
All 2,016 baseline diagnostic raw files were rescored without changing their audio.

F2's ±6% fundamental band contained no bin in the legacy 4096-point FFT.
The scorer doubles its window only when a band is empty; existing nonempty
bands keep their arithmetic. A known four-second exponential verifies
E2/F2/F#2/A4. F2 was remeasured from retained audio into separately named
`decay-resolved` reports, with metric-source hashes and the original reports
retained. Independent 8192/16384-window fits also show ordinary decays with
R² at least 0.994. This was a measurement gap, not abnormal sustain.

The supplied seven-gap comparison is useful as a set of diagnostic questions,
but its raw libraries and original scorer were unavailable. Fixed-frequency
HF bands change harmonic membership by register; short-window interharmonic
energy includes leakage and cannot prove missing noise. Three selected takes
do not establish a variance distribution. Fixed absolute bridge distance
correctly weakens the fret dependence of compliance; it is not itself a bug.
The steel library's published autopitch processing also complicates beating
and inharmonicity comparisons. No nylon target, register gain curve, arbitrary
noise floor or suggested unmeasured physical constants were fitted.

| Supplied lead | Disposition in this audit |
| --- | --- |
| Upper-register two-stage HF decay | Corrected the independently demonstrated loss-law cap under extended bends. The unbent reference-library slope claim remains unverified; finite-window decay estimates do not establish its proposed cause. |
| Flat attack crest | Corrected release geometry and bent unloading using waveform and force/impedance oracles. No global sustain shortening was justified by the reported crest alone. |
| Interharmonic transient floor | No noise was added. A noiseless harmonic reference can show a large register-dependent change under a fixed short window; the unavailable original scoring method was not reproduced. |
| Narrow take variation | Existing correlated hand travel remains. Three selected takes do not justify broadening the distribution; this audit does not claim a new population-variance estimate. |
| Pick/Finger velocity brightness | Public amplitude response is strictly monotone in the measured matrix, and force/release oracles pass. That does not by itself settle spectral brightness or prove equivalence to sampler velocity layers. |
| Weak low/mid beating | The missing contact-reference cache dependency is fixed. No extra detuning is fitted to processed reference recordings, and no matched beating comparison is claimed. |
| Pick register level slope | The physical fixed-distance compliance law is retained. Full-keyboard, per-string and pre-guard checks distinguish existing capture/body structure from new level defects. |

A proposed replacement fret contact was rejected before shipping: fixed-port
passivity did not establish moving-contact energy and physical clearance.
The useful derivation and limitations are in
[the contact research note](../research/fret-contact-scattering-2026-11-09.md).

## CPU and validation

CPU work follows the acoustic/source freeze. Initial Callgrind captures of two
seconds of shipping Finger/Pick phrases attribute 51.3%/42.2% of instrumented
instructions to axial stretch quadrature, 17.0%/19.4% to the main process and
16.9%/20.7% to body/bridge modal work. Two exact adjacent-pair load prototypes
preserve phrase sample hashes but increase instructions (about 4–5%, then
0.7–0.9%); both are rejected and archived outside the checkout. Profiling is
not a native wall-time or host CPU claim. Those instruction totals include
the render loop's hash and observation bookkeeping as well as the callback.

The retained optimization separates the exact axial-design key check from the
large redesign body. Rounded inputs, key dependencies, miss-side state order
and coefficient arithmetic are unchanged. In 123 configurations plus 67
alternate callback partitions, 165,634,428 Main/Piezo sample values and
8,728,538 public-state words match exactly. Callgrind counts fall
0.388%/0.344% on the Finger/Pick excerpts. This is a modest optimization,
not a model reduction.

The paired callback benchmark now records calling-thread CPU and steady wall
for the same callback, with snapshot restore/checksum outside. Thread CPU
includes the nested wall-clock reads; that overhead is not subtracted.
Factory phrase timing also includes MIDI dispatch. Each measured phrase uses
a fresh prepared instance and includes its initial attacks; the separate warm
run does not make it a steady-state engine snapshot. Measurements used a
coordinated idle-machine slot and three alternating
paired rounds. The exact axial guard saves about 0.5–0.8% across the two native
protocols; full factory CPU is 0.99968 times shipping overall. Per-case variation
is wider than that small aggregate effect. See the [CPU record](../benchmarks/cpu-optimization-2026-11-09.md)
for raw-clock scope, ratios and the inherited callback deadline limitations.

**Inherited CPU limit remains.** Palm Muted has 113 repeatable callback
positions above the 5.333 ms period at 48 kHz/256 in shipping, acoustic and
final builds. First-second profiling assigns 70.42% of instructions to the
coupled-polarization damped-pole solve during smoothed palm-control changes.
The cost is present with FTZ/DAZ enabled; six on/off diagnostic renders have
identical hashes and no consistent denormal penalty. Removing the loss input
would change the acoustic calibration policy, so no such shortcut is made.
The deliberate 24-event Gather stress and some live transitions also exceed
one callback period. Passing the average CPU budget does not establish
universal real-time safety.

| Validation | Status |
| --- | --- |
| Physical contact, release and pitch contracts | Passed focused contracts, including independent waveform/force/cache oracles |
| Retention across rates/tunings/wheels | 8,424 live/tail cases pass; maximum mix error 1.645e−7; normal retention-rate error 2.364e−6 |
| Bounded phase-design cache | 48 complete-tuple uncached comparisons reproduce coefficients exactly; original high-slide solve bound passes |
| Measurement oracles | Mode identity, missing/ambiguous partials and empty-band decay checks pass |
| Complete Release build | Passed |
| Engine contract | Acoustic freeze passes in 218.495 s; final DSP passes in 235.94 s, both under the unchanged 600 s limit |
| Performance gates | Both pass unchanged on final DSP; factory median factors 0.09265–0.18661 at 48 kHz/256, below the 0.28 average budget |
| Complete unit/audio gates | Final 122/122 unit tests pass in 1482.87 s; 3/3 audio tests pass in 13.12 s |
| Focused ASan/UBSan gates | All 9 affected suites pass in 369.86 s, including deterministic fuzz replay and the cross-translation-unit axial design call |
| Final native CPU and exact optimization comparison | Three factory/callback rounds complete; 123 configurations plus 67 alternate partitions match 165,634,428 sample values and 8,728,538 state words exactly |
| Strict C++17 DSP portability | Final Clang 19 x86-64/AArch64 compilation passes with `-Werror -fno-exceptions -fno-builtin -ffp-contract=off`; no actual ARM execution claimed |
| Native wrapper-host contracts | Final Clang 19 authored native host-mock suite and separate StringAxialForce cross-translation-unit link/run pass; no Reason SDK integration claimed |

An earlier Engine run hit its unchanged 600-second wall limit during concurrent
team workloads, without a logged assertion failure. The exclusive rerun passed in
218.495 s; no timeout or signal threshold has been relaxed. One quiet-key
survey was interrupted by a concurrent executable rebuild; subsequent runs
use immutable binaries and the failed partial receipt is retained.

Integration receipts are under
`/workspace/scratch/instrument-realism-20261109/portability-runs/final-three-clang19/`
and `wrapper-runs/final-acustra-clang19/`. They freeze and hash the final source;
portable compilation and native host-mock execution are distinct validations.
