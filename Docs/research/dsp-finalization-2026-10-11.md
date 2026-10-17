# DSP finalization review — 2026-10-11

This is the current requirement and evidence register for ACUSTRA's six-string
steel instrument. The starting source is `6b80170`, the model/parameter audit
on latest canonical `main` `a534d77`, fetched again after validation with no
change. The canonical corrections and native validation are complete; Rack
integration is recorded separately below. This review distinguishes an
implemented approximation from a verified error and from a phenomenon whose
parameters have not been measured. It does not claim a physically exhaustive
guitar simulation.

The [preceding audit](model-parameter-audit-2026-10-11.md) records 83 passing
tests, construction calibration, native plug-in builds and CPU measurements
for its frozen source. Those results are baseline evidence, not validation
of later edits. Test names below identify existing coverage unless a fresh
measurement is explicitly identified. A passing test establishes its stated
invariant, not realism of an entire guitar or preference by a listener.

The follow-up [existing-instrument completion](../release/project-completion-2026-10-11.md)
uses this register and the user's scope: complete the current instrument,
without adding playing techniques. Its DSP source is `2decfdf`. Current Room,
piezo and moving-stop inventory entries below reflect that follow-up; the
original dated validation and timing sections retain their original source.

## Completion requirements

- Every audible control must have one documented meaning, bounded values,
  a coherent effect on the appropriate physical model or observation, and
  preserved host/state identity. A deliberately inactive legacy slot must
  not be presented as an active sound control.
- Quantities shared by the strings, bridge, body and sensors must use
  compatible physical coordinates, frequencies, damping and mass scaling.
  Independent equations and actual runtime coefficients must establish
  this; copying the implementation's formula into a test is insufficient.
- Attacks, repeated notes, releases, pitch motion, chords and control changes
  must preserve ownership, scheduled timing and retained waves. The audible
  routes must observe the same instrument without changing its motion.
- Supported rates, block partitions, state restoration, reset, silence and
  extreme controls must remain finite and bounded, with explicit rate and
  real-time limits. Finite output after a safety limiter alone is not proof
  of internal stability or physical passivity.
- Important missing phenomena must either be modeled with justified inputs
  and evidence, or remain explicit limits. Historical measurements,
  accepted listening choices and unknown calibration inputs must not be
  described as newly validated physics.

## Instrument and performance inventory

| Requirement / phenomenon | Current implementation and evidence | Remaining closure condition |
| --- | --- | --- |
| Six stiff steel strings; pitch and dispersion | Published string construction inputs; physical plain-steel stiffness and effective wound-core stiffness. `MaterialStiffness`, `StringPitchRealism`, `Engine`, `PhysicalStopping`. | H1–H12 coverage does not validate every higher partial; the two-section dispersion approximation and wound effective stiffness remain limits. |
| Intrinsic loss and String Age | Equal intrinsic sections in both planes; authored round-trip gain 0.99915, steel dislocation loss, wound tension friction and bending loss. `StringDecayRealism`, `RetuneContinuity`, [loss report](string-hf-loss-2026-10-10.md). | Fundamental T60/fret slope and accepted wound blend are calibrated, not measurements of this exact set. No current claim of unequal per-plane intrinsic losses. |
| Excitation and picking | Continuous force/contact response, finite Finger/Pick/Thumb release, physical travel to the bridge, posture history and return-stroke direction. `VelocityRealism`, `ContactRelease`, `ContactTravel`, `PhysicalHandRealism`, `PhysicalHandGeometry`, `StrumDirection`. | Tool dimensions, force/displacement scale and motor variation are partly authored; no complete measured plectrum/skin contact solver. |
| Repeated attacks and stopped contacts | Continuing string, local damping contact, retained waves and bounded additional release. `RepeatedPluckRealism`, `PickingHandBoundary`, `FractionalContact`, `PhysicalStopping`. | A moving fret boundary preserving the full displacement/velocity field is not implemented. The [independent timing witness](moving-stop-limit-2026-10-11.md) establishes premature return-path changes and why a read-offset-only patch would conflict with local contacts and retained tails. |
| Releases, palm mute and fret motion | Gesture damping, release-velocity-dependent wound brush where supplied, continuous stopped wound-string roughness, actual pitch/contact geometry. `ReleaseRealism`, `ReleaseBrushTrajectory`, `FretMotion`, `PalmMutePitch`, `PhysicalVibratoStopping`, `ReleaseJoin`. | Roughness is an authored transverse approximation; wrap pitch, absolute friction and full longitudinal squeak are unidentified. |
| Chords and sympathetic strings | Reciprocal shared bridge, six-string allocation, strum scheduling, unique playable chord planning and duplicate owners. `Performer`, `HandAllocator`, `VoicingRealism`, `SympathyRealism`, `StrummingRealism`. | A keyboard chord arriving without Gather cannot always become one reachable static fingering without moving notes already sounded. Coincident partials and bridge doublets can pull measured chord pitch. |
| Harmonics, bends and vibrato | Natural-harmonic projection, conventional sliding stop, MPE tension bend and geometry-aware vibrato. `GuitarModels`, `PhysicalStopping`, `PhysicalVibratoStopping`, `Performer`. | Artificial/pinch harmonics, fret buzz, tapping, slapping and body knocks are not implemented. Lower-zone MPE only. |
| Full nonlinear/contact physics | Bounded attack-tension surrogate and explicit short contacts are implemented. Historical nonlinear and collision experiments document their assumptions. | Full geometrically exact string/contact motion requires justified displacement, action and termination inputs; it cannot be claimed from the surrogate. |

