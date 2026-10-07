# Classical 78 capture investigation — 2026-10-07

The user identified an unnatural emphasis at G4 in two matched demos, then
requested a cause-level correction and explicitly allowed microphone changes.
The reference is canonical `c073471143bee4c6d40d2f25012a8674e3e4637e`.
The earlier single-mode attenuation experiment is not the selected fix.

## Established causes and limits

The [Mores measurement archive](https://zenodo.org/records/4604577) contains
three omnidirectional microphones only 10 cm above the soundboard. The
current right/mono channel uses the upper microphone beside the soundhole;
the other stereo channel uses the treble-side bridge microphone. An old code
comment described this as a bridge/twelfth-fret pair incorrectly. The method
mentions a 1 m microphone but does not publish that channel. Rotating an omni
or assigning an invented distance cannot reconstruct its missing response.

The approximately 411 Hz pressure peak is in the actual measured guitar.
For the treble-side impact, the upper microphone's raw peak is 8.270 Pa/N;
the fitted peak is 7.199 Pa/N. At G4, the runtime high-E force/moment basis
has only about 1.07 dB fitted excess over raw at that microphone. The raw
bridge microphones have much weaker peaks, approximately 3.1 and 2.9 Pa/N.
The local centre-impact check also agrees with interpolation between the
two measured bridge impacts. These checks do not support a gross unit,
gain-index, modal-Q, or pressure-residue error.

The audible held component follows the played note. The nearby runtime
body mode's free T60 is only 146 ms; its unforced amplitude would fall
123 dB in 300 ms. Removing the static pluck preload changes the later
G4 output by less than 0.1 dB. The continuing string force explains the
mode's drive, rather than an autonomous, seconds-long 414 Hz oscillation.
Missing player damping and unseparated measured-string effects remain
possible measurement limitations, not demonstrated faults to correct by
guessing new mechanical losses.

Two observation choices amplify the perceptual difference. Original has
broad recording-based compensation for its close measurement microphones;
Classical 78 previously bypassed that compensation entirely. In addition,
the construction loudness reference raises Classical 78's default Thumb
cell 4.415 dB more than Original at the same Shape/Wood. This explains
almost all of the native 4.31 dB difference on the isolated G4. The gain is
correctly applied once: it equalizes a mixed phrase, not every note or
register. A scalar adjustment alone does not change fundamental dominance.

## General capture experiments

All experiments keep the physical bridge, string excitation and radiation
poles. The actual measured bass-side bridge microphone was tested instead
of the soundhole-side microphone. It reduces the raw 410 Hz peak, but does
not sufficiently reduce the performed G4 fundamental's dominance and loses
upper-band energy elsewhere. It is not selected merely because it is a
physically available alternate microphone.

Borrowing Original's complete modal weighting changes bass balance too
strongly. Multiplying complex modal residues by a frequency-dependent
magnitude is not equivalent to filtering the final microphone pressure:
off-resonance tails cancel, so changing their weights can unexpectedly
increase the low-E fundamental. The new experiments therefore apply a
causal filter to the summed observation, preserving those modal sums.

A broad 800 Hz high shelf reduces G4 dominance but boosts high frequencies
that are already excessive in the Eastman recordings. Its unchanged
115-recording descriptor score worsens, so that candidate is rejected.
Microphone-capture correction must address the measured spectral imbalance
without implying an exact new physical distance or listener preference.

## Selected implementation

Classical 78's summed body-microphone pressure receives two broad, causal
RBJ peak sections: 500 Hz, −6 dB, Q 1.2, followed by 1.4 kHz, +6 dB, Q 1.2.
They address the excessive body band and missing presence band identified
across recordings, rather than changing a G4 event, a single measured pole,
or the physical instrument's damping. Their parameters are an authored
capture contour supported by the recording comparisons, not a measured new
microphone position. Original retains its existing response.

Filtering happens after the complex modal sum. Every physical mode, residue,
string and bridge remains intact. Each body bank carries its own two-channel
filter histories through the existing 40 ms construction/model fades;
clear/reset also clears those histories. Coefficients are designed at the
host sample rate outside the sample loop. The direct contact observation is
separate, so final native renders are necessary to check the whole-output
offline prediction. There are no audio-thread allocations, look-ahead or
additional host-reported latency.

The offline whole-output comparison improves the unchanged descriptor score
by 0.23% on Eastman Finger, 3.73% on Eastman Pick and 5.40% on Martin. Pooled
error falls 2.22%, with approximately 2% improvements in each pitch register.
This modest overall support is distinct from an audible preference. G4's
pooled total error worsens 0.90% even though its harmonic term improves
2.03%: first-second fundamental share moves 93.5%→83.3% for Finger against
57.6% recorded, and 68.0%→30.8% for Pick against 34.6% recorded. The paired
native checks and listening files below determine the delivered result.

## Native confirmation and listening

The calibrated engine confirms the proxy's score direction on all 115
recorded-note rows: Eastman Finger −0.230%, Eastman Pick −3.730%, Martin
−5.404%. Finger's body descriptor still worsens 1.405%; its harmonic error
improves 2.268%. These mixed terms remain visible in the detailed report.

Seventeen paired native performances preserve the exact plucks, string
activity and dumped modal parameters. The 40-second Original prefix of the
promo is bit-identical. Dedicated pickup RMS differences are below
8.1×10⁻⁸ relative, consistent with rounding of its preserved gain reference.
Every affected Classical 78 segment and isolated-note render stays below
the output limiter's knee. The unchanged Original promo prefix already
crosses it, identically before and after.

For the user's Thumb G4, the early 80–250 ms fundamental share falls
93.63%→84.76%, and the fundamental/H2–H12 ratio falls 12.14→7.69 dB.
During 300–1000 ms, those values change 95.16%→87.43% and
12.97→8.45 dB. This approximately 4.5 dB change in harmonic balance
survives global level matching. It is not a claim that every resonance is
gone or that a listener has preferred the result.

`fix-02/listening/` contains the same-melody A/B, isolated G4 A/B, the
whole Classical 78 passage and a nine-note register check. A plays first;
B follows a one-second gap. Comparison versions apply one +1.0643 dB
gain to B, measured once over the full 18-second Classical 78 passage.
There is no per-note leveling. The native-level melody comparison is also
included. All files are 48 kHz, stereo, 24-bit PCM; original float renders,
source hashes, controls and numeric checks remain in `fix-02/`.

Construction gains were regenerated for Bellido only. All 108 Original
table entries remain bit-identical. The new generator option
`--preserve-piezo-level` compensates the relative pickup trim when changing
the microphone reference. Without it, unrelated drift in the old
calibration target would have changed the pickup by up to 0.349 dB.
The preserved Bellido pickup reference changes by at most two float ulps.

## Verification and CPU

The full Release DSP/tool build and all 62 registered CTests pass, including
required Python checks, with no skips. C++17 DSP compilation also passes.
Linux VST3 and Standalone builds succeed; both native host integration tests
pass. No new Reason Rack package was built in this investigation.

The new tests compare the complex capture response with an independent
bilinear prototype across 24/44.1/48/96/192 kHz, including phase. They verify
Original bypass bits, both sections' histories, clear/reset/reprepare,
retained construction history, cancelled and queued model fades, and exact
active bridge/pickup state under filter bypass. Existing model-response
bounds remain unchanged. The existing ultraquiet idle flush observes the
filtered output, so its eventual subfloor clear time is not claimed to be
identical.

The fresh 288-render calibration check verifies all 216 capture/style/
construction loudness cells within ±1 LU. Its command exits 1 solely for
41 **preexisting** extreme-Pick headroom-compensation policy flags:
29 unchanged Original capture settings and 12 preserved Bellido pickup
settings. All were present before the gain update; there are no new flags.
All four previous Bellido microphone flags are resolved. The unchanged
policy is not waived or reported as a full pass. Receipts and exact cells
are in `fix-02/calibration/final-calibration-validation.json`.

After all builds, tests, calibration and corpus renders stopped, the frozen
before/after engines ran 252 callback configurations with 128 alternating
measured pairs and 16 warmups each, pinned to one AMD EPYC 9V74 CPU.
GCC 14.2 Release
flags are identical (`-O3 -DNDEBUG -fPIC`, C++20); setup, snapshot restoration,
exports and hash checks are outside timing. Coverage includes 44.1/48/96
kHz, 64/128 frames, dry/room, attacks, held chords, bends, queued contacts,
hand-back and tuning transitions. Thumb has the full sweep; Finger and Pick
have control sweeps.

Across 126 Classical 78 configurations, median paired callback time rises
0.75%, with a median per-configuration increase of 1.55 µs. Held-chord cases
rise 1.51%, approximately 1.23 µs. The median paired Original ratio is
0.9993, and all 126 Original output hashes are exact. The median per-case
p95 ratio for Classical 78 is 1.0098. This shared cloud host has variable
tails: 215 baseline and 219 candidate observations exceed their nominal
deadlines across the whole matrix. The result quantifies local incremental
cost, not a hard-real-time guarantee on other hosts. Full raw timings,
build/source hashes and machine details are in `fix-02/cpu-*.json`.

## Two-way room coupling

The user also proposed a room that returns pressure to the guitar. Acustra
already has ten early reflections and an absorbing eight-line late-room
network in the microphone path. It does not feed room pressure back into
the string/bridge/body junction. The present correction does not add that
feedback.

The g35 source measurement is anechoic, with neck/end-block supports and a
free back. Its troublesome response already exists without reflected room
pressure. A room can change the instrument's acoustic load and the
microphone's interference pattern; it can also reinforce or lengthen
resonances. Extra room absorption is not automatically extra mechanical
damping. Player contact is a more direct candidate for changing the
free-supported instrument's loss, but a held/free comparison is needed to
quantify it.

A future compact reciprocal acoustic port could couple a dominant body
volume velocity to delayed wall-return pressure. It would require modal
effective areas, a calibrated pressure-to-force receiving response and a
room response at the guitar, separately from the microphone response.
The combined free-field/room load must remain passive, and the existing
measured radiation loss must not be counted twice. Feeding the current
equalized, output-gained microphone or reverb signal into the bridge would
not provide that physical coupling.

## Reproduction

Ignored task artifacts are under `build-bellido-resonance-oct07/`:

- `root-cause/measurement/`: fresh raw acquisition, calibration, complex
  pressure comparisons, microphone setup evidence and file hashes.
- `root-cause/audio/`: matched full/no-static-step/step-only renders,
  actual force/moment and mode traces, pole-recurrence attribution.
- `root-cause/calibration/`: calibration audit, fixed nine-note probes,
  per-mode versus actual output-filter comparisons and matched listening.
- `root-cause/corpus-candidates/`: unchanged scorer and matched Eastman
  Finger/Pick and Martin recordings, per-pitch/register tradeoffs.
- `fix-02/`: frozen baseline source and the final implementation evidence.

The 115 rows include repeated takes from two independent instruments;
they are known-source development comparisons, not 115 independent guitars
or untouched held-out validation. Playback preference is left to listening.
Raw archive SHA256 is
`8a016fab0b3f086a49b2e740c2588fc61938fd09251aab5314413ea4bab72fb6`.
