# Auditorium A2 and Classical 78 resonance investigation

The close and mono microphone contours reinforced two unwanted tonal balances:
Original Auditorium's low A fundamental and Classical 78's upper harmonics.
This refinement reduces those emphases in the summed microphone pressure.
The user auditioned the first pass and confirmed less boom, while reporting
that the Classical 78 banjo impression remained. The user also rejected the second EQ pass as still banjo-like. Its
measurements remain below as evidence; investigation has moved to the
coupled response rather than accepting a spectral-ratio change as a fix.
The 24 constructions retain their measured body modes, bridge and steel
strings. The accepted Original boom refinement is an authored capture
change. The user subsequently described the focused physical pluck-direction
comparison as "Less banjo-like" and identified the remaining cue as "Sharp or
tinny attack". The validated fixed-string direction change is now in the
working source. The subsequent half-share Pick audition was rated "Natural
guitar attack"; the user asked to retain the full-slip Thumb sound for now.
Those release choices are also integrated and validated below. That feedback
establishes improvement on the auditioned upper notes, rather than complete
resolution across constructions. The user then selected gentle sustain warming
for Pick and Thumb to address "Tone stays too static as it fades". The applied
profile and final all-construction audio are verified below.

## Cause and correction

Auditorium A2 follows the played 110 Hz string force. Its sustained component
has a fitted T60 of about 2.52 seconds, matching the force's 2.51 seconds;
the nearby 95.72 Hz body mode has a free T60 of about 0.54 seconds. Capture
bypass renders retain identical force trajectories. The previous contour
adds 5.72 dB at 110 Hz but attenuates 220 Hz by 1.21 dB, increasing fundamental
dominance on an already bass-heavy soundhole microphone. The left microphone's
separate air-mode gain cannot directly cause the same mono imbalance.

Original now removes the 125 Hz +2.75 dB peak and lowers the 120 Hz shelf by
3 dB. A separate +1.2172 dB scalar restores the existing default phrase level.
Classical 78 now halves its 500 Hz cut from -6 to -3 dB and removes its
1.4 kHz presence boost. The first presence-only reduction to +3 dB still
left a 5.21 dB cut at A4's fundamental and 5.69 dB at B4's. The broad cut
deepened existing dips in the measured complex microphone response, rather
than correcting an erroneous mode phase or index. The original -6/+6
contour gave A4's third harmonic about 10 dB more gain than its fundamental.

Native force traces also show a physical contribution on Pick A4: Classical
78's moment-force fundamental decays about 13.1 dB/s while its third harmonic
decays about 6.1 dB/s. The Original control reverses that balance, about
7.1 and 12.6 dB/s. Halving Classical bridge mobility improves that contrast,
but changes its deliberately selected measured bridge, including the pickup.
It remains a listening experiment; the source retains its physical response.
Direct/contact clicks are zero in the shipped calibration. Removing the
shared attack burst barely changes sustained harmonic balance; removing
model convergence makes the microphone brighter.

Component-separated renders identify a slow parallel-string tail that the
Classical microphones reveal more strongly. On Auditorium/Spruce Thumb A4,
moment pressure supplies 99.7% of H10 power at 1.4-1.6 seconds. The normal
force decays about 37.8 dB/s while the moment force decays about 11.1 dB/s.
Original also has a slow moment tail. Classical's stronger moment projection
agrees with the qualified raw microphone measurements; no polarity, ordering
or moment-unit defect was found. A broadband rocking-loss addition spreads
the slow tail into the normal response and is rejected as a correction.

A normal-direction pluck experiment restores each fresh stroke's total
slope energy before merging a repluck into the preceding waves. It changes
the authored release direction, including the pickup response, while keeping
the measured body data. It is not a confirmed explanation of the listener's
banjo impression.

The global +0.30 normal-share experiment created an uneven Jumbo D#3 attack.
A pitch-limited A3-E4 version retained the bass response but failed two
alternate-tuning level guards after phrase calibration. An earlier E3-A3
transition can satisfy both existing limits with constrained calibration,
but its full D/G sweep exposed a D#4 D-string transient amplification of
6.84 dB in local prominence. Component attribution shows cancellation
relief during the attack, not a new ringing pole or longer sustained decay.
Those versions remain diagnostic and are not retained in the source.