## Bridge, body and material inventory

| Requirement / phenomenon | Current implementation and evidence | Remaining closure condition |
| --- | --- | --- |
| Reciprocal bridge and termination | Measured positive-semidefinite heave/rock residues; strings end on the bridge, without the false saddle-to-ground spring. `BodyShape`, `BodyRealism`, [termination report](saddle-termination-2026-10-10.md). | Actual dense-frequency passivity and cumulative work must remain bounded under the final coupled changes, including retained tails. |
| Broadband bridge loss in both planes | Plate continuation is scaled to steel mobility and has PSD heave and rocking participation. `BodyShape`, [loss report](string-hf-loss-2026-10-10.md). | Its chosen conductance and unmeasured high-frequency rocking continuation remain approximations, not a new measured bridge. |
| Shape scaling | Coupled air/top frequencies and higher-plate mass/coordinate scaling, with low-mode bridge participation now following independent eigenprojectors and the force-coordinate factor V/A². `BodyShape` checks 135 low-mode residues; worst relative error 5.70e−7. | Focused and complete-suite checks pass; the final 108 loudness cells and 180 native strum cases also pass. Listening and real-time evidence remain separate. |
| Consistent bank anchor | Both fitted banks now use the same nearest-log-frequency map of the original g21 anchor. The original bank is preserved and the joint bank no longer receives an unrelated row-parity detune. `BodyShape`: worst map relative error 1.44e−7. | Nearby fitted resonances remain distinct; this mapping does not assert that two measured modes are identical. |
| Shared bridge/radiation resonance convention | Twinned and retained joint modes now derive a passive mobility prototype by inverse-bilinear mapping the actual radiation pole. Runtime denominator error is at most 6.66e−16 across 8/44.1/48/96 kHz, Q scales 0.05/1/1.8 and all constructions. `BodyShape`, generator derivative/root checks. | This supersedes the then-retained convention mismatch in the [preceding audit](model-parameter-audit-2026-10-11.md). Unpaired bridge modes and the dense floor retain their original natural-frequency prototypes. Raw-data fit limits are recorded below. |
| Wood mass and orthotropic stiffness | Effective equal-thickness top substitution; modal mass, stiffness, low-pair participation and dense-floor scaling. `WoodMaterial`, [wood report](wood-materials-2026-10-11.md). | Spruce/Mahogany/Maple are representative top materials. They are not measured complete guitars or back/side substitutions; species damping is not invented. |
| Body realism outside the measured bank | Shape/material morphs, authored anchor and statistical high-frequency continuation are explicit. The measured whole-body transfer already contains back, neck, cavity and capture-path contributions present in that measurement. | Independently parameterizing top–air–back coupling, neck/fret admittance, higher cavity modes, player loading or tangential bridge translation needs identified inputs without double counting measured modes. This is a morph/extrapolation limit, not proof that those components have no influence. |
| Construction levels and history | Generated capture/technique references; exact settled construction return and suspended adoption. `ConstructionLoudness`, `ConstructionHistory`, `ConstructionMatrix`. | Final regenerated calibration passes all 108 level cells and the 180 native strum cases. Phrase matching is not note-by-note equalization; hard Finger piezo strokes can still reach the safety limiter. |

