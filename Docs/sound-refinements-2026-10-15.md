# Ten sound-refinement experiments — 2026-10-15

Starting from clean Acustra 1.5.0, revision `90d2d8c`, this pass screened ten
directions for a cleaner, smoother, natural and mix-ready steel-string sound
without adding controls. Two narrowly targeted changes are implemented. The
other eight remain rejected or deferred experiments; they are not ten claimed
improvements. After receiving the repaired A/B player, the user accepted the
complete candidate on 2026-10-15 and requested a push. No per-pair preferences
or completed listening panel are claimed. The accepted DSP is unchanged from
the frozen and validated candidate.

## Selected changes

**Quieter independent pick-contact roughness.** Pick now excites its existing
contact-noise burst at half amplitude. The calibrated displacement and velocity
release, contact timing, pitch, string losses and performance random draws stay
the same. Finger and Thumb are unaffected by this change. Across twelve
note/velocity cases, the first 15 ms loses a median 1.35 dB in 4–16 kHz, while
80–1000 Hz changes by about 0.0003 dB at the median. Whole-clip RMS changes by a
median −0.011 dB (−0.067 to +0.082 dB). This aims at a less scratchy attack while
preserving steel-string articulation. The factor is an authored sound choice,
not a newly measured physical constant.

**More centered bass in the room sound.** A 150 Hz, second-order Butterworth
high-pass acts only on the room's stereo side component, after reconstruction
at the host sample rate. The original room mid is retained directly. The dry
microphones, dedicated pickup, room diffusion, delay lines, decay and early
reflection timing retain their existing behavior. Mono Capture and Width zero
keep the original room. On the isolated room impulse, the 40–120 Hz stereo
energy falls 2.10 dB and correlation rises to 0.928; 120–250 Hz changes −0.37 dB
and above 500 Hz less than 0.004 dB. The effect on a complete guitar mix is
smaller and depends on Room. This is an authored stereo presentation, not a
claim about the measured room's physical width.

Both have independent offline `PerformanceRealism` switches,
`restrainedPickRoughness` and `roomBassCoherence`, on by default. These are not
plug-in parameters, do not add UI, and are not stored as host automation.
Existing sessions use the refined engine. The switches permit exact controlled
ablations without retaining two production implementations.

## What was tested

| # | Direction and experiment | Result / decision |
| --- | --- | --- |
| 1 | Smoother plectrum slip: effective pick edge radius ×1.5 | Quiet notes lose up to 1.80 dB overall and midband up to 2.81 dB. Reject: too much collateral change to the approved pitched attack. |
| 2 | Independent Pick contact burst ×0.5 | Implement: reduces early high-frequency roughness with negligible body/level change; see above. |
| 3 | Strum force-variation depth ×0.7 | Repeated-chord level standard deviation falls 1.065→0.814 dB. Reject: making the sound more uniform also weakens existing measured hand variation. |
| 4 | Pick release velocity-energy share ×0.65 | Harmonic high-frequency tail falls about 1.20 dB. Reject: changes the approved periodic release and sustain, beyond the independent roughness target. |
| 5 | Revisit the legacy 1/1.4 kHz microphone presence contour | Tested smaller boosts and a 1.4→1.8 kHz shift. Current eight flat-top reference notes no longer show the historical 1.26 kHz excess. Cuts worsen occupied presence-band error 4.675→4.920 dB. The shift improves the pooled result to 4.485 dB but overfills 2 kHz and worsens 3.175 kHz. Defer; retain an optional audition. |
| 6 | 35 Hz second-order microphone high-pass | Little useful energy to remove; microphone/piezo phase changes move peaks −0.65 to +1.08 dB. Reject: unnecessary phase tradeoff. |
| 7 | −1.5 dB microphone shelf at 4 kHz | Small overall difference, about −34 dB RMS relative to the matched signal, without evidence of a broad excess. Reject: no reason to dull the whole instrument. |
| 8 | Remove the existing positive modal brightness tilt | Changes level and darkens the upper bands by up to roughly 2.3 dB. Reject: overrides an accepted tonal choice without stronger evidence. |
| 9 | Narrow only the low-frequency room side; compare 100/150/200 Hz | Implement 150 Hz: centers wet bass while keeping original mono and upper-frequency width. See above. |
| 10 | Stronger anti-alias filtering before the decimated room | A 63-tap FIR suppresses synthetic out-of-band aliases by 86–108 dB, but adds 31 host samples of delay and processing cost. No ordinary-guitar audibility witness was established. Defer. |

