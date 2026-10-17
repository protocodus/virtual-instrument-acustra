# Verification

Moved out of the README on 2026-10-17 with its content unchanged.

Exact-input delay caches and paired bridge modes reduce median DSP callback
time by 14.3% with GCC and 4.8% with Clang in the 2026-10-05 cloud measurements.
Frozen comparisons preserve every tested audio bit, and all ten demos remain
byte-identical to the global 1/32 release versions. See the
[CPU optimization protocol and results](../benchmarks/cpu-optimization-2026-10-05.md)
for coverage, compiler/processor scope and reproduction evidence.

The JUCE-free suites cover:

- exactly three supported capture observations, mono channel identity and width
  independence, deterministic legacy remapping, capture switching without
  changing the mechanical state, and silence at 44.1, 48 and 96 kHz;
- pick/thumb excitation and preservation of ringing notes when the picking
  tool changes: they change only by the Picking's level, gliding;
- the strings' 2026-09-30 excitation (Tests/AcustraEngineTests.cpp): Pluck
  Position moves the pluck point and changes the written stroke's partials
  by more than 1 dB at every fret of the low and high E for every technique;
  a Finger's loud-over-soft H5-H12/H1-H4 rise within 2 dB of the recordings'
  9.0; the Finger's and Thumb's first 15 ms under -16.5 dB of 2-12 kHz with
  the Pick 6 dB brighter; String Age speeding a low E's H5-H8 decay by more
  than 8 dB/s from the default to 1; and the released contact's Fourier
  coefficients as the Gaussian contact's times the slip ratio's response;