## Capture, controls and state inventory

| Requirement / phenomenon | Current implementation and evidence | Remaining closure condition |
| --- | --- | --- |
| Capture is observation only | Stereo pair, independent Mono microphone and loaded piezo stay warm. Switching leaves the physical instrument and separate piezo unchanged. `Capture`, `OutputBuses`, `PluginProcessor`; new `CaptureTransition` adds combined control reversals. | The combined baseline and current body/tuning candidate pass, with a failing deliberate routing mutation of the baseline. The representative score does not exhaust the Cartesian product of all controls. |
| Microphone filtering | Causal double-precision contour on summed pressure, separate histories through body fades and authored brightness tilt. `CaptureVoicing`, `MicrophoneBalance`, `Capture`. | The accepted contour is inferred from other guitars and captures. Updated channel-aware fitting tools do not mean a new contour was adopted or prove a far-field measurement. |
| Room | Retained mono-send image-source/FDN model; microphones only, Width controls its spread, tails survive a send reduction and follow Output. `Capture` checks routing, broadband decay, level, block identity and exact silence. | The completion correction preserves each line's accepted 48 kHz decay at DC and 8 kHz (fs/4 below 32 kHz internal rate), with exact 48 kHz coefficients. Remaining limits are cancelling pair-mid input, late output near 25 ms, a heuristic stereo field and a one-pole rate approximation between the matched frequencies. The two-microphone alternative was [heard and rejected](room-2026-10-10.md); do not silently restore it. Room is a send amount, not a physical microphone-distance control. |
| Piezo mechanics/electronics | Shared saddle reaction force, per-string weights, saddle filter, published preamp circuit, nonlinear headroom and antialiasing. Independent analog/circuit response checks in `PiezoCircuit`; playing/switching checks in `Capture`. | No matched physical piezo reference. Slot friction, saddle bending and real string balance remain unidentified. The saddle damping conductance and bridge's chosen continuum are not the same measurement. |
| Released static load at the piezo | The [completion correction](piezo-release-load-2026-10-11.md) includes the same released crown force in the body and weighted piezo sum. Independent signed-load units and the complete reaction identity are checked; circuit, sensitivity and gain are unchanged. | The reduced spring-back envelope is not a measured under-saddle transient. Original and corrected overload-test outcomes are preserved; full-band error/headroom limits remain enforced. |
| Piezo Mix and optional output | Main and piezo share the seven-sample engine time base. Piezo Mix acts only beneath microphone capture; the separate output is unaffected by capture, Room, Body or Width. `OutputBuses`, `PluginProcessor`. | Mic/piezo interference remains physical to the selected transfer paths. Polarity, passive-load and distance controls are absent; adding them is a product decision, not a silent repair of the current mix. |
| Continuous controls | Body is microphone gain with an audible minimum; Width is stereo only; Output scales all output routes; Room/Piezo Mix settle exactly. `Capture`, `OutputBuses`, `Engine`, `RetuneContinuity`. | Endpoint tests must verify internal pre-limiter peaks and passive sections as well as finite output. Native Output ends at +6 dB; the lower-level engine also accepts zero gain and gains up to 4. |
| Saved parameter identity and migration | Stable IDs/order/ranges; retired slots are inactive; absent old Room/Release Noise become zero while malformed explicit values become declared defaults. `PluginProcessor` covers round trips, migration, bad values and live editor binding. | Host automation/UI qualification is distinct from these in-process tests; a real DAW save/reopen/automation session remains needed for host claims. |
| Controller/state lifecycle | Panic, sustain, queued strums, RPN/MPE and sample-offset MIDI retain direct coverage. Host Master Tune now uses a separate fixed-length retensioning reference, with independent geometry, rendered pitch, controller composition, cache, copy, tail and reset/prepare tests in `MasterTuning`. | Initial-idle and cleared-state tuning now adopts the reference immediately; live/retained waves still glide. The focused witness and rebuilt registered CTest pass; the initial full-run failure used a stale test object, as recorded below. Captured tails retain their original pitch and port. The [completion report](../release/project-completion-2026-10-11.md) records the now-passing final Rack pin and wrapper validation. |

## Rates, silence and resource limits