The attack screen used twelve notes (MIDI 40/52/64/76 × velocities 40/80/120)
plus repeated six-string chords. The capture screen used twelve public
Performer phrases, four Shapes × Finger/Pick/Thumb, followed by freshly
rendered embedded flat-top reference notes. Reference observations use the dry
stereo pair's per-channel power, independently detected onsets and equal
per-note power. These eight reference notes are development evidence, not a
new held-out corpus. Empty/noise-dominated full-band error was discarded; the
presence comparison covers 0.8–3.2 kHz. With the same reference/baseline
−40 dB occupancy mask, the shifted contour worsens median per-note error
7.608→7.843 dB despite its pooled improvement. Per-note tradeoffs are retained
in the capture report instead of treating the pooled result as a verdict.

## Research and prior decisions

Acustra already models the main textbook candidates: lossless fractional
delay and filter-phase tuning, shaped finite pluck contact, pluck-position
filtering, frequency-dependent decay, tension settling, two polarizations,
bridge coupling, measured body radiation and smooth contact/release histories.
The recent [decision log](decisions.md),
[playing-physics report](playing-physics-2026-10-13.md),
[capture observation study](capture-observation-2026-10-10.md) and
[studio smoothness report](studio-smoothness-2026-10-11.md) were checked before
choosing new experiments. Previously rejected hole boosts, blanket piezo
polarity changes and the two-microphone room were not reintroduced.

Sources informing the screen:

