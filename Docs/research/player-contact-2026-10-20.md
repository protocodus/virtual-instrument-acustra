# Fixed player contact on the steel-string body

Version 1.10.0 adds a restrained, always-active playing-posture loss to the
existing calibrated body. The user asked for torso and forearm contact
after hearing excessive after-note resonance. This is an intentional sound
change for existing sessions, with no added saved parameter or panel control.

## Physical basis and limits

[UNSW's virtual guitar](https://newt.phys.unsw.edu.au/music/guitar/virtual.html)
illustrates how a back constrained by the player differs from a free guitar;
its clamped-back example is qualitative. [Le Carrou, Pate and Chomette,
JASA 146 (2019)](https://www.lam.jussieu.fr/Membres/LeCarrou/Articles/A25_LeCarrou_InfluencePlayerElectricGuitar.pdf)
measures player-dependent modal damping on an electric guitar. It supports
the contact-loss mechanism, not numerical coefficients for this acoustic
guitar. Contact participation depends on the mode and contact location.

Acustra has no measured torso/top/back mode-shape contact map or held/free
pair for its source guitar. The implementation therefore uses two bounded
frequency envelopes: a lower structural band for torso/back contact and a
broader band for forearm/top contact. The coefficients, frequency scales and
division between the contacts are authored approximations. They do not
represent measured tissue impedance, posture, contact area or pressure.
Existing recording-guided Q adjustments remain; their unknown player load
cannot be separated from intrinsic damping using the available data.

Only extra dissipative loss is modeled. Contact stiffness, added mass,
changing posture, neck grip and mode-shape changes are outside this change.
It does not gate on Note Off, remove room reflections or impose a shorter
string-release envelope. A body mode's free ringing decays faster, but a
coupled string can sustain longer near a deadspot when extra body damping
reduces the bridge's resonant mobility. Whole-note tails must therefore be
assessed separately; no universal shorter-note claim is made.

## Loss law and integration

For a mode at its configured physical frequency `f`, after construction,
material and existing calibration, with pre-contact decay parameter `Q`:

```
Ltorso   = 0.004 * f²/(f² + 180²) * 700²/(f² + 700²)
Lforearm = 0.002 * f²/(f² + 300²) * 1800²/(f² + 1800²)
Qheld    = 1/(1/Q + Ltorso + Lforearm)
```

The terms are positive. Their low-frequency suppression preserves the
mainly-air resonance; their high-frequency rolloff preserves the established
broadband drain. Representative pre-contact modes at 209 Hz/Q28.67,
410 Hz/Q30.05 and 765 Hz/Q53.14 have 7%, 10% and 15% shorter free T60.
These are modal-law examples, not an emitted-note measurement or a fit to
player recordings.

`PlayerBodyLoading.h` supplies this setup-time law. Measured radiation, the
aligned bridge modes, unpaired bridge modes and the kept joint-body modes
receive it once. The deployed bridge and cached tuning observer use the
same visitor. No new recurrence or per-sample calculation is introduced.
Continuous force/moment mobility residue matrices are unchanged. The
broadband conductance-floor prototype is unchanged, and synthetic high-band
radiation continuation is not loaded twice: its density sees the configured
measured bank.

Radiation keeps its oscillation frequencies and changes its exponential
decay. The shared bridge prototype comes from inverse-bilinear mapping of
that actual stable digital pole, preserving shared poles and positive-real
mobility. Unpaired positive-Q mobility modes remain positive real. A
nonnegative modal sum with positive-semidefinite force/moment residue
matrices remains passive. This establishes passivity of the added loss in
the fitted model, not identifiability of the original measurements.

For the measured radiation component, continuous input coupling is retained
through the exact held-input integral `(exp(s*T)-1)/s`. Comparing the loaded
pole to the unloaded 48 kHz reference requires both discrete-pole and
continuous-pole ratios; changing only the radius or only the rate ratio
would alter the coupling. The four projected low-mode observations evaluate
their existing spatial Green function at the loaded continuous pole; this
can legitimately change their pressure phase. Their measured left-minus-
right component cancels that symmetric spatial contribution and provides an
independent constant-coupling witness.

`PerformanceRealism.playerBodyLoading` defaults to true. Its false state is
an offline setup ablation, not a product control. Changing it invalidates
all twelve precomputed construction banks, the analytic bridge cache and
voice configurations, then clears sounding state. Construction automation
retains the existing finite bank fades and retained histories.

## Verification

`Acustra.PlayerBodyLoading` checks the independent inverse-Q law, actual
configured and deployed poles, continuous input coupling recovered by
numerical held-input integration, force/moment response against an
independently integrated continuous state equation, and a negative control
that omits the continuous-pole correction. It checks deployed bridge
positive-real matrix responses against the analytic tuning cache, unchanged
mechanical residues and conductance floor, actual body impulse decay,
held-note pitch changes, finite chord/release audio, reprepare, block
partitioning and interrupted construction fades. A true/false/true toggle
after playing and changing construction compares all twelve cached banks
against fresh engines.

Historical raw-fit fixtures explicitly disable contact only where they
assert the pre-contact data: the selected air-decay extension, measured
modal damping, construction/wood-only pole/residue transforms, frozen
approved microphone observation and the frozen Finger-velocity observer.
The latter retains its separate native Finger-brightness comparison with
contact enabled. Their numeric tolerances remain unchanged. Other
passivity, rate, cache, tuning and lifecycle cases run the production held
model. The capture rate oracle compares against the loaded 48 kHz reference;
the frozen pre-contact CSV retains its separate raw-observation check.

Focused reproduction from the repository root:

```sh
cmake -S . -B build-player-contact -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON -DACUSTRA_BUILD_UNIVERSAL=OFF -DBUILD_TESTING=ON
cmake --build build-player-contact --parallel 4 --target AcustraDSPCxx17 AcustraPlayerBodyLoadingTests AcustraEngineTests AcustraBodyShapeTests AcustraBodyRealismTests AcustraBodyConfigurationCacheTests AcustraGuitarModelTests AcustraWoodMaterialTests AcustraCaptureObserverBlendTests AcustraCaptureTests AcustraStringPitchRealismTests AcustraStringDecayRealismTests AcustraReleaseTests AcustraConstructionMatrixTests
ctest --test-dir build-player-contact -R '^Acustra\.(PlayerBodyLoading|Engine|BodyShape|BodyRealism|BodyConfigurationCache|GuitarModels|WoodMaterial|CaptureObserverBlend|Capture|StringPitchRealism|StringDecayRealism|Release|ConstructionMatrix)$' --output-on-failure
```

Run the final broad native suite after these focused checks, using a Python
interpreter with NumPy and SciPy and `ACUSTRA_REQUIRE_PYTHON_TESTS=ON`. Rack
build/package/host validation belongs to the consuming Rack release. Native
contracts and a changed waveform do not establish a listening preference or
same-instrument physical calibration. Before/after listening files retain
the same gain because excess resonance level is part of the user's question.