| Requirement | Evidence and current boundary | Outstanding evidence |
| --- | --- | --- |
| Block partition invariance | Capture/Room, output-bus and Performer fixtures compare different block partitions with common event times. `CaptureTransition` adds a common event score with extreme control changes and release/repick edges. | Baseline and current body/tuning candidate pass the interaction run below; final frozen-source provenance is retained in the validation receipt. |
| Rate behavior | Fresh baseline probe on `6b80170`: nine MIDI 76–84 notes at 44.1/48/96 kHz give H8 decay spread **1.14291–4.83627%**, maximum H1 spread **0.0176261%**. Raw log: `build-audit/dsp-finalization/rate-decay-current.log`. The Engine guards are now 6% H8 and 0.1% H1 and pass on the current candidate. | These are isolated-string partial-decay measurements, not full-instrument rate invariance. Loss, dispersion and sampling approximations remain. Refresh provenance/measurement if the affected DSP changes. |
| Extended storage/rates | `ExtendedSampleRates` covers bounded state and low bends through 768 kHz; `SampleRateLayoutParity` compares both ABI layouts on the stated shared-rate scores. [Contract](../guide/extended-sample-rates.md). | Neither test proves identical sound at different sample rates, general 384 kHz ring-history equivalence, or every 768 kHz capture/control combination. |
| Reset, reprepare and exact silence | `Capture`, `OutputBuses`, `ExtendedSampleRates`, `ConstructionHistory` cover warm paths, tails, reset and reconstruction. | Held-voice count alone does not establish silence: queued strikes, retained waves, body, piezo and Room histories must be considered. |
| Real-time cost | The [completion timing](../release/project-completion-2026-10-11.md) covers 42 attack, repeat, queued-contact and control-transition cases on final `2decfdf`. The dated timing section below retains the preceding baseline. | Construction automation now meets the measured deadlines. 96 kHz/32-frame repeated chords still overrun, and one 48 kHz/32-frame Master Tune wall-only miss is retained. This is not a universal callback-deadline guarantee. |
| Independent acoustic validation | Controlled local model tests, dated recording comparisons and listener choices exist. | No same-instrument calibrated string/bridge/body/capture corpus or controlled expert comparison establishes an end-to-end realism rank. A pooled descriptor improvement cannot substitute for one. |

### Independent body and tuning correction evidence

The new body assertions fail against the matching `6b80170` library and
headers in five intended places: Shape participation, cross-bank anchor,
actual twinned denominator, retained-joint resonance and retained-joint Q.
Baseline relative errors are 0.320538, 0.0301845 and 0.341218 for the first
three. They pass on the corrected source. Actual runtime mobility remains
positive-real at the tested frequencies; free rings decay and static body
work is nonnegative in the existing focused suite. These bounded fixtures
do not prove passivity under every possible time-varying parameter path.

`Tools/ValidateSharedBodyPoles.py` verifies the original measurement archive
MD5 and compares the old and corrected basis with retained coefficients,
without claiming a refit. Its `shared-pole-revalidation.json` records:

| Measurement comparison | Previous complex error → corrected | Previous worst median magnitude error → corrected |
| --- | --- | --- |
| Full joint bank, 60 Hz–10 kHz | 0.181147 → 0.180803 | 1.40207 → 1.40916 dB |
| Runtime joint subset (550 Hz cutoff), evaluated 60–600 Hz | 0.203196 → 0.202906 | 1.21928 → 1.25189 dB |
| Own bank with previously adopted frequency/Q alignment, 60 Hz–10 kHz | 0.280997 → 0.279943 | 1.432484 → 1.431394 dB |

The full joint bank remains within its historical 0.24 complex-error and
1.6 dB gates. The aligned own bank **already exceeded** the original 0.24
complex-error gate before this correction and still does; the correction
slightly improves that value. This is not newly gate-passing calibration.
The own-bank comparison precedes the authored anchor, air extension,
Shape/Wood morph and level calibration. The truncated joint subset is not
expected to fit the whole 10 kHz response on its own. Source coefficients
are retained, while generated-header provenance states the new convention.

Master Tune's 216 string/fret/rate cases show maximum tension relative error
1.78e−7 and requested-frequency error 1.23e−7. Independent rendered H1
measurements at 0/±100 cents and 44.1/48/96 kHz show worst absolute pitch
error 0.03943 cents and interval error 0.00601 cents. The original witness
showed +50 cents shortening a held open string from 0.648 to 0.629553 m at
unchanged tension; the corrected route retains the open length, retensions
it and produces no false wound scrape or finger vibrato. Separate isolated
zero-tune comparisons are byte-identical for nine rate/technique cases;
that statement isolates tuning and does not describe the audible body edits.