The current source assigns fixed normal-share offsets to the
physical strings: 0/0/0/0.1944/0.30/0.30 from low E to high E. Fret or tuning
changes do not change the authored direction of the same string. Its 5,760
paired diagnostic outputs cover 12 Classical constructions, three techniques,
both microphones and the simultaneous pickup. D-string output is exact on
all 1,368 isolated controls; 576 B/high-E controls match the full-angle
reference. All outputs are finite and unclipped. It eliminates the new
D#4 D-string spike, but does not improve that string's tail. The G-string
upper-tail ratios also show little improvement, and switching from D to G
around G3 can add up to 1.94 dB of note prominence. High-E upper-tail ratios
improve by roughly 2.5-6 dB. This is a partial treble candidate with material
fingering caveats, not a complete banjo correction.

Exact phrase fitting still leaves the Jumbo/Maple/Finger mono DADGAD chord
9.66 dB below its default reference. A measured intersection of the existing
1 LU phrase tolerance and five-tuning 9 dB strum guards is feasible for every
cell. The nearest feasible fit needs a +0.678 LU phrase offset on that one
capture, with a 0.02 dB rounding margin. The rebuilt prototype passes all 360 native strums and all 216 phrase/capture
cells. The actual adjusted mono phrase is +0.710 LU above target; its
0.033 LU difference from the scalar prediction comes from loudness gating.
No checker limit is widened. All 108 Original float-table entries, 180
Original strum rows and every default reference remain exact. The isolated
review project passes all 74 suites; two documentation checks initially
lacked copied repository files and pass after those files were supplied.

The first canonical physical checkpoint included the fixed-string pluck direction, its
constrained calibration and energy tests. Its rebuilt PerformanceRenderer is
byte-identical to the reviewed renderer, SHA-256
`bc4d00109ac6ff541b729a11076b4fdecfdbf64a68b6a6c926d65e007a9eeb9d`.
Eight affected canonical suites pass, including capture, construction/tuning,
calibration and pluck-energy checks; the byte-identical isolated review build
has the full 74-suite validation described above. The user's earlier
"Still banjo-like" verdict referred to the second-EQ comparisons. The later
"Less banjo-like" verdict referred to the focused physical normal-share
comparisons, with the remaining cue identified as sharp or tinny attack.

An attack audit now distinguishes the periodic release from contact noise.
Across the long-held review corpus, the remaining bright attack is strongest
for Pick on high E. Removing the shared coloured travelling burst preserves
the initial periodic waves and changes Pick A4/E5 upper attack little;
removing the low-frequency release step also changes 2-6 kHz power by about
0.01 dB. In separate matched-fresh-energy boundary controls, removing the
authored Pick release-velocity share reduces high-E A4/E5 attack treble/lower
contrast by 5.19/5.27 dB and 6-12 kHz power by 7.88/8.30 dB. Retaining Thumb's
full 0.2 mm authored slip instead of cancelling its nominal slip reduces
the corresponding contrast by 2.27/2.35 dB. These controls identify release
choices that can explain the attack cue; they are not an adopted tuning or
a listener verdict. Compact evidence is in `classic78-attack-audit-2026-10-09.json`
and `classical78-native-burst-pressure-attribution-2026-10-09.json`.

An independent fresh-note screen covers 12 constructions, both microphones,
seven string/note anchors and Original/Finger controls. All 1,008 Before
native eight-channel renders match the independently compiled canonical
engine exactly; all 672 Original/Finger after controls remain exact. The
336 Classic Pick/Thumb pairs are finite, with maximum native microphone peak
0.3674 and maximum matched fresh-energy error 1.30e-7. Pick's 5-80 ms
treble/lower contrast decreases in every pair. High-E A4 medians decrease
3.94/4.13 dB for close/mono, and E5 decreases 4.79/4.94 dB. Thumb's high-E
and D/G anchors improve, while low-E E2/E3 can become relatively brighter.
Pick's held 0.2-1.2 second RMS can rise 2.39 dB. These are modal-population
tradeoffs, with measured body poles and losses retained. The full screen is
recorded in `classical78-periodic-release-attack-screen-2026-10-09.json`.

