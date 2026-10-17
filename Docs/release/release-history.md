# Acustra release history

The detailed per-version notes, moved out of the README on 2026-10-17 with
their content unchanged. The README keeps the concise
[version history](../../README.md#version-history).

## Detailed version entries

Each entry records a source version in this repository. No build of any of
them has been published as a release; [Build](../../README.md#build) describes how
packages are built and signed.

### 1.5.0 — 2026-10-14 — the playing hand, measured

- **Finger chords.** Under Finger a chord of three or four strings is plucked
  by the thumb and fingers together: each string takes GuitarSet's median
  lag behind the highest string plus a draw of its own, 4.47 ms standard
  deviation, the earliest at the note-on. It is not a stroke. Five and six
  strings, Pick, Thumb and a latched direction strum as before.
- **Dead strokes.** A strum that crosses a string its chord leaves out gives
  it a dead stroke at its place in the sweep, 4.6 dB under a played string's
  force and under a 43-period touch, as GuitarSet's crossed strings show.
- **The release fall, measured.** The fall follows GuitarSet's median pitch
  against the hand's damping: hardly any for the first 4 dB, then about
  2.3 cents a dB, to 80 cents at 35 dB down.
- **Strum scatter.** Each string of a swept stroke sounds at its own moment
  about the sweep, a uniform 4.35 ms standard deviation added after the
  stroke's speed draw, so a third to two thirds of strokes sound out of
  string order, as GuitarSet's do. The earliest string sounds at the note-on.
- **Picked pairs.** Under Pick a two-note group is a stroke across its two
  strings, swept, alternated and scattered as a strum.
- **Stroke span.** A stroke's span grows as the 0.64 power of the spacings
  it crosses, the profile inside each of GuitarSet's takes, so a small stroke
  crosses each string more slowly; a six-string stroke keeps its speed.
- **Stroke edges.** A stroke of three or more strings strikes the strings it
  enters and leaves on 1.8 and 2.0 dB lighter, as measured inside single
  strokes, and an upstroke tilts by 0.30 dB a string instead of 0.9.
- **Coverage.** New `FingerChords`, `MutedStrings`, `StrumScatter`,
  `TwoStringStrokes`, `StrokeSpan` and `StrokeEdges` suites; `ReleaseFall`
  follows the measured curve. Every mechanism has a `PerformanceRealism`
  switch, and all of them off with the fall let go render 1.4.0's own
  fall-off output bit for bit; `Tools/BuildPlayingPhysicsReview.py` checks
  that while building the blind A/B pack. No parameter, saved state or
  layout changes. Played passages cost the same within run-to-run noise.
  In the blind A/B pack the user picked the candidate in four of nine pairs
  (the dead strokes, scatter, picked pairs and the picked song) and chose to
  ship all seven.

### 1.4.0 — 2026-10-13 — upstrokes lean treble, picked notes alternate, released notes fall

- **Stroke depth.** An upstroke string is released at `-1.2 + 0.9 (k - 2.5)`
  dB of its downstroke force, measured on GuitarSet's 180 comping takes
  string by string; downstrokes are bit-identical.
- **Alternate picking.** Under Pick a single note or two-note group
  alternates down and up with the strums by the dotted-eighth rule; one
  within 30 ms of the last stroke continues it, and the keyswitches latch
  picked notes too. Finger and Thumb are unchanged.
- **Release fall.** A released fretted note falls toward the previous fret
  as the hand damps it, a quarter of the hand's T60 its time constant,
  stopping at 86% when the note is 30 dB down.
- **Coverage.** New `StrokeDepth`, `AlternatePicking` and `ReleaseFall`
  suites; `Tools/BuildStrokeReleaseReview.py` builds the blind A/B pack and
  checks that the three switches off reproduce 1.3.4 bit for bit. No
  parameter, saved state or layout changes. The user preferred the
  candidate in the blind A/B pack.

### 1.3.3 — 2026-10-12 — launch preparation: packages, notices, Standalone MIDI, editor size

- **The Standalone enables a connected keyboard** (built, not yet run with
  a MIDI device). JUCE's desktop Standalone opens a MIDI input only once it
  is ticked in Options > Audio/MIDI Settings. Acustra's now enables each
  input the first time it sees it, at launch or when it is plugged in, and
  records it in the Standalone's settings, saving the Audio/MIDI settings
  with it; after that the saved Audio/MIDI settings decide, so an input
  the user turned off stays off while it keeps its identifier (on Linux an
  ALSA client and port number, which can change). Nothing changes in the
  plug-in formats.
- **The editor fits the display and remembers its size.** It opens at the
  width it was last resized to in the session, or its design size,
  1120 × 980, made smaller at the same proportions when the display under
  the mouse has less room below a 48-point title bar; its minimum,
  896 × 784, drops to fit a smaller display, down to 672 × 588. The width
  is saved with the session's state beside the parameters, not as a host
  parameter; a width made smaller to fit is not saved, and a state without
  one leaves the last width.
- **Unsupported rates are silent and say so.** Outside 8-384 kHz the engine
  would run at a rate other than the host's and play off pitch; the
  processor now outputs silence there and the status line reads
  UNSUPPORTED RATE.
- **The build identity is in every binary and on screen.** The editor
  reads the version and build number from the identity record CMake
  freezes, which also keeps that record from being dropped at link time.
- **Packages carry a user guide and the notices they owe.**
  [Docs/guide/user-guide.md](../guide/user-guide.md) replaces the developer README in
  every package; `ThirdParty/JUCE-DEPENDENCY-LICENSES.txt` carries the
  licences of the code JUCE compiles in (VST3 SDK, AudioUnitSDK, SheenBidi,
  HarfBuzz, IJG, libpng, zlib, FLAC and Ogg Vorbis), and
  THIRD_PARTY_NOTICES.md credits GuitarSet (CC BY 4.0) for the values
  derived from it and Steinberg's VST trademark.
- **Release engineering.** The Windows build links the C runtime
  statically, so its zip needs no VC++ redistributable; the macOS pkg
  installs each bundle at its own path and replaces any version; the
  plug-in wrapper tests run on Linux and Windows as well as macOS; and a
  tag-driven workflow (`.github/workflows/release.yml`, see
  [Release workflow](../../README.md#release-workflow)) builds universal macOS, Windows
  and Linux packages, runs pluginval and auval and drafts a release.
  Signing steps stay dormant until the owner provides identities; nothing
  has been released.
- **An idle instance sleeps** (launch audit, idle CPU). By the Rack
  Extension's rule (silent under -120 dBFS for 0.5 s, no string played, a
  strum rest since the last key), once the engine has also cleared its own
  residue, the plug-in stops computing the strings: an idle 64-frame block
  at 48 kHz drops from about 62 µs to about 0.5 µs, roughly, on an unquiet
  shared build machine, and a playing one is unchanged within measurement
  noise. An instance never played sleeps after half a second, a played
  one 12 to 17 s after its last key in the cases measured. The engine
  gains a suspended mode (`AcustraEngine::suspend`) whose clocks run on,
  so sleep changes no sample in any case tested: a note after it plays bit
  for bit as on an instance that never slept. A struck key, or a changed
  parameter, wakes it; every other MIDI message reaches it asleep. See
  [Outputs](../../README.md#outputs) and [the decision](../decisions.md).
- No parameter or saved parameter value changes. The Performer is
  unchanged, and the engine's only change is that suspended mode, which
  nothing but the plug-in's sleep uses.

### 1.3.2 — 2026-10-12 — the mid-band peak takes a dip, and a bit of piezo

- **A dip at the mid-band peak.** On the E2-E5 sweep
  (`Tools/MeasureNoteEvenness.py`, velocity 96, Finger, stereo mic, Room
  0.5) each Shape's upper strong mid mode put one note 8-11 dB over its
  neighbours (Dreadnought A#4, Auditorium C5, Jumbo G#4; the Parlor's D#5
  5.4 dB). The summed microphone pressure now dips -9 dB at Q 8 on that
  mode, which `AcustraEngine::locateMidBand` finds per Shape and Wood at
  prepare (404-614 Hz) as the highest point of the engine's own response
  within a fourth below its deepest antiresonance between 450 and 800 Hz;
  the piezo never passes through it. The peaks fall to +5.0 (Dreadnought),
  +3.8 (Auditorium), +5.7 (Jumbo) and +2.2 dB (Parlor), and the sweep's
  RMS deviation from 4.20 to 3.52, 3.51 to 3.20, 3.81 to 3.22 and 3.35 to
  2.71 dB. The holes above the peaks are radiation nulls and stay; a boost
  at them was measured and not shipped. See
  [the report](../research/mid-band-dip-2026-10-12.md).
- **The level is restored.** The dip carries a +0.5263 dB restore sized on
  the default construction's calibration phrase, every construction is
  re-levelled to it by `Tools/CalibrateConstructionLoudness.py`, and the
  mono microphone takes -1.0 dB, which puts the default's mono capture on
  the calibration target (0.85 dB under where 1.3.1 played it against the
  pair).
- **Piezo Mix starts at 20 %,** the Rack's default, so a new instance
  starts with a bit of the under-saddle piezo beneath the microphones; a
  session saved before Piezo Mix existed still loads at 0 %. A chosen
  default, not a listening verdict.
- Nobody has listened to the dip; the user directed that it ship on the
  measurements (Docs/decisions.md).

### 1.3.1 — 2026-10-12 — the Parlor's air-mode dip, and the second MIDI audit

- **The Parlor takes the air-mode dip** the other bodies took in 1.2.0, its
  upper bound moved from 117 to 130 Hz to reach the Parlor's 121-128 Hz air
  mode. At Room 0.5 B2 falls from +5.7 to +4.6 dB over its neighbours and
  A#2 from +4.0 to +3.4. The Parlor's Finger and Thumb cells sit 0.5 LU
  over the calibration target, so the construction loudness fixture keeps
  its 3 LU contract (Parlor Spruce Finger at -2.57 LU). No parameter or
  saved state changes. Nobody has listened to it; the user directed that
  it stay as merged (Docs/decisions.md).
- **The second MIDI audit's fixes.** A full Gather Chords queue drops its
  oldest controller value rather than the newest, so a wheel's release or
  a bend's return is never lost, and messages the instrument ignores
  (program change, polyphonic pressure, system bytes) are never queued. A
  note group stamped by an offset that decreased within a block measures
  its stroke interval from the rendered position. The plug-in plays its
  MIDI silently while bypassed, so a key-up sent then is not lost, treats
  the host's reset as Panic, and consumes Panic before the on-screen
  keyboard adds its keys.

### 1.3.0 — 2026-10-12 — strum direction timed by the song, and keyswitches

- **A dotted-eighth alternation window.** A strum used to alternate with
  the previous one whenever less than two seconds had passed, so slow
  chords came out down, up, down. Now a strum within a dotted eighth of the
  last at the host tempo (`Performer::strumAlternationBeats`, 0.375 s at
  120 BPM) is the hand's return and alternates, and a later one is a
  downstroke again; the two-second rest remains the cap at any tempo. The
  window takes in eighth and sixteenth patterns, swung eighths and triplets,
  and leaves quarter-note chords all downstrokes. The limit of an interval
  rule: two upstrokes a beat apart, as in a syncopated pattern, need the
  keyswitch; a rule that reads the beat grid is the alternative, deferred
  until this one has been heard.
- **Only the tuning's range answers.** A pitch below the lowest open
  string or above the twentieth fret of the highest is ignored: no string,
  no sound. Pitches above the fretboard used to reach the open strings'
  natural harmonics; the user asked for the range alone (Docs/decisions.md).
  The node-touch machinery stays in the engine but nothing reaches it.
- **Keyswitches C0, C#0 and D0** (MIDI 24-26, below Drop D's low string)
  latch downstrokes, upstrokes or the automatic rule until the next
  keyswitch or Panic, on any channel; they never sound, take no string and
  are not gathered into a chord. The strings panel prints them with the mod
  wheel's and pitch bend's roles. The user chose the rule and the keyswitch
  on 2026-10-12 (Docs/decisions.md); the Rack Extension prints the same
  legend on its rear panel.

### 1.2.0 — 2026-10-11 — a Sustain switch, and the sweep smoothed by measurement

- **A dip at the construction's air mode.** Measured with the new
  `Tools/MeasureNoteEvenness.py`, the note at each Shape's air mode stood
  3-6 dB over its neighbours (F#2/G2 on the Dreadnought, G#2/A2 on the
  Auditorium, A#2/B2 on the Parlor), over the balanced observation blend
  too. The summed microphone pressure now takes a -2.5 dB, Q 2.5 dip at the
  construction's own air mode, following Shape and Wood, on every
  construction whose air mode sits under 117 Hz (all but the Parlor); the
  piezo never passes through it. The Dreadnought's G2 falls from +3.1 to
  +2.0 dB over its neighbours with E2 rising from -4.3 to -3.1, the
  Auditorium's G#2 from +5.5 to +4.6 and its Maple box's A#2 from +5.1 to
  +4.3, the Jumbo's E2 from +2.6 to +2.3. The default phrase loses 0.24 LU,
  inside the construction loudness contract, and the dipped constructions
  are re-levelled to it. Since 2026-10-12 the Parlor takes the dip too
  (B2 +5.7 to +4.6 dB over its neighbours at Room 0.5), its Finger and
  Thumb cells sitting 0.5 LU over the calibration target so the
  construction loudness fixture stays inside its 3 LU (Known gaps). The
  body's 400-600 Hz modes still put one note 10 dB over
  its neighbours and the next 10 dB under (A#4 and C#5 on the Dreadnought):
  a damping candidate measured well but was dropped, since the model audit
  pins the radiation to each mode's measured damping. Authored and
  measured, then heard: on 2026-10-12 the user preferred this engine (B)
  over main's (A) on a blind pack of the phrase, seven single notes and an
  A4-D5 melody on the Dreadnought and Auditorium (Docs/decisions.md). See
  the [report](../research/studio-smoothness-2026-10-11.md) for the before and
  after tables and the pack.
- **Sustain, a host-automatable switch** beside CHORDS and PANIC
  (`Performer::setSustainHold`): on, every released note rings on exactly as
  under the CC64 pedal, on every channel, until its string is taken or the
  switch goes off, which damps the held notes as a pedal-up does. The
  switch and the pedal hold independently; Reset All Controllers lifts only
  the pedal, and Panic clears the strings and leaves the switch where it
  was. Off, the default, is an exact no-op, and a session saved before it
  existed loads it off. The Rack Extension's Sustain property already
  spells CC64, so it needs no change.
- **Release Noise and Room reach the panel.** Both were host-automatable
  only; the Player panel now holds four knobs (String Age, Pluck Position,
  Touch, Release Noise) and Capture & Output five (Body, Stereo, Piezo,
  Room, Output), on the same grid. Parameters, defaults and saved states
  are unchanged. The screenshot above is painted by the new
  `AcustraEditorSnapshot` (Tools/EditorSnapshot.cpp), the editor off screen
  at its design size, so a layout change can be looked at without a host.
- **Gather Chords no longer loses key-ups or regroups them.** A full
  gathering queue dropped whatever arrived next, a Note Off, pedal-up or
  All Notes Off included, and the string stayed held for ever; it now drops
  the oldest queued continuous gesture instead. A gathered Note On was
  pulled ahead of an earlier key-up of the same key on its channel (stray
  once another note had taken the key's string), which then released the
  new note under a held key; a released key now ends the chord. MPE member
  channels are no longer gathered, so a member's CC74, pressure and bend
  stay ahead of their note. The channel-mode messages CC124-127 also act
  as All Notes Off, as MIDI 1.0 requires. Each fix has a Performer test
  that failed on the previous engine.

### 2026-10-10 — the strings' loss angles

- **Constant loss angles.** Every string carries a dislocation loss angle in
  place of the authored broad shelf. The wound strings add winding friction,
  a constant loss angle on their tension. Both planes lose alike, and the
  plate floor meets the bridge at steel level. See
  [the report](../research/string-hf-loss-2026-10-10.md).
- **Picked wound strings keep their top end short.** Heard blind, friction
  alone let a pick's 4-16 kHz on the wound strings ring 16-27 dB over the
  previous engine for 0.4 s; the listener said "absolutely not" on the picked
  open strings. The wound strings now keep half their former bending loss
  beside half the blend's friction. Picked, their 4-16 kHz matches the
  previous engine. Fingered, their brightness sits halfway between it and
  the blend. A blind A/B approved it.
- **String Age glides** instead of stepping, and keeps a fixed deadline under
  per-block automation.
- **The constructions are re-levelled** by
  `Tools/CalibrateConstructionLoudness.py`.

### 2026-10-10 — bridge-hand damping follows slides and bends

- **Bridge-hand damping follows slides and bends.** CC2 pressure now keeps
  the hand's decay rate consistent as the speaking string changes pitch,
  including vibration retained when pressure changes mid-note. Unmuted
  renders are unchanged. The correction is shared by the plug-in and its
  Reason adapter; it adds no controls or changes to stored parameter values.

### 2026-10-10 — the strings end on the bridge

- **The strings end on the bridge.** The model stood the short segment behind
  each saddle as a spring to ground, which drained the strings at 6-34 times
  the bridge's conductance above the body's lowest modes. It is gone, and the
  strings drain into the measured bridge alone. The Dreadnought's air mode
  moves to 94 Hz and its first top mode to 176 Hz, where recorded
  dreadnoughts ring. The release thump keeps the level a listener chose. In a
  blind A/B the listener preferred it on 9 of 12 passages and approved it;
  strumming with the Pick went the other way. See
  [the report](../research/saddle-termination-2026-10-10.md).
- **Shape's plate modes keep their level.** Plate modes above T1 take the
  top's mass as they move. Against the old law, before the re-levelling, the
  Parlor's 315 Hz-10 kHz bands rise 4.4-5.1 dB, the Auditorium's 1.8-2.4 dB,
  and the Jumbo's fall 1.3-1.7 dB. Not heard on its own.
- **The constructions are re-levelled** for both changes, by
  `Tools/CalibrateConstructionLoudness.py`. The default construction plays
  1.28 LU quieter until the capture voicing is refitted.
- **Body, Width and Output settle exactly** at their targets instead of
  stalling on a subnormal.
- **The calibration tools compare like with like.** Each recording is
  scored against the capture it matches, not against the cancelling L/R
  average of the spaced pair
  ([the capture-observation record](../research/capture-observation-2026-10-10.md)).
  No shipped voicing has changed yet.
- **A two-microphone Room was heard and not adopted**
  ([the room report](../research/room-2026-10-10.md)).

### 2026-10-10 — Classical 78 removed

At the user's request the second guitar model, the steel-strung Classical 78
(Mores archive g35), is removed with its measured body and bridge banks, its
microphone contour and its classical-only attack, sustain and player-loading
policies. The Original is the only guitar body; its sound is unchanged in the
matched native comparison. The `guitarModel` host parameter remains as a
hidden, non-automatable legacy slot, so saved sessions and index-addressed
automation keep working, and a session that chose the Classical 78 plays the
Original. See [the removal record](../research/classic78-removal-2026-10-10.md).

### 2026-10-09 — project cleanup

Removed rejected, shipping-zero DSP paths and their 13 inactive calibration
fields, obsolete tooling, archived experimental implementations and dead Rack
panel code. Current measurements and fitting references remain, alongside
saved-session compatibility and historical evidence. The accepted attack and
sustain tuning is preserved exactly in the matched native comparison. See
[the cleanup record](../research/project-cleanup-2026-10-09.md).

A concise ledger of the changes that move what Acustra sounds like or how it is
controlled. Pure refactors, deduplications and test-coverage additions are in
git history rather than here.

### 2026-10-04

- **Ten playing and physical behavior improvements, awaiting listening review.**
  Stroke brightness follows force; slide geometry and partial loss follow the
  speaking string; body and sympathetic tails survive live control changes;
  voicings fit a conventional four-finger grip; equal-velocity repeats vary
  within a bounded force range; rapid strums finish their traversal; explicit
  CC65/CC84 finger transitions retain the sounding wave; and key-up firmness
  controls passive damping. CC68 remains ignored. Each improvement has its
  own commit and regression. All 115 real-recording rows in each of the two
  presets retain byte-identical isolated-note renders and unchanged descriptor
  losses. The original-song blind A/B package evaluates the changed performance
  behavior; its scores never approve a change automatically. See
  [the ten-change evidence and listening protocol](../research/realism-ten-improvements-2026-10-04.md).

### 2026-10-01

- **The top springs back from the held string, at the user's request**
  ("sounds better, pls merge and continue improving", on the entry below).
  The static force the hand held each string aside with is released at the
  pluck, so every note rings the body's air mode and low top modes as the
  Eastman E1D and Martin HD28 recordings do, more for the Pick than the
  Finger and Thumb and growing with velocity; the capture voicing was
  refitted after it (its bass lift +1.1 to +1.5 dB at 80-125 Hz, from +3.3
  to +3.8) and every construction re-levelled. A listener liked the thump
  and chose it at 80% of the full release ("i like the thump but i would
  maybe make it 80% of the current strength"), and kept it on hearing it
  ("80% is better, keep it and merge to main"). Against main at Room 50%:
  Eastman picked -8.1%, Eastman finger -4.7%, Martin -3.6%, the bank's
  flat-top -5.8%, GuitarSet's log-spectral error 14.00 -> 13.93 dB; the
  archtop training and validation rows +1.4% and +3.9%. See
  [The top's spring-back](../../README.md#the-tops-spring-back) and Docs/decisions.md.
- **The microphones as a recording hears them, and a room around them, at
  the user's request** ("further improve realism of the instrument audibly
  by multiple techniques, make the sound more pleasant, natural, balanced,
  benchmarking with real audio"). The Original's capture voicing - +3 to +4
  dB at 80-125 Hz, -6 dB at 250-500 Hz, +5 to +7 dB at 1-1.6 kHz, fitted to
  the Eastman E1D, Martin HD28, bank flat-top and GuitarSet recordings - and
  Room, a small studio's reflections and 0.45 s field around the
  microphones, 50% for a new plug-in instance and 0 for an older session.
  Against main, with Room at 50%: training -9.7%, development validation
  -8.4%, Eastman picked -8.9%, Eastman finger -3.8%, Martin -9.4%, the
  bank's flat-top +0.3%, GuitarSet's log-spectral error 14.42 -> 14.00 dB,
  convergence 0.828 -> 0.804 and chroma 0.224 -> 0.210; note to note 3.3 ->
  2.7 dB rough. Every Original construction's sound changes (each was
  re-levelled to its loudness); the Classical 78's microphones are unchanged. See
  [The microphones and the room](../../README.md#the-microphones-and-the-room) and
  Docs/decisions.md.

### 2026-09-30

- **Expressive repertoire demos, at the user's request** ("create a few demos
  in WAV (max quality) - various pieces by tarrega, renaissance authors,
  baroque, bach, then also some pieces using multiple different guitars
  playing various parts"; then "ensure the play is: felt (difference between
  quiet and loud parts, tempo change, slowdown on phrase end)", "humanize
  timings, make the performance human-like, not machine" and "use 80% of
  velocity range when playing and change tempo 50% up or down"). Fifteen
  pieces at 96 kHz / 24-bit, four of them for two to four differently built
  guitars; see [Expressive repertoire demos](../../README.md#more-demos).
  New tools only (`Tools/PerformRepertoire.py`, `AcustraRenderRepertoire`);
  the engine is unchanged.

### 2026-09-29

- **Steel strings only, three woods and one bridge, at the user's request**
  ("simplify acustra: remove nylon model. only keep 3 most different woods.
  cleanup all leftovers."; then "the bridge choice does not do much
  difference"). The String Material and Bridge Model controls are gone; Body
  Material is Spruce, Mahogany or Maple (Cedar, the closest of the four to
  Mahogany, is removed). The Classical 78 stays, strung with steel. The
  Guitar menu's Classical nylon and Fylde bridge / steel presets give way to
  one preset for the Classical 78 (Auditorium, Mahogany). Saved sessions
  migrate: Cedar loads as Mahogany, and a session that played nylon or the Fylde
  bridge plays steel on the Original bridge (on the Classical 78 if it chose it),
  so it sounds different. Every steel construction that remains sounds
  exactly as before, bit for bit. The nylon demo is gone; Recuerdos and
  Lágrima play on the Classical 78 with steel strings, and the picking-techniques
  demo on steel alone. The nylon calibration, the g34 and Fylde banks, the
  classical rows of the dry-note benchmark, the Iowa corpus and the nylon-only
  tools and reports are removed.
- **A Piezo Mix knob, at the user's request.** It mixes the under-saddle
  piezo, at the level Capture = Piezo gives it, beneath whichever microphone
  Capture selects, the same signal on both channels. At 0%, its default and
  what an older session loads with, the output is unchanged; with Capture on
  Piezo it does nothing. The separate Piezo output is unchanged.
- **An audit of the engine, at the user's request** ("walk through the
  engine and fix any gaps, obvious issues, and ensure compatibility between
  different woods, models"). What a player hears change:
  - Playing: released natural harmonics and slid notes now damp like the
    string they sound; a short strummed stab sounds every string (its upper
    strings used to vanish); a member-channel bend's tension stops at six
    semitones and a wider glide carries on as a slide instead of blowing up
    26 dB; All Notes/Sound Off acts before the notes on its own sample, so
    those notes sound; Reset All Controllers also resets the wheel's vibrato
    and channel pressure; master tune reaches an MPE note once.
  - Constructions: Shape keeps each of the Classical 78's bridge modes on its
    radiation; nylon's and the Classical 78's bridges follow Wood and Shape; the
    Classical preset is nylon's measured body at the cedar it was built of
    (+31 cents, brighter than the cedar warp it was); a Shape, Wood, Bridge
    or Model change under a ringing chord no longer ticks, and a change onto
    steel strings no longer swells; a Bridge choice a construction does not
    play changes nothing; an instrument left to ring out reaches exact
    silence.
  - Unchanged: the default construction on its microphones, bit for bit,
    wherever those playing fixes do not reach. Nylon changes by the body
    fixes and by its level (below). Test, tool and documentation fixes
    change no sound. What is left open is in Known gaps.
- **Every construction and Picking plays as loud as the default, at the
  user's request.** A gain per Strings x Model x Bridge x Shape x Wood x
  Picking cell, measured as BS.1770 loudness, brings each within 1 LU of
  the default construction (before: about 15 LU apart), with the mono
  microphone and the piezo level with the stereo microphones on every one.
  The default construction's microphones are unchanged to the bit; every
  other construction changes only in level, so saved sessions on them play louder or quieter: nylon at
  the default body +2.1 dB, the Classical preset +3.1 dB, steel on the
  Classical 78 up to +9.9 dB, the Fylde Jumbo with the Pick -5.3 dB. The
  default's Pick plays 1.25 dB quieter, its piezo 3.45 dB louder with the
  fingers. A change of any of them glides over 20 ms.
- **Steel plays a lighter body that stands in for the one Set 21
  preferred.** The user asked for the cheaper CPU option, as close to their
  preference as possible. B's bridge stays whole, D is at 0.85, and only
  the jointly fitted body's 9 modes below 550 Hz play, at 0.075. It keeps
  83-85% of the midpoint's measured move away from B+D, for +2-4% CPU over
  B+D instead of +40-55%. It has not been heard against the midpoint.
- **The piezo is modelled as its real circuit, at the user's request**
  (after Set 20 preferred the analog piezo but found it "maybe too much").
  Blind Set 22 chose it over the previous piezo on all five pieces.
  The saddle is a 3.8 g mass on its element over the measured bridge (a
  6 kHz resonance of Q 3.3, +10.6 dB), the element a 0.2 V/N charge source
  into its cable, and the preamp ESP Project 202's OPA2134 circuit into a
  1 MOhm DI, solved as a circuit with its op-amp limits, Shockley diodes and
  a band-limited clip. Against the chain it replaces, Set 20's pieces have
  1.6-4.6 dB less in the 4 kHz octave, 2.8-5.6 dB more in the 8 kHz octave
  and 3.7-7.8 dB more in the 63 Hz octave (loudness matched): the honk is
  softer and the top less dark, and the bass is the preamp's, whose only
  audible low corner is 31 Hz rather than the old load's 177 Hz. Level-matched
  to the microphones again; the microphones are unchanged down to the bit.
- **The piezo is an under-saddle pickup with an onboard preamp, at the
  user's request.** Piezo, on Capture and on its own output, now weights each
  string's saddle force, rings the saddle's mass on the element at 5 kHz,
  drives the measured electrical load at a calibrated 1 V for the hardest
  Pick strum (a Finger strum at a third of that),
  and passes an onboard preamp's buffer, soft knee and output coupling. Its
  level now matches the microphones', so sessions on Capture = Piezo play
  louder and sound different, with the saddle's resonance 9.7 dB up at 5 kHz:
  about 8 dB louder on steel, and about 15 dB on nylon, where this stacks
  with nylon's own 5.5 dB rise below.
  The microphones are unchanged down to the bit. The values chosen by ear
  await a blind comparison with the previous piezo.

### 2026-09-28

- **A separate Piezo output, at the user's request.** Alongside Main, which
  still follows Capture, the saddle piezo can be taken as an output of its
  own, rendered in the same pass: in the plug-in as an optional auxiliary bus
  (off by default), and through the engine and player for the Rack
  Extension's back panel. Main is unchanged down to the bit whether or not it
  is enabled, and costs the same when it is not.
- **Nylon plays as loud as steel, at the user's request.** Nylon's output
  reference rises by 5.47 dB, the median loudness (BS.1770) it measured below
  steel over the factory constructions strung with each material and typical
  playing with Finger and Pick. Over that grid the two medians are now equal;
  the nylon presets themselves remain 3.5 dB below the steel presets, since
  their Auditorium cedar body is itself quieter than the Dreadnought. Saved
  sessions that play nylon get louder too, as asked. Steel is untouched, and
  nylon changes by that one gain: every nylon sample is the old one times
  1.8774 to within one float rounding.
- **Legato is gone, at the user's request.** CC68 no longer switches the
  fretting hand to hammer-ons and pull-offs, and release velocity no longer
  lifts a finger: every note is plucked, every key-up damps, and CC68 is
  ignored (it also no longer ends a gathered chord). The fretting-hand demo
  went with it. Everything played without CC68 renders exactly as before.
- **The snap is confirmed by ear, steel plays one body at the saddle, and
  the plate-Q rule reaches down to 150 Hz.** In a blind pair the listener
  chose the snap on every piece. Steel's presets and new sessions now play
  g21's own bridge measurement. Each bridge mode that is the same resonance
  as a radiation mode (18 of 47) sits on that mode's pole. The whole bridge
  is scaled to a steel-string guitar's mobility, measured on the Fylde at
  0.274 of the flamenca's. A partial on a body peak now blooms and drains
  instead of ringing on through a band-pass filter. T1, the 209 Hz rocking
  mode and the 229 Hz mode now take the anechoic flamencas' damping, as the
  plate modes did (the rocking mode's Q 37 to 27). The listener chose both
  changes, and they ship together. Against the previous engine every
  never-fitted corpus reads closer (Eastman picked 1.7%, finger 0.8%,
  Martin 2.7%), and so does the frozen test split (Finger 1.0%, Pick 1.1%).
  GuitarSet's log error falls from 14.88 to 14.73 dB. Picked notes' prominent
  partials now decay 2.2 dB/s faster than their neighbours, against 1.0
  before and 4.0-5.4 on the recordings. G2's fundamental rings on the air
  mode, E2's is 5-8 dB lower, and the one-second chord pull is 8.6 cents
  (Known gaps). A steel strum costs about 5% more CPU, 8% on a held chord.
  The Fylde bridge stays selectable.
- **The strings' upper partials die faster, and the pluck is rebuilt around
  them, chosen by ear.** Each string now loses its high partials through its
  own bending stiffness (Valette; Woodhouse 2004): steel wound 0.035 and plain
  0.0023, about a third of the rate the recordings' first 300 ms measure, and
  nylon's wound strings at their recordings' rate. Steel's pluck moves with
  it: a narrower contact, a louder finger burst and, under Pick, a
  release-velocity share of 0.58 at full velocity with a 0.116 mm edge. A
  blind listener preferred both a half-measured loss with its refitted pluck
  and the benchmark's own optimum to the previous engine and asked for 70% of
  the way from the optimum toward the half loss; that is what ships. The
  benchmark reads steel 3-6% further from the recordings (the frozen test
  split +4.0% Finger, +4.8% Pick) and classical rows 5-6% closer; with each
  recording's hiss floor added steel is level. The pluck's contact noise and
  click stay at zero, rejected by ear as too loud, and cost nothing there: the
  string's bending section now costs a few percent of a 48 kHz block (a
  held chord 40.6 -> 39.6 us with the loop inlined; the Rack Extension's
  build 41.2 -> 42.8 us), every sample unchanged by that rearrangement.

### 2026-09-27

- **A plectrum lets go as the string slides round its edge.** The held force
  unloads over the time the string's own motion takes to cross a 0.15 mm edge,
  so a soft stroke is released darker than a hard one: the picked archtop's
  soft-to-loud brightness rise is 4.1 dB where it was 0.7 (the recordings'
  8.4). Every Pick reference reads closer - archtop training 6.3%, the frozen
  test split 4.2%, the Eastman dreadnought's picked take 2.0%; the refit
  around it drops the pick's release-velocity share and transient. Finger and
  Thumb are unchanged.
- **Steel's plate modes are damped to the anechoic flamencas'.** g21, the
  measured body behind the steel presets, rang with more Q in every band above
  its top-plate mode than five flamencas of the same archive measured
  anechoically; each mode now takes their median Q (the 655 Hz mode 60.6 to
  46.6), with frequencies, levels, the air mode and T1 as measured. Flat-top
  rows, both Eastman takes, the Martin and GuitarSet read closer, and so does
  the frozen test split under Finger and Pick; development validation reads
  0.5% further. Removing g21's music room from its measurement was also tried:
  the ringing is the body's own, not the room's, and that change does not ship.
- **The fretting hand remembers where it has been.** Live notes are fretted
  within reach of the hand the sounding and recently released notes define (a
  four-fret position with a one-fret stretch, after Heijink and Meulenbroek
  2002), then on open strings, then low frets, the order the listener asked
  for; a chord rolled in within 30 ms is re-fretted as one shape. With nothing
  fretted for two seconds the previous allocator runs exactly, so single notes
  are unchanged.
- **CHORDS gathers live keyboard chords**, off by default: while on, every
  MIDI event waits 30 ms (reported to the host as latency) and a chord rolled
  in inside that window is voiced as if sent on one sample.
- **Stereo Width stays at 0.62**, chosen by ear against 1.0 ("it is close").
- **The engine costs about half as much CPU, with every sample unchanged.**
  Strings whose inputs have not changed keep their configuration, the body
  bank advances four modes at a time and skips empty slots, strings share
  completed dispersion solves, and the math the Rack Extension toolchain
  would turn into library calls is inline and exact. A held chord at 48 kHz
  costs 40 us per 64-frame block where it cost 76 (host build); every
  benchmark, corpus, performance and allocator render is byte-identical.

### 2026-09-25

- **Three blind verdicts give steel a recorded guitar's body.** Stereo mic
  now pairs the treble-bridge microphone with the upper-bout one, the
  bridge/twelfth-fret placement, where it paired the two bridge
  microphones. A Finger meets a steel string 149 mm from the bridge, at the
  soundhole's edge, where it met it at 74 mm; Pick follows it to 60 mm. And
  steel's air mode radiates four times stronger at the bridge microphone,
  which alone under-heard it. The picked E2 of a real dreadnought recording
  and the engine's now stand the same against their harmonics, within 1 dB.
  All three were chosen by ear on every pair. On the benchmark the
  finger-played flat-top rows read 2.0% closer (Fylde). The picked archtop
  rows read 5-6% closer when rendered with Pick and 0.5-12.6% further with
  Finger. The classical rows, now heard partly through the upper-bout
  microphone, read 4.3% further on training and 0.6% closer on validation.
- **The plectrum is refitted at its new place**: a smaller release share and
  a faint pick transient (Pick training -0.5%, validation -0.7%).
- **The bridge keeps its tuning at every host rate.** Its modes run in double
  precision. A float's direct-form coefficients held the lowest modes' pole
  angles to about 5% at 384 kHz, which turned the heave's fundamental 90
  degrees against the rock and raised the upper-bout microphone's low E by
  6 dB. It already moved 1-2 dB at 96-192 kHz.
- **The Washburn, Santa Cruz and Martin models are retired.** Their
  coefficients were fitted from Mark Rau's measurements, which carry no
  redistribution license, so they are not published with this repository.
  Model offers the Original and the second measured guitar (called the
  Classical 78 here); a saved session or host that still selects a retired
  model hears Original.

### 2026-09-24

- **Nylon is plucked where a classical player plucks.** The fit had been held
  within about 107 mm of the bridge while the classical recordings were
  played near the soundhole (their open G and B carry almost no third
  harmonic); with the bound widened, nylon's finger lands 178 mm from the
  bridge at the default Pluck Position, the fundamental comes up against the
  second and third harmonics (from 3.5-4.2 dB under the recordings' balance to
  1.6-2.6 over), and the classical rows improve on training (7.554 → 7.164)
  and development validation (6.911 → 6.418).
- **The steel body rings like wood.** The measured flamenca body steel plays
  is now fitted from 250 ms of its impact responses instead of 62.5 ms, a
  window so short it had set the damping of every mode below 300 Hz: the air
  mode now rings at Q 19 instead of 6, the main top mode at Q 17 instead of
  10. A listener comparing the instrument with real recordings heard it lack
  "the massive wooden resonance, low-mids, resonating with the string"; the
  resolved body puts 9 dB more of a steel note's energy below 120 Hz and
  improves every benchmark split (Fylde training 6.182335 → 6.000217,
  development validation 6.097915 → 5.991366, flat-top 7.556267 → 7.328740).
- **A pluck is released at the force the hand holds.** The same stroke now
  displaces a stopped string less than an open one, as a point force on a
  shorter string does, so notes up the neck no longer grow louder and glide
  further than a real guitar's; a blind listener chose it on every
  up-the-neck pair. Benchmark neutral (Fylde bridge 6.180182 → 6.182335,
  6.103707 → 6.097915, 7.572502 → 7.556267), pitch trajectory better.
- **Three blind verdicts set what the benchmark could not.** High fretted
  notes now keep their pitch as they ring: the parallel polarisation's 0.8 mm
  end correction, which let the flatter member take over within half a
  second, is zero. Nylon notes ring longer (fundamental T60 scale 1.4 over the
  fitted 1.038), as the classical bank's open strings do. Steel's default
  Dreadnought is the wider, bassier box the local line authored (air 98 Hz,
  modes x0.900, bass x1.28). A blind listener chose all three (Docs/decisions.md);
  the benchmark reads the never-fitted flat-top rows 7.948071 -> 7.572502 and
  training and development validation 6.112127 -> 6.180182 and 5.928819 ->
  6.103707 (Fylde bridge).
- **A retaken string keeps both planes under the hand.** The wave a
  repluck or a chord change leaves behind is carried and damped in the plane
  parallel to the top as well as the normal one, instead of being dropped;
  that plane holds most of a pluck and radiates through the rocking saddle.
  Single notes are unchanged (the benchmark renders byte-identically).
- **A steel pluck leaves the string moving mostly along the top.** A
  finger's free stroke and a pick both cross the strings parallel to the
  soundboard and press in only partly, so on steel the normal plane now
  takes 0.30 − 0.08·Touch of a pluck's energy (0.25 at the default Touch)
  instead of 0.91 − 0.08·Touch. With both planes radiating, a sweep of that
  share improves every steel split down to about this value (Fylde bridge
  training −0.39%, development validation −2.38%, flat-top −3.46%; Original
  −1.10%, −2.78%, −5.13%), and a blind listener preferred it on both steel
  pairs. Nylon keeps the normal-led share: the classical rows split on it and
  the listener preferred the normal-led pluck on every nylon pair. Hammer-ons
  and pull-offs keep the energy a pluck puts in the normal plane; steel's
  output reference rises 5.41 dB so sessions keep their loudness. The
  plectrum was refitted for the new release (0.0625·v^0.47, the Finger
  burst; picked archtop training under Pick 6.334019 → 6.284861,
  development validation 5.941904 → 5.904206).
- **Both string polarisations are heard.** The plane parallel to the
  soundboard pushes the saddle crown sideways at its published height, a
  moment on the measured rocking, and reaches the microphones through the
  measured moment paths, so each partial becomes the slowly beating doublet a
  real guitar's is instead of one smooth exponential. Both planes share one
  string length, so the doublet's width is the bridge's pull on the normal
  member and changes note to note; where the saddle rocks, the coupled pair is
  tuned by its sustained centre. Nothing is fitted. Every split of the
  benchmark improves: with the Fylde bridge training 6.268767 → 6.148927,
  development validation 6.172187 → 6.008802, flat-top 8.591335 → 8.232583;
  with the Original bridge 6.392396 → 6.167890, 6.378758 → 6.042989 and
  9.256751 → 8.559757; under Pick 6.546029 → 6.308471 and 6.256259 → 5.950761.
- **The two lines of development are one instrument again.** The measured
  guitar models, three-choice capture, continuous Gaussian contact, steel
  contact transport, release and picking-character work join the coupled
  top/cavity body shapes, the plectrum's release velocity and the measured
  Fylde bridge on the steel presets. Shape now moves the bridge's modes by the
  same coupled-model factors as the radiation, and each named guitar is
  unwarped at its own family's box. Original's anchors stay the transform
  both calibrations were fitted on, which the development rows prefer by
  3.3% on steel over the wider authored Dreadnought the local line had
  introduced, while training and the flat-top rows prefer that wider one by
  2.2% and 4.7%; the default Dreadnought therefore sounds as it did before
  the local widening. The plectrum was refitted for the merged Pick contact
  (0.3125·v^0.93, transient 0.125). Benchmark with the Fylde bridge:
  training 6.268767, development validation 6.172187, flat-top 8.591335.

### 2026-09-10

- **A plectrum releases the string with velocity.** Under Pick the string
  leaves the tip carrying a velocity over the contact width beside the fitted
  displacement, its energy share growing as v^2.93 to equal the
  displacement's at full velocity, with a pick-speed contact transient the fit
  left at 0.078 of the Finger burst. Fitted on the picked archtop recordings
  across their four layers, written in the frame the calibration was fitted
  in, read by Pick only: archtop training 6.296551 → 6.189238, development
  validation 6.376421 → 6.273555, the never-fitted 188-row test split
  6.264245 → 6.204019; the loud layer's H1–H3 excess and H9–H12 deficit close
  by about half and the soft layer stays put. The calibration grows to 32
  values, the fit renderer gains `--archtop-picking` and the optimizer a
  `pick-release` stage, `--stages`, `--start shipping` and `--archtop-picking`.
- **Steel plays the measured steel-string bridge.** The Dreadnought, Auditorium
  and Parlor presets, a new session and the demos select the Fylde bridge,
  which with the unchanged string calibration sits closer to the recordings on
  every dry-note split (training −1.8%, development validation −3.4%, the
  never-fitted flat-top rows −7.1%). The fitted flamenca bridge remains the
  Original bridge / steel preset and the benchmark's reference; sessions saved
  before the bridge parameter existed keep it.

### 2026-09-09

- **Shape is a box.** Parlor, Auditorium, Dreadnought and Jumbo re-couple the
  measured body's A0 and T1 through Christensen and Vistisen's top/cavity model
  for the published dimensions of a Martin Size 0, a Martin 000, a D-28 and an
  SJ-200, and scale the plate modes above T1 with the top; the earlier
  authored air-mode, mode-scale, bass and volume factors are gone. Steel's
  Dreadnought and nylon's Auditorium (the Classical preset) are the anchors
  their calibrations were fitted on and are bit-identical, as are all 79
  benchmark renders; the nylon Dreadnought, and steel's Parlor, Auditorium and
  Jumbo, are now physically spaced around them. The fit renderer renders each
  material at its anchor (`--shape` overrides it), gains `--archtop-picking`
  for the tool on the picked archtop rows, and records both in its manifests.

### 2026-09-08

- Steel picking contact now travels from the picking point toward the bridge
  and nut, with pending waves retained and damped correctly during release and
  repicking. Fixed recording comparisons show small attack improvements;
  [qualification and limits](../research/contact-travel-2026-09-08.md) include native,
  plugin, audio-invariance and callback checks. No new control is added.
- Capture now offers only Stereo mic, Mono mic and Piezo for both steel and
  nylon. Mono uses a single measured microphone response in both channels;
  piezo always includes its electrical load. Retired captures migrate on state
  restore, and the magnetic and unloaded-piezo output paths are removed.

### 2026-09-05

- Body changes preserve overlapping re-pluck tails. Rapid construction changes
  finish the sounding crossfade before applying the latest choice, avoiding
  abrupt loss of body vibration.
- Added optional Loaded piezo capture from published pickup/preamp component
  measurements. Existing Capture automation and old sessions retain their
  mappings; the simple menu and capture demo now cover all seven observations.
- Removed unintended zero clipping after pluck-contact smoothing, preserving
  its specified harmonic response and eliminating rectification artifacts.
  The selected stereo body, contact width and pluck timing remain intact.
- Guitar preset selections now survive an intervening display refresh, so the
  timer cannot silently discard a queued menu choice. Restoring a saved state
  into an open editor also verifies its guitar, picking and capture controls.
- Added stereo and individual microphone capture, saddle piezo and steel-only
  magnetic pickup, with smooth switching during sustained notes.
- Added Finger/Pick/Thumb contact ranges and construction presets for
  Martin-style dreadnought, Taylor-style auditorium, Fender-style parlor and
  classical nylon. A compact setup row keeps the existing sound controls
  visible; prior sessions retain their default sound and automation IDs.
- Added paired recording comparisons that verify reference identity and
  report material-specific changes; render manifests retain calibration values.
- Added picking and capture audio demonstrations.
- Added a six-player, 315-note real-performance benchmark with fixed excerpts,
  descriptor scores and reproducible, level-matched listening exports.
- Added an optional measured Fylde steel bridge in the Guitar menu. Paired dry
  descriptor losses improve 2.5% on training, 5.5% on development and 7.9% on
  flat-top recordings; original sessions and nylon remain unchanged.
- Corrected string-loss filter conversion between sample rates, retaining the
  exact 48 kHz sound. Pitch-tracked upper-partial decay spreads fall from
  19–33% to 4–13% across 44.1/48/96 kHz; residual frequency warping remains.
- Added the measured upper microphone for both string materials. The Capture
  menu stays simple; existing audio, saved sessions and normalized capture
  automation retain their previous behavior.
- Held notes now wait for their scheduled pluck during repeated chord sweeps.
  All Notes Off and legato releases cancel queued attacks without deleting
  the preceding vibration that is still allowed to ring.
- Corrected the bridge load of retained re-pluck waves, including their
  captured bend impedance. The transition remains an approximation of hand
  contact. Real-performance descriptor changes are small and mixed; isolated
  dry-note renders remain identical.
- Corrected hammer-on and pull-off wave states: finger motion now acts at the
  fretting end with the intended direction, and released displacement starts
  at rest. This removes an unwanted cross term between the hammer's shape
  and velocity, without changing its energy budget or finger-speed map.
- Corrected first-fret action coordinates and the open nut in hammer-on and
  release geometry. Low-fret releases now use the full supported-string
  clearance; setup heights and the velocity map retain their existing values.
- Adopted the wider stereo body selected in the listening comparison. The
  microphone paths now retain measured phase and respond to both bridge force
  and rocking moment. Pickup observations and string mechanics are unchanged;
  existing sessions acquire the new microphone tone and spatial presentation.

### 2026-09-04

- **The calibration the listener chose ships.** A bounded pattern search over
  the 115-note benchmark converged on a new vector; rendered level-matched
  against the shipping build with the letters unread, the user chose it by
  ear. Two of its values are pinned back to what the repository measured
  rather than to what the search wanted — the bridge mobility to the
  archive's own measured value, and the saddle-to-anchor length to DAFx-26's
  published 3.25 mm stub — and the pinning costs 7.9%, 3.0% and 16.8% of the
  three splits against the free fit. The audible consequences it does carry:
  the body's Q scale halves, so 77 of steel's 96 measured modes sit on the
  Q=4 floor where 29 did, and the steel attack-pitch cue rises from 4.0 to
  7.8 cents at full velocity.
- Benchmark: training 6.319236 → 6.111540, development validation 6.327235 →
  6.072344, flat top 7.948337 → 8.073304 — better on both fitted splits, 1.6%
  worse on the eight flat-top rows, which are reported and never fitted.

### 2026-09-03

- **Nylon's bending stiffness is Woodhouse's own measured per-string table**
  (Acta Acustica 90, 2004, Table I), not a fitted diameter-based formula; the
  engine's post-change per-string bending coefficient sits within about 14%
  of his published values on every string.
- **Nylon plays a measured classical guitar, not the flamenca blanca.** A
  1971 Manuel Contreras (measured anechoic) supplies nylon's bridge and body;
  steel keeps the flamenca (g21), adapted to steel strings. The earlier claim
  that g21 itself was steel-strung was incorrect.
- **Strums draw one shared pick-speed per stroke** rather than independent
  per-string release jitter, so a strum's strings can no longer cross order
  mid-stroke; the level jitter is retuned to the corpus's own measured
  spread.
- **The string loop reads its fractional delay losslessly**, through a
  second-order Thiran allpass rather than an interpolated read. This removes
  a loop-length-dependent upper-partial decay rate that used to range 47:1
  in loss per round trip across the fretboard; it is now a smooth 1.8:1 rise
  with the partial's own frequency, and no note's decay varies more than 13%
  across sample rates.
- **The bridge has two ends.** Each string now terminates at its own point
  along the saddle on a two-degree-of-freedom (heave + rock) bridge model,
  fitted from both of the archive's bridge-end measurements instead of the
  treble side alone, and the six saddle-to-anchor stubs form a matching 2x2
  stiffness matrix instead of one scalar sum.
- **The string's two transverse polarisations split by the measurement.**
  Woodhouse's published 0.8 mm end correction, not an authored guess, so
  every note settles about 0.3 cents closer to its requested pitch.
- Changed the retained re-pluck loop's nominal T60 from 160 ms to 10 ms.
  The September 5 source audit corrected the original claim that this was
  a measured post-release decay time; the cited work describes pre-pluck
  damping instead.
- **CC1 (the modulation wheel) now moves the sounding pitch.** It previously
  modulated only the inharmonicity and the bridge-junction impedance and left
  the fundamental fixed; review caught it. An MPE member channel's own pitch
  bend is now a real string-tension bend (Grimes' published law) rather than
  a slide; the zone manager's bend and a conventional channel's bend remain
  slides.
- **MPE gains per-note pluck point (CC74) and channel pressure** (a
  vibrato-depth bias, never a pull-off's release energy), and an opt-in
  string-per-channel mode (MIDI Mono Mode On / Poly Mode On on the basic
  channel) for hexaphonic-pickup-style controllers.
- Benchmark: training 6.606652 → 6.319236, development validation 6.521525 →
  6.327235, flat top 8.120169 → 7.948337.

### 2026-09-02

- **The guitar plays in tune in chords.** The B string in an E chord read 23
  cents sharp for half a second and the worst note in five common chords 23
  cents off; both were the fitted 17 mm anchor spring resonating with the
  body between its modes. The anchor is now DAFx-26's 3.25 mm stub, the
  length at which chords pull least: the worst note sits 9 cents off early
  and 5 late on steel, 7 and 7 on nylon, and the low E decays at 7.9 dB/s
  inside the recordings' 4.5 to 8.6.
- **Idle strings load the bridge.** Every string is a member of the
  six-string junction, so sympathetic energy is bounded the way a real
  bridge bounds it: ten hand-damped repeats leave the idle string 44 dB under
  the note where it sat 7 dB over, open-string partials move 4 to 10 dB
  toward the recordings, a note no longer drifts sharp as its halo takes over
  (E5 at 1 s: +0.5 against +4.9 cents), and a taken string's tail rides in
  the junction. The picked benchmark splits improve to 6.607 and 6.522; the
  eight finger-played flat-top rows, whose over-damped strings the halo was
  masking, worsen to 8.120 (Known gaps).
- **Same-sample chords are strummed.** A chord on one sample used to pluck
  all six strings at once and identically, stroke after stroke (repeats
  correlated 0.98). The engine can now schedule a pluck: the string is taken
  and fretted at the chord's sample and released when the pick reaches it,
  bit-identical to a note-on issued then. The plug-in sweeps every group of
  three or more notes on one sample, low to high then back, at the pick's
  speed for the velocity, and restarts with a downstroke after a rest.
- **Every pluck lands in its own place**, within the ±0.02-of-length spread
  the reference recordings' round robins show, so repeated notes and strums
  are takes rather than copies; consecutive strokes now correlate −0.16.
  Every single note on a fresh engine moves with it: the three benchmark
  splits read 6.8066, 6.8697 and 5.7034 against 6.8353, 6.8238 and 5.6547,
  a random pluck point's worth either way.
- **An eighth demo**, strummed chords, and the other seven re-rendered.

### 2026-09-01

- **Gave the fretting hand its own sounds, from the MIDI a player already
  sends.** A hammer-on under CC68 now strikes: the finger drives a dent down
  onto the fret whose width the finger's speed sets, and note-on velocity sets
  that speed through the pluck's own velocity law, so a hammered note lands
  within +7 and −2 dB of a pluck at the same velocity where it arrived at 1 to
  6% of it before. Note-off velocity lifts the finger: above MIDI's unsensed
  64 the pressed string follows the rising finger and rings on at its new
  length, louder and brighter the faster the lift, to a full pull-off to open,
  reached on most strings well below 127;
  64 and below is the finger staying on the string, exactly as every note-off
  was. Pull-offs under CC68 are plucked by the leaving finger the same way.
  The heights are the published set-up dimensions and the speeds come from
  the energy law the pluck already obeys; the one convention is where the
  finger stops staying, and it is MIDI's own default. The corpus, which never
  releases a note, is bit-identical.
- **Every note-off stopped thumping.** The release loss now slews in over one
  round trip instead of stepping the wave the junction reads, which had let a
  low fretted note peak 4.6 to 8.6 dB above its own level as the key came up.
- **Repeated notes stopped clicking, piling up and wandering.** A held note
  replucked lands the picking hand on the string before the new pluck, through
  the same tail a taken string uses, where the one-sample contact projection
  had clicked into the limiter and let the near-node partials grow 15 dB over
  six replucks; and a note repeated after its key came up is replucked on the
  string still sounding it instead of hopping to another free string that
  could also reach it and leaving both ringing.
- **Added the fretting-hand demo.** `07-fretting-hand.wav` plays the
  hammer-ons, the pull-off and the three releases.
- **Corrected the benchmark record.** The committed report and its summary
  still quoted the scores of the longitudinal path at gain 0.025 after that
  gain had been set to zero; they now carry the shipping engine's own:
  6.8353, 6.8238 and 5.6547 on the three splits (the benchmark table's
  shipping column had been missed by that correction and now agrees).
- **Removed the pitched drip from string attacks.** The unmeasured axial-mode
  radiation was a narrow resonant ping at every pluck. Its mechanism remains
  available to calibration and measurement tools, but its shipping gain is
  now zero so the displacement release defines the attack without an
  artificial water-drop tone.

### 2026-08-31

- **Restored the next observable axial string mode.** The integrated extension
  drive projects onto the odd fixed-fixed longitudinal modes, so each string
  now carries its first and third axial resonances rather than truncating that
  wave after one pole pair. Rebalancing the shared longitudinal gain from
  0.030 to 0.025 improved the identical 115-note benchmark on all three splits:
  training 6.7685 to 6.7679, development validation 6.7750 to 6.7683, and the
  independent flat-top rows 5.6454 to 5.6139.

- **Gave the strings their longitudinal wave.** Stretching a string adds
  tension, and that tension resonates at the string's own axial modes — 1.5 to
  3.9 kHz for this steel set, computed from the construction data already in
  the model rather than chosen. Driven by the squared slope, what it adds are
  the sum and difference phantom partials, growing 7.3 dB faster than the note
  between a quarter velocity and near full. It is the first change today that
  improves all three splits at once: training 6.836184 to 6.768470, development
  validation 6.823805 to 6.774969 and the flat-top rows 5.655082 to 5.645406.
  Nylon's wound basses are excluded, because their published density is a
  transverse figure and does not fix their axial speed.
- **Settled how far, by ear.** A symmetric bracket around the adopted point
  found the letter half way back toward the fitted calibration clearly worst —
  a third independent confirmation of the direction — and the letter half way
  further indistinguishable from what ships. The corpus broke that tie in
  favour of not moving, since spending 3% of its score buys nothing the ear can
  hear.
- **Followed the flat-top rows on the body, by ear.** A listener heard the
  fitted build against the direction the eight Eastman flat-top rows prefer and
  reported the fitted one as the less realistic of the two, with the full
  flat-top direction a little too dull. Five values moved to the middle of that
  range: the body's low-mode gain 4.07 → 7.5 and residue tilt +1.86 → −1.0,
  steel's fundamental T60 1.267 → 1.53 and frequency loss 0.597 → 0.52, and the
  plate conductance floor 0.0063 → 0.011. Bassier, darker and longer-ringing;
  6.1% further from the corpus it was fitted to and 23.9% closer to the only
  flat-top guitar the bank holds. Recorded as chosen by ear, because it was.
- **Added legato on CC68.** A note a sounding string can reach is hammered on
  rather than replucked, and releasing it pulls off to what the hand is still
  holding. MIDI's own Legato Footswitch is the switch, so the panel is
  unchanged and the default is an exact no-op. What the switch settles is the
  one thing the model cannot: a lone note over a held string is a hammer-on and
  six arriving together are a chord, and note-on has no lookahead to tell them
  apart.
- **Made a chord decay like a chord.** The lumped anchor behind the saddle was
  summed over the played strings only, so the junction stiffened with every
  voice held and a note inside a chord rang 2.15 times longer than the same
  note alone - including beside a note too quiet to hear. It is now the
  constant every string's anchor actually presents, summed over all six, with
  the saddle-to-anchor length a bounded fitted parameter instead of the 3.2 mm
  stub the DAFx-26 attachment point happened to leave. Held voices now change a
  note's own decay by 1%.
- **Stopped every note-on after the first from clicking.** Starting a note
  while the instrument was sounding put a whole released shape into the
  junction's wave variables in one sample, and the finite differences that make
  bridge velocity read that as motion: an impulse about ten times the note it
  belonged to, buried under the safety limiter where it measured as a loud
  note. Releasing a chord did the same at 20.8 times its own background, and
  switching the string set under one did it at 26 times. A note over a
  six-string chord now peaks like the same note on a silent instrument.
- **Measured the instrument against a flat-top guitar for the first time.** The
  eight Eastman E1D regions - the only flat-top steel acoustic in the reference
  bank, while the steel fit is against a miked archtop - are now a third split
  that is scored and reported and never fitted.
- **Refit and level.** The bounded refit that pays for the new anchor moves the
  demo renders 0.9 to 2.2 dB quieter at the same public default gain; Output
  covers it.

## Notes formerly at the top of the README

Version 1.5.0 adds seven things a guitarist's hands do, each measured on
GuitarSet's hex-pickup recordings of six players, with no new control.
Fingers pluck a chord of three or four strings together instead of sweeping
it. A strum gives the strings it crosses but leaves out a dead stroke; each
string of a stroke sounds at a moment of its own about the sweep; a pick's
two-note group is a small stroke; a small stroke crosses each string more
slowly than a full one; and a stroke strikes the strings it enters and
leaves on more lightly, which replaces most of 1.4.0's upstroke tilt. A
released fretted note's fall follows the measured curve; otherwise single
notes, open strings and held notes sound as before. In the blind A/B pack
the user picked the dead strokes, the scatter and the picked pairs over
1.4.0's behaviour and 1.4.0's over the other four, then chose to ship all
seven; see
[the measurements, tests and A/B pack](../research/playing-physics-2026-10-13.md).

Version 1.3.4 build 2 names the CC2 playing gesture **Mute** in the editor's
Picking tooltip and user guide, matching the Rack control. Its behavior is unchanged.

Version 1.3.4 fixes common-tone chord strokes that could be lost when a new
chord refingers inside the release-join window, and gives every damping
contact its own string travel geometry while preserving earlier arrivals.
Open **Back** for the new **Invert Piezo Blend** switch and Normal/Inverted
indicator. It reverses only the pickup mixed under microphone Capture;
Piezo-only Capture and the separate Piezo output retain their polarity.
Normal is the default, including for older sessions. Switching glides smoothly.

Start with Guitar, Picking and Capture: construction presets,
finger/pick/thumb styles, and stereo mic, mono mic or an under-saddle piezo
through its onboard preamp, with the piezo also available as a separate
output or, through Piezo Mix, beneath the microphones.
The strings are steel. Shape and Material directions, String Age, Tuning,
Pluck Position, Touch, Release Noise, Body Amount, Stereo Width, Piezo Mix,
Room and Output remain directly accessible. Bridge-hand damping is a playing gesture
rather than a setting, so it arrives on CC2 and costs the panel nothing.
The microphone observer uses a fixed blend of measured pressure responses
and low-mode studio-position projections. It retains the existing recording
voicing and Room parameter (50% for a new instance). See the
[studio capture implementation](../research/studio-capture-implementation-2026-10-11.md)
and [the microphones and the room](../../README.md#the-microphones-and-the-room).
"Top of market"
remains the goal, not a validated claim; controlled listening against real
guitars and leading instruments is still required.

Automatic return strokes now reverse the sideways string release as well
as the traversal order. Attacks under slides and MPE tension bends follow
the current string geometry, including queued attacks and picked repeats.
See [audible gesture changes and before/after evidence](../research/audible-realism-2026-10-07.md).

Finger now applies its finite release smoothing at full force as well as
soft force, producing a rounder upper-partial balance. Matched comparisons
with real Finger recordings improve, with a tradeoff on brighter Martin
recordings; see [the recording benchmark](../research/recording-realism-2026-10-07.md).

Recent strokes now share a gently changing picking-hand posture. Finger bass
attack noise follows the actual contact release, and note endings respond to
a local damping-hand contact. Velocity accents, sustain and the existing note-join window remain
directly playable, without added input latency or new controls. See the
[natural-performance changes and comparisons](../research/natural-performance-2026-10-07.md).
The subsequent [CPU optimization](../benchmarks/cpu-optimization-2026-10-07.md) preserves
the rendered audio while reducing redundant pitch and pluck calculations.

Version 1.3.3 prepares the packages for release without changing the
sound: a user guide and the third-party notices they owe ship in every
package, the Windows build needs no VC++ redistributable, the Standalone
enables a connected MIDI keyboard, the editor fits the display and
remembers its size, an idle instance stops computing until it is played
(see [Outputs](../../README.md#outputs)), and a tag-driven workflow drafts
signed-when-possible releases. Nothing has been released yet; see
[Release workflow](../../README.md#release-workflow).

Version 1.3.2 takes the mid-band peak down by measurement: every
construction's summed microphone pressure dips at its own strong mid mode
(404-614 Hz as Shape and Wood place it), so the note on it plays 2-6 dB
over its neighbours rather than 8-11 (5 on the Parlor), and the
constructions are re-levelled. The plug-in's Piezo Mix now starts at 20 %, the Rack's
default; a saved session keeps its own value. See
[Versioned builds and packages](#versioned-builds-and-packages).

Version 1.3.0 times strum direction by the song: a strum within a dotted
eighth of the last alternates, a later one is a downstroke again, and the
C0, C#0 and D0 keyswitches (MIDI 24-26) latch downstrokes, upstrokes or
the automatic rule. It also answers only its tuning's range, ignoring pitches below the
lowest open string or above the twentieth fret.

Version 1.2.0 adds the Sustain switch, puts Release Noise and Room on the
panel, fixes the Gather Chords defects the MIDI audit found and takes the
air-mode boom down by measurement (a dip at the air mode of every
construction but the Parlor).

Version 1.1.4 expands the MIDI velocity range: soft strokes apply much less
force to the string and its physical release contact. Wood and Shape now
settle to exact construction references and string delays, so returning to a
settled selection recovers the same sound with the same playing gesture.
See [velocity and construction tests](../research/dynamics-construction-2026-10-10.md).

The [model and parameter audit](../research/model-parameter-audit-2026-10-11.md)
restores fitted broad-mode damping, corrects chord and pedal edge cases, and
exposes Room and Release Noise on the panel. It records the refreshed level
calibration, regression checks and remaining model and CPU limits.

Guitar construction, Shape, Material, Tuning, Picking and Capture use
directly visible radio switches; the continuous sound controls remain knobs.
The 1.1.0 interface pairs a satin walnut chassis with honey spruce inlays,
bone lettering and aged brass hardware. Construction stays at the left,
playing technique above capture and output, with the keyboard along the bottom.
The wood grain is embedded in the plug-in; labels, graduations, switches and
focus rings are drawn independently for clear interaction at every supported size.
A six-string fretboard above the keyboard shows the actual frets being played,
released notes, and sympathetic vibration on open strings. Its animation shows
vibration activity rather than an audio-rate waveform. The editor opens at
1120 × 980, with all nine continuous sound controls visible, or at the width
it was last resized to in the session: that width is saved with the
session's state, not as a parameter, so hosts never automate it. When the
display has less room under a window's title bar, the editor opens smaller
at the same proportions, and on a display too small for its usual
896 × 784 minimum the minimum drops too, down to 672 × 588; a size made
smaller to fit is not remembered. The header shows the version and build
number. The Standalone enables each MIDI input the first time it sees it,
at launch or when it is connected (built, not yet run with a MIDI device);
one turned off in Options > Audio/MIDI Settings stays off while it keeps
its identifier, which on Linux is an ALSA client and port number that can
change.
The Picking tooltip names CC2 as
the bridge hand. Ordinary Note-off allows a 1/32-note gap before damping,
so a following stroke can continue the ringing string. Existing
saved sessions retain their parameter IDs. The revised picking styles and
stronger body variations intentionally change their sound. See the
[latest picking/body character comparison](../research/control-character-feedback-2026-09-08.md)
and [realism work](../plans/realism-work.md) for measurements and limits.

## Versioned builds and packages

Version 1.3.2 dips the summed microphone pressure at each construction's
mid-band peak, the strong body mode the E2-E5 sweep showed 8-11 dB over
its neighbours (Dreadnought A#4, Auditorium C5, Jumbo G#4; the Parlor's
D#5 5 dB): -9 dB at Q 8, centred per Shape and Wood on the engine's own
response at prepare, the sweep's RMS deviation falling from 4.20 to 3.52
dB on the Dreadnought, 3.51 to 3.20 on the Auditorium, 3.81 to 3.22 on
the Jumbo and 3.35 to 2.71 on the Parlor, and every construction
re-levelled ([the report](../research/mid-band-dip-2026-10-12.md)). The holes
above those peaks are radiation nulls and stay; a boost at them was
measured and not shipped. The plug-in's Piezo Mix default moves from 0 to
20 %, the Rack's, so a new instance of either starts with a bit of the
under-saddle piezo beneath the microphones; a session saved before Piezo
Mix existed still loads at 0 %. Nobody has listened to the dip
(Docs/decisions.md).

Version 1.3.1 gives the Parlor the air-mode dip the other three bodies
took in 1.2.0 (its B2 falls from +5.7 to +4.6 dB over its neighbours, A#2
from +4.0 to +3.4) and re-levels it, with its Finger and Thumb cells half
an LU over the calibration target so the construction loudness fixture
keeps its 3 LU contract (Parlor Spruce Finger at -2.57 LU). No parameter
or saved state changes; nobody has listened to it yet (Docs/decisions.md).
It also closes the second MIDI audit's findings: a full Gather Chords
queue now drops its oldest controller value rather than the newest, so a
wheel's release or a bend's return is never lost; messages the instrument
ignores are never queued; a strum stamped by an offset that decreased
within a block measures its interval from the rendered position; the plug-in plays its
MIDI silently while bypassed and treats the host's reset as Panic, and
Panic is consumed before the on-screen keyboard adds its keys.

Version 1.3.0 changes when a strum reverses. A strum within a dotted eighth
of the previous one at the host tempo (0.375 s at 120 BPM) is the hand's
return and alternates; after a longer gap, or any rest over two seconds, it
is a downstroke again, so quarter-note chords are all downstrokes and
eighth-note, sixteenth-note, swung and triplet patterns alternate. Three
keyswitches below the guitar's range latch a direction until the next
keyswitch or Panic: C0 every strum a downstroke, C#0 every strum an
upstroke, D0 back to automatic; they never sound. The strings panel prints
the keyswitches and the two wheels' roles. The instrument now answers
only its tuning's range: a pitch below the lowest open string or above the
twentieth fret of the highest is ignored, where pitches above the
fretboard used to reach the open strings' natural harmonics. No parameter
is added and no saved state changes; the user chose both on 2026-10-12
(Docs/decisions.md), and neither has been heard yet.

Version 1.2.0 adds the host-automatable Sustain switch (a new, appended
parameter, off by default), puts Release Noise and Room on the panel, fixes
the two Gather Chords defects the 2026-10-11 MIDI audit found (a full queue
losing key-ups; a gathered note pulled ahead of its key's stray key-up),
makes the channel-mode messages act as All Notes Off, and takes the
air-mode boom down by measurement (`Tools/MeasureNoteEvenness.py`): a dip
at the air mode of every construction but the Parlor, on the summed
microphone pressure after the balanced observation blend, with the dipped
constructions re-levelled. The piezo's saddle force, the measured body
data and its damping, parameter IDs, saved states and the wooden interface
are retained; the sound change is authored and measured, and was preferred
blind over main's engine on 2026-10-12. See the
[release history](#detailed-version-entries) and the
[smoothness report](../research/studio-smoothness-2026-10-11.md).

Since 2026-10-10 (after 1.1.3) Acustra plays one guitar body, the Original:
the Classical 78 Model, its measured g35 body and bridge and
its classical-only attack, sustain and player-loading policies are removed at
the user's request. A session that chose it reloads every other setting and
plays the Original; every Original sound is unchanged to the bit. See the
[removal record](../research/classic78-removal-2026-10-10.md).

Version 1.1.3 adopts the October 9 listening choices: reduced close/mono
Auditorium boom, the fixed physical-string pluck direction, Classic Pick's
half-share release, the retained full-slip Thumb attack, and Classic's gentle
sustain warming. The intrinsic sustain profile covers all Classic strings and
techniques; Original keeps its preceding sustain profile. Measured body/bridge
data, parameter IDs and the wooden interface are retained. See the
[resonance investigation](../research/microphone-resonance-2026-10-09.md) and
[selected sustain integration](../research/classic78-sustain-integration-2026-10-09.json)
for the listener choices, engineering checks and scope of those judgments.

Version 1.1.2 reduces delay-ring indexing work while preserving rendered audio,
parameter identities and the wooden interface. See the [CPU validation and
screening report](../benchmarks/cpu-optimization-2026-10-08.md). The measured Rack screening
also includes wrapper changes; it does not establish a desktop CPU percentage.

Version 1.1.1 included the October 8 body/capture and bridge-alignment fixes,
correct room output gain, clamped-pitch cache geometry and invalid-preset
parameter handling from upstream `3beb61c`. It retains the wooden interface
and stable parameter identities. The matching Rack release uses this same
canonical DSP with its existing dry Room policy.