Independent state review also found that one empty callback before a tuning
notification could leave unused sympathetic strings in a 6 ms transition.
At +50 cents the initial-idle witness had maximum first-attack sample
difference 5.57739e−5 and relative RMS difference 0.000489617. Using the
existing sound-state flush marker to identify initial/cleared silence makes
both differences exactly zero. The regression also tests tuning around
All Sound Off with the same subsequent note-on time, keeping repetition
history independent of the ordering comparison. Focused tests pass; the
registered CTest also passes after rebuilding its stale test object; acoustic and real-time validation remain separate.

The calibrated velocity law remains relative to the current string tuning.
Its fixed-force shortening and member-bend compliance laws do not promise
the same absolute hand force when the instrument's tuning reference changes.
This retains the existing alternate-tuning calibration policy; no unmeasured
absolute-force response is introduced by Master Tune.

Logs are in `build-audit/dsp-finalization-evidence/` (body, negative control,
raw-data JSON) and `build-audit/dsp-finalization/master-tuning/` (before/after,
focused tests, idle before/after witnesses, isolated zero-tune comparison and
manifest). The earlier six focused CTests (`Engine`, `Performer`, `BodyShape`, `GuitarModels`,
`MasterTuning`, `CaptureTransition`) passed in 62.71 seconds before the final
idle-state regression was added. That regression also passes in the focused
MasterTuning run. The subsequent complete native run passed 84 of 85 tests;
the sole failure used a MasterTuning test object compiled before its final
All Sound Off fixture aligned the next re-pick time. Rebuilding that target
without further source changes gives a passing registered CTest (1.87 s).
Together these runs validate all 85 registered tests. The full native build
and C++17 compatibility also pass; the original failure log and final targeted
result are both retained, rather than presenting a single all-green run.
The frozen 194-file source manifest matches the source used for validation.

Final construction calibration passes its `--check`: all 108 Shape/Wood/
Picking/Capture cells lie within ±1 LU of the reference phrase. Stereo spans
−0.23 to +0.36 LU, Mono −0.24 to +0.84 LU and Piezo −0.90 to +0.21 LU.
The 180 native tuning/construction/capture strum cases also pass. Hardest-case
pre-limiter peaks are −2.02 dBFS for stereo Pick, −2.63 for Mono Pick and
−1.02 for Piezo Pick. Hard Finger piezo strokes reach −0.11 dBFS, exceeding
the −1 dBFS safety knee in all twelve constructions; this retained limit is
not hidden by the loudness pass. Evidence is in `loudness-check-final.log`,
`construction-loudness-final.json`, `full-tests.log` and
`master-tuning-ctest-final.log` under `build-audit/dsp-finalization/`.
Nine baseline/current listening pairs (18 WAVs) are rendered at 48 kHz,
stereo float32, with no added gain or individual normalization: Dreadnought/
Spruce, Parlor/Spruce and Jumbo/Mahogany through each of the three captures.
`listening/manifest.json` records the frozen renderers, files, peaks, RMS and
signal differences. Its `listened: false` is explicit: the files are review
artifacts, not evidence of a listening preference or acoustic improvement.
The [validation receipt](../validation/dsp-finalization-2026-10-11/receipt.json)
retains the source hashes, build settings, recovery from the stale test object,
calibration, negative controls, audio manifests and timing. All 194 frozen
computational files matched after the final runs. A later same-line-count
header comment clarifies that Release Noise is sampled at damping onset;
[preprocessor parity](../validation/dsp-finalization-2026-10-11/comment-only-parity.json)
proves unchanged Engine/Performer translation units in C++17/C++20 and both
storage layouts. Both validated and final source manifests are retained.
The 72 Finger/Thumb gain cells are unchanged; only the 36 Pick cells were
regenerated.

### Final native callback timing

On Apple M1 Max/macOS 26.5.1 with Apple Clang 21, the frozen baseline and
candidate were compiled with identical Release flags. Each of 42 cases uses
four warmup pairs and 32 measured pairs, alternating order, with preparation
and output verification outside the timer. Builds, tests and other task
renders were idle; ordinary desktop applications remained open. The harness
includes direct engine attacks and Performer control callbacks, excluding
JUCE/Reason host overhead. Full observations are in
[callbacks-final.json](../validation/dsp-finalization-2026-10-11/callbacks-final.json).