The focused audition is `build-resonance/evidence/attack-review.html`, with
Pick and Thumb separated, both microphones, upper notes and bass/D controls.
Each note begins with a fresh engine, holds 2.4 seconds and releases for
0.6 seconds. Before/experiment initial periodic-wave energy is independently
matched, and every clip shares the previous review's +7.492857 dB playback
gain. Combined upper clips play 12 seconds Before, one silent second, then
12 seconds experiment. All 48 native files and 72 decoded PCM16 files pass
frame/hash/segment checks; all 36 combined comparisons have exact segments
and silent gaps. The page loads 12/12/25 second default players without
media or console errors. The user rejected the Pick zero-share experiment
as "Too dull" and described Thumb full slip as "Less banjo-like, still
unresolved". Neither attack experiment was adopted at that checkpoint. Pick refinement was
bracketed between the current sharp release and the rejected zero endpoint;
one half-share experiment was prepared. Thumb's residual attack was
isolated from its shared contact burst. Post-NoteOn fresh-energy
matching is a causal fairness control, not an adopted production repluck
algorithm.

The half-share Pick audition is now available in
`build-resonance/evidence/attack-midpoint-review.html`. Its 12 new native
clips retain the positive Pick-release branch and match initial wave energy
within 9.8547e-8. Twelve independently rerendered Before controls match the
previous native files exactly. The page reuses the exact Before WAVs and
shared +7.492857 dB playback gain; 36 new PCM/combined files pass segment,
frame, decode and hash checks. Default 12/12/25 second media load without
errors. The user then rated the half-share Pick experiment "Natural guitar
attack" and said the revised Thumb "sounds better, i would keep it for now
as is". Those release settings are accepted for integration: Classic Pick
retains half the calibrated velocity share and Classic Thumb retains its
full authored slip. Further Thumb attack changes are stopped. The canonical
source now includes both choices, the existing direction profile and a
recalibrated construction table. Original and Finger release arithmetic stays
unchanged. Production removes the kinetic work belonging to the rejected
release component; the diagnostic's matched-energy normalization is not
carried into production. Direction restoration preserves fresh energy under
the new release law before the unchanged retained-wave merge bound.

At the accepted-attack checkpoint, all 75 suites pass in the isolated build, including independent
Pick source, Fourier Thumb, queued-model and full-velocity checks. Incorrect
Pick scale, restored Thumb inverse and requested-model ownership fixtures
fail 27, 153 and 99 intended assertions. The canonical rebuild matches all
200 reviewed source files and reproduces PerformanceRenderer SHA-256
`64ce04e1a84267ccc4acf5820fd0394a40dc7c9379fe33fa969b4bc77c016d59`
exactly; nine affected canonical suites pass again. All 216 phrase levels
and the existing Pick level policy pass. The 360 native Finger strum rows,
108 Original table entries and 36 Classic Finger entries remain exact.

The new `build-resonance/evidence/final-attack-review.html` uses ordinary
production renders, without diagnostic energy matching. Its four Before,
After and combined comparisons retain the same +7.492857 dB playback gain.
Sixteen independently rendered upper-note outputs differ from the accepted
heard experiments by at most 0.302 dB in early RMS and 0.119 dB in early
treble/lower contrast. This verifies the integrated sound against the selected
experiments; it does not establish a new listening verdict. Compact integration
and calibration records are `classic78-release-policy-integration-2026-10-09.json`
and `classic78-accepted-release-calibration-2026-10-09.json`.

The preserved `build-resonance/evidence/final-construction-review.html` supplies
actual accepted-attack checkpoint audio across both models, four shapes, three woods,
three techniques and both affected microphones: 144 cells and 288 retained
native/PCM16 phrases. Each cell has twelve-second bass and upper phrases.
The bass phrase brackets open-A A2 with G#2/A#2 and adds A2 on low E; the
upper phrase plays G4/A4/B4/E5 on high E. Every phrase starts a fresh engine
and uses ordinary performance scheduling at velocity 96, dry Room and the
same Touch/Pluck Position. All clips use the existing +7.492857 dB gain;
the largest playback peak is 0.6335. Independent checks verify every native
and PCM file, all 1,152 note segments, event scores, construction controls
and that checkpoint's renderer/source freeze. All 24 Classic Finger upper
phrases match the preceding physical-direction checkpoint's frozen native
hashes exactly. Default and alternate construction/comparison routes load
twelve-second media with no browser errors. This filled that checkpoint's
listening-coverage gap; it does not establish acceptance of every sound or
every note, fingering, rate and control. See
`classic78-final-construction-review-2026-10-09.json` for the protocol and hashes.

