# Acustra fret/upper goal — 2026-11-02

The user's goal is researched, iterative improvement of acoustic-guitar
realism, with natural audible fret movement and improved upper articulation.
The canonical DSP and Reason adapter stay synchronized; changes and audio
remain reviewable. No PR merges without the owner's explicit request and
green checks with feedback addressed.

## Established evidence

- The 1.9.14 candidate's wound-string shifts are 12.04 dB more present at
  Release Noise 10%, while zero, open/plain strings, repeated frets and
  fixed-fret member bends retain their exclusions. Speed-dependent winding
  texture is based on the published handling-noise mechanism; absolute level
  remains an explicitly authored balance.
- The pad's intended sampled half-power corner is now exact. Paired soft
  high-note renders show a modest median H2–H6/H1 rise of 0.38/0.34 dB for
  Finger/Thumb. This is articulation evidence, not proof that the wider
  snap/mellow realism gap is closed.
- [Validation follow-up](../validation/fret-upper-goal-2026-11-02.md): all
  115 native gates have passing final results. The two previous timeouts
  are resolved without dropping cases, reducing lengths, changing tolerances
  or increasing deadlines. The same-DSP eight-stage Rack suite passes.
- Review audio, fixed scores, raw levels and hashes remain outside Git.
  No listener preference is asserted; the owner's recorded candidate-wins
  rule licenses the current candidate, not a claim that someone heard it.
- The actual Rack wrapper/output converter at fresh 2x defaults retains the
  effects across 28 finite captures. Telemetry verifies the declared strings
  and frets, explicit default-switch setup has byte parity, and additional
  one-change lettered pairs preserve both old mechanisms in their baseline.

## Bounded research result and future model work

The broader upper-register attack/sustain realism is not proved by a small
brightness change. The [follow-up audit](../research/upper-register-audit-2026-11-02.md)
does not support an arbitrary velocity-phase flip or extra constant damping.
The [reference follow-up](../research/upper-register-reference-audit-2026-11-02.md)
has now separated the identified archtop and dreadnought sources, checked
onsets and floor-censored partial envelopes, and found layer- and
pitch-dependent attack differences. It does not justify another blanket
gain, phase or damping change. Retain the validated candidate at its actual
scope: clearer soft upper contacts and audible wound-string shifts.

Further model research is a separate backlog:

1. Expand attack/partial-envelope measurements with technique- and
   register-matched release recordings. Shinyguitar is an archtop and the Eastman E1D is a dreadnought;
   technique, unknown MIDI force, capture, noise floor and historical fitting
   overlap must be explicit. Use upper-register cases and lower controls,
   native levels and a common matching protocol; do not retune per note.
2. Evaluate a physically derived initial-state/contact or weak geometric
   nonlinearity experiment for the missing early transient and partial
   evolution. A modal prototype may compare linear/nonlinear strings using
   the existing construction and displacement scales, with a convergence
   and energy-balance check. It is an experiment, not a production promise.
   Do not invent a burst gain or increase damping to hide missing excitation.
3. Adopt a new mechanism only if the measured attack/decay direction improves
   across the declared cases, tuning and energy remain bounded, and a matched
   A–Z set exposes its actual effect. Preserve the existing mean-pitch and
   contact choices unless evidence justifies changing them. Reject a null
   or contradictory experiment rather than claiming an improvement.
4. For an adopted DSP change, bump canonical/Rack versions, update the pin
   deliberately, rerun relevant contracts and the default CPU policy, then
   the applicable native and Rack suites. Fresh measurements replace stale
   claims; frozen review evidence remains intact.

The requested improvement iteration does not require every known guitar-model
limitation to be solved. Its acceptance evidence is the independent release
corner proof, controlled fret-source checks, native and Rack-default audio,
unchanged compatibility contracts and passing validation. A broader claim
that the recorded snap/mellow behavior has been solved is not made.