The median of the per-case paired median ratios is 1.0054. That aggregate
does not erase deadline failures. Every row with a miss in either build is
listed here; all other 38 cases have no observed misses:

| Rate / frames / case | Deadline µs | Baseline median / p95 µs | Current median / p95 µs | Baseline / current misses out of 32 |
| --- | ---: | ---: | ---: | ---: |
| 96 kHz / 32 / six-string re-pick | 333.33 | 345.92 / 503.42 | 347.67 / 524.88 | 19 / 20 |
| 96 kHz / 32 / held Master Tune | 333.33 | 175.63 / 199.42 | 195.67 / 238.38 | 0 / 1 |
| 96 kHz / 32 / construction and capture change | 333.33 | 440.29 / 495.79 | 450.42 / 508.00 | 32 / 32 |
| 96 kHz / 128 / initial six-string attack | 1333.33 | 356.29 / 456.33 | 349.58 / 416.79 | 1 / 1 |

The new Master Tune route performs physical retensioning rather than sliding
the stop. Its median cost rises in this witness. The isolated long callbacks
are not attributed to scheduling without a simultaneous thread-time measure.
The short-buffer repeat and construction cases are established product limits;
this correctness pass does not claim to resolve them.

### Fresh combined-control probe

`Tests/CaptureTransitionTests.cpp` was compiled as C++17 against the copied
`6b80170` library and matching headers in
`build-audit/dsp-finalization/baseline-6b80170`. It plays one 650 ms score
with hard six-note attacks, released/repeated strokes, twelve construction
changes 17 ms apart (overlapping the body fades), all three capture routes,
Room 0/0.5/1, Body/Width/Piezo Mix endpoints, zero/−24/0/+6 dB output, then
a quiet stroke and panic. All twelve Shape/Wood combinations are reached;
this is a representative path through the controls, not exhaustive coverage.

All five rates (32/48/96/192/384 kHz) pass. The three output buses are
bit-identical at block sizes 1, 37 and 256. Against a parallel engine with
the same construction/playing/output changes but fixed microphone observation,
the separate piezo and bridge force observations are unchanged. The main
outputs have a measured nonzero squared difference, so this comparison is
not silent or vacuous; this assertion is not a listening test. Panic leaves
exact zero on all three buses.

The block-one run checks every sample's stored microphone signals, raw loaded
piezo and active body/bridge modal histories before the final safety limiter.
Its conservative output-signal bound is the largest microphone delay value
plus two copies of the current raw piezo output. It peaks between 2.40270
and 2.50458; the integrated squared bound is 0.291243–0.323394. These are
signal bounds in the output units, **not physical stored energy** or actual
post-blend peaks. The test's bound of 16 is a runaway fence, not a headroom
or loudness target. Internal modal coordinates are finite; their magnitudes
cannot be interpreted as comparable physical energy across rates.

A separate compiled mutation multiplies the loaded piezo by `1 + room`.
It fails the route-independence assertion at every rate (309,824 sample
failures, exit 1), while the unmodified baseline has zero failures. Production
DSP is untouched by the mutation. The earlier focused candidate, after the
body/tuning corrections and before the final gain-table regeneration, passes
the same score: its raw bound spans
2.35517–2.51968 and integrated squared bound 0.291785–0.324307 across the
five rates, with zero assertion failures. These candidate numbers come from
the focused CTest run, independently of the frozen baseline numbers above.
The final complete-suite run also passes CaptureTransition; its pass is not
a claim that those earlier numerical bounds belong to the final gain table.
Logs and a provenance manifest are retained in
`Docs/validation/dsp-finalization-2026-10-11/capture-transitions-*`.

## Native and Rack differences are intentional contracts

The Rack adapter is a separate repository. At the original finalization pass
it pinned canonical
`1fda1c6fd753e729eb46a1497d8a34e2968df991`, replacing `7516d8c` and including
both this pass and the preceding `6b80170` audit. The Guitar selector covers
four shapes; Wood exposes Spruce and Mahogany, preserving stored Maple for
old patches. The pin was fetched from the local canonical repository; these
commits have not been published or merged.