The newly authorized sustain exploration retains this accepted checkpoint as
its reference. Sixty public-calibration probes increase both string planes'
bending loss by 1.3 or 1.6, including twelve Original transfer controls. All
twenty baseline controls match accepted native output exactly. This barely
changes high-E color: the 1.6 multiplier adds only about 0.06-0.12 dB to A4's
early-to-middle H5-H12/H1-H4 decline and 0.24-0.46 dB to E5's, while wound
G4 and low-E E3 change much more. The plain string's bending loss is too small
in the persistent lower partials for this modest multiplier to be an effective
treble sustain adjustment. These results do not establish a listening verdict.

A separate authored loss-shape experiment lowers the existing broad string-loss
corner to 0.75 or 0.50 of its current value, consistently in the runtime shelf
and dispersion phase fit. It changes exactly two expressions off-tree, keeping
the measured body/bridge, bending loss and selected release policies. Twenty-eight
native cases cover Classic Pick/Thumb A4/E5 on both microphones and a small
Original control. Ten same-toolchain baseline outputs match accepted output
exactly; twelve close/mono physical-trajectory pairs are byte-identical.

The gentler broad option adds 2.70-4.40 dB to early-to-middle upper-harmonic
decline, with 5-80 ms attack contrast changes of -0.095 to -0.167 dB. The
stronger option adds 7.96-15.05 dB, but attack contrast changes range from
-1.07 to +1.01 dB because its phase/source geometry also moves. Its A4
integer-period transition removes about 1.09% of fresh source work and lowers
cached attack hardening by 0.082 cents. Middle native RMS changes by
-0.08..-0.56 dB for the gentle option and -0.18..-1.39 dB for the stronger one.
The Original stronger-option control loses 1.97-3.02 dB of middle RMS, so these
focused observations cannot justify a global steel calibration change.

The player `build-resonance/evidence/sustain-broad-review.html` exposes current,
gentle and stronger outputs individually, in pairs, and in 29.6-second upper-note
comparisons. Every version shares +7.492857 dB gain; decoded PCM and silent
gaps are exact, and browser media/console checks pass. Native phase/attack
changes are disclosed rather than called byte-exact preservation. The first
loss study remains separately available in `sustain-review.html`. Both studies
were kept outside the canonical source during audition. The gentle option was
subsequently adopted for Classic; the historical study outputs stay immutable.

Time-resolved scoring uses stretched harmonic centers, low-partial ratios,
separate stereo-channel powers and floor qualifications. Independent equal-fade,
differential-decay, stretched-H12, silence and noise controls pass. An audit of
56 admitted local recordings finds predominantly mellowing archtop takes but
mixed Eastman Finger evolution; surviving-partial/floor limits prevent treating
all later ratios as whole-spectrum targets. Neither metrics nor those differently
recorded instruments can choose the sustain option. The user selected the
gentle option for both Pick and Thumb. The Classic-specific production profile
is now applied, including Finger and sympathetic strings so a held string's
intrinsic loss does not follow changes of UI technique. Original retains its
previous profile. These historical study protocols, hashes and tradeoffs remain
in `classic78-sustain-exploration-2026-10-09.json`.

The adoption matches runtime and phase-fit broad-loss corners and keys both
configuration and completed dispersion design by the effective profile. The
active configured model owns the loss while a model switch is queued. Existing
frequency, inharmonicity and age tolerances are retained. All 76 suites pass in
the frozen isolated build; canonical rebuilding reproduces renderer SHA-256
`4b3948ca2c10b9a40ca6cf36aef005ee77ce156a16b9de1fac1ec9e300157248`
and the strum helper exactly, with 221 reviewed non-Docs files exact and twelve
affected canonical suites passing. The new inverse-cutoff/cache/reference suite
rejects four deliberately faulty implementations. A separate configured-transfer
audit covers 7,776 static states per version without finite, bound or pole
failures; it is not an empirical coupled-pitch or realism measurement.

All 216 phrase levels and 360 native Finger strums pass their existing limits.
The gain fit changes 108 Classic output entries and preserves every Original
entry, phrase/stress measurement and native strum row. No guard is widened.
Twelve final Classic native captures retain exact physical columns 3-7, initial
waves and state versus the selected or prefit gentle profile. Column 2 is the
calibrated Piezo output bus, so it and microphone outputs are verified against
their table scalars; their maximum relative residual is 9.925e-8. For the eight
selected Pick/Thumb notes, calibrated RMS changes only -0.00165..+0.07439 dB and
upper-partial ratio drift stays below 1.53e-6 dB. Finger's gentle transfer is
included separately without an inferred ear selection.