- every construction and Picking at one loudness
  (Tests/ConstructionLoudnessTests.cpp: its own phrase on every
  construction with every Picking, BS.1770, within 3 LU of the default;
  the default's microphone levels exactly 1);
- every construction a player can choose, crossed
  (Tests/ConstructionMatrixTests.cpp): all 180 settings of Shape x Wood x
  Tuning x Capture strum their tuning's open strings at 48 kHz and
  stay finite, under full scale, audible and within 9 dB (K-weighted) of the
  default construction on the same tuning and capture; the Piezo output is
  bit-identical whatever Capture selects and equals Main on Piezo, the mono
  mic is one signal, and requesting the Piezo output leaves Main unchanged;
  a reduced set covering every value runs at 44.1, 88.2 and 192 kHz;
- six-string bounds, deterministic allocation and block partitioning;
- the player (Tests/PerformerTests.cpp) on a battery of performances
  (Tests/PerformanceBattery.h: single notes, two- to seven-note same-sample
  chords, rolled chords, alternating strums and their two-second restart,
  overlapping notes released at every release velocity, sustain, bridge-hand
  and vibrato sweeps, bend under
  RPN changes, the MPE lower zone, string-per-channel, notes off and panic,
  host edge cases, control changes, and every construction control - Shape,
  Wood, Width, Age, Pluck Position, Touch and all five
  tunings - switched under a ringing chord), with and without Gather Chords: finite,
  repeatable, allocation-free, independent of block size, and identical when
  spelt through the helpers a front end without MIDI uses; plus canonical
  same-sample order, the gathered roll, master tune and overflow counts, and
  that retired CC68 changes no sample at the same supplied release velocity;
  explicit release firmness is checked separately for passive damping,
  nominal-velocity parity and unchanged audio before key-up;
- the separate Piezo output (Tests/OutputBusTests.cpp), over that battery:
  Main bit-identical whether or not Piezo is wanted, Piezo equal to Main with
  Capture on Piezo and independent of Capture, a null
  pointer never written, exact silence when idle, and its cost per 64-frame
  block;
- the piezo chain against its circuit (Tests/PiezoCircuitTests.cpp and the
  fixtures `Tools/PiezoReference.py` writes to Tests/Fixtures): the DC
  operating point and clip points; the small-signal response from 5 Hz to
  0.45 fs at 44.1, 48, 96 and 192 kHz; the saddle
  filter against its analog form; harmonics 2-5 and THD from 1 to 12 dB over
  the clip at 100 Hz, 1 kHz and 5 kHz, and nothing measurable below it;
  aliasing 3 and 6 dB over the clip; overload bursts to +20 dB and the
  recovery of C3, C4, C5 and the input's bias shift; exact silence, a
  non-finite force; and its cost per block;
- the piezo in the running instrument (Tests/CaptureTests.cpp): unit string
  weights reproduce the junction's reaction force bit for bit through bends,
  tails, releases and an uncoupled bridge, and the shipped weights give the
  weighted sum of the six strings' forces; the clip's aliasing on a strum
  driven 1.25-1.45 times the hardest playing (the Pick at velocity 127 at the
  bridge, which does not clip it) against an 8x-oversampled clip, at the
  default Touch against a bare clip and at Touch 1, whose release is brighter,
  under -60 dB at every drive; exact silence from a never-played engine, within 5 s
  of the chain's hardest moment, and on Main and the Piezo output within 25 s
  of a released chord; a Capture switch equal to its crossfade and a Piezo
  output cabled mid-note equal to one cabled from the start; microphones
  untouched by the piezo; headroom (no reference strum, velocity 100 or 127
  with Finger and Pick and any Touch, nor the player's own
  strums, reaches U1A's range or U1B's swing; the hottest puts 0.7-2.5 V on
  the element); and the chain's response agreeing between 44.1 and 96 kHz;
- the fretting hand (Tests/HandAllocatorTests.cpp): exposed rolls retain
  their sounded assignments and attacks, pending attacks can move with
  their delays and owners, E major, A minor, G and C one key at a time in
  their open shapes, a
  scale that stays in position and then shifts, a melody that leaves a held
  bass string alone, repeats replucked where they ring, string-per-channel
  and MPE member notes unchanged, one-sample chords planned as one shape, the handless allocator
  exactly whenever the hand is forgotten, and the allocator's cost per
  note-on;
- tuning, pitch bend, duplicate ownership, note-off and sustain behavior;
- release-from-rest onset, settled pitch, physical decay and stiff-string
  dispersion across notes and sample rates;
- exact complex sample-rate conversion of the 48 kHz body residues, tested
  body-only from 44.1 to 384 kHz so a direct path cannot mask drift;
- stiff-string dispersion below 3.0 cents over H2--H12 on every string at
  every fret from open to the 20th at 44.1, 48 and 96 kHz, and at MIDI 40 and
  84 up to 384 kHz, using the H1/H7/H11.5 collocation on flexible strings and
  a two-section allpass refitted by least squares on stiff ones (measured
  worst 1.1 cents in the standard tuning at 44.1 to 96 kHz and 2.0 in any
  tuning from 32 kHz up; above H12 the stretch still falls short, a Known
  gap);
- fitted register-dependent pluck aperture with an exact legacy identity point,
  a tested negative-exponent continuation and held-out spectral improvement;
- a bounded, monotone steel Kirchhoff--Carrier pitch surrogate across velocity
  and 8--384 kHz;
- the fitted steel fret-decay slope, with regressions proving that it changes
  fretted steel loop gain without changing open steel;
- the fitted shared upper-loss cutoff, with regressions proving
  that it changes 8 kHz loss while preserving fundamental T60 and pitch;
- the plate conductance floor, proven to shorten the 2--4 kHz tail by more than
  2 dB while leaving the 80--200 Hz tail within 1.5 dB, to be exactly inert at
  a zero plateau whatever its corner, and to leave the measured modal weights
  scaling exactly with bridge mobility;
- the radiation above the bank's fitted band: for every Shape, Wood and
  three rates, no third octave from 1 to 16 kHz falls more than 8 dB under
  the one below it, the top band stays within 18 dB of 2-4 kHz, and 8-16 kHz
  agrees across rates within 0.3 dB;
- Piezo Mix on one time base: the mic x piezo cross-spectra at 48 and 96 kHz
  line up with no lag (under 20 us; the old seven-sample offset read 74 us)
  and the blend's third-octave excess over the sensors' power sum agrees
  across the rates within 0.5 dB on average;
- independently ablated body-mode broadening and calibrated 85--145 Hz modal
  balance, followed by a joint bounded refit;
- every tuning playing each open note on its own string
  and refusing the note under its lowest, and every retuned steel string (13
  across the five tunings) keeping its linear mass while its tension,
  impedance and inharmonicity change together;
- passive bridge branch balance, measured-body output and selective audible
  sympathy: an E3/low-E harmonic match changes the 0.30--4.00 s tail by 0.805
  of its RMS, 6.28 times the adjacent F3 case, and raises resonant tail RMS by
  7.26 dB;
- the bridge port: no transient erupts from a decaying chord left alone for
  three seconds, and a note after a two-, four- or eight-second silence peaks
  within a factor of two of the same note on a fresh engine;
- the tuning's range: in every tuning the lowest open string and the
  twentieth fret of the highest sound, and one semitone outside either end,
  and every pitch beyond, takes no string and stays silent (natural
  harmonics above the fretboard were retired on 2026-10-12);
- bridge-hand pressure: exactly no-op at zero, monotonically shortening the
  1--2 s tail and darkening the 2--6 kHz attack across four pressures, never
  louder than open, finite and inside headroom, and reaching the engine from
  CC2 through the wrapper;
- selective audible sympathy, where the ceiling on the resonant share of the
  tail is a bound on balance rather than on coupling — it moves 0.806 to 0.927
  with body tilt alone and only 0.806 to 0.819 with a doubled low-mode gain —
  and the assertions that make it a sympathy test are the off-resonant share
  under 0.30 and a resonant share above five times it;
- the constant six-string anchor: a note's own decay stays within a quarter of
  its rate whether it sounds alone, beside a neighbour too quiet to hear, or
  inside a six-string chord, where the played-subset anchor moved it 2.15x;
- released shapes and release losses that are not bridge motion: a note started
  beside a held neighbour, on a string taken from another note, over a
  six-string chord and over a released one peaks within twice the same note on
  a silent engine, at 44.1, 48 and 96 kHz, measured below the safety limiter,
  and switching the tuning under a ringing chord stays within
  twice the chord it lands on;
- the stolen-string tail: a chord change keeps more than half the taken
  string's stored energy, never more than it held, decays it below 1% within
  half a second, also retains a held note's wave when replucked, stays finite
  and inside headroom from 44.1 to 192 kHz, survives forty chord changes at
  100 ms spacing at 44.1 and 384 kHz, and is silenced by All Sound Off;
- audible construction controls, bounded fitted calibration, finite hostile
  inputs and a final limiter that leaves ordinary output linear;
- finite, bounded Main and Piezo outputs at 8, 44.1, 48, 88.2, 96, 192 and
  384 kHz while every automatable control moves, Tuning and Capture
  among them, and a measured six-string runtime ratio of about
  0.035× realtime (Apple silicon) against a 0.25× gate;
- the fractional-delay read: a slewing bend never clicks above 14 kHz and its
  per-round-trip loss stays within a factor of 2.5 across the fretboard at
  44.1, 48 and 96 kHz;
- the two-point bridge: every fitted residue matrix stays positive
  semidefinite, every one of the six string positions' own combined residue
  stays non-negative, both shipped banks carry at least one rocking mode, and
  a rocking-free bank falls back to the exact one-point port algebra;
- MPE per-note pitch bend as a string tension bend: pitch and the twelfth
  partial's stretch track Grimes' published law within 0.001 cents and 0.08
  cents respectively over a whole-tone bend at three sample rates, and a
  bend on one member channel retunes only that channel's string;
- the vibrato wheel: rate and depth stay inside Erkut's published ranges at
  three sample rates, the sounding pitch never dips more than a bound below
  the note's own, CC1 at zero is bit-identical to no vibrato at all, and an
  open string gets none;
- MPE Timbre (CC74) moves only its own voice's comb nulls, and a same-sample
  chord swept in string-per-channel mode lands each channel
  on its own string;
- same-pitch repeats use one continuing string, retaining phase and incoming
  packets with a bounded sampled input-work budget; long phase-locked and
  slightly detuned tremolo tests guard against accumulating energy;
- the different-pitch cut-short tail: its captured energy is below 1% of what it
  started with 60 ms later, at three sample rates;
- the 1/32-note release window: held-audio identity before damping, inclusive
  same-string join boundaries, actual sample timing, live tempo changes,
  MIDI ownership, sustain/controller behavior and scheduled strums; the native
  processor additionally checks host BPM, fallback and existing MIDI latency;
- release damping does not create a new attack: paired low fretted notes and a
  six-string chord at three rates constrain the first
  5 ms after damping starts against the preceding and simultaneous held peaks, with a separate
  allowance for later ringing. Restoring the abrupt release loss fails all
  nine cases; the peak gates are not physical-energy measurements;
- release: missing velocity and MIDI 64 keep the nominal damped wave; an
  explicit softer/firmer release changes passive loss without replacing
  stored waves or adding a picking attack, and CC68 changes nothing.
  The release suite also bounds the release touch: nothing before damping starts,
  28-60 dB under the attack, no new onset, headroom kept. A dedicated release
  suite, played through the player, checks high note-on velocities across
  all four body shapes, three registers and
  44.1/48/96 kHz, plus CC68 moved under the sustain pedal and the immediate
  released-versus-held peak;
- replucks: a held note replucked six times at 250 ms neither climbs above
  1.5 times its first peak nor clicks on its first samples, and a note three
  strings can reach, repeated after its key came up, stays on one string at
  one level.

The physical-fit scorer has a synthetic closeness self-test. Its C++ renderer
also smoke-tests manifest/file consistency and verifies that model-only updates
cannot modify reference targets. Every Python tool that has a `--self-test` or
`--smoke` runs under ctest (Acustra.SelfTestRegistry fails on one that does
not), among them the open-corpus benchmark end to end on a synthetic corpus
it writes itself. Further checks keep the documentation and tooling in step
with the code: the calibration vector's documented size against the one the
renderers take (Acustra.CalibrationVector), the optimiser leaving every value
a listener chose frozen (Acustra.OptimizerFreeze), CI's demo count against the
renderer's list (Acustra.DemoCount), and a configure without NumPy saying
which tests it skipped (Acustra.PythonTestsReported).

The C++ suites run under ctest at a shell's default 8 MB stack (ctest itself
raises the limit on macOS), so each also runs directly or under a debugger;
the tests keep their engines and processors on the heap.

The wrapper suite additionally checks nominal release parity, explicit
release damping and inert CC68, sample-accurate MIDI, canonical
same-time chord order, gathered live chords (a triad rolled low to high over
20 ms, and a six-string E major rolled over 25 ms after a D major the hand
remembers, sound bit-identical to the same chord on one sample, while notes more
than the window apart and a key repeated inside it sound
bit-identical to the same timeline played 30 ms later, and the 30 ms is
reported as latency on top of the engine's seven samples), conventional channel isolation, lower-zone MPE setup,
RPN 0/RPN 6 ranges and lifecycle, frozen member-tail bends,
panic/controller-reset behavior, state migration and editor rendering, and
that the processor plays the whole performance battery bit-identically to the
player alone. VST3, Audio Unit and Standalone targets are built from the same
engine.