- Julius O. Smith, [Loop Filter Identification](https://dsprelated.com/freebooks/pasp/Loop_Filter_Identification.html),
  [Delay-Line Interpolation](https://dsprelated.com/freebooks/pasp/Delay_Line_Interpolation.html)
  and [Acoustic Guitars](https://dsprelated.com/freebooks/pasp/Acoustic_Guitars.html):
  separate tuning, loss and radiation; feedback interpolation errors can be
  particularly audible for steel strings. These support retaining Acustra's
  existing careful string/bridge treatment.
- Laurson et al., [Methods for Modeling Realistic Playing in Acoustic Guitar Synthesis](https://research.aalto.fi/en/publications/methods-for-modeling-realistic-playing-in-acoustic-guitar-synthes/):
  pluck shaping, coupled strings and playing transitions; natural variation
  should not be flattened merely to improve level statistics.
- Bank, [Energy-Based Tension Modulation](https://home.mit.bme.hu/~bank/publist/dafx09.pdf)
  and [Passive Admittance / Coupled-String Examples](https://home.mit.bme.hu/~bank/publist/dafx10adm/index.html):
  pitch settling, evolving decay and passive coupling. More nonlinear or
  resonant behavior is not automatically a smoother sound.

The numerical choices of half-amplitude contact noise and a 150 Hz wet-side
corner are this experiment's bounded engineering choices; none of those papers
is presented as specifying them.

## Reproduction and evidence

The original source and baseline renderer are frozen in
`/workspace/acustra-realism-20261015/baseline`. The listening pack freezes both
actual DSP trees and its renderer, with compiler flags, source and audio hashes.
Native float audio is retained separately from level-matched playback copies.
Identical MIDI, controls and deterministic draws are used for each pair; no
time alignment, per-band processing or dynamic normalization is applied to
make a candidate win.

- [Attack measurements](validation-sound-refinements-20261015/attack/measurements.json)
  and [baseline bypass parity](validation-sound-refinements-20261015/attack/bypass-parity.json).
- [Attack experiment tool](../Tools/ExperimentAttackRefinements.py).
- [Capture screen and reference tradeoffs](validation/capture-refinements-2026-10-15/README.md)
  and [native capture probes](../Tools/CaptureRefinementAudit/README.md).
- [Room study](room-smoothness-2026-10-15.md),
  [room measurements](validation-sound-refinements-20261015/room/measurements.json)
  and [reproduction tool](../Tools/MeasureRoomRefinements.py).
- [Listening-pack builder](../Tools/BuildSoundRefinementReview.py) and
  [public Performer scores](../Tools/RenderSoundRefinementReview.cpp).

Disabling both refinements reproduces the frozen baseline byte for byte on all
thirteen attack-screen renders. Rerunning the four attack experiments reproduced
all 65 exported WAVs byte for byte. Focused tests also check unchanged non-Pick
and burst-free Pick output, rate coverage, finite output and block-partition
invariance. Final build, regression, listening-pack and timing receipts are
recorded below after the source freeze.

## Final validation

The Release build completed for every JUCE-free target, including the C++17
DSP/Performer compatibility and extended-layout targets. All **104 registered
DSP/tool checks pass** on the final DSP: 102 suites from the broad run and the
final Capture and Engine reruns after two test-only expectation updates.
[Validation receipt](validation-sound-refinements-20261015/validation.json).

The original Engine test assumed identical Pick/Finger burst amplitudes.
It still tests that legacy law, at its unchanged tolerance, with the refinement
off; it additionally tests the default half-amplitude law and exact unchanged
pitched release. The Capture test's original 11–14 dB room calibration likewise
remains unchanged with the refinement off. The refined default's intended
0.144–0.149 dB reduction in total wet energy on that chord now has its own
0.10–0.25 dB bound, with mono and cross-rate checks. No production DSP changed
while correcting these fixtures, so the frozen A/B renders remain valid.

The listening pack contains ten balanced blind pairs: eight musical/control
scenes and two isolated refinement tests. It keeps native floats and provides
48 kHz PCM24 playback with static RMS matching and shared headroom. RMS matching
is not a BS.1770 loudness measurement. Disabling both changes reproduces all
eight baseline scenes byte for byte. Two intentionally unchanged controls
(dry Finger and mono Finger) remain exact. The key is separate and hidden until
requested. The picked-strum scene uses actual alternating eighth-note strokes.

No VST/DAW or Reason Rack host result is claimed. This environment lacks native
plug-in development prerequisites and the Rack SDK. The production changes are
in the canonical DSP; host parameters, UI, wrappers and package versions are
unchanged.

The quiet, pinned CPU run measured 61,852 alternating callback pairs over 56
configurations at 48/96 kHz and 64/128 frames, including held chords, repeated
attacks, Room 0/0.5/1 and verified full-force contacts. Matched GCC 14.2 C++17
`-O3 -DNDEBUG` builds show a median per-configuration callback cost change of
**+0.47%** (−0.52% to +1.77%); the median dry case changes +0.05%, Room 0.5
+0.58%, and Room 1 +0.62%. For Pick repeated attacks at 48 kHz/128 frames and
Room 1, median time changes 168.895→171.879 µs and p95 284.991→294.164 µs.
Every finite-output/contact-state/drop check passes. Cloud scheduling produces
large isolated timing outliers in both versions, so this is a local overhead
measurement, not a callback-deadline guarantee.
[CPU evidence](validation-sound-refinements-20261015/cpu/README.md) and
[reproduction harness](../Tools/SoundRefinementCpu/README.md).

## Listen

- [Private hosted blind player](https://acustra-sound-ab.voho-evangelist.chatgpt.site),
  [local repaired page](../../acustra-realism-20261015/listening-fixed/index.html)
  and [portable audition ZIP](../../acustra-realism-20261015/listening-fixed.zip).
- [Picked strums, A then B](../../acustra-realism-20261015/listening/audio/06-picked-strums-A-then-B.wav).
- [Finger arpeggio, A then B](../../acustra-realism-20261015/listening/audio/04-finger-arpeggio-A-then-B.wav).
- [Listening validation](../../acustra-realism-20261015/listening/validation.json).

The ZIP includes the page, matched/native PCM playback files, event scores,
manifest and separate key. Native float renders and frozen source/builds remain
beside the page in the full workspace output. The A/B identities vary between
pairs; A is not consistently the baseline. Pairs 9 and 10 isolate the two
refinements. The strum-direction witness records fourteen strokes in each
direction, and the browser transport, switching, seeking, reveal and preference
export were exercised without claiming perceptual listening.

The player was subsequently repaired after the user reported unavailable audio.
Use the hosted player or extract the complete ZIP; an isolated HTML preview may
not serve its adjacent audio directory. The repaired controls load clips on demand
and preserve seek position even on servers without byte-range support. All 26
targeted browser checks pass, including playback of all ten scenes. Audio bytes,
blind identities and scores are unchanged; the original pack is preserved.
[Player repair evidence](validation-sound-refinements-20261015/player-fix/README.md).