The final `build-resonance/evidence/final-sustain-review.html` supplies applied
9.2-second Pick/Thumb upper-note pairs and optional immutable Before/Selected
comparisons at the same gain. The new
`build-resonance/evidence/final-sustain-construction-review.html` supplies all
144 construction/technique/microphone cells and 288 retained native/PCM phrases.
All 144 Original phrases are byte-exact to the previous accepted-attack pack;
the largest playback peak is 0.6350. Every file, all 1,152 note segments, score,
source freeze and default/alternate/comparison browser routes pass checks. No
all-construction subjective approval is inferred. Current adoption and review
records are `classic78-sustain-integration-2026-10-09.json` and
`classic78-final-sustain-construction-review-2026-10-09.json`.


Residual Thumb controls preserve periodic-wave hashes, initial energy,
release step and attack cache. Removing the shared burst barely changes
net A4 treble contrast, but this does not establish a small causal or
perceptual contribution: in Auditorium/Mahogany A4, the differential signal
has 95%/108% of close/mono baseline 6-12 kHz band power, with destructive
cross terms cancelling 71%/86%. These are identities between complete
native renders including feedback, rather than independent additive
pressure components. E5 shows a modest net reduction; low-E changes are
much larger. Applying Finger's existing actual-slip noise-corner bound
leaves all upper cases byte-exact because the current colour corner is
already slower. A separate noise-only audition and stroke-repeatability
check can distinguish texture from a scalar band-level change. No blanket
burst cut is adopted from these descriptors.

`MicrophoneBalanceData.h` stores these decisions separately from the generated
recording fit. The fitter accounts for the authored contour when interpreting
new native renders, preserving it during future base-contour regeneration.
Its synthetic refined-corpus recovery has 0.19 dB worst error; the old
calculation has 1.88 dB against the same 0.8 dB guard.

## Native comparison

Both models, four shapes, three woods, three techniques and three captures
receive matched renders: 216 cases and 3,960 clips per version, plus 216 E5
pairs for the before/first-pass comparison. The second pass repeats the same
protocol, giving 12,528 native clips across three frozen engines. Every
isolated note starts with a fresh engine. A2 on the open A string
and low E fret 5 distinguishes fingering from construction; same-string
neighbors also bracket A3 and A4. Default Touch, Pluck Position, velocity 96
and dry Room are fixed. The PCM16 audition uses one shared +3.406 dB gain;
native floats and frozen binary, score and audio hashes remain available.

A separate string-release review adds 648 native 12-second phrases: two frozen
engines, 12 Classical constructions, three techniques, three captures and
three fingering groups. The groups cover high E, wound D/G at G4/A4, and the
D-to-G fingering transition around G3. Notes hold 2.4 seconds before release.
All clips share +7.493 dB PCM16 playback gain; 324 combined comparisons play
12 seconds of current EQ, one second of silence, then 12 seconds of the
physical candidate. The previous player and its audio remain unchanged.
The new page is `build-resonance/evidence/string-review.html`; its manifest
records scores, controls, every audio hash and the frozen candidate renderer
`bc4d0010…`. At 1.4-1.6 seconds in the long-held default Auditorium/Mahogany
Thumb phrase, high-E G4/A4/B4 H8-H12 relative to H1 fall 3.96/5.20/6.79 dB;
E5 changes little in that ratio. Wound G changes are smaller and include
construction-dependent increases. This longer phrase is a separate protocol
from the shorter isolated fingering sweep. Compact hashes, checks and signal
descriptors are in `classic78-string-review-2026-10-09.json`. Direct audition
remains unavailable to the agent; the listener's verdict is required to
assess the remaining banjo character.

Across Auditorium woods and techniques, A2 fundamental power decreases
2.45-3.78 dB on the microphones, and its fundamental/upper-partial ratio
decreases 3.22-4.04 dB. Total sustained A2 level decreases 2.40-3.20 dB.
Its local rise over adjacent notes decreases only modestly: the physical
instrument still colors neighboring notes differently.

