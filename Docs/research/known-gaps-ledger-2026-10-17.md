# Known gaps: the research ledger to 2026-10-17

This is the **Known gaps** section the README carried until 2026-10-17, moved
here unchanged when the README was cut to a list of current gaps
([README: Known gaps](../../README.md#known-gaps)). Its entries are dated
measurements and decisions; many predate later engine changes, as its own
introduction says. Section names it cites in parentheses (Pluck and strings,
The top's spring-back, Construction controls) are in
[how it works, in detail](how-it-works-1.7.0.md).

## Known gaps

The [current DSP completion checklist](dsp-finalization-2026-10-11.md)
separates modeled behavior, verified inconsistencies, accepted approximations
and missing evidence. The measurements below are a historical research ledger:
many predate the 2026-10-10 saddle termination and string-loss changes or the
2026-10-11 material, hand and broad-Q corrections. Their numbers are not a
fresh benchmark of the current build. In particular, unequal intrinsic
polarisation losses, an unscaled heave-only plate floor, absent slide roughness
and an unadopted 94.2 Hz air retune no longer describe the current instrument.
Dated evidence is retained so the earlier decisions remain traceable.

- The default Piezo Mix of 20 % deepens two holes: where the
  microphones' antiresonance meets the piezo in opposite phase, the
  Parlor's F#5 falls from -7.5 to -14.3 dB against its neighbours and the
  Jumbo's C5 from -2.8 to -8.5 (Room 0.5, E2-C6 sweep); on the Dreadnought
  and Auditorium the piezo fills the hole instead. Inverting the piezo or
  shifting it in time moves the cancellation to other notes rather than
  removing it, so the blend is unchanged; the holes start in the
  microphones' top-string response. Not heard yet; see
  [the report](mid-band-dip-2026-10-12.md#over-the-full-range-and-at-the-default-piezo-mix)
  and [the blend study](piezo-blend-2026-10-12.md).
- Strum direction follows an interval rule, not the beat grid: two
  upstrokes a beat apart (the second and third strokes of a "down, down-up,
  up-down-up" pattern) come out down and up unless the C#0 keyswitch asks
  for upstrokes. A rule reading the host's beat position, down on the
  eighth grid and up between, would follow the hand itself; it waits on a
  listening pass of the interval rule (2026-10-12, Docs/decisions.md).
  Picked notes under Pick follow the same rule since 1.4.0.
- The upstroke's 1.2 dB offset comes from comparing whole takes' upstrokes
  with their downstrokes; part of it may be the players' accents on the
  beat, which the corpus cannot separate. Its tilt across the strings and
  the stroke's lighter edges are measured inside single strokes since
  2026-10-14; the edges are means over strokes of four to six strings
  applied to every stroke of three or more. GuitarSet's upstrokes also
  reach the bass strings half as often as its downstrokes and cross each
  string 18% more slowly; neither is modelled, since a MIDI chord names the
  strings it wants and the slower span may be onset detection on weak
  attacks ([the measurement](stroke-release-realism-2026-10-13.md),
  [the refit](playing-physics-2026-10-13.md)).
- A single picked upstroke reverses only the sideways release, the
  return-stroke model strums have had since 2026-10-07, so at the saddle its
  moments add on the treble strings and cancel on the bass ones: in
  isolation it radiates 2.1-3.8 dB more than a downstroke on the G, B and
  high E (up to 5.7 on one note) and 0.6-1.0 dB less on the low E and A.
  No measurement here says whether a real plectrum's upstroke differs that
  much, though the user's blind A/B preference for the candidate included
  it; `PerformanceRealism::alternatePicking` off restores downstrokes.
- The release fall is GuitarSet's median curve against the level the
  hand's damping has taken; real note ends spread widely about it (at 35 dB
  down the quartiles are 6 and 288 cents of fall) with how fast a finger
  lets go, which MIDI's key-up only partly carries. During the fall the
  string keeps the fretted note's dispersion, bridge compensation and hand
  loss, a period at most 5% shorter than the one sounding.
- Left by the 2026-10-11 MIDI audit, each a limit rather than a stuck note:
  - The MPE lower zone is not saved with a session: after a reload, a
    member channel's events are conventional (a ±2 semitone bend range)
    until the controller sends RPN 6 again, which MPE controllers do on
    connection and most hosts repeat at transport start.
  - RPN Data Increment and Decrement (CC96/97) are ignored; only Data
    Entry (CC6/38) sets a bend range or the zone.
  - The Sustain switch acts at the start of the block the host changes it
    in, before that block's MIDI, so an automation point lands up to one
    block early, and under Gather Chords a key-up sent within the 30 ms
    before a switch-on is still held, since the switch is not delayed with
    the notes.
  - Panic and Reset All Controllers leave the bridge hand (CC2) where it
    is, as MIDI's reset list does; a controller left at full pressure keeps
    the strings muted until it moves.
- The Parlor's air mode, at 121-128 Hz, still lifts B2 4.6 dB and A#2 3.4
  over their neighbours on the microphones with the air-mode dip (5.7 and
  4.0 without it, at Room 0.5): the dip is sized for the other bodies and
  is not deepened for the Parlor
  ([the report](studio-smoothness-2026-10-11.md), Docs/decisions.md
  2026-10-12). The other three
  Shapes' air-mode notes sit 2-4.6 dB over their neighbours after the dip,
  as the real flat-top's A#2 does.
- Left by the 2026-09-30 defect audit, each a trade in the shipped sound
  rather than a fix:
  - The parallel polarisation shares the normal loop's bridge compensation,
    so the H1 doublet reaches about 18 cents and sustained pitch sits up to
    9 cents off on the default Dreadnought (low E frets 14-17, A frets 9-11).
    Tuning each plane against its own port puts sustained pitch within
    about 2 cents at every rate but removes most of the doublet's beat; a
    blind listener heard that as detuned and kept this (Docs/decisions.md,
    2026-09-30).
  - Where the pluck shape nearly nulls a partial, the random release burst
    decides its level: MIDI 83's H8 differs by 27 dB between two burst
    noise draws.
  - Historically, unequal per-plane loss factors left a note's realised T60
    up to about half its requested one. Since 2026-10-10 both planes use
    identical intrinsic loss, including the authored 0.99915 round-trip
    factor; the old T60 measurement has not been repeated here.

- The radiation above each measured bank's fitted band (8-12 kHz up) is a
  statistical continuation of its top octave, not a measurement: its level
  follows the bank's own top-octave density and a 6 dB per octave mass law,
  its phases are pseudo-random. It lifts the body's 16 kHz band by 10-27 dB
  over the cliff it replaces, and with it a strong body peak at the
  band's edge weighs more: on the default Dreadnought's 8.3 kHz peak the
  open high E's 25th partial, 14 dB under E6's natural harmonic at the
  saddle, reaches 0.2 dB over the harmonic's octave at the microphones (it
  was 1.2 dB under), so the harmonic test reads the saddle. A microphone
  response measured past 10 kHz would settle both.
- The hardest playing - velocity 127 with the Pick at Touch 1 and Pluck
  Position 0 - reaches the output's soft safety limiter on most
  constructions. Since the top's spring-back (2026-10-01) its peak before
  the limiter is up to +1.59 dBFS on the stereo microphones, past the knee on
  20 of the 24 constructions then shipping (7 before), -0.13 on the mono (5,
  from 2) and +2.02 on the piezo (all 24, as before); the 24 included the
  Classical 78's 12, removed on 2026-10-10. The default construction's
  reaches -1.20 dBFS on the stereo microphones, 0.2 dB under the knee, where
  it kept 2.1 dB under it. A hand at the bridge holds the most force against
  its note, and six strings released on one sample add their releases. That
  one segment is the peak: a chord on one channel is strummed (Performer),
  and the hardest case's strums - 9 ms and 4 ms a string - stay under the
  knee on 22 of the 24 on the stereo microphones (at least 1 dB under on 20)
  and at least 1 dB under on 23 on the mono; the two over the knee were the
  Classical 78's Parlor in mahogany and spruce (-0.04 and -0.06 dBFS), so every
  remaining construction's strums stay under it. Six on one
  sample reaches the engine from a chord spread over channels (MPE,
  string-per-channel). Keeping it under the knee there on every
  construction at the same parity means lowering every construction, the
  default with it, by 2.6 dB (3.6 for the calibration's 1 dB margin),
  which is the user's to choose.
- Loudness is matched over a phrase set, not note by note. Across
  constructions and tunings one open strum sits -6.6 to +6.0 dB from the
  default construction's on the same strings and capture
  (ConstructionMatrixTests): the low extreme is the Parlor in Spruce in
  Standard (mono mic), the high one the Jumbo in Spruce in Half-step down
  (stereo mic), whose low E-flat lands on the Jumbo's air mode.

- Single notes' radiated level is still rougher from note to note than the
  open recordings', though less than it was. Over E2-C6, 1 s RMS at the
  Stereo mic, the RMS deviation from a seven-note neighbourhood (each note
  against the mean of itself and up to three neighbours each side, as
  `Tests/BodyShapeTests.cpp` scores it and as the recordings' figures were
  scored, Docs/decisions.md 2026-10-01) is 2.2 dB
  finger-played on the steel Dreadnought (2.6 before the mid-band dip of
  2026-10-12, 3.3 before the capture voicing of 2026-10-01), against 1.8
  on the Eastman E1D played with fingers and 2.2 with a pick; the deepest
  one-note hole is 7.4 dB against the mean of its two neighbours (7.7 on
  1.3.1; 5.3 just after the capture voicing of 2026-10-01, 9.1 before it),
  against 4.4-9.3 in the recordings. `Tools/MeasureNoteEvenness.py` leaves
  the note out of that mean, which reads the same levels 17-22 % rougher
  (its E2-E5 sweeps in the mid-band dip report: 3.52, 3.20, 3.22 and 2.71
  dB, against 2.95, 2.63, 2.75 and 2.26 with the note counted), so its
  figures are not directly comparable with the recordings'. The holes are
  the measured bodies' own: a close microphone
  over the bridge hears same-sign modal pairs, such as g21's 515 and 589
  Hz, as an antiresonance, and they move with Shape; the mid-band dip
  takes the peak below each such hole down 9 dB
  ([the report](mid-band-dip-2026-10-12.md)); a boost at the hole,
  measured, raised the hole's note about 1 dB but the next note 4-5 dB,
  leaving the hole deeper against its neighbours, so the hole stays. A fit of the bridge
  microphone as a blend of the archive's measured positions, heard before it
  ships, would be the way to close them. Against the Eastman rows the low
  E2 still sits 6-8 dB under its neighbours, and the first voicing lifted
  G2-G#2, near the air mode, 2-5 dB over theirs; its refit took 2.3 dB of
  that lift back. BodyShapeTests keeps it from getting rougher than the
  voicing left it.
- The capture voicing (The microphones and the room) is a fitted
  correction, not a measured far-field response: the consensus of five sets
  of recordings made on other guitars, by other microphones, in rooms. Held
  out of its own first fit, each source but two improved; the two that did
  not were the finger-plucked ones (Eastman finger +1.0%, the bank's
  flat-top +4.1%), and with the room they read -3.8% and +0.3%. It was
  refitted, the same way, after the top's spring-back. A steel-string
  guitar's own response measured where a recording is made would replace it.
- The Finger's attack is brighter than the finger-plucked flat-top
  recordings'. Over the first 15 ms the Eastman finger take puts -27.8 dB of
  its energy at 2-12 kHz; the engine put -19.4 dB there before the voicing
  and -14.9 dB after it (its 1-1.6 kHz lift reaches the attack too).
  Measured over MIDI 40-66 since the spring-back (-17.3 dB against the
  Eastman take's -29.1), the soft burst carries 3.2 dB of it and the
  contact's width 1 dB; releasing the finger through its full slip, rather
  than its slip as a ratio to a full-velocity stroke's, with the burst
  halved, reached -21.8 but took the finger's soft-to-loud brightening
  from 9.1 dB to 5.9 against a picked-recording target of 9.0, scored worse on the
  picked sets rendered with the Finger and on GuitarSet, and better on the
  finger-plucked flat-top sets. What remains, about -21 dB with no burst
  and a wide contact, is not the Finger's. The Finger's contact and
  release were chosen by ear (2026-09-28, 2026-09-30). The isolated Finger
  full-slip revisit on 2026-10-07 keeps the burst and Thumb unchanged:
  Finger recording scores improve about 5%, but the bass attack excess
  remains about 21 dB. See the [current recording comparison](recording-realism-2026-10-07.md).
- The room is a model of a room, not a measured one: an image-source
  shoebox for its first reflections and a feedback delay network for its
  field, at a level and decay a small treated studio has. The recordings it
  was checked against were made in rooms nobody measured.
- A note near a strong, lossy low bridge or body mode sounds a few cents
  from its request: the loop is tuned to cancel the bridge's reflection
  phase at the note (bridgePhaseDelay), but the damped string-body pole sits
  where the reflection's loss also changes fastest. Sustained pitch against
  the same note with the bridge decoupled, 1-2 s, default sympathetic
  strings, 48 kHz: G3 on the steel Jumbo -2.0 cents (up to 5 cents on the
  since-removed Fylde bridge and nylon strings); open strings and notes
  away from those modes within 0.3 (the rates agree within 0.6). The 2026-09-30
  audit found larger pulls on the default Dreadnought, up to 9 cents on the
  low E around frets 14-17, and traced them to the parallel loop sharing the
  normal loop's compensation (the first open item above). A
  first-order correction of the loop's pole (one Newton step on the
  single-string loop, through the measured port's slope) accounted for at
  most 2 of those 5 cents, so it is not applied: the rest is the parallel polarisation and the other strings'
  anchors, which the tuning leaves out. A real guitar pulls similarly near
  its body resonances; a bridge mobility measured strung would say by how
  much. BodyShapeTests keeps it within 5 cents.
- With Gather Chords off, its default, a chord played live on a keyboard is
  fretted as it arrives. Already sounded notes stay on their strings;
  unfired attacks can be moved inside the 30 ms forming window. Some
  arrival orders therefore keep a wider final fingering than one hand can
  reach. A dense roll may occupy every string that can fret its last high
  note; the incoming note then takes an earlier note's string, retaining
  its old wave as the normal re-pluck tail.
  Switching Gather Chords on plans the entire shape before sounding it,
  at 30 ms of latency. Successive notes more than 30 ms apart are taken
  as melody: each note goes where the hand reaches. The
  hand's span, memory and costs are set by the listener's direction and the
  finger-span study above, not fitted to recorded fingerings.
- Capture choices provide measured microphone positions and a piezo chain
  modelled at component level from published values: a documented preamp
  circuit (ESP Project 202) with datasheet op-amp and diode behaviour, a
  published element sensitivity and capacitance, the measured bridge
  conductance, and a saddle mass and seat stiffness chosen from documented
  ranges. The saddle's own bending modes, friction in its slot, its real
  string-to-string balance and named microphone electronics remain
  unidentified. Matched captures with documented loading and channel gains
  are still needed to identify them. Piezo has no matched reference yet.
  Pick and Thumb use authored hand-position/contact-width ratios, not measured
  tool dimensions or a beam/friction plectrum solver; Pick adds a fitted
  release velocity and contact transient (Pluck and strings), and the picked
  archtop is the only picked recording in the bank, so the plectrum's values
  are fitted to one guitar and one player. The real-note archive supports a
  brighter Pick direction but merges finger and thumb labels.
  [Picking comparison and assumptions](picking-styles-2026-09-08.md)
  record the audible separation and the remaining identification limits.

  An offline prototype audited Perng's published two-dimensional beam/string
  contact geometry.
  With the stated rectangular geometry, the paper and thesis force laws fail
  near the rounded tip. A separately derived virtual-work tip force reaches
  the prescribed 90-degree boundary, but still exerts about 149 N against a
  135 N string and retains pick energy: physical detachment is unvalidated.
  Four rates leave 0.94–4.22% of grip work lost to substep averaging, so the
  string field was not fully converged either. The unavailable solver was
  removed during the 2026-10-09 cleanup; its recorded findings remain in
  the historical documentation.

- The recordings snap and then mellow; the engine mellows part of the way and
  does not snap. Over their
  first 300 ms the archtop's low steel strings lose 1-12 kHz at 96-121 dB/s
  and the never-fitted Eastman E1D's wound strings 2-4, 4-8 and 8-12 kHz at
  86, 153 and 300 dB/s (picked); the engine's fall at 15-32. The strings'
  bending loss (above) reaches those rates at a steel wound factor of 0.10,
  and the benchmark refuses it even with the pluck refitted around it: a joint
  fit over Finger and Pick (the loss with the contact width, burst, velocity
  brightness, plectrum edge, release velocity and pick burst) takes steel's
  wound loss back to zero from shipping and from the measured value alike, and
  holding it at half or all of that value leaves training 5-8% worse with the
  Finger contact on its 0.35 floor, the finger burst on its 3.0 ceiling and,
  under Pick alone, the release-velocity share on its 2.0 ceiling. Three
  measurements say why. The attack the lost partials were standing in for is
  missing: at the loud archtop layer the recordings' energy between partials
  at 2.3-12 kHz stands 10-25 dB over the engine's in the first 15 ms (a pick
  transient, then a tail falling at 100-200 dB/s), the Eastman's wound strings
  start 13-21 dB brighter at 1-5 kHz, and the engine's first period carried
  its first corner as a/L of the real step while the held string's static
  load was not released, so its loud onset came a period late (since
  2026-10-01 the load is released, The top's spring-back, and the scorer
  finds the onset within 0.6 ms of the first sample, where it found it up
  to 4 ms late).
  The recordings have a hiss floor the engine does not: the soft archtop
  layer's 5-12 kHz is that floor from the first frame, and strings whose upper
  partials die as fast as the recordings' fall a median 12-16 dB under it by
  0.6-0.9 s (shipping sits 3-5 dB over), which the log-averaged and band
  descriptors charge. With each recording's own floor added to both engines
  ([`AuditRecordingFloor.py`](../../Tools/AuditRecordingFloor.py), a diagnostic, not
  the benchmark), the half-loss candidate of the listening set beats shipping
  under Finger on training, validation and the frozen test split (-1.3, -2.3,
  -1.8%) and on the Eastman finger rows (-7.4%); under Pick it trails on
  training and the test split (+1.7, +0.6%), leads on validation (-2.3%) and
  is level on the Eastman picked rows (+0.4%). And the recordings' partials
  decay in two stages, faster before 0.3 s than over the decay term's 0.12-4
  s, which one loss per partial cannot follow. The finger-plucked flat-top and
  the picked archtop also disagree about the attack itself: against the
  Eastman finger rows the engine's plain strings are 13-18 dB too bright at
  5-12 kHz (measured before the Finger's burst lost its white share on
  2026-09-30, which took a mean 3.7 dB off its first 15 ms' 2-12 kHz share)
  and its wound strings 7-14 dB too dark at 1-5 kHz, the opposite of
  the authored contact law's extra width on the three wound strings, while the
  archtop, rendered with Finger, wants every Finger contact narrower. A blind listener preferred both the half-loss snap
  and the joint optimum to the loss-free engine and asked for something
  between them, and what ships since 2026-09-28 is that, chosen by ear
  (Docs/decisions.md): steel 70% of the way from the joint optimum toward the
  half-loss snap in the loss and in every pluck value the two fits set (wound
  0.035, plain 0.0023). What it closes: steel's
  upper partials now die at about 70% of the half-loss snap's added rate, 35%
  of the rate the recordings measure. What
  it does not: the attack those partials stood in for is still missing. A
  measured contact noise and click per pluck (after Akay, JASA 111 (2002)
  1525-1548: one random contact force from the release, decaying at the
  recordings' 420 dB/s) was rejected by a blind listener as too loud on every
  picked pair. It stayed at zero and its implementation was removed during
  the 2026-10-09 cleanup. These loss comparisons
  predate the static-load spring-back added on 2026-10-01. The benchmark
  then charged the shipped loss: steel training
  +4.6% (Finger) and +5.7% (Pick), development validation +3.9% and +3.1%,
  the frozen test split 6.1015 -> 6.3473 (+4.0%) and 5.4031 -> 5.6644 (+4.8%),
  the flat-top rows +2.1%. With
  each recording's floor added it is level: steel under Finger -0.3%, -0.6%
  and -1.1% on training, validation and test, under Pick +1.8%, +0.1% and
  +1.7%. What would settle it by measurement is a noise-free or
  floor-documented finger-plucked steel corpus and an attack that supplies
  the recordings' transient at a level a listener accepts.
- The highest steel partials still vary with rate. A fresh 2026-10-11 probe
  on `6b80170`, following each partial's windowed peak on isolated strings,
  measures H8 decay spreads of 1.14291–4.83627% across MIDI 76–84 at
  44.1, 48 and 96 kHz, with maximum H1 spread 0.0176261%. This supersedes
  the older 4.1–12.8% H8 and 0.21% H1 measurements; the Engine guards are
  6% and 0.1%, respectively. The fixture disables bridge coupling and
  observes saddle force, avoiding microphone coloration and mistaking
  tension-glide detuning for decay. It does not establish full-instrument
  rate invariance: loss, dispersion and sampling designs remain approximate.
  See the [current evidence checklist](dsp-finalization-2026-10-11.md).

- The bridge mobility the corpus wants is not the mobility that was measured.
  The 2026-09-04 pattern search drove `bridgeMobilityScale` to its 0.25 floor,
  a quarter of the archive's own value, and that rail was worth 3.9% of
  training and 11% of the flat-top rows. It is not what ships: at the floor a
  nylon hammer-on (legato and nylon, both since removed) got *quieter* as it was played
  harder, by 0.35 dB per velocity step at all three rates, so the measured
  0.754677154 is restored
  and the fit's preference is recorded rather than taken. What the corpus is
  buying with that rail — less bridge damping, or something the modal fit
  above 10 kHz does not carry — is the open question.

- Repeated strums with identical MIDI differ by their stroke's speed, force
  and direction, each string's own moment and pluck point, and the strings'
  state, the hand's variation measured on GuitarSet's strokes. Sixteen
  alternating picked E major strokes 0.3 s apart, keys released 0.2 s in,
  correlate 0.015 between downstrokes and 0.034 between upstrokes over their
  first 250 ms, 0.093 and 0.127 without each string's own moment
  (`Docs/validation/playing-physics-2026-10-13/probes`); the 0.93 and 0.995 of
  2026-09-02 were eight hand-damped strokes against the first, by an earlier
  protocol and engine. Where a stroke starts against
  the beat is still the sequencer's: its earliest string sounds at the
  note-on, and the sweep only touches chords that arrive on one sample or
  are gathered.
- Velocity reaches attack brightness under Pick only in part. The string
  now slides off the plectrum's edge in r/u, u the speed its own held force
  gives it, so a soft stroke is released darker than a hard one: on the
  archtop's picked training layers the H5-H12 over H1-H4 balance rises
  4.07 dB from MIDI 16 to 112 against the recordings' 8.43 (0.65 before),
  and the 0-12 ms centroid 554 cents against 1944 (183 before). The soft
  layer's excess treble is gone (H11/H12 +1.7/+2.9 dB from +5.6/+6.1), but
  every louder layer was already too dark and the slip darkens it slightly
  more (H1 at MIDI 80 +11.1 dB, at 112 +9.2); the remaining loud-end
  brightness belongs to the string's nonlinearity and the high-frequency
  loss, not the release. Rendered with Finger, which the slip did not then
  touch, the same rows sit within a decibel at the soft layer and 9.6 dB
  too dark at the loud one. These figures are at the fitted 0.15 mm edge
  with no release share; the edge is a fitted length (0.116 mm since
  2026-09-28, chosen by ear between two fits) because a single note's pick
  speed is not measured here. Since 2026-09-30 the Finger and Thumb release
  by the same slip law, as a ratio to their full-velocity release, which
  closes the finger-plucked instrument's velocity-brightness gap described
  next: on the strings' own probe (eight notes, MIDI 16 against 112, the
  default controls) a Finger's H5-H12 over H1-H4 now rises a median 9.2 dB,
  against the recordings' 9.0 and 2.1 before. The loud-end attack centroid
  and the attack energy between partials are not closed by it. What follows
  describes the finger-plucked instrument before that change. A fresh comparison uses all 54 already-opened Shinyguitar
  training recordings: nine roots, MIDI layers 16/112 and three takes each,
  against 18 deterministic model renders. On the shared finite same-root
  loud/soft pairs, the recordings' median 12 ms power-centroid rise is 2,054
  cents and the model's 76 cents; 60 of 81 round-robin combinations are
  measurable. The median H5--H12 versus H1--H4 balance rise is 9.04 dB in the
  recordings and 0.36 dB in the model, with all 81 combinations measurable.
  These combinations reuse 54 recordings; they are not 81 independent
  performances. Median paired model-minus-recording differences in normalized
  attack energy are +4.97/+4.32 dB at the quiet layer and −9.02/−10.82 dB at
  the loud layer in the 2.56--5.12 and 5.12--10 kHz bands, respectively.
  These descriptors preserve their existing window-stability guards and
  establish a velocity-response gap; they do not isolate its physical source
  or make body effects cancel. This two-layer training comparison replaces the
  obsolete current-build claim based on the older four-layer audit.
  Removing unintended pluck rectification leaves mixed recording agreement:
  median absolute harmonic-balance error moves from 3.89 to 4.38 dB on quiet
  steel and 9.80 to 9.85 dB on loud steel. These use unchanged common masks; this is a
  correction to the specified linear contact model, not an overall realism
  improvement established by these recordings.
  Earlier audio-rate tension modulation was built and
  is inert at the fitted displacement (the decision log says by how much). A
  finite plectrum release — the string held by a rounded tip of radius r
  advancing at the pick's speed, so the corner is spread over c·t_release of
  string rather than let go instantly — was tried since and rejected on
  measurement, not shipped: on the archive's two lowest roots it does what
  the physics says, but above about the twelfth fret c·t_release exceeds the
  sounding length itself, so both velocity layers lose their upper partials
  and the model's already-small velocity-to-brightness rise inverts sign
  rather than growing toward the recordings' nine decibels, and the corpus
  gets worse at every published tip radius. Earlier percussive descriptors did
  not isolate the current random release burst. A controlled source-on/off
  subtraction now keeps every random draw and the entire instrument unchanged:
  on quiet/loud E2, its microphone perturbation has a power centroid of
  4.94/5.73 kHz over 80 Hz–10 kHz and a stereo level of −47.9/−32.6 dB relative
  to the whole first 350 ms note. Touch changes the burst's level, duration and
  colour. These are native causal differences,
  not new recording comparisons or identification of a real pick click.
  Since 2026-09-08 the burst leaves the picking point toward the bridge and
  the nut as two travelling waves on fixed fractional delays, so its first
  bridge arrival is the contact's own travel time rather than a string period
  later ([Steel contact travels from the picking point](contact-travel-2026-09-08.md));
  natural harmonics, whose burst the node filters out, keep no travel. The
  burst itself still has no measured local-force interpretation.
  Sustained radiation above 5 kHz is 6 to 13 dB weak on steel. In the scalar body used before
  2026-09-05, the 2026-09-04 refit halved the Q scale to 0.053, placing 77 of
  steel's 96 original modes and 80 of nylon's 103 on the Q=4 floor; the previous
  0.098 scale placed 29 and 30 there. At that older 0.098 scale, fine modal
  structure above 300 Hz measured 0.8 dB of standard deviation after octave
  smoothing, against the measurement's 4.6 dB. The selected force-pair bank now uses unit
  Q calibration, removing that fitted suppression while retaining the wood
  morph and Q limits. The banks are now kept for the 250 ms at which every
  mode below 700 Hz has converged, so their low modes carry the guitars' own
  decays (steel's air mode Q 19 at 90.8 Hz, not the 62.5 ms window's Q 6 at
  95.5); earlier scalar attempts to restore those decays on the old
  single-input body had worsened the corpus, while the resolved force-pair
  bank improves every split. Real flat-top fundamentals beat at about 1 Hz and
  5 dB, a doublet 5 to 9 cents wide; the radiating parallel polarisation
  (Pluck and strings) now gives the model's partials a beat of the same kind,
  but its fundamentals still beat less than the recordings' (flat-top H1
  0.33 against 1.50 dB RMS residual), since the parallel plane's path through
  the rocking is weakest at the lowest partials. And
  the flat-top targets carry 11 to 12 ms of pre-roll that the
  scorer charges to every such row's attack term as a constant the model
  cannot remove.
- The polarisation end correction is set by ear, not by the benchmark. At
  Woodhouse's 0.8 mm the parallel loop is longer at every fret, 4.8 cents of
  doublet at the 14th fret and 6.8 at the 20th, and since a steel pluck
  leaves most of its energy in that plane, its member took over from the
  faster-dying normal one within 0.2-0.4 s: the soft steel F#5 and C6 rows
  sat 4.0-6.3 cents above their settled pitch over the first 400 ms where the
  recordings sit within 0.5, and the pitch-trajectory term doubled. A blind
  listener preferred high notes that keep their pitch on every pair, so it
  ships at zero, though zero loses slightly more on the attack and decay
  terms than it gains (under 0.7% on every split, measured before the other
  by-ear changes; the decision log has the figures). Woodhouse measured the
  correction on an open string and publishes no law for a fret; a measured
  split for stopped strings would settle it.
- The top's spring-back (The top's spring-back) excites the low body modes.
  The earlier 85-105 Hz envelope comparison did not establish matched air
  decay: it cut through the model's then intrinsic 84.7 Hz radiation pole
  and mixed adjacent components. A broad-filter, source-domain audit
  separated that pole from the dominant rendered transient near 99 Hz, which
  was the anchor spring's. Since 2026-10-10 the air mode sits at 94.2 Hz, and
  its transient sounds at 94.3 Hz with a T60 of 0.55 s.
  With the modest Original air-Q extension, its fitted output T60 rises
  from about 0.45 to 0.50 s, against screened medians of 0.65 s for Eastman
  picked, 0.51 s for Eastman finger and 0.55 s for Martin. Those recording
  estimates include room and microphone effects. Frequency and spring-back
  level retain the listener's choices. Martin still has an approximately
  4.9 dB air-component onset deficit on matched notes; Eastman does not
  support a general gain increase. The 94.2 Hz retune was subsequently
  adopted with removal of the false saddle ground spring on 2026-10-10;
  see the [accepted termination change](saddle-termination-2026-10-10.md).
  The [2026-10-02 audit](realism-consolidation-2026-10-02.md) describes
  the earlier configuration, not the final retuned instrument.
  The bank's archtop
and classical rows, whose tops barely ring there, read the step as 13-43 dB
too much low end in their attacks, and the piezo reads only the saddle
motion it causes, not the released force itself: fed that, the hardest
Pick strums kept 0.11 dB of the preamp's swing, and the preamp's gain was
set without it.
- A natural harmonic sheds its open string's held force at the pluck, as
  any note does (The top's spring-back), and against a quiet harmonic that
  knock stands near the harmonic's own level: on E6 and F#6 its 60-180 Hz
  over the first 300 ms reads -0.2 to -4.9 dB against the harmonic's first
  second, where an ordinary note's reads -10 to -15. The harmonic is quiet
  for the string's own reason, not the model's: read in the string, it
  keeps the modes its pluck put on the node's multiples to within 1.2 dB,
  and the hand that plucks the open high E stands at 0.237 of it with the
  Finger, beside the fourth harmonic's node at 1/4, so those modes hold
  27.8 dB under the pluck's energy (13.9 with the Thumb at 0.328, 7.4 with
  the Pick at 0.099). Without the knock E6 sits 25 dB under an E4 at the
  same velocity with the Finger, 20 with the Thumb and 14 with the Pick. A
  player moves the hand off a node for a harmonic; the engine keeps it
  where Pluck Position puts it, and moving it would be a choice by ear.
- Steel's excitation was fitted with Finger on recordings that were picked,
  and refitting it with the technique that played them does not ship. The
  optimizer's pick-excitation stage (aperture, transient, pluck distance and
  velocity brightness with the plectrum's three values, 166 evaluations under
  Pick) improves every technique-matched split: picked archtop training
  6.284861 -> 6.101598 and development validation 5.904206 -> 5.846431 under
  Pick, and the eight finger-plucked flat-top rows under Finger 8.120313 ->
  7.821725 on steel's own bridge. It gets there by
  running the aperture scale to its 2.5 ceiling and still improving toward
  it, which at a soft velocity is a Gaussian contact with a sigma of 8 cm
  on an open A (13% of the string, against 2 cm now): far wider than a
  fingertip,
  so the pluck is standing in for the recordings' darker body and
  microphone rather than describing a finger. It is the direction a listener
  already called "a bit too dull" at full extent, it dropped a soft pluck's
  first second 5 to 6 dB against a hammer-on (legato, since removed) at the
  same velocity (steel
  hammer-over-pluck energy +10.1 dB at velocity 0.3 and +13.7 at 0.1, from
  +5.2 and +7.4), and rounding the hammer's dent with the same contact does
  not close it, because at soft velocities the whole segment moves and there
  is no front to round. A following steel-string stage under Pick also
  reverses the fret T60 slope (-0.060 -> +0.017), which the picked archtop
  wants and the flat-top rows do not. A matched
  finger-plucked steel corpus, or a measured contact, would decide it.
- The loudest layer's missing brightness has been split between two owners
  by measurement. A geometrically exact (non-linear) string solver, driven by
  the engine's own string data, supplies between nil and about 58% of the
  recordings' loud-layer harmonic-balance rise — the span reflecting a loud
  displacement known only to within a factor from the recordings' own pitch
  excursion, not any uncertainty about the mechanism — and about 4% of their
  attack-centroid rise, so the centroid deficit belongs to the excitation
  (items above), not the string. It has not been built into the engine: doing
  so is an L-effort project of its own, and the measurement was made with an
  offline solver instead. The reference recordings' own excitation still
  leaves the top of the velocity map short of real.
- Since 2026-10-11 a restrained roughness source follows the continuous
  stopping-point motion of an already-held wound string, through Release
  Noise ([the model and limits](fret-motion-2026-10-11.md)). Its level
  and texture are authored; the string set's wrap pitch and absolute friction
  force are not measured. The earlier evidence remains limited: measured against
  GuitarSet's acoustic (not hex-pickup) stream with a band that excludes the
  note's own harmonics, solo slides on wound strings show a real, moderately
  elevated broadband-excess ratio (about 1.4x, a dozen usable slides), but
  that statistic conflates the wrap-wire friction pulse with other
  position-change noise a mono room microphone also hears, so it is not
  specific enough to license a shipped gain. A close-miked solo slide/bend
  capture built to isolate the mechanism would close it.

- Explicit hammer-on and pull-off gestures now preserve the sounding string,
  but their short contact is an authored approximation rather than a full
  moving-fret scattering boundary. The contact and release paths include
  authored hand sounds. A complete physical fret/finger-friction boundary,
  buzz, artificial and pinch harmonics, and body knocks are absent. Sliding
  roughness is now approximated, and automatic return strokes reverse the
  sideways release direction; neither is a fully measured hand-contact model.
  A harmonic's sounding partial runs a few cents sharp of equal
  temperament, which is the stiff string's own inharmonicity plus the model's
  partial-placement residual, and is not corrected. Bridge-hand pressure
  now reaches a tail left by a string taken for a different pitch while its captured
  pitch and intrinsic filters stay fixed. The nominal 10 ms loss setting does not
  bound its lifetime: bridge back-drive kept it active for 1.4–5.0 seconds
  in the overlap audit. Same-pitch repeats now act on one continuing string,
  with an ideal stopped contact and a bounded new release. A moving physical
  contact boundary preserving displacement and velocity through an ordinary
  re-plucked refret remains unimplemented; its two-port accounting only makes
  that transition's junction algebra consistent.
- The instrument varies pluck position, polarisation share and shared stroke
  speed, but that variation remains narrower than real takes. Across the reference corpus's
  round robins — the same root at the same captured velocity, played again —
  attack spectral centroid ranges by a median 287 cents over the first 40 ms.
  Measured against the model's own sensitivities, reproducing that would take
  30 mm of pluck-point movement on an 85 mm pluck distance, 14 dB of plucking
  force, or a 111% change in release aperture. All three are an order of
  magnitude beyond anything a player does, so the model's attack colour is
  too closely tied to note and velocity for a reason its excitation parameters
  cannot reach. The plucking angle is the mechanism this points to. The parallel
  polarisation now radiates where the rocking was measured (Pluck and
  strings), so an angle colours a note rather than only scaling it, but each
  pluck still draws its share from a narrow authored spread (+-0.025 about
  its material's share), and what spread a player's angle actually has is not
  measured here. Widening it does not reach the recordings either: on the
  picked archtop's three takes per note the recordings' 0-40 ms centroid
  ranges a median 267 cents, the model's 34-81, and even +-30 degrees of angle
  moves the model's to only 35-94. Level spread cannot be read from that corpus,
  whose per-zone playback trim normalises it, and its settled-pitch spread sits
  below the analysis resolution.
- The string waveguides remain linear apart from the bounded steel
  Kirchhoff--Carrier energy surrogate. The
  full geometrically exact nonlinear string and finger/fret collision model
  are not implemented.
- The initial pluck is a propagated ideal shape, not a complete finger-loaded
  rest state. Changing its phase or subtracting its travelling-wave mean alone
  does not transform the coupled filter and body histories. The scalar loop
  attenuation also replaces the static elastic limit with a small creep term;
  this is a limitation of the high-frequency loss approximation, not something
  filter priming can repair. The modal preload reference above retains the
  static elastic term and checks release energy, but mapping that state into
  the shipping waveguide remains unresolved. The pressure bank is fitted over
  80 Hz–10 kHz and has nonzero static force-to-microphone transfer. Its constant
  force history has an algebraically defined filter state, but that does not
  identify a real microphone's acoustic equilibrium before release. This is a
  limit on a new preload model; it does not establish an audible DC fault in
  the current differentiated-wave drive.
- Above H12 the loop's dispersion is not fitted at all, and drift there
  reaches 29.6 cents by H24 on an open steel low E. High on the neck the
  two-section fit roughly halves the drift the single collocated section
  left there, but not all of it: E2 at the 20th fret, measured on the loop
  alone at 48 kHz, sits 23 and 69 cents flat at H16 and H20 (43 and 104
  before). A multi-biquad cascade sized by the published group-delay-area
  method (Abel and Smith, DAFx-06) was built and re-solved on the loop's own
  collocation; it reaches 1.7-1.8 cents worst case, still short of the
  1.2-cent target and costing about two orders of magnitude more design time
  per note on the audio thread, so it is not shipped; the two-section design
  that is costs less than the collocation alone did.
- The parallel polarisation radiates through the measured rocking, not
  through a measured tangential admittance. Woodhouse, who measured the full
  2x2 bridge admittance matrix on a guitar and synthesised with and without
  the second polarisation, reports that including it "makes rather little
  difference" to the damping factors and found no convincing double
  exponential decay across every note to the twelfth fret of his test guitar;
  what he does report is that plucks parallel to the soundboard are markedly
  quieter and that where a partial splits into a doublet the normal pluck
  excites the upper member and the parallel pluck the lower. Acustra's
  parallel plane now reaches the body the way a sideways crown force does,
  as a moment through `h/a` onto the measured rocking and moment paths, with
  `h` the published crown heights of the measured guitars (Pluck and
  strings). Two things that route does not cover remain open: the saddle's
  in-plane translation, which no archive here measures, and the rotation
  axis's depth below the top, taken as the top surface. In a modal expansion the direct and cross admittances all follow
  from each mode's normal and tangential components, so one extra measured
  number per mode would replace the rocking route with the full matrix.
  Where the rocking is strong the two loops form one pair of modes; the pair
  is tuned by its sustained, energy-weighted centre, so its attack can sit a
  few cents from its sustain (the flamenca bridge's open B starts about 7
  cents under the pitch it sustains at), and across the fretboard the settled
  pitch of a single note stays within -1.8 to +3.0 cents on the flamenca
  bridge where it was -4.2 to +3.7 with one radiating plane. The benchmark's pitch-trajectory term
  rises with it (Finger, Original bridge: training 0.455 -> 0.855,
  validation 0.143 -> 0.499) while every total falls. The superseded
  saddle-height prototype tuned each
  plane against its own port, which is what left its open B 19.6 cents from
  the mode that sustained.
- The two-way junction is calibrated around a one-way, 17 mm-spring engine,
  and since 2026-10-10 the strings end on the bridge with no anchor at all;
  no refit has followed either change. With the stub anchor, the open-string
  partials sat 21 to 25 dB under a played fundamental against the
  recordings' 25 to 40. The played strings' own damping, which the idle halo
  used to fill in, showed: on the finger-played flat-top rows the fundamental
  decayed at about 15 dB/s against the recordings' 6. The benchmark read
  6.111540, 6.072344 and 8.073304 against the previous engine's 6.835, 6.824
  and 5.655, better on the picked splits and worse on those eight rows.
  Without the anchor it reads 6.9190, 6.8121 and 6.5366 (training,
  development validation, flat-top) against the stub's last 6.8630, 6.5775
  and 6.5853. A staged refit around the
  new termination was run twice. The first, with a one-sided-derivative
  solver, was rejected on a held-out miss. The second, with a bounded compass
  search, converged and beat the shipping calibration on all four splits, but
  it doubles the open low E's own fundamental decay rate away from its
  recorded target and fails eleven engine regressions, so it was held back
  for the ear rather than adopted (Docs/decisions.md).
- Chords still pull. On steel's own bridge the worst note in E, G, Am, D
  and C chords sits 7 cents off equal temperament early and 9 late: G3 in the G chord reads +8.6
  cents at one second and B3 in the E chord +7.4. Neither string is detuned by
  its bridge. G3 alone reads +1.9 and B3 alone +0.1. Each reading is a
  coincident partial of a lower string coupled through the bridge: G2's
  second partial on G3, and E2's third on B3, which equal temperament and
  stiffness put 6.7 cents sharp. Damping the 190 Hz rocking pole changed
  which member of the G2/G3 doublet outlasts the other (1.0 cents before it).
  The other pulls are the anchor's resonance at 306 and 668 Hz coupling E4 and
  E5 to their coincident partials, and coupled doublets at exact coincidences
  (E3 on the low E's second partial sits on the lower member, -1.5 cents).
  The levers are measured values the listener chose, the steel mobility level
  and the plate-Q damping, and no correction is derived from them. A bridge
  mobility measured strung on the same body would show whether a real guitar
  pulls as far.
- Shape and Body Material morph one measured body, the g21 flamenca, and
  are not separate measurements of four guitar sizes or three woods. Shape
  re-couples the measured A0/T1 pair for a published box and scales the
  plate modes with the top, but the Original's anchor is an authored
  transform, a wider box chosen by ear, and the E1D rows place a real
  dreadnought's A0/T1 above it (Construction controls). What would settle it is a measured body of
  each size. The original steel bank adapts a nylon-strung flamenca blanca;
  the archive lists Savarez Tomatito strings for g21. Steel's own bridge
  takes a steel-string guitar's mobility level from one measured Fylde
  Falstaff (strings damped) against one flamenca: 0.274, 0.23-0.30 over bands
  and statistics. On it the picked notes' prominent partials now fade as the
  recordings' do, but ring gain (3.8/4.5 dB against the recordings' 6.9-8.5),
  late flatness and beating do not follow them, finger-played
  notes' early decay residual does not improve, and the flamenca's full
  mobility, which drains far more, thinned the low E through the 3.25 mm
  anchor stub the strings then ended on. The one-second chord pull read 8.6
  cents then (G3 in a G chord; above).
  g21's damping is
  read against five anechoic flamencas by one octave-median rule: its
  208.7 Hz rocking mode keeps Q 27.4 where the population's rocking modes
  in 200-280 Hz measure Q 11-21, because the rule halves an excess that is
  concentrated in one mode; a rule by mode class would bring in a
  classification and a constant of its own, and is not made. No second
  measured body remains beside it: the Classical 78's matched bridge and
  radiation bank, once a separate Model, was removed on 2026-10-10, and the
  three steel guitars before it were retired (Construction controls).
- The band audits now measure a distance the build chose rather than an error
  it is trying to close. With the body moved by ear toward the flat-top rows,
  the model over the first 350 ms carries 4.0 dB more energy than the archtop
  references over 160--320 Hz, matches to 0.1 dB over 320--640 Hz, and carries
  7.1 dB less over 640--1280 Hz, 7.8 dB less over 1280--2560 Hz, 3.8 dB less
  over 2560--5120 Hz and 10.5 dB less over 5--10 kHz. Steel's
  80--160 Hz band is not reported at all: the archtop puts almost no attack
  energy there, so the reading is its neighbour's leakage skirt and moves 19 dB
  with one sample of window length.
  [`Tools/AuditAttackBands.py`](../../Tools/AuditAttackBands.py) measures each band at
  two window lengths and withholds any that disagree.
- Whether that darkness is a dreadnought or an overshoot is the open question,
  and it is the one the outstanding bracket asks. The listener who chose the
  direction also called its full extent "a bit too dull", and what shipped is
  the middle of the range the two verdicts describe, not that extent. Against
  the archtop it is 10.5 dB down at 5--10 kHz, and against a real flat-top
  nothing here can say, because the flat-top evidence is eight notes and was
  used to choose the direction.
- The decay audit moved with it. Steel now decays 6.4 dB/s too fast at
  80--120 Hz and 8.7 dB/s too fast at 180--270 Hz against the archtop, but
  4.4 and 4.6 dB/s too slowly over 270--600 Hz. The 80--120 and 180--270 Hz excesses
  were the fitted anchor spring, retired on 2026-10-10. The rest are g21's own
  modes, and only those need a matched measurement or an invented
  conductance.
- The steel calibration corpus is a miked archtop, which limits how literally
  the current fit can describe a flat-top steel guitar. None of the 280 real-note
  regions was recorded from the g21 guitar that supplied the measured bridge
  and body, and none includes calibrated pluck force or bridge motion. A
  decisive end-to-end fit therefore needs same-instrument controlled plucks,
  reciprocal two-axis saddle admittance and microphone paths for one
  flat-top steel guitar. The flat-top split now says what that costs, on
  eight rows nothing fits or gates on: the body term reads 14.73 there against
  12.99 on development validation, and the body is the largest term in both.
  Eight rows cannot become a corpus, and their score is not comparable with the
  other splits' — different rows — but the same eight rows on two builds are.
- Damping a note or a chord no longer makes a sound of its own. The release
  loss is a per-round-trip gain, and applying the whole of it to the first
  sample after a key came up drove the junction filter with a step; a chord
  release peaked 0.83 times the chord's own level, but a single low fretted
  note - the case the chord measurement hid - peaked 4.6 to 8.6 dB above what
  it had at that moment. Slewing the gain across one round trip leaves every
  release below the note it ends. Moving the loss to what the loop stores
  instead only delays the same step by a round trip and was measured worse
  again. A 96-semitone pitch bend, where the delay line clamps, still peaks
  above the chord; two semitones does not.
- The by-ear plate conductance floor (0.011 m/s/N) is not the measured plate
  constant. Measured from the same g21 record it extrapolates — the
  sliding-window mean mobility over 2-10 kHz (Elie, Gautier and David, JASA
  132(6), 4013-4024, 2012, Eq. 25) — the conductance the bridge fit is
  missing is about 0.0015 m/s/N (measured against the 50-mode single-point
  fit that shipped when the reading was taken), a seventh of the shipping value; even the
  whole measured mean mobility of that plate over the band is less than half
  0.011. Substituting the measured value trades one band for another: it
  brings 2-4.5 kHz decay closer to the recordings but over-damps 4.5-6.8 kHz
  by nearly 13 dB/s, so the by-ear floor is standing in for a loss whose
  frequency dependence a plate resistance does not explain (the modal fit's
  10 kHz ceiling, string-side high-frequency loss, or radiation, are the
  candidates). A bridge mobility measurement of the modelled instruments, or
  an accounting of that missing loss, would close it.
- The remaining damping error is at the low end. With the strings ending on
  the bridge (2026-10-10) the steel median early decay reads 11.4, 10.8, 8.7
  and 13.9 dB/s over 80-120, 120-180, 180-270 and 270-400 Hz, against the
  recordings' 5.1, 9.6, 12.0 and 15.0 (`Tools/AuditDampingCurve.py`). So it is
  too fast at the bottom and too slow above 180 Hz. The anchor spring's
  resonances at 102 and 233 Hz, which made the earlier excesses, are gone.
  The rest are g21's own modes: the bridge's 412 Hz Q 38 beside the body's
  406 Hz, and its 591 Hz Q 75 and 656 Hz Q 65. Those cannot be corrected
  without either a matched measurement or an invented conductance.
- The gated loss and the damping audit used to disagree about the upper band,
  and no longer do. Re-run at three cutoffs, every band the audit will report
  prefers the fitted 2.170 except steel's 2000--3000 Hz, which prefers 1.2 by
  1.3 dB/s out of 27. The loss and
  all three splits agree with it. The bands above 4.5 kHz that were the grounds
  for "opposite corrections" are withheld by the audit's own stability guard at
  every cutoff for steel, so there is nothing there to disagree about.
- Bridge-stage telemetry shows that the extreme low-E loss is a
  frequency-specific bridge/body effect; removing g21's 82.764 Hz bridge mode
  helped low E but worsened harmonics overall. Eight alternate public
  measured-body banks also lost on a training screen when paired with the
  current g21 bridge. A
  raw-phase modal replacement and an exact 3000-tap, two-microphone FIR were
  both materially worse on the full training split, without clipping; neither
  was allowed to open validation or blind data. The next justified work is
  therefore a richer, simultaneously measured
  bridge/contact/body model followed by measured per-string damping; more
  sympathy, a scalar darkening filter, a note-specific body correction and a
  phase-only body substitution are not the first fixes.
- The training corpus and a flat-top guitar want opposite instruments, and the
  build now follows the flat-top rows rather than the corpus on five values.
  Six parameters swept one at a time, all three splits rescored: against the
  fitted corpus the model wants less bass, a brighter body, more string loss
  and shorter sustain, and against the eight flat-top rows it wants the
  opposite of all four. No single move improves all three splits, so the fit is
  at a real local optimum on the corpus it was given and the distance to a
  flat-top is not closed by fitting harder — which is why this was put to a
  listener rather than to the optimiser. Both directions remain defensible on
  their own evidence: the fitted corpus's rows of real recordings on one side, eight rows of the
  right kind of guitar and one listening verdict on the other.
- The bridge mobility scale improves training all the way to its 0.25 lower
  bound — 6.1266 training and 6.2146 development validation against the fitted
  6.4414 and 6.4278 — while the flat-top rows are worst there. That is a real
  improvement the staged optimiser never reaches, and it is deliberately not
  taken: it buys training score by scaling a measured bridge three times away
  from its measurement, in the direction that moves the flat-top evidence the
  wrong way.
- Fret collision is absent, and it is not because it would be inert. A rigid
  barrier at the published action line plus published neck relief (Martin's
  and Taylor's own set-up specifications) was built and measured: read from
  the string's own d'Alembert swing, a released triangle does reach the
  frets, and the barrier does fire — an open low E at full velocity crosses
  the crowns of frets 2 and 3 by up to 0.04 mm, and a fretted note starts
  buzzing at about half velocity. It is passive (zero energy added in any
  gesture at three sample rates) but does not ship, because the pluck gives
  every string at every fret the same released peak while the clearance one
  fret above a stopped note is only 0.13 to 0.24 mm, so the fitted corpus
  moves against it on both splits (+2.4%) even though the reported-only
  flat-top rows improve (-4.2%), and three measured engine gates break. What
  is missing is not the collision model but a displacement measurement on
  the instrument: the displacement scale it reads against is fitted to the
  attack-pitch glide, not measured as a physical displacement.
- One listening question from 2026-09-01 is outstanding, a set with the user
  and a key unread by design: the close-microphone pair (the shipping bridge
  pair against the treble-bridge/upper-bout pair of the same measurement). The
  other, the body's low-mode damping, was overtaken by a measurement: the
  force-pair bank regenerated at the resolved 250 ms window improves every
  split (decision log, 2026-09-24). The bridge-anchor and two-way-junction
  sets of 2026-09-02 were likewise decided by chord tuning. A verdict for any
  letter but A moves a header, re-pins the regression it trips, and is
  recorded as chosen by ear; a tie keeps A.
  The body direction chosen earlier stands: a symmetric bracket around it
  found the letter half way back clearly worst and the letter half way
  further indistinguishable from what ships.
- A mechanism-level scan of what else exists puts one gap ahead of the others.
  Applied Acoustics Systems' Strum GS-2, the established physically modelled
  acoustic guitar, states that it models the vertical *and* horizontal
  vibrational motion of each string, along with an acoustic body and its air
  cavity. A recent finite-difference academic prototype pairs a collision-capable
  string with an impulse-response body and offers sustain, palm mute, tapping,
  slapping, sliding, legato, natural harmonics and both fingerpicking and
  plectrum modes. Against that list Acustra has sustain, bridge-hand damping,
  natural harmonics, continuous finger-to-pick contact through Touch, slides —
  pitch bend retunes a sounding string without replaying it, verified by the
  delay line moving 276.4 to 245.6 samples for two semitones and back. It
  lacks tapping, slapping and buzz. Its explicit connected finger gestures
  remain an approximation needing controlled listening. Like the commercial
  instrument it now radiates both transverse axes, the second through the
  measured rocking rather than a modelled one. Its measured-body and
  published-fit approach has no counterpart in either. This is a reading of
  what those products say they model, not a listening comparison.
- No controlled expert study has compared this build with real guitars and
  leading commercial instruments. “Top of market” remains the target, not a
  validated product claim.
- MPE support is lower-zone-only: member/manager pitch bend and sustain, plus
  member CC74 (per-note pluck point) and member channel pressure (vibrato-depth
  bias only), all scoped to the zone's member channels. Upper-zone MPE, and
  per-note aftertouch beyond that one bias, remain absent.
- A note held in a chord no longer rings longer than the same note alone.
  Until 2026-10-10 a fitted saddle-to-anchor spring closed that gap as a
  corpus-fitted low-frequency shield rather than a bridge dimension: its
  resonance with the body's mass between modes drained the strings at 6 to
  34 times the bridge's conductance above A0 and T1. With the strings ending
  on the bridge, what the coupling does near A0 and T1 shows instead
  ([the saddle-termination report](saddle-termination-2026-10-10.md)):
  - The worst separable note in E, G, Am, D and C chords reads 10.3 cents
    early and 2.1 late on the Dreadnought (E3 in Am and C, against the idle
    low E's second partial), 8.2/1.5 on the Parlor and 3.2/7.0 on the Jumbo.
  - Each Shape has a note on its A0 that drains fast and splits: the
    Dreadnought's F#2, the Jumbo's E2 at 23.5 dB/s, and the Parlor's G2/B2.
  - On notes near A0 and T1 the late pitch drifts by up to about 10 cents as
    the parallel plane takes over, since it shares the normal loop's
    compensated length.
- Starting a note while the instrument is sounding no longer clicks, and the
  port impedance was not the reason it did. That step is real but small: with
  the bridge's immediate admittance at 0.00305 and a string impedance of
  1.0371, the junction's `1 + Y_b*sum(Z)` moves only from 1.0032 to 1.0114
  between one string and six. What made the noise was differencing a released
  shape. On this build a note over a six-string chord peaks 0.0162 against
  0.0169 for the same note on a silent engine, and always on its own attack
  rather than the first sample. A repluck of a note already sounding on its
  string now lands the hand on it first, and its first samples sit 25 dB and
  more below its own attack.
- None of the three faults above could be seen by the reference corpus, which
  renders one note on a fresh engine and never releases it. A benchmark of
  single notes cannot measure a polyphonic error of any size, and this
  instrument is played in chords. There is no held-out evidence for any of
  these corrections: they are gated by derivation and by regressions that
  assert the behaviour directly.