| Contract | Native plug-in | Reason Rack adapter |
| --- | --- | --- |
| Room | New instance 50%; old state without Room loads dry. | Fixed dry engine policy, no Room property. |
| Body / Piezo Mix defaults | 82% / 0%. | 70% / 20%; compare sound only with matched controls. |
| Release Noise | 70%; MIDI release velocity can shape the gesture. | 70%; Jukebox supplies no independent release velocity, so nominal firmness. |
| Construction surface | All three woods and five tunings available. | Two wood buttons and four tuning buttons; stored Maple and Half-step down remain loadable. Legacy Model storage remains inactive. |
| Playing input | MIDI channels, lower-zone MPE, RPN, optional Gather and string-per-channel behavior. | Jukebox note states/CV and shared controls; no channel/MPE/release-velocity data, Gather off. Bridge Hand has a panel value combined with Reason breath control. |
| Latency / quality | Seven engine samples, plus 30 ms when Gather is enabled; no Rack quality banks. | Fixed 49 host samples across 1x/2x/4x wrapper quality paths; every bus is resampled/aligned. |
| Lifecycle | JUCE prepare, buses, state migration and audio callback. | Preallocated quality banks, Reason reset/idling and batch events require wrapper-specific checks. |

Rack's obsolete Master Tune-as-wheel expectation was replaced with
independent wheel/tuning checks. New open/fretted witnesses verify fixed
speaking length, the physical tension ratio, no false slide/noise or
open-string finger vibrato, and first-note adoption after Reset Audio in
all three quality banks. Existing audible pitch checks remain. The wrapper
already called the canonical `setMasterTuneCents` API; no alternative tuning
implementation was added.

All seven Rack contract stages pass, including 48 construction-history
cases and factory-patch rendering. SDK 5.0 (`JukeboxSDK_500_028`) baseline
and candidate Deployment ARM64 modules build with static analysis. Both
retain an analyzer warning on `configureBody`'s lambda reference to
`wideSteelAnchorTransform`; all six members of that constant are initialized,
the reference is bound before lambda construction, and calls are synchronous.
It is not a reachable uninitialized anchor. ACUSTRA's source-ownership/freshness
check passes; the repository-wide command could not complete because an
unrelated YouKnow submodule was uninitialized, and that product was left alone.

Rack's separate quiet paired measurement uses 36 fresh-process runs:
baseline/candidate × six workloads × three repetitions at the shipped
1x/48 kHz/64-frame setting. Five workloads measure eight seconds after a
one-second warmup; the tail workload includes startup and measures nine
seconds. Each module contributes 110,250 measured callbacks, with no thread-CPU
or wall time exceeding the 1333.33 µs deadline. Candidate worst times are
1145.167/1146.000 µs; baseline 1184.292/1185.084 µs. Candidate median-run
means span 33.5–121.2 µs. These are native SDK-module measurements, not
Reason host performance or an all-quality/all-rate guarantee. The native
96 kHz/32-frame limits above remain.

The Rack receipt and raw timing summary live in `reason-rack-extensions`,
under `Examples/Acustra/Docs/validation/dsp-finalization-2026-10-11/`, with the
integration report `Examples/Acustra/Docs/DSP_FINALIZATION_2026-10-11.md`.
Rack integration is committed locally as `d816e5a6fbfabc04853b52e691f819c26fbff723`;
the preceding selector simplification is `d6b6dbf`.
No installation, universal package, interactive host qualification or
publication was performed.

## Required final evidence before closing this pass

- [x] Resolve the verified body participation/anchor/pole-convention findings,
  or document a concrete retained limitation with its mathematical consequence.
- [x] Attach independent regression evidence for every correction; exercise a
  negative control where useful so a passing test cannot merely mirror code.
- [x] Recheck combined capture/control transitions at minimum/default/maximum
  values, with warm room/piezo histories and internal stability observations.
- [x] Refresh construction loudness/headroom and affected listening renders on
  frozen source; label measurements separately from actual listener judgments.
- [x] Run the affected suites, C++17 compatibility, then one complete applicable
  native suite/build; retain source, toolchain, settings and result provenance.
- [x] Run final real-time timing in a quiet serial window and retain any deadline
  failures as explicit product limits.
- [x] Reconcile this register with the final source and current README. Historical
  reports remain dated; unavailable acoustic/host evidence remains open.

The model/parameter review, verified corrections and Rack integration are
complete for the existing instrument and supported playing controls. The
inventory explicitly retains reduced-model and calibration limits; it does
not turn unsupported playing techniques or unmeasured physical parameters
into claimed features. Host qualification, controlled listening and release
publication remain separate from these verified code/model contracts.