The second-pass native balance suite reduces A4 H3/H1 by 7.71-7.73 dB and
E5 H2/H1 by 6.25-6.26 dB relative to the original contour across its rates,
woods and techniques. Its guard excludes the rejected first pass's 2-3 dB
reduction. A separate 1,080-render capture ablation covers all 12 Classical
constructions, three techniques and both microphones. Relative to the first
pass, A4's early fundamental/upper-partial ratio improves 2.79-4.52 dB and
B4's improves 3.33-4.68 dB.

The tradeoff is stronger G4 fundamental dominance: the worst late share
increases from 93.96% to 97.15%. A narrower mode-centered notch retains G4
control but barely improves Auditorium/Mahogany A4, so it is also retained
as experimental evidence. The player exposes the notes and constructions
for comparison. These measurements do not establish that the remaining
banjo impression disappears. Direct audio audition is unavailable in the
execution runtime; the user's listening feedback determines that outcome.

The full 216-cell second-pass calibration passes: stereo -0.90..+0.00 LU, mono
-0.90..+0.35 LU and piezo -0.90..+0.31 LU relative to the -24.0513 LUFS
default phrase. Absolute pickup trims are retained within float rounding.
Among 1,152 paired piezo clips, 608 are byte-identical; maximum sample
difference is 8.94e-8 and the relative difference energy is -143.05 dB.
Those figures compare the initial refinement with the original baseline.
The second-pass tables retain every Original first-pass entry exactly and
preserve absolute pickup gains within 8.28e-8 relative to the original baseline.
All 2,088 Original first/second-pass clips are byte-identical. Among the
1,152 first/second-pass piezo pairs, 928 are identical; the maximum sample
difference is 5.96e-8 (four float ulps) and difference energy is -148.75 dB.
All audition clips are finite, non-silent and unclipped. Maximum-velocity
Pick stress can still reach the safety limiter: eight stereo, one mono and
24 piezo cells cross its knee before limiting, while meeting the level policy.

## Reproduction and validation

Use a Python 3 runtime with NumPy and SciPy, then configure a Release,
JUCE-free build with `ACUSTRA_BUILD_PLUGIN=OFF`,
`ACUSTRA_BUILD_UNIVERSAL=OFF` and `ACUSTRA_REQUIRE_PYTHON_TESTS=ON`.
Build all targets and run `ctest --test-dir build-resonance --output-on-failure`.
The second-EQ build passed all 73 suites on AppleClang 21 arm64 and matched
its frozen renderer (`9765c34a…`). The subsequently adopted fixed-string
profile adds the registered `AcustraPluckPolarisationTests` suite. Its reviewed
build passes all 74 suites; canonical rebuilding reproduces that renderer
exactly (`bc4d0010…`) and the eight affected suites pass again. Default and
extended strict C++17 compilation also pass. The current independent energy
suite measures at most 3.39426e-7 relative fresh-energy error and preserves
the existing repluck work bound. Its no-restoration negative control fails
129 energy assertions: 108 fresh, 18 refret and three repluck cases. Earlier
pitch-dependent candidates and their tests remain archived experiments.
The accepted release integration adds `Acustra.ReleasePolicy` for 75 suites
in total at the accepted-attack checkpoint; its canonical/source-parity
verification is described above. The adopted gentle sustain adds
`Acustra.SustainPolicy` for the current 76-suite build.
The new native balance suite covers 81 pairs at 44.1, 48 and 96 kHz and
preserves exact bridge force/motion and dedicated pickup trajectories.
Both a restored old-filter fixture and the actual pre-refinement source
fail its 168 intended balance assertions.
An isolated build with only the rejected Classical first-pass filter
restored fails 111 assertions, including all 54 Classical native direction
guards; its Original, physical and pickup guards still pass.

`Tools/CompareConstructionResonance.py --renderer baseline=BEFORE
--renderer candidate=FIRST --renderer second_pass=SECOND --output build-resonance/evidence/construction
--jobs 4 --no-wav` reproduces the core matrix. The local evidence also contains
the supplementary E5 scorer, PCM16 audition generator and browser player.
Compact results are in `microphone-resonance-comparison-2026-10-09.json`;
`microphone-resonance-second-pass-2026-10-09.json` records the new comparison.
The separate microphone, Classical capture and banjo ablation JSONs record
the mechanism checks. Desktop bundles,
Rack packages and interactive host playback are outside this source change.
