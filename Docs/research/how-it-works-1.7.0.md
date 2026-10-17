# How Acustra works, in detail (1.7.0)

This is the full block-by-block description that the README carried under
**How it works** until 2026-10-17, moved here unchanged when the README was
cut to a summary ([README: How it works](../../README.md#how-it-works)). It
describes the engine as of version 1.7.0; dated remarks inside it keep their
original dates. Where the text says "Known gaps", the entries it means are in
the [known-gaps ledger](known-gaps-ledger-2026-10-17.md).

## How it works

Acustra is two layers, and only the outer one knows a host. The instrument is
JUCE-free C++17 under `Source/DSP/`: `AcustraEngine` is the physical model,
and `AcustraPerformer` is the player in front of it. The player splits each
block at its events and turns MIDI into engine calls - channels, the MPE lower
zone and its controller scope, RPN bend ranges, CC1/2/64/74 (CC68 is ignored; see below), channel
pressure and the string-per-channel mode, same-sample chord grouping and
strums, and the Gather Chords window with the latency it costs. The plug-in's
processor is a thin JUCE adapter around it (parameters, saved state, editor,
bus layout) that decides nothing about how a note is played. A front end
without MIDI, such as the Reason Rack Extension, spells its notes and
controllers as the same MIDI messages at their sample offsets, so every front
end plays one behaviour rather than a port of it.

### Six-string performance model

MIDI first selects one of six guitar strings. Each string covers its open note
plus twenty frets. The allocator replucks a note on the string still sounding
it once its key is up, as a guitarist does; before that rule a repeated E4
hopped across five strings, one per repeat, and left every one of them
ringing. It preserves duplicate-note ownership and never takes a string whose
key is still down while another string can sound the note.

Beyond that it places notes with a fretting hand, the rule the listener set:
"imagine a player's hand on the frets that are currently playing or were
playing previously - try to fit a fret that a finger would reach. then,
prefer empty strings, then prefer lower frets." Each string remembers the
last fretted note it sounded. Held notes, key or pedal, place the hand with
weight 1; released ones with exp(-age / 1 s), and not at all after 2 s. A
hand in position covers four frets, the highest at most three above the
lowest, and reaches a fifth by stretching: the small and large finger spans
of [Heijink and Meulenbroek, J. Motor Behavior 34(4), 2002](https://www.socsci.ru.nl/meulenbroek/Publications/Heijink%20en%20Meulenbroek%202002.pdf),
where the five-fret span was rated harder and six professional guitarists,
fingering note sequences themselves, moved the hand in 2 of 31 fingerings and
then by one fret. A candidate string and fret costs nothing when the hand
reaches it where it is, half a fret for each fret held outside the four-fret
position, and each remembered finger's weight per fret the hand would have to
move; notes the hand must hold that no hand spans are impossible. The
allocator takes the cheapest, then an open string, then the lower fret, then
a silent string before a ringing one. With nothing fretted held or released
in the last two seconds (a fresh engine, or the longest rest after which a
strum still alternates) the hand is forgotten, and a note is
placed exactly as before the hand existed: the free string with the lowest
fret, else the string released longest ago, else the oldest. That keeps every
single-note benchmark render bit-identical. String-per-channel controllers
and MPE member channels keep their own strings and the handless allocator.

Standard, Drop D, DADGAD, Open G and half-step-down tunings change those six
physical constraints rather than transposing the final mix.

Alternate steel tunings retain each string's standard linear mass and derive
the new tension from its tuned open frequency. Drop D therefore loosens the
same low-E string instead of silently substituting a heavier string at standard
tension.

The instrument answers only its tuning's range: from the lowest open string
to the twentieth fret of the highest (E2 to C6 in Standard, D2 to C6 in
Drop D, D2 to A#5 in DADGAD and Open G, D#2 to B5 a half step down). A pitch
outside it, above or below, is ignored: no string takes it and nothing
sounds. Until 2026-10-12 pitches above the fretboard reached the open
strings' natural harmonics (the node-touch projection); the user asked for
the range alone (Docs/decisions.md), and the on-screen keyboard shows that
range. The stroke keyswitches live below it.

Ordinary Note Ons pluck, and notes that arrive together are voiced and
strummed as chords. Under Finger a chord of three or four strings is not
strummed: the thumb and fingers pluck it together, a few milliseconds apart
in no fixed order, as GuitarSet's finger-played comping does; five or six
strings are brushed as a strum. A strum that crosses a string its chord
leaves out (open E2, B3 and E4, say) gives that string a dead stroke under
the fretting hand that keeps it quiet, as GuitarSet's strummers do. Three keyswitches below the guitar's range set the
stroke direction, on any channel, latched until the next keyswitch or Panic:
C0 (MIDI 24) makes every strum, and with a pick every picked note, a
downstroke (and under Finger strums the chords it would pluck), C#0 (25) an upstroke and D0
(26) returns to the automatic rule (keyswitch names count middle C as C3,
as the editor's keyboard does; the ranges elsewhere in this README count
it as C4, so E2 is MIDI 40 and C6 MIDI 84) (a stroke within a dotted eighth of the
last alternates, a later one is a downstroke). A keyswitch never sounds,
takes no string and is not gathered into a chord; under Gather Chords it
applies to the chords sent after it. Retired CC68 remains ignored. Connected finger gestures
are explicit: turn on CC65 on the note's channel, send CC84 with the held
source pitch, then send one destination Note On. A valid request transfers
that string's sounding wave to the new fret, with a short bounded finger
contact and a faint filtered fret-friction sound rather than a new picking
attack. The friction shares the contact's energy limit and grows with finger
travel, with more roughness on wound strings. The source request is consumed
once; ambiguous ownership, chords, out-of-range frets and intervals over an
octave fall back to an ordinary pluck. CC65 off or Reset All Controllers
clears the request. The JUCE-free API exposes the same operation as
`transitionNote`. There is no new panel control or host latency. The current
Rack wrapper cannot send these explicit gestures. The contact map is
authored and accepted for adoption in the user's listening review; it is not
a fitted hammer-on recording. See `Docs/decisions.md` for the review and
publication instruction.

CC2 is continuous bridge-hand pressure: the heel of the picking hand resting
by the saddle, a soft lossy absorber whose loss rate adds to the string's own,
darkening the note as it shortens it. It is a playing gesture rather than a
construction setting, so it stays a controller and the panel keeps its ten
controls; a pressure of zero is an exact no-op, proven by 71 bit-identical
corpus renders.

Ordinary Note-off ends MIDI ownership immediately and waits one 1/32 note
before beginning hand damping. A same-pitch/channel stroke on that physical
string within the window, including its endpoint, cancels the pending damping
and merges with its continuing vibration. This applies to every ordinary
key-up, so an unrepeated note also rings for that extra interval. The window
follows host or score tempo: 62.5 ms at 120 BPM and about 101.35 ms at 74 BPM,
with 120 BPM used when tempo is unavailable or invalid. Live tempo changes
preserve the remaining musical duration. Note-on timing is unchanged.

After the window, nominal hand damping has a T60 of 160 ms for a fretted note
and 1.25 seconds for an open note. Explicit softer key-ups extend that damping
time by up to about two times, and firmer key-ups shorten it to about half;
missing velocity and MIDI 64 retain the nominal time exactly. This is a
bounded playing map, not a contact-force calibration from the sustain corpus,
and that loss is a gain per round trip: the loop slews the gain it applies
toward the requested one across one round trip, in either direction, because
applying the whole of it to the first sample after the key came up stepped the
wave the junction reads by up to a third on a low fretted note, and the bridge
and body rang on that step as a thump 4.6 to 8.6 dB above the note's own level
at that moment. Paired held/released renders guard against a new attack at
44.1, 48 and 96 kHz. The comparison includes the held note at the same time:
its evolving envelope can exceed the preceding window's peak, and damping
can also change the interference between modes. These peak checks are not
a proof of physical energy conservation.

CC64 sustain holds notes released under the pedal until pedal-up, which
starts damping directly. Pressing the pedal after a key-up does not catch its
pending release. The SUSTAIN switch beside CHORDS is the same hold on every
channel at once (`Performer::setSustainHold`): on, every released note rings
on, its string still fretted, until the string is taken for another note or
the switch goes off, which damps the held notes as a pedal-up does. The
switch and the pedal hold independently: a pedal lifted under the switch,
or the switch turned off under a channel's pedal, keeps the note, and
Reset All Controllers lifts only the pedal. Like the pedal, the switch does
not catch a key-up already in its release window. It is host-automatable,
so a track can alternate let-ring and damped passages, and it takes effect
at the start of the block the host changes it in, before that block's MIDI.
Off, the default, is an exact no-op; a session saved before it existed
loads it off. All Notes Off uses its existing sustain-aware release directly, and
All Sound Off/panic clears sound immediately. Queued strums still sound when
the pick reaches each string; their ordinary release window starts there.
For longer gaps, holding the note or using sustain keeps it ringing. The
Recuerdos demo also holds its melody through thumb slots without changing
the written pitches, attack times or velocities. See the
[release-window contract and verification](release-join-2026-10-05.md).

Once its window expires, Note-off damps the note at every release velocity,
including 127, without adding a new pluck, so a fast keyboard key-up never
sounds like another stroke. A fretted note's fingertip lifts the string off
the fret as it relaxes and becomes the string's end, so the dying note falls
as GuitarSet's hex-pickup pitch tracks measure it on 4,329 fretted note
ends: nothing in the first 4 dB it loses, then about 2.3 cents a dB, 29 cents
at 12.5 dB down and 80 at 35 dB, where it stops; it follows the hand's own
damping, so a firmer key-up falls faster. Roland's guitar-to-MIDI patent
describes the same drop (US 7470851 B2: "the pitch drops about a half tone"
as the envelope nears zero). Open strings, held and pedalled notes do not fall, and a new
stroke presses the string back onto its fret
([the report](stroke-release-realism-2026-10-13.md)). At that damping
start it adds the hand's own touch
(Release Noise): a short soft-contact force where the hand lands - the
fretting pad a finger's width behind the fret, the fingertip back at the
pluck point for an open string, or the palm by the saddle under a pick -
launched both ways along the string by the same travel paths the pluck's
contact uses, so it rings through the string, the bridge and the body and is
damped by the hand like everything else on the string. Its level follows what
the string still carries and the key's release velocity (64 is nominal; 1 about
9 dB under, 127 about 5 dB over), wound strings add a faint brush as the finger
leaves the winding, and every level, corner, time and position is drawn afresh
per damping gesture. A joined stroke cancels this deferred touch. At the
50% nominal it sits about 35-47 dB under the note's attack;
at 0 it is an exact no-op.

Release Noise also controls a very faint roughness while a held finger slides
along a wound string. Its level and broad spectral colour follow continuous
physical speed, and contact withdraws smoothly near the nut. Stationary fingers,
plain strings and fixed-fret MPE tension bends add no such roughness. This is
an authored, reduced contact approximation; see the
[fret-motion model and checks](fret-motion-2026-10-11.md).

A chord that arrives on one sample, which is what a sequencer sends and no
hand can play, is swept as a strum: newly assigned strings are fretted at
that sample, and each is released when the pick reaches it. An already held
note keeps its preceding vibration until its scheduled re-pluck. The k-th string
sounds k string-spacings later at the pick's speed, low to high on a downstroke and
back on the return, and then at a moment of its own about that sweep: a
uniform draw of 4.35 ms standard deviation, the scatter of GuitarSet's
picked strokes about their own sweeps less the attack detector's own
(`Tools/MeasureStrumScatter.py`), which sounds a third to two thirds of the
strokes out of string order as the corpus's are. It is a time, not a place on
the sweep, since a take's slower strokes scatter no more than its faster ones;
the earliest string sounds at the note-on. A strum within a dotted eighth of the last at the host
tempo is that return and alternates; a later one, or any after a rest of
more than two seconds, starts over with a downstroke, unless a keyswitch
has latched a direction (see the MIDI section). A stroke of three or more
strings strikes the strings it enters and leaves the chord on more lightly
than those between them, in either direction: 1.8 dB under for the entry
string and 2.0 dB for the exit string, what GuitarSet's picked strokes show
when each string is compared with the rest of its own stroke
(`Tools/MeasureStrokeEdges.py`). An upstroke enters from the treble side and
is released at `-1.2 + 0.30 (k - 2.5)` dB of the force a downstroke gives
each string, k 0 for the low E: the offset from comparing each string's
upstroke attacks with its own downstroke attacks
(`Tools/MeasureStrumDirection.py`), the tilt what is left inside one stroke
once its edges are counted (0.9 dB a string in 1.4.0, which the whole
takes' comparison gave). A lighter string is let go more slowly and darker,
as any softer stroke. With a pick, a
single note or two-note group is a stroke of the same hand: it alternates
with the strums and the other picked notes by the same dotted-eighth rule,
and one within 30 ms of the hand's last stroke continues it; fingers and the
thumb pluck every single note toward the palm. A pick's two-note group is
swept across its two strings as a strum is, since one plectrum cannot reach
both at once: of GuitarSet's 1,526 picked two-string groups on adjacent
strings only 5% sound within 2 ms of each other, a median 13.0 ms apart and
mostly in the stroke's direction (`Tools/MeasureTwoStringStrokes.py`).
Fingers and the thumb still sound two notes together. The shape is planned before this
timing: string order can cross pitch order, and an unplayed string between
two reached strings still takes one spacing to cross. The spacing is the set-up dimension
at the saddle; the pick's speed is a player's map of velocity over the
0.51–2.46 m/s range measured from GuitarSet strokes. The corpus does not
establish that velocity-to-speed relationship. That speed is a six-string
stroke's: a smaller stroke is not the same sweep cut short, but crosses
each string more slowly, its span growing as the 0.64 power of the
spacings it crosses, the profile of each of GuitarSet's picked takes
(`Tools/MeasureStrokeSpan.py`), so at MIDI 96 two strings sound 9.7 ms
apart and six span 27 ms. Chords whose notes arrive
spread in time are played as sent, unless Gather Chords is on.

Notes that land on one sample and one channel are fretted as one shape
(AcustraEngine::planChord, which the plug-in calls before sounding them):
every way of putting them one to a string is scored by the hand's rules, a
shape that takes no held string, fits one hand and is reached from where the
hand is with the fewest frets moved, re-strikes a still-ringing note where it
rings, and has the most open strings and the lowest frets. E major, A minor,
G, C and D land in their first-position open shapes; C4-E4-G4 on its own
becomes G fret 5, B fret 8 and open E.

A chord played live reaches the plug-in one key at a time, in whatever order
the fingers land. When a new note arrives within 30 ms of the previous note
on its channel, the hand can improve the forming shape by moving attacks
that have not fired. A note already sounded keeps its string and vibration;
a later key cannot pluck it again just to improve the final fingering.
Without future pitches, some exposed rolls retain a wide shape: C4-E4-G4
over 20 ms keeps C4 on B1 and E4 on the open high E, leaving G4 on G12.
Denser rolls can also fill every string that reaches the incoming high
note, so the normal allocator takes an earlier note's string.
Gather Chords, the CHORDS switch beside PANIC, can instead plan G5, B8 and
open E before any attack, at the cost of latency: it holds every MIDI event
back by 30 ms.
A Note On that comes due takes along the Note Ons its channel received in the
30 ms after it, and the group reaches the allocator as one wrist event:
fretted as one shape and strummed exactly as the same chord sent on one
sample. Thirty
milliseconds is where one chord ends and notes meant apart begin. A pianist's
louder melody note strikes 20 to 30 ms before the rest of its chord, mostly
because a faster key travels sooner, which a keyboard's key-bottom contact
shares, while asynchronies played on purpose usually exceed 30 ms
([Goebl, JASA 110(1), 2001](https://iwk.mdw.ac.at/goebl/papers/Goebl_JASA2001_melodyLead.pdf)).
The cost is 30 ms of latency on every note, which the plug-in reports (on
top of the engine's own seven samples, Outputs) so a host compensates it on
playback; a player hears it live. The switch is off by
default. A key repeated inside the window, a key whose key-up the channel
sent inside it (a later Note On of a released key stays behind that key-up,
which may be stray, its string already taken by another note), or a
controller that changes how notes are allocated (sound or notes off, reset,
the channel-mode messages, the RPNs that lay out an MPE zone), ends a
chord. String-per-channel controllers and MPE member channels already say
how each note is played, so their events are only delayed, never regrouped:
a member's CC74, pressure and bend stay ahead of the note they belong to.
The window's queue holds 1024 events, the window plus one block. When a
stream fills it, the oldest queued continuous gesture (bend, pressure, an
ordinary controller), whose later value supersedes it, is dropped and
counted, so a wheel's last value is the one that survives; a message that
starts or ends a note, the pedal, a reset or a mode message is never lost,
the oldest queued continuous event making room for it. Messages the
instrument ignores (program change, polyphonic pressure, system bytes) are
never queued.

And no two plucks land in the same place: each draws its own point within ±0.02
of the string length, the take-to-take spread the reference recordings'
round robins show, which is what makes a repeated stroke a new take rather
than a copy.

Re-plucking the same pitch now continues the existing string in both planes.
The contact constrains its local displacement and motion, then adds the new
release to that same travelling field, preserving its phase and filter memory.
Already emitted string-borne contact packets reach the continuing string;
the new stroke creates no second bridge port. A conservative per-stroke work
bound prevents positive interference from adding more sampled slope energy
than the nominal new release. This remains an idealised contact, not a resolved
finger or plectrum trajectory. [Recuerdos and the merge audit](recuerdos-tremolo-2026-10-05.md)
describe the checks and listening comparison.

Taking a still-sounding string for a different pitch preserves its preceding
waves, in both planes, in separately damped loops; the parallel plane holds
most of a pluck and reaches the body through the rocking saddle, so dropping
it at a repluck, as the build before this did, cut an E3's old wave (a
nylon E3, measured when the instrument still had nylon strings) off 24 dB
under the new note's first 30 ms instead of letting it die under the hand. This is a provisional transition model. [Laurson et al., CMJ 25(3), 2001](https://aaltodoc.aalto.fi/bitstreams/768dbe9c-526c-414a-b2ce-0742574b8064/download)
reduce loop gain over about 10 ms **before** the next pluck;
[Erkut et al., AES preprint 5114, §3.2](https://aaltodoc.aalto.fi/server/api/core/bitstreams/a290c5f2-9a62-4d17-ba32-160615a9c670/content)
measure a 20–60 ms finger-contact interval. Neither establishes the current
model's 10 ms post-release T60 or its two concurrent wave states. The retained
loop's stored-state energy falls below 1% in the existing 60 ms regression,
but the moving bridge keeps driving it: its active lifetime can last seconds.
Each independently returned wave therefore contributes its own impedance to
the junction - the parallel one at (h/a)^2 of it on the rocking - with the
old wave retaining the impedance it had at capture. When that wave is
let go - quiet for 80 ms, or its string handed back to the open string after a
key-up - its port leaves the junction on the delay's 6 ms time constant rather
than in one sample: cut at once, the impedance step moved the bridge under
everything else sounding, a faint tick 240 ms after a fretted key-up (1.33 s
after an open one) about 30 dB over the local high-frequency floor.
Measured on a chord change 0.6 seconds after the first chord, the strings
still hold 25% of their initial energy and an earlier build discarded all of
it. A held note
replucked goes through the same hand: projecting its stored shape onto the
modes with a node at the pluck point in one sample, as the build before this
did, clicked into the limiter and let the near-node modes pile up pluck after
pluck, a held G2 replucked six times at 250 ms peaking 15 dB above its first
pluck where it now peaks 3 dB above it with nothing on the first samples.
The sustain pedal defers the hand's damping until pedal-up. A channel's pitch
bend is a slide — the fretting hand moving along the neck, so the sounding
length changes and the tension does not — and it changes the active string
length without replaying the pluck. An MPE member channel's own bend is a
different gesture: one finger pushing a single stopped string across the
fret at the length the fret fixes, so it goes through the string's tension by
Grimes' law (PLoS ONE 9(7):e102088, 2014, Eq. 6), which moves the
inharmonicity (bending stiffness B goes as 1/T) and the bridge-junction
impedance the string presents (Z = sqrt(T·mu)). The port is followed sample
by sample, because the junction sums all six strings' impedances every
sample; measured against Grimes over a 0-200-cent bend on four steel strings,
pitch error is 0.0002 cents and the twelfth partial's stretch moves within
0.15 cents of what the bent tension predicts. The MPE manager's zone-wide
bend stays a slide.

CC1, the modulation wheel, is the fretting hand's vibrato: an axial tension
modulation at fixed length (Grimes Sec. 0.3; Erkut, Valimaki, Karjalainen and
Laurson, AES 108th Convention, Paris, Feb 2000, preprint 5114, Sec. 3.3), so
the same tension route that carries a member bend also carries the vibrato,
moving the sounding pitch, the inharmonicity and the junction port together —
an earlier build moved only the last two and left the fundamental fixed,
which review caught. It is one-sided upward from the note's own pitch,
converges over a 0.5 s onset, and runs from Erkut's measured 4.9 Hz
fast-and-shallow to his 1.4 Hz slow-and-deep as the wheel opens; full-wheel
depth is an authored 20 cents (no source gives guitar vibrato depth in
cents), measured in the rendered pitch at 20.0 to 20.1 cents and never
falling below the note's own pitch by more than 0.06 cents. An open string
gets none, since no finger is stopping it. Like CC2 it is one gesture across
the instrument, not per channel.

Conventional MIDI is the default: all sixteen channels are independent and
channel 1 is not implicitly global. A lower MPE zone is created only by RPN 6
on channel 1. Channel 1 then manages the contiguous member channels above it;
its bend and sustain are global to that zone, while bend and sustain remain
scoped to each member channel. RPN 0 sets 0--96-semitone bend sensitivity, with
MPE defaults of 2 semitones for the manager and 48 for members. A released MPE
tail freezes its member bend so reuse of that member channel cannot retune the
old string, while subsequent manager bend remains live. MPE Timbre (CC74) sets
a member channel's own pluck point for the note it carries, after Traube and
Smith's plucking-point estimation (Proc. COST G-6 DAFx-00, Verona, Dec 2000,
153-158), and moves only that voice's own comb nulls; MPE channel pressure
biases that channel's vibrato depth down toward half its unbiased value (never
above the wheel's own ceiling) and does nothing else. An opt-in string-per-channel mode — MIDI
1.0's own Mono Mode On (CC126, value = string count) / Poly Mode On (CC127)
on the basic channel, the standard spelling of a one-string-per-channel
layout such as a hexaphonic pickup or a guitar-to-MIDI converter might send —
puts each of the six channels onto its own string, bypassing the allocator;
no control-surface driver on hand is confirmed to transmit CC126 itself, so
today the mode is reachable through a MIDI editor or a host's own CC tools.
As MIDI 1.0 requires, the four channel-mode messages (CC124-127) also act
as All Notes Off on their channel. Upper-zone MPE is not implemented.

### Pluck and strings

Each audible string path is a full-round-trip stiff-string waveguide. A note
begins with a triangular travelling-wave history. A continuous Gaussian
contact profile rounds that shape at the existing aperture's RMS width. It
removes the former five-point profile's repeated high-frequency unity lobes;
the signed result preserves the linear smoothing without rectification adding
extra harmonics. The profile is a numerical contact assumption, not a measured
finger shape; [its qualification](contact-kernel-and-power-2026-09-08.md)
records the small, mixed improvement against real guitar recordings. Its current phase origin and initially
empty filter/body histories do not describe a complete release from rest.
A short deterministic noise burst adds release detail through the measured
body, which is the only microphone radiator. On steel the burst's level (2.21 of the
authored law), the contact's width (0.49 of its reference) and the velocity
brightness (1.106) were chosen by ear with the strings' bending loss below
(Docs/decisions.md, 2026-09-28): 70% of the way from the joint refit's
optimum (0.38, 0.81, 1.073) toward the pluck refitted around half the loss
the recordings measure (3.0 on its ceiling, 0.35 on its floor, 1.120). Pluck Position moves the
displacement point; Touch and MIDI velocity alter its aperture, level and
brightness. The hand keeps its distance from the bridge, so on a short
(high-fretted) string, or under the Thumb, that distance reaches toward the
string's middle, where every even partial is nulled and no player plucks.
Up to a quarter of the string the point is the hand's; past it the point
bends smoothly toward 0.36 of the string and never reaches it
(p' = 0.36 - 0.11^2/(p - 0.14)), so Pluck Position still moves the stroke at
every fret for every technique. It replaced a hard stop at 0.46, which left
the control dead for a Thumb from the 11th fret up and put high notes on a
hollow near-midpoint pluck (Docs/decisions.md, 2026-09-30); MPE Timbre still
names any point of its 0.05-0.46 band directly. The authored contact law
widens the three lowest strings' contact and the upper register's; both
terms are ramps (full on the low E and A, half on the D; from the 14th fret
to full at the 19th), not the steps they were, which changed a note's
contact by up to 70% between neighbouring strings or frets. The Finger's and
Thumb's burst is its noise through the same corner twice with no white
share (-12 dB per octave above the corner): a fingertip is not a plectrum,
and the finger-plucked flat-tops carry 10-25 dB less 2-16 kHz in their first
15 ms than the plectrum's one-pole-plus-white burst gave them. The Pick
keeps that burst. The hand lets the string go at the force it can hold, not at a
set displacement: a point force at distance a from the bridge deflects a
string of tension T and speaking length L by F·a(L − a)/(T·L), so the same
stroke displaces a stopped string less than the open one the velocity law is
calibrated on, by (L − a)/L against (L0 − a)/L0 at the same hand position (up
to 2.7 dB less at the 17th fret), and its attack-pitch glide shrinks with it;
the contact noise stays on the force. A blind listener chose this on every
up-the-neck pair (Docs/decisions.md); the benchmark is neutral on it
(training +0.07%, development validation −0.07%, flat-top −0.24%) while its
pitch-trajectory term improves on every split. A fitted register law broadens the effective low-note contact and
narrows it in the upper register, correcting a repeatable corpus-wide spectral
slope without applying note-dependent body EQ. The fitted exponent continues
past the pitch-independent point, so low-note contact is broader and high-note
contact narrower than in the earlier model.

A string does not leave a plectrum's tip from rest. Under Pick the released
state carries, beside that displacement, a velocity over the same contact
width — the string the tip was carrying when it slipped off — written as
Smith's equal integrated waves and so unfolding to a hump of velocity with no
displacement of its own. Its kinetic energy is a fitted share of the
displacement's, `share(v) = pickReleaseVelocityShare · v^pickReleaseVelocityExponent`
for MIDI velocity v in 0–1. The share and exponent are fitted on the picked
archtop recordings across their four velocity layers and read by the Pick
technique only; Finger and Thumb, and so the
finger-plucked flat-top rows, are bit-identical. The first fit
(share 1.0 at full velocity growing as v^2.93, transient 0.078 of the Finger
burst) belonged to a Pick with the wider 0.5 contact and the five-point
kernel; the merged Pick meets the string narrower (0.35 of the Finger width),
more bridgeward (0.40 of its distance) and through the continuous Gaussian,
and the same stage refitted from that vector wants 0.3125·v^0.93 and a 0.125
transient (archtop training under Pick 6.623494 → 6.546029, development
validation 6.271127 → 6.256259). Once a pluck released most of its energy
parallel to the top (below), the same stage refitted again wants only a
small, nearly flat share, 0.0625·v^0.47, and the transient gain on its zero
bound, which is the Finger burst law (archtop training under Pick 6.334019 →
6.284861, development validation 5.941904 → 5.904206). With the strings'
bending loss the share returns: since 2026-09-28 it is 0.582·v^0.859, chosen
by ear 70% of the way from the joint refit's 0.008·v^0.49 toward the
0.83·v^1.02 the plectrum was refitted to around half the measured loss, whose
pick the listener heard as too intense (Docs/decisions.md). The separate
impact-burst experiment stayed at zero and its implementation is now removed;
the standard excitation burst remains. The velocity hump is smoothed by that same
Gaussian contact as the displacement, exactly: the rest wave gains the
tabulated corner function at its two folded apex corners and the velocity box
is the difference of two Gaussian edges. The velocity's
partials fall 6 dB/octave slower than the displacement's, which is the tilt the
recordings gain with dynamics: before the mechanism the model's loud layer
(MIDI 112) carried 5–9 dB too much H1–H3 and 4–6 dB too little H7–H10 while its
soft layer (MIDI 16) sat within a few dB, and its first 12 ms were 10–19 dB
short above 4 kHz at the loud layer only. Two things are fixed by measurement
rather than fitted. The written frame: the plucked shape every calibration was
fitted with is the rest state advanced by half the apex phase and negated
(partial magnitudes agree to 0.001 dB, phases differ by exactly −πpn), and a
rest-frame pluck with the *same* magnitudes moved the archtop harmonics term
from 8.61 to 12.77, because the idle strings, bridge and body at coinciding
partials interfere with the string according to the phase it starts with — so
both components are written in the fitted frame, where they stay in
quadrature. And the energy: the fitted level law describes the displacement,
and the velocity is energy on top of it; redistributing one fitted energy
between the two read worse on both splits, since the hump's energy sits in
partials that decay fast and the sustained level then rose too little with
velocity. What the plectrum still lacks is any measurement of its own: the
share and exponent are fitted, not a beam and friction solver, and the
archtop's four layers are the only picked recordings in the bank.

Nor does the string leave the tip at an instant. It slides round the
tip's rounded edge, and while it does the held force F0 falls away; the
pluck point answers at once with a velocity dF/(2Z), so, linearising the
edge's hold, the force unloads as e^(t/tau) with tau = r/u,
u = (c/2) y0 (1/a + 1/(L - a)) the speed an instantly released pluck point
starts with. Every partial is multiplied by 1/(1 - j omega tau), a
first-order low-pass whose corner rises with the force the tip held: a soft
stroke is let go slowly and dark, a hard one fast and bright. The radius r
was first fitted at 0.15 mm on the picked archtop training rows (the
pick-release stage, refitted around it, then wanted the release-velocity
share and the pick burst at zero), moving Pick only: archtop training under
Pick 5.9391 -> 5.5639, development validation 5.5588 -> 5.4839, the frozen
test split 5.6678 -> 5.4272, the Eastman's picked rows 6.6596 -> 6.5254
(Docs/decisions.md). It ships at 0.116 mm, chosen by ear with the bending
loss on 2026-09-28: 70% of the way from the joint refit's 0.122 mm toward the
0.114 mm refitted around half the measured loss.

A fingertip or the thumb's pad lets go the same way, over an effective
0.2 mm edge (a chosen length, not a measured fingertip). Finger applies the
full slip, including at velocity 127; Thumb retains the ratio to its
full-velocity reference. The line is rescaled to its original AC spread,
preserving the displacement level law, although the emitted level changes
with the spectrum. A softer stroke is released more slowly and darker.
Finger's sustained H5-H12 over H1-H4 rises a median 6.67 dB from MIDI 16
to 112 in the current fixture. The previously cited 9 dB recording target
was picked material and does not establish a Finger velocity law. Thumb's
existing contrast remains 8.2 dB. Touch is how fast the hand
lets go, for every technique: it scales the release edge over two octaves,
none at its default of 0.58, so a firm Touch releases faster and brighter
(bounded to three times the full-velocity release speed for the fingers) and
a soft one slower and darker, even at velocity 127, where the contact's own
Touch law has saturated.

Transverse motion still drives the existing tension and pitch model. A separate
axial-resonator experiment was rejected because its narrow onset sounded like
a pitched water drop, and it never contributed to the shipping sound. Its
resonators and calibration controls are now removed. Historical measurements
remain in the dated decision log.

Each steel string takes its scale, diameter, tension or density, and Young's
modulus from published construction data. The loop reads its fractional delay through a second-order
Thiran allpass held inside a fixed fractional band (Laakso, Valimaki,
Karjalainen and Laine, IEEE Signal Processing Magazine 13(1), 1996, 30-60,
Eq. 86), not an interpolated read: an allpass has magnitude exactly one at
every frequency, where the read it replaced lost up to 0.7 dB per round trip
at a half-sample fraction, so a string's upper partials used to decay at a
rate set by the accidental fraction of its loop length (a 47:1 range in loss
per round trip across nine notes); it is now a smooth 1.8:1 rise with the
partial's own frequency. On the 2026-10-11 isolated-string probe, H8 decay
varies by 1.14–4.84% across MIDI 76–84 at 44.1, 48 and 96 kHz; this remains
an approximation, not exact rate invariance. Frequency-dependent losses make upper partials
decay faster, and two second-order allpass sections in cascade follow the
stiff-string inharmonicity law, re-solved at the tap the delay currently
occupies so a bend or vibrato's slewing fractional part does not stall the
solve. On a flexible string (B N^2 up to 0.015 over its N fitted partials:
every open string, and in the standard tuning the low E below its 3rd fret
and the others below their 7th to 18th) the first is collocated at H1, H7
and H11.5 and the second is bypassed. One collocated section dipped between
its points on the stiff wound strings high on the neck (E2's H3-H5 16-18
cents flat at the 20th fret), so as B N^2 rises to 0.05 the second section,
placed from the stiffness, is switched in smoothly and the first is refitted
with the delay by least squares over the integer partials H1-H12.

Since 2026-10-10 both polarisations carry the same intrinsic string loss:
the authored round-trip factor 0.99915, equal cutoff sections, a constant
steel dislocation loss angle, and a bending-loss section. Their bridge
terminations still differ. The four wound strings also carry winding
friction as a constant loss angle on tension. For partial n, tension and
bending contribute in the proportions 1/(1 + B n^2) and B n^2/(1 + B n^2),
using the same inharmonicity B as the dispersion. The section design includes
its phase in the tuning and its fundamental magnitude in the loop gain.
This replaces the earlier unequal per-plane shelves and wound bending-only
description; the dated comparisons in Known gaps retain those older results.

String Age lowers the high-frequency cutoff and increases dislocation loss.
It scales both wound friction and wound bending loss by
max(0.2, 1 + 8 (age - 0.15)), pivoted on the default age; the plain-string
cutoff falls as exp(-0.9 age). The shared fitted cutoff scale is 2.286.
The accepted wound-string blend is friction 2.25e-4 plus bending loss 0.0175;
plain bending loss is 0.0023. The listener preferred keeping some bending
loss beside friction so a pick's highest partials do not ring too long.
These are calibrated and authored losses, not an independently measured
damping law for this exact string set. See the current
[string-loss model and listening record](string-hf-loss-2026-10-10.md).

A second,
orthogonal polarisation loop can be detuned from the first by an end
correction at the string terminations: Woodhouse (Acta Acustica 90 (2004)
945-965, Sec. 4.3) measured the polarisation parallel to the soundboard as
about 0.8 mm longer, in 650 mm, than the perpendicular one, on an open
string, and calls the exact size tentative ("would require detailed
computation"). Carried as a fixed length it splits a stopped string's planes
further the higher it is fretted (4.8 cents at the 14th fret, 6.8 at the
20th), and once the parallel plane radiated and held most of a steel pluck,
high notes drifted flat as that member took over, where the recordings' do
not. A blind listener preferred the steady pitch on every high-fret pair, so
the correction ships at zero, chosen by ear, over the published length the
benchmark mildly prefers (Docs/decisions.md); the mechanism stays
calibratable up to one string diameter. On steel this second loop
contributes slope energy to the attack-pitch surrogate below.

The parallel loop reaches the body too. A string vibrating parallel to the
soundboard pushes the saddle crown sideways at the crown's height over the
top, and a sideways force there is a moment about the string's own axis - the
rocking the archive's two bridge-end impacts measure. In the model's
normalized rocking coordinate `r = a*theta` that moment is `h/a` times the
horizontal force and the crown moves `h/a` times the rocking, so the parallel
polarisation's port is `(h/a)^2` times the measured rocking mobility, and
its load reaches the microphones through the measured moment-to-pressure paths. `h` is the
published height of the strings over the top at the bridge, to their lower
bound (R. Mores, *List of guitars measured*, 2021, HSaT): 8.1 mm on g21;
`a` is the 23.2 mm impact half-spacing the saddle lever arms already assume. Nothing is fitted. Both polarisations are one string: the normal
loop is tuned so that, loaded by the bridge, it sounds the requested pitch,
and the parallel loop shares that bare length (plus any end correction, zero
as shipped), so the pair's split - the doublet each partial beats with - is
the bridge's own pull on the normal member and varies note to note (0 to
about 18 cents here at the fundamental, widest on the low E near frets 14-16,
where the bridge pulls hardest; 5 to 9 in Woodhouse's measurements). Where the saddle rocks, the two loops are
coupled through it and ring as one pair of modes; its sustained,
energy-weighted centre is found from the eigenvalues of the loops' round trip
times the saddle's 2x2 reflection, and both loops are lengthened by what it
sits above the request - the flamenca bridge's open B, whose parallel-led mode
sustains while its bridge-led partner sheds 0.24 dB a period, would otherwise
sound 6.6 cents sharp.

How a pluck divides between the planes follows the hand. On steel, a
finger's free stroke and a pick both cross the strings moving along the top
and press in only partly, so most of a steel pluck's energy is released
parallel to the top: the normal-plane share is 0.30 - 0.08 Touch (0.25 at the
default Touch, a release about 60 degrees from the normal), bounded to
0.17-0.35. That share was selected on the benchmark once both planes
radiated: from the 0.86 that had kept the one radiating plane audible, every
steel split improves - training -1.10%, development validation -2.78% and
flat-top -5.13% on steel's own bridge - and a blind listener preferred it on
both steel pairs. Each pluck draws its own +-0.025 about its share. Steel's
output reference rises by the 5.41 dB its demos lost at the same controls,
so a session keeps its loudness. The
effect
is audible as the slow beating a guitar partial has and a single radiating
plane cannot: over 0.3-2.3 s, the median RMS envelope residual of H1-H6
after a quadratic trend is 0.33-1.74 dB on the flat-top rows against the
recordings' 0.58-2.97 (0.11-0.59 with one plane), 0.17-1.75 dB on the steel
archtop rows against 0.26-1.69 (0.02-0.81). It also lengthens what
sustains: the decay term falls on every steel split.

Steel attack pitch uses a bounded Kirchhoff--Carrier energy surrogate. The
released waveguide displacement supplies its slope energy; string axial
rigidity converts that energy into extra tension, and the existing loss makes
the resulting sharpened pitch fall monotonically toward the settled pitch. The
single displacement calibration is 0.007736 m and the excursion is bounded to
20 cents. The last mechanism-specific interaction sweep selected it on
training total and improved early-pitch trajectory on both splits; at that fit
stage, the development-validation aggregate was 0.0215 lower when it was
disabled, and that tradeoff is retained explicitly in the fit report. A
separate fitted steel fret-decay slope of -0.0598 lengthens T60 with fret
number.

### Bridge, sympathetic strings and body

The original model's played strings drive a passive spatial bridge approximation,
fitted through 10 kHz from both sides of the archive's bridge. Method.pdf places
the hammer impacts between B3/E4 and E2/A2; its Figure 3 places the accelerometers
behind the saddle on the tie block, with their exact spacing unreported. Treating
these sensor responses as collocated bridge-end mobilities is a model assumption.
Each string terminates at its own point along the saddle, at a lever arm
`u = (i - 2.5)/2` for string `i`; each mode keeps one pole pair and a residue
matrix `[[heave, cross], [cross, rock]]`, so a string at `u` sees
`heave + 2u*cross + u^2*rock`, fitting the treble-side response at
`u = +1` and the bass-side response at `u = -1`. Holding every
residue matrix positive semidefinite keeps each string's own combination
positive real, so the junction stays passive: it can redistribute or
dissipate string energy but cannot create it. This is a property of the constrained
model, not proof that the measured responses form a collocated passive matrix.
The disagreement between the two cross-side records sets a heuristic corner —
2245 Hz on g21, steel's guitar. Sensor offsets and setup differences
can contribute to that disagreement; it does not uniquely identify loss of bridge
rigidity. Above that corner every string reads the mean of the two side responses,
and no mode carries a cross or rocking residue. g21's
bank keeps 47 modes (30 of them rocking). A bank with
no rocking mode at all falls back to the single-point port every string
shared before; the engine suite asserts the shipped banks carry at least one.

An independent [center-impact audit](../../Tools/AuditBridgeSpatialMap.py) checks the
unused third hammer position against the mean of the two end responses, without
fitting gain or phase. Broad 120–500 Hz complex errors are 1.6–10.4%; at 2–4 kHz
they reach 32–76%. Narrow-band microphone errors near 794 and 891 Hz reach
74% and 73% on g21, so the smaller broadband error does not establish rigidity
at every mode. Shorter common windows leave the high-frequency discrepancy.
The full frequency and sensor results are retained in the fit report.

The Original model plays steel's own bridge, g21's, on its radiation's
poles (the paragraph after this), as part of the steel blend described
after that; it is the only bridge. Until 2026-09-29 a second, 44-mode passive bridge fitted
to a custom Fylde Falstaff measured by [Carcagno et al.](https://doi.org/10.1121/1.5084735)
(Sitka spruce top, Brazilian rosewood back and sides, normal bridge
velocity/force with the strings damped, between strings 5 and 6) was
selectable as a Bridge Model beside it. It was another guitar's bridge under
the flamenca's radiation, its modes did not line up with the radiation's,
and the choice made little audible difference, so it was removed with its
bank. That measurement still sets derived constants: the steel-string
mobility level below (for g21's bridge and for the jointly fitted body) and
the piezo's bridge conductance, which
[`FyldeBridgeReference.py`](../../Tools/FyldeBridgeReference.py) regenerates. A session saved
with the old `bridgeModel` parameter loads on the Original bridge.

On the Original bridge, steel's string is loaded by the guitar it is heard
through. In a modal body one mode supplies both halves on one pole: the
mobility residue phi_k(bridge)^2/m_k and the radiation residue
phi_k(bridge)psi_k(mic)/m_k, so a partial landing on a radiation peak also
meets a conductance peak and blooms, then fades faster than its neighbours.
g21's bridge measurement comes from the same guitar and hammer impacts as its
radiation bank, and [the generator](../../Tools/GenerateMeasuredBridge.py) records
which of its 47 modes is the same resonance as a radiation mode: the nearest,
inside that mode's as-fitted half-power band (`|f_b - f_k| < f_k/(2Q_k)`),
with the bridge mode itself resolved (`f_b/Q_b` under the radiation bank's
local spacing). 18 pair, at 90.8, 178.7, 208.7, 412.4, 484.1, 517.8, 591.1,
656.3, 728.8, 766.1, 793.2, 867.9, 919.9, 1032.7, 1084.7, 1330.8, 1374.8 and
2341.6 Hz, and each takes its twin's engine pole exactly - the wide steel
anchor's 165-205 cent shift, Shape, Wood and the plate-Q rule included (a
unit test pins it to the float for every Shape and Wood). The unpaired modes
(82.8, 1269 and 1466 Hz and the broad fill modes, Q 5-14, at 371, 848 and 955
Hz and 1.7-9.1 kHz) take the same maps by class, and the plate-Q rule's octave
factor on their Q. The flamenca's top is about 3.6 times as compliant as a
steel-string guitar's: the Fylde measures 0.274 of g21's |Y| (geometric mean
over 80 Hz-4 kHz at the Fylde's measurement point, g21's bass side; 0.23-0.30
over other bands and conductance), so the residues are scaled by that ratio
under the unchanged fitted mobility scale, and every residue matrix and so
passivity is kept; the plate conductance floor is not scaled. Without the
ratio, measured while the strings still ended on a 3.25 mm anchor stub, a
picked E2's fundamental fell 13 dB against its 2nd and 3rd harmonics. With it, the radiation/conductance prominence correlation is
+0.41/+0.46/+0.41 at zero offset, and on picked notes the partials that stand
out at onset fade 2.1 dB against their neighbours by the late window (the recordings 0.8-1.8). Wood now moves this bridge with the
radiation, exchanging the load as Shape does.

**The steel blend, lightened (a measured stand-in for a by-ear choice).**
After Set 18 the listener asked for its model enhancements combined by
weight. Blind Set 19 heard that blend (B 1, D 0.7, C 0.15, E 0.15) against
B+D and chose B+D, with "something between B and C is the best"; Blind Set
21 then preferred the midpoint (B 1, D 0.85, C 0.075, E 0.075) over B+D,
"not a big difference". The midpoint cost about +40% (strums) / +55% (a held
chord) CPU over B+D, and the listener asked for the cheaper option, as close
to the preference as possible. Steel plays a lighter body
measured to stand in for the midpoint, with the weights in
[`SteelBodyBlend.h`](../../Source/DSP/SteelBodyBlend.h). Steel's own bridge is the
aligned g21 bridge above, whole (B at 1: a share of the Fylde bridge in
parallel, tried while that bridge existed, passive as it was, moved the peaks of the port the strings drain into, which is not
linear in the bridge's admittance, and at 30% left the 190 Hz radiation pole
0.37 of B's drain, outside both parents; B's poles are never moved part of
the way either, which would reopen the gap between a bridge mode and its
radiation peak). T1 and the rocking modes are damped 0.85 of the way in log
Q from their as-fitted Q to the 150 Hz plate-Q rule's (D, below). The
decay-Q grid (C) is gone: its difference sat 24-26 dB below the music at
twice this weight. Of a jointly fitted pole set for g21's bridge and
microphones ([`MeasuredJointBodyData.h`](../../Source/DSP/MeasuredJointBodyData.h),
E, verbatim, so D does not damp its own T1 and rocking modes) only the 9
modes below 550 Hz play, radiation and bridge (8 of them carry a bridge
residue) in parallel at 0.075. Below 550 Hz g21's radiation and B's bridge
play at 0.925; above it B's bridge is whole and g21's radiation plays at
0.985, the level at which the midpoint's balance of highs against 100-400 Hz
is matched. Every part is passive and the sums are too. Each of E's bridge
modes rings on its own radiation mode's pole, and Shape and Wood move the
joint body by its own A0 and T1 as the rest of the construction moves, so
its drains stay on its radiation everywhere (BodyShapeTests).
On Set 21's pieces and a single-note sweep this body keeps 83% / 85% of the
midpoint's measured move away from B+D (body under the treble notes, 63-80
Hz, 159/200/317 Hz early, tilt), and costs +2-4% CPU per 64-frame block over
B+D (Docs/decisions.md, 2026-09-29). It has not been heard against the
midpoint. With B at 1, D at 1 and E at 0 the engine is the unblended B+D
bit for bit. The benchmark readings quoted above are B+D's.

The strings end on the bridge. The short segment from the saddle crown to the
bridge pin has both its ends on the bridge, so it holds the bridge against
nothing and presents no port of its own (`bridgePortMobility`): each string
drains into the measured bridge admittance alone, at its own point on the
saddle. Until 2026-10-10 the model stood each segment as a spring from the
bridge to ground, DAFx-26's fixed-end tail at a 3.25 mm stub. Above each body
mode that spring resonated with the body's mass and drained the strings at 6
to 34 times the bridge's own conductance, 2-5 semitones above A0 and T1.
[The saddle-termination report](saddle-termination-2026-10-10.md) has
the measurements, and the decision log (2026-09-02, 2026-09-04) how the stub
was chosen while it was live.

The junction turns those displacement waves into bridge velocity and force
with finite differences, so anything that moves a wave without the bridge
moving must not be differenced. Filling the delay line with a pluck shape
is an initialization operation, not measured bridge motion; the present
initialization is still an approximation of physical release. Likewise, a hand
damping a string does not itself displace the bridge. Both re-reference the
differences across the sample they land on, which is what keeps a note started
over a ringing chord peaking like the same note on a silent instrument.

That modal fit reproduces the measured mobility magnitude but loses the
conductance a real top plate keeps between its high modes, where modal overlap
is dense; a plate's driving-point mobility tends to a real constant there. One
over-damped positive-real section restores that floor, with real poles at a
fitted lower corner and a fixed 16 kHz upper limit, so it adds conductance
across the top of the band and vanishes below. It is passive like the measured
modes, and a zero plateau reproduces the previous build exactly. This is what
sets how much faster a string's upper partials decay than its fundamental: with
the bridge disconnected the junction supplies 30.3 dB/s of extra decay over
400--600 Hz but only 1.1 dB/s over 3000--4500 Hz, and that collapse is why the
model's upper partials formerly rang about 7 dB/s too long against the
reference recordings.

Every string on the bridge is a member of that junction whether or not it is
played: F_b = Σ Z_i (2a_i − x_b) and x_b = Y_b F_b, summed over all six. An
idle string on a moving bridge carries a wave, and at its resonance it
presents thousands of times its characteristic impedance, which pins the
bridge there and bounds the sympathetic energy the way a real bridge does.
Driving the idle strings from the bridge while leaving them out of the sum,
as the engine did before, let their reaction grow without limit: ten
hand-damped repeats of one note pumped the idle string 7 dB above the note,
and open-string partials sat 15 dB under a played fundamental where the
recordings sit 25 to 40 dB under. A released string is handed back to the
allocator when its release damping has run its time, keeping whatever it
still carries as an idle string's wave.

The body's normal force and normalized moment excite measured radiation modes:
132 shared poles on steel's g21 flamenca blanca, measured in a semi-reverberant
music room. The archive
contains normal hammer impacts at both sides of the bridge, observed by three
microphones. Their raw complex responses retain a common time origin and
relative phase. For bass and treble forces `Fb` and `Ft`, the inputs are
`F = Fb + Ft` and `T = Ft - Fb = M/a`, where `a` is the assumed impact
half-spacing. The corresponding pressure paths are half the sum and half the
difference of the measured responses. Each mode has separate force and moment
states, shared by all three microphone outputs. Mapping this pair to arbitrary
string positions assumes a rigid saddle; it does not identify horizontal
forcing or the saddle's rotation axis.

Each bank stops where its fit stopped: 10 kHz before the anchor and Shape
move it, so 8.1 kHz on the steel Jumbo and 11.7 kHz on the Parlor. Above its
last mode the radiation used to fall 12-25 dB within one third of an octave,
its 16 kHz band 23-39 dB under its 2-4 kHz level. A plate's modal density is
constant in frequency and its high modes overlap, so the radiation above the
fitted band is continued statistically (configureBody, since 2026-09-30):
modes on a 1/16-octave grid at unit modal overlap (Q 22.6) from the bank's
top to 18 kHz or 0.45 fs, each part's residues (both microphones, force and
moment) carrying the power density the bank's own top octave has, with
deterministic pseudo-random phases, since the fitted high modes' phases are
uncorrelated, falling at the bridge's mass law, 6 dB per octave. They are
passive and linear like the rest of the bank and are built as 48 kHz modes
converted to the host rate as the measured ones are, so the band above 8 kHz
keeps its level within 0.2 dB at 44.1 and 96 kHz. Every construction holds
19 slots for them (9-19 of them sound), which keeps each one's state in its
slot across Shape and Wood changes. The band below the top keeps its tone to
within about half a dB near the edge (the continuation's first modes overlap
the bank's last). Nothing measured is behind the continuation's level beyond
the bank's own top octave; a microphone response measured past 10 kHz would
replace it.

Stereo mixes complex measured pressure responses and low-mode projections,
with no added channel delay or reverb. The archive's treble and bass microphones were 10 cm above
the top plate and 20 cm apart; its third microphone was 20 cm toward the
upper bout from the treble microphone, also 10 cm above the top. The g21
setup used a floor absorber, with first room reflections at least 12 ms away.
It was measured nylon-strung and remains an adaptation for steel.

The current microphone observation is a fixed weighted pressure mixture:
40% of the preceding treble/upper capture, 20% unboosted measured treble,
10% measured microphone averages, and 10% each of constrained neck at 30 cm,
constrained off-hole at 45 cm, and measured-phase neck at 30 cm. Spatial
terms receive common distance compensation before weighting. They project
only the four identified air/top modes; the other modes retain the declared
measured/continuation mixture. Geometry and radiation profiles are explicit
model assumptions, not new measurements at these positions.

**Mono mic** uses the blend's right observation and sends identical samples
to both channels. It is not a sum of the final stereo outputs, but the
observation mixture can itself cancel partials. The stereo pair remains the
default. See [implementation and A/B limits](studio-capture-implementation-2026-10-11.md).

The generator fits all six impact-to-microphone paths jointly, then checks
both those paths and their force/moment combinations against the existing
complex, magnitude and auditory-band limits. The measured input paths also
pass the microphone-balance limits. It chooses the smallest passing prefix in
the measured prominence ordering, not a globally
optimal pole bank. The responses are kept for 12000 samples (250 ms)
at 48 kHz, with the final tenth faded: the shortest window at which every mode
below 700 Hz has its Q within 10% of the 1 s value. g21, steel's body, was
once kept for 3000 samples (62.5 ms), a window whose own 16 Hz bandwidth set
the Q of every mode below 300 Hz: its air mode read 95.5 Hz at Q 6 where the
guitar's is 90.8 Hz at Q 19, its main top mode Q 10 where it is 17, the 209
Hz mode Q 15 where it is 37. That is the low wooden ring a listener missed
against the real recordings; resolved, it improves every benchmark split,
with Finger and with Pick (the decision log, 2026-09-24, has the figures),
with the body, attack and decay terms all better. These remain
windowed measurements, not identified intrinsic body decays.
No independent minimum-phase conversion or convolution remainder is added.

g21 rang longer than the flamenca blancas the same archive measured
anechoically (g37, g38, g39, g42 and g43, each fitted by this generator and
its gates at its own converged window; g41 fails them), in every band from
its top-plate mode up, and the music room does not explain it. Every g21 mode
from 150 Hz to 10 kHz therefore has its Q scaled by the population's median Q
over g21's own median in the octave round it, never raised
(`--plate-q median`); frequencies and residues are kept, so a mode starts at
the level it was measured at and rings for a shorter time. The air group below
150 Hz, where the by-ear gain below acts, is left as measured. Up to
2026-09-27 the band started at 300 Hz; since 2026-09-28 (chosen by ear,
Docs/decisions.md) T1 and the rocking modes are under the same rule (in the
steel blend 0.85 of the way in log Q: 14.3, 28.7, 12.8 and 2.5 for the four
below): T1 at 178.5 Hz Q 17.5 -> 13.8 (the population's T1s 8.8-15.5), the
208.7 Hz rocking mode Q 37.3 -> 27.4 (the population's rocking modes in
200-280 Hz: Q 11-21), 229.0 Hz Q 17.8 -> 12.1, and 286.8 Hz Q 3.7 -> 2.3.
The former Q >= 4 runtime floor was removed on 2026-10-11, so these broad
fitted decays now survive. In the current Dreadnought anchor the first three
are the 176, 190 and 204 Hz modes; the rocking
path's 190 Hz ring falls from T60 0.43 to 0.32 s and its peak from 7.8 to
6.6 dB over its third octave. On steel's own bridge the 178.7 and 208.7 Hz
bridge modes are twins of T1 and the rocking mode and take these poles, so the
damping reaches the string's load as well as the radiation; no unpaired bridge
mode lies between 150 and 300 Hz, so the band's move changes none of the
bridge's own Q ratios. Since the 2026-10-11 finalization pass, shared modes
map the actual radiation pole back through the bilinear transform before
constructing their passive mobility. This removes the damped-frequency versus
natural-frequency discrepancy at broad Q. Unpaired bridge modes retain their
original prototypes; the [current evidence register](dsp-finalization-2026-10-11.md)
records both the mathematical checks and the retained measured-fit limits.

The selected bank uses neutral body calibration factors: frequency and Q
scales of 1 and residue tilt of 0 dB/octave. The original capture's authored
fourfold treble-microphone air-mode gain remains inside its 40% contribution;
unboosted measured and spatial contributions do not inherit it. Shape and
wood apply their existing construction morphs and Q limits. The fixed blend
changes observation only, with physical poles and coupling unchanged.
The rejected bridge-local direct branch has been removed. The existing fixed
radiation gain remains, with no runtime RMS matching or automatic level trim;
absolute sound pressure is not calibrated. Body Amount scales measured
radiation, Stereo Width moves its two channels toward mono, Output applies
final gain, and a headroom-only limiter remains linear through -1 dBFS.

### Construction controls

The construction controls are deliberately bounded directions around the
measured reference guitar, not claims that one measurement identifies several
distinct instruments.

Shape is a box rather than a tone curve. The measured body's two lowest strong
modes, the air resonance A0 and the top's first mode T1, are the two modes of
Christensen and Vistisen's coupled top-plate/air-cavity model (J. Acoust. Soc.
Am. 68(3), 1980, 758-766): a top piston on a cavity spring and the soundhole's
air plug, whose coupled pair obeys f₋² + f₊² = f_p0² + f_a² + f_h² and
f₋²f₊² = f_p0²f_h². A measured A0/T1 pair plus the box's own rigid-walled
Helmholtz frequency therefore identify the top's two frequencies, and a
different box re-couples the same top into a different pair. The boxes are
published set-up dimensions: Martin's Size 0 for Parlor (the class the PS-220E
belongs to), the Martin 000 "Auditorium", the D-28 for Dreadnought and Gibson's
SJ-200 for Jumbo, each with the 4-inch soundhole a flat-top carries; the plate
modes above T1 follow the equal-thickness plate law f ∝ 1/A_top, and their
residues follow the top's mass, 1/A_top. So a plate mode keeps its peak
mobility and radiation as it moves. At the bridge that is heave 1/A, cross
A^-1.5 and rock A^-2, one congruence that keeps each residue matrix positive
semidefinite. The A0/T1 radiation ratios come from the same model's
eigenvectors. Their bridge residues also follow the plate participation,
including the cavity-volume/top-area-squared force-coordinate factor; Wood
composes with that Shape change. Both fitted banks use one frequency-based
anchor-detune map, preserving the original g21 map without applying unrelated
row parity to the joint bank. On steel that puts A0/T1 at 121/208 Hz for the
Parlor, 106/191 for the Auditorium, 94/176 for the Dreadnought and 84/162 for the Jumbo. The
measured body has an anchor the other shapes are placed relative to: the
Dreadnought, under the wider box the local line authored (modes x0.900 above
T1, bass x1.28), chosen by ear on 2026-09-24 on all four blind pairs over the
fitted transform (101 Hz, x0.972, x1.08). Its air ratio was 98 then, and the
listener heard the air transient at 99 Hz through the anchor spring the
strings then ended on. With the strings ending on the bridge (2026-10-10) the
ratio is 109. That puts A0 at 94.2 Hz, where both recorded dreadnoughts ring,
and T1 at g21's measured 178.5 Hz under the same detune, 176.3 Hz; a blind
listener approved the change. On 2026-09-24 the benchmark split on the anchor:
steel training -0.5%, the never-fitted Eastman E1D flat-top rows (a real
dreadnought) -5.2%, but development validation +3.2%. Published dreadnought
measurements report roughly 100 and 190 Hz (Fletcher and Rossing, *The
Physics of Musical Instruments*, ch. 9). The outline's fraction of its width-by-length
rectangle (0.72, and 0.75 for the dreadnought's shoulders) is read off the
plantillas, and the plate law assumes tops of one thickness; both are model
assumptions, not measurements of those instruments.

The Guitar construction switches apply construction controls together, using the documented
body families as starting points: Dreadnought / Martin style (spruce, as in the
[D-28](https://www.martinguitar.com/guitars/standard-series/D-28.html)),
Auditorium / Taylor style (spruce, the
[Grand Auditorium family](https://blog.taylorguitars.com/buyers-resources/an-introduction-to-taylor-acoustic-guitar-body-shapes)),
Parlor / Fender style (spruce, as in the
[PS-220E](https://www.fender.com/products/ps-220e-parlor)), all three on the
Original body and its own bridge. These reuse the bounded construction
directions; they are not independently measured models of those
manufacturers. Each switch changes Shape and Material with host automation
gestures, leaving tuning and output where the player set them. Other
combinations show Custom construction.

The Original is the only guitar model. The Classical 78, a second measured
guitar (Mores g35, a classical strung with steel), was removed on 2026-10-10
at the user's request; three steel models fitted from Mark Rau's
measurements, which carry no redistribution license, were retired before
publication. The `guitarModel` host parameter keeps its ID, AU version hint 7,
index and two-choice range as a non-automatable legacy slot, like the three
retired capture slots, so a host that addresses parameters by index finds
every later control where it was; nothing reads it, and a session that chose
the Classical 78 reloads every other setting and plays the Original. Since
2026-09-29 a saved state carries `stateVersion` 2; an older one loads with
its `stringMaterial` and `bridgeModel` values dropped, so a nylon or Fylde
session plays steel on the Original bridge, and with Cedar as Mahogany.
`piezoMix` is appended after every earlier parameter and loads at 0 from an
older state.

Shape moves the bridge's modes by the same coupled-model factors as the
radiation - its A0 group, its modes up to the body's T1 and the plate modes
above - so it also changes the saddle-force Piezo capture and the body's
interaction with the strings. Low-mode bridge residues follow the coupled
plate participation and force coordinates; higher modes follow the top's
mass and moment coordinates. Q is retained and the residue scaling preserves
positive semidefiniteness. Body Material changes the radiation and the bridge
that belongs to it. Steel's own bridge takes the radiation's poles (Bridge, sympathetic
strings and body), which preserves ringing strings and overlapping re-pluck
tails. Dreadnought has unity Shape factors relative to the authored anchor;
the anchor transform and other documented calibration still modify the
measured radiation.
If a body crossfade is already sounding,
the latest selection waits for its remaining duration (at most 40 ms), then
uses the same 40 ms crossfade. Same-tick updates replace only the silent target.
A construction changed under a ringing chord rebuilds the bridge the same
way: its mobility crossfades over 20 ms from the modes that were sounding,
and where Shape or Wood only retune the same measured bank its bridge and
body modes keep ringing from where they were rather than restarting from
rest; the bridge's motion is carried across the switch sample. Above 5 kHz a
Shape or Wood switch now stays within about 2 dB of the
steady sound where it used to tick 12-48 dB over it; a retune ticks 2-9 dB
over (from 10-30).

Every construction and every Picking plays at one loudness, the default
construction's (Original, Dreadnought, Spruce, Finger), at the user's
request: `Source/DSP/ConstructionLoudnessData.h` holds a gain for each of
the 36 Shape x Wood x Picking cells, measured by [`CalibrateConstructionLoudness.py`](../../Tools/CalibrateConstructionLoudness.py)
as BS.1770 integrated loudness over a fixed phrase set of strums, single
notes and held chords from soft to hard, and multiplied into the output
reference through the same 20 ms smoothing, so a change glides the level
rather than stepping it. The mono microphone and the piezo have their own
factor per cell and stay level with the stereo microphones. Every cell and
capture is within 1 LU of the default, where the same playing spread over
about 15 LU before. Only the level
moves: each construction's samples are its old ones times its gain, and the
default construction's microphones are unchanged to the bit. A Pick cell whose hardest playing would come within 1 dB of
the safety limiter sits up to 0.9 LU under the default (Known gaps). The
match is of loudness over that phrase set, in Standard tuning: one chord at
one velocity, or one note, can sit several dB either side where the body's
resonances meet it (Known gaps).

| Control | Audible behavior |
| --- | --- |
| **Shape** | Parlor, Auditorium, Dreadnought or Jumbo: the measured body's A0 and T1 re-coupled for that box's published volume, soundhole and top area, with the plate modes above T1 scaled with the top, in the bridge and the radiation alike; all three captures hear the resulting instrument. |
| **Body Material** | Spruce, Mahogany or Maple effective plate material: representative density and directional stiffness change coupled air/top resonances, radiation and reciprocal bridge loading at fixed geometry and thickness. Spruce retains the reference construction. Measured damping is retained; no species brightness EQ is imposed. This is a material substitution model, not identification of an exact guitar or board. See [wood model and research](wood-materials-2026-10-11.md). A session saved with Cedar loads as Mahogany. |
| **Tuning** | Standard, Drop D, DADGAD, Open G or Half-step down: the six open strings and the notes each can reach. A retuned string keeps its gauge (its linear mass), so its tension, impedance and stiffness follow its new pitch, as on a real guitar. |
| **Picking** | Finger retains the calibrated contact and applies its finite release slip in full, including at maximum velocity, with normalized displacement. Pick is sharper and farther bridgeward, and lets the string slide off its edge in a time that shortens as the stroke hardens, so soft strokes are released darker than hard ones (edge radius and release-velocity share chosen by ear between two fits on the picked archtop recordings, with the strings' bending loss; the pick burst stays at zero). Thumb is rounder and farther neckward with a soft contact contribution that remains at high velocity, retaining its slip ratio to the full-velocity release. Finger and Thumb have a soft double-corner contact burst with no white hiss. Explicit MPE position overrides hand position and can reduce the distinction. |
| **Capture** | Stereo mic, a single measured mono mic, or an under-saddle piezo through a modelled preamp circuit. |
| **Piezo Mix** | Mixes the under-saddle piezo, at the level Capture Piezo gives it, into Main beneath whichever microphone Capture selects, the same signal on both channels, on one time base: the microphones wait out the piezo chain's seven-sample pipeline (Outputs). At 0% the output is exactly as without it; a new instance starts at 20%, a bit of the piezo under the microphones (the default chosen on 2026-10-12, the Rack's new-instance default), and a session saved before Piezo Mix existed stays at 0%. With Capture on Piezo it does nothing, since the piezo is already the whole output. |
| **Release Noise** | The damping hand's touch when release damping begins, after the ordinary key-up's 1/32-note window; a joined stroke cancels it. Also controls subtle speed-dependent friction during held wound-string slides. 0-100% (50% nominal, 10% default), scaled by release velocity and what the string still carries. Explicit saved values retain their levels. 0% adds no new noise; already emitted waves continue. A session saved before it existed loads at 0%. Available on the Player panel and host-automatable. |
| **Room** | Small-studio reflections around the microphones, 0-100% with a 50% plug-in default. Zero becomes dry once the existing room tail fades. The piezo remains dry. Available on the Capture panel and host-automatable; a session saved before Room existed loads at 0%. |
| **Sustain** | On, every released note rings on as under the sustain pedal, on every channel, until its string is taken or the switch goes off, which damps the held notes as a pedal-up does; CC64 keeps working beside it, and each holds independently of the other. Off (the default) damps every key-up as before and is an exact no-op. Host-automatable; the SUSTAIN switch beside CHORDS. |
| **String Age** | Lowers the string cutoff and increases dislocation loss; on the wound strings it also scales winding friction and bending loss (0.2 of the default's on fresh strings, 7.8 times it at 1), so an old set's basses lose their upper partials first. |
| **Pluck Position** | Moves the base hand position from bridgeward toward the neck; the selected picking style applies its distance ratio. Past a quarter of a (short, high-fretted) string the point bends smoothly toward 0.36 of it and never reaches it, so the control changes the stroke at every fret for every technique. Explicit MPE position overrides that ratio. |
| **Touch** | Changes displacement aperture, transient colour and brightness with one shared velocity law; picking styles set the relative contact width. It also sets how fast every technique lets the string go: the release edge scales over two octaves across the control (none at its default), firmer faster and brighter, softer slower and darker, even at velocity 127. |
| **Body Amount** | Scales microphone radiation; pickup observations have their own signal path. |
| **Stereo Width** | Blends the measured stereo pair toward mono; Mono mic and Piezo ignore width and send identical samples to both channels. |
| **Output** | Final gain, default 0 dB; exactly linear through −1 dBFS, then bounded by a headroom-only safety limiter. |

Stereo mic uses the fixed measured/spatial observation blend. Mono mic uses
its right observation, duplicated to both outputs. Phase cancellation remains
a possible property of either microphone route. Piezo observes net saddle
force before body radiation. These observations never feed back into the instrument.
Capture changes crossfade on the existing parameter smoothing, preserving
ringing notes.

Piezo is a real, documented under-saddle pickup chain modelled at component
level (`AcustraEngine::PiezoDesign`, Docs/decisions.md 2026-09-29 "Accurate
piezo chain"); values marked chosen were picked from a documented range. In
signal order:

1. **The strings' forces on the element.** Each string's saddle force - its
   incident force less its port moving with the saddle - weighted -0.8,
   +0.4, +0.9, -0.3, +0.6 and -1.0 dB low E to high E (chosen, inside the
   +-1-2 dB a good install gives), plus the axial force through the strings'
   rear break angle (sin 25 degrees, chosen; zero while its gain ships at
   0). In newtons: 0.00773577847 x 48000 = 371.3 N per engine force unit:
   the strings' own fitted displacement unit (7.74 mm,
   `steelDisplacementScaleMetres`) per 48 kHz sample, at every rate. The
   engine reads it from the calibration at run time, so a calibration set
   with `setPhysicalCalibration` moves the piezo with the strings
   (Docs/decisions.md 2026-09-30; until then the chain used the struct
   default's 6.1 mm, 292.8 N).
2. **The saddle on its element.** A 3.8 g bone saddle (chosen within
   2.7-5.1 g) on the element's stiffness, placing it at 6 kHz (chosen within
   5-7 kHz), with the element's loss (Zollner's Q of 18 as a bound), the
   strings' summed wave impedance loading the saddle and the bridge
   conductance measured on the Fylde Falstaff (1.59e-3 s/kg over 5-7 kHz)
   under the base:
   F_p / F_r = Zk Q / (1 + Zk Q), Zk = k/s + c_m, Q = 1/(sM + SZ) + G. A
   5.98 kHz pole pair of Q 3.26: +10.6 dB at 5.85 kHz, +0.24 dB at 1 kHz,
   -4.8 dB at 10 kHz. Its poles map exactly; its zeros are a weighted
   least-squares fit with the DC gain held at 1, within 0.23 dB and 1.1
   degrees of the analog response to 12 kHz at 48 kHz (0.34 dB, 1.8 degrees
   at 44.1 kHz; 0.009 dB at 192 kHz).
3. **The element**: a charge source across its 1.45 nF at 0.2 V/N
   open-circuit (Zollner, *Physics of the Electric Guitar*, ch.6: the Ovation
   EA-68), into 3 m of Mogami 2524 (390 pF, chosen length).
4. **The preamp**: ESP Project 202 Fig. 1 (R. Elliott,
   [sound-au.com/project202](https://sound-au.com/project202.htm)) on one 9 V
   battery with OPA2134 halves (TI SBOS058B). C1 (4.7 nF) into a follower
   whose 1 MOhm bias network is bootstrapped through C2 and R4 (129 MOhm
   seen), guarded by two 1N4148s solved as Shockley diodes; C3 into R5 || R6
   (30.8 Hz, the chain's only audible low corner); a gain stage of
   1 + R7/R8 = 1.62 (R7 6.2 kOhm, lowered from the figure's 10 kOhm the way
   the article sets the gain, so the hardest strumming does not clip it at
   the strings' fitted force unit) whose gain falls to one below 0.48 Hz
   (C4); R9 and C5 into the volume pot at full and a Radial PZ-DI's 1 MOhm.
   The op-amps stop where the datasheet says: U1A's input at its typical
   common-mode range (2.5 V either side of the 4.5 V bias), U1B's output at
   its swing for its 6.18 kOhm load (+3.25 / -3.89 V about the bias).
   Mid-band gain from the element's open-circuit voltage to the DI: 1.256
   (+1.98 dB); -1.5 dB at 20 Hz.

The sections are solved in turn, each as deviations from the 4.5 V operating
point (so silence maps to exact zero): the input network as a trapezoidal
two-state system, U1A's range and the diodes by a Newton solve of the diode
equation in the samples they conduct, C3, C4 and C5 as trapezoidal
one-poles, and U1B's clip with its corners band-limited by a 12-tap BLAMP
residual (a Kaiser-windowed sinc integrated twice,
`Source/DSP/PiezoBlampTable.h`). The chain's output is seven samples behind
its input at every rate; there is no oversampling. `Tools/PiezoReference.py`
simulates the same circuit as a stiff ODE system (op-amp macro-models, stray
capacitance, Shockley diodes) and `Tests/PiezoCircuitTests.cpp` holds the
engine to its results.

The reference headroom measurements are not a guarantee for every gesture.
Maximum-velocity Pick strums at the bridge with softer Touch can briefly
reach the modeled 9 V preamp's limits as their string forces sum. The
circuit retains its physical overload and recovery; construction level
matching happens after it and cannot remove internal saturation.
`CaptureTests` separately checks playing extremes, isolated output-stage
aliasing with an unclipped input stage, and combined-stage stress.
`PiezoCircuitTests` compares overload, recovery and harmonic behavior with
the independent continuous-circuit reference. See the
[candidate validation](material-gestures-realism-2026-10-11.md).

Its level is matched to the stereo microphones (the median BS.1770 loudness
difference over the factory constructions, Finger and Pick and typical
playing), and then per construction
and Picking by the construction loudness table, which brings it to the
same loudness as the microphones on every cell. The whole chain runs
every sample whatever Capture selects, so a Capture change or a newly cabled
Piezo output lands on warm state; a switch is exactly the 20 ms Capture
crossfade between the two sensors. It rings down to exact zero within 3.5 s
of the hardest strum's hardest moment (the input network's 1 Hz pair and
C4's loop take that long in the circuit too); the strings' saddle force never
quite does, and is read as none below 5 pN. It costs about 0.5 us per
64-frame block at 48 kHz (C++17 -O2 -fno-builtin), up to 4 us when driven
far past its clip. The string weights' real
pattern, the saddle's own bending modes and friction in its slot are not
modelled; no matched piezo recording benchmark is available yet.

The public three-choice Capture parameter is appended as `captureMode`,
retaining all previous parameter IDs and indices. On loading an older saved
state, Stereo remains stereo, treble/bass/upper microphones migrate to Mono mic,
and ideal/loaded/magnetic pickups migrate to Piezo. An existing upper-mic
override takes precedence during that migration. An explicit saved
`captureMode` always wins. Legacy slots remain for state compatibility and no
longer advertise automation or drive sound; old capture automation lanes must
be moved to the new Capture parameter. This deliberate migration removes the
retired sensors rather than keeping hidden magnetic or unloaded-piezo paths.

### The top's spring-back

Before a pluck the hand holds the string aside, so the saddle carries the
static force of the string's slope there, T y / a, and the top stands
deflected under it; at the release that force goes and the top springs back,
ringing the air mode and the lowest top modes on every note, however high.
The wave a pluck writes carries what follows the release, not that the force
was there before it, so until 2026-10-01 the top never sprang back: on
notes above 200 Hz, the 60-180 Hz band over a note's first 100 ms stood
-30 to -38 dB under the note where the Eastman E1D and the Martin HD28 put
it at -12 to -21, and their air mode, at 93-95 Hz, rang 23-32 dB over the
model's. The
engine now reads the force from the line it wrote - 2Z times its steep
flank's rise per sample, the normal plane's share - and gives it back to
the junction as an external force on the saddle at the string's point: a
step high-passed at 16 Hz by a double pole at 10 ms, with no net impulse.
Its size is the force's own, given back at 0.45 of it. A listener liked the
thump and chose it at 80% of the full release (2026-10-01), by ear, through
the anchor spring the strings then ended on, which took part of the step.
With the strings ending on the bridge (2026-10-10) the step reaches the body
4.8-5.1 dB louder, and 0.45 = 0.8 x 10^(-4.95/20) keeps it where it was heard,
within +0.5 to +1.4 dB on every Picking. Before that change, in the first
100 ms the low band read within 0.5 dB of the Eastman's picked take,
1.3-2.1 dB under its finger take and 5.3-5.5 under the Martin; after it the
model's air mode still rings shorter than theirs (Known gaps). It follows
the playing as the force does: on E4 and A4 its 60-180 Hz power over the
first 300 ms reads about -14 dB against the note for the Finger, -10 to -12
for the Pick, whose hand holds the string nearer the bridge, and -15 for the
Thumb, at every velocity, so it grows with the note (about 7 dB from
velocity 0.3 to 0.9). A re-pluck adds its step to one still running out,
a bend after the pluck leaves the force already let go alone, and a natural
harmonic, held aside by the picking hand like any note, sheds its open
string's force (Known gaps). The piezo now observes that same released
crown load at the string's existing saddle sensitivity, as well as the
saddle motion it causes. The [released-load correction](piezo-release-load-2026-10-11.md)
records the force-unit and headroom evidence, including the revised
passband error test; the circuit and its gain remain unchanged.

### The microphones and the room

**Capture voicing.** The retained contour was fitted before the studio blend,
using g21's close bridge/upper-bout observations morphed to steel Shapes.
Those source observations were compared against five sets of real
flat-top recordings - the Eastman E1D's picked and finger-plucked takes, the
Martin HD28, the bank's flat-top notes and GuitarSet's six performances -
that earlier pair heard about 5 dB too little at 125 Hz, 4-6 dB too much at
250-630 Hz and 2-7.5 dB too little at 0.8-1.6 kHz, every source agreeing,
and the 1.26 kHz hole did not move with the pluck point, so it was the
microphones' and not the strings'. [`Source/DSP/CaptureVoicingData.h`](../../Source/DSP/CaptureVoicingData.h)
holds the difference as one smooth gain - a low shelf and five broad peaks,
none past 6 dB: +1.1 to +1.5 dB at 80-125 Hz, -6 to -6.5 dB at 250-500 Hz,
+5 to +7 dB at 1-1.6 kHz, nothing above 4 kHz where the recordings disagree;
refitted after the top's spring-back, which supplied low end the first fit's
+3.3 to +3.8 dB had stood in for. Since 2026-10-08, six causal filter
sections apply this contour to the summed microphone pressure. The previous
per-mode weighting could disrupt cancellation between modal tails and turn
an intended low-mid cut into a boost. The correction preserves the physical
bridge and radiation poles, never reaches the piezo, and keeps separate
histories for each bank during construction fades. See the
[body and capture audit](body-capture-2026-10-08.md).
Its earlier level calibration kept constructions within +-1 LU. The studio
blend retains those gains and adds flat microphone-only phrase-level
compensation; its level and spectral changes are measured
separately in the [implementation report](studio-capture-implementation-2026-10-11.md).
[`Tools/FitCaptureVoicing.py`](../../Tools/FitCaptureVoicing.py) refits it from
renders of the five sources (twice, the engine in the loop, after any change
to the Original's radiation).

**Microphone brightness tilt.** The microphones also receive a small
tilt, 0.4375 dB/octave brighter around 1 kHz, chosen on 2026-10-02 when the
listener asked the two models then shipping to come about 25% closer to each
other's brightness; the Original keeps it after the second model's removal.
The tilt stops changing below 60 Hz and above 10 kHz.
[`Source/DSP/MicrophoneBalanceData.h`](../../Source/DSP/MicrophoneBalanceData.h)
holds it; it is applied once to the completed radiation bank, including its
high-frequency continuation. It keeps the poles, mode count, complex residue
phases and physical bridge/string behavior; the summed microphone phase may
move. It adds no per-sample processing, and the piezo pickup response stays
intact.

**Room.** `EngineParameters::room`, the plug-in's Room, puts a small studio
(5.2 x 4.1 x 2.7 m, the microphones 0.32 m from the guitar) around the
microphones: its ten strongest early reflections from an image-source model
within two bounces, panned by side, then an eight-line feedback delay network
with a nominal 0.45 s low-frequency decay and shorter high-frequency decay.
Its late output starts around 25 ms, after the last early tap at 17 ms;
the 8 ms delay is the late network's input delay, not its audible onset.
Room changes the send level, not microphone distance or reflection geometry.
Every open recording is made in a room and the model is
anechoic: between partials the recordings carried 15-25 dB more over a
note's first 0.3 s, and the scorer's spectral body term, the largest on
every split, was mostly that. At 50%, the plug-in's default for a new
instance, the room sits about 12.5 dB under a held chord and 10 dB under a
released phrase, and each halving of Room lowers it 10 dB; a session saved
before Room existed loads it at 0. It feeds on the microphones' mid signal,
is spread by Stereo Width like the pair (0 is mono), never reaches the
piezo or the Piezo output, and is energy-normalised at prepare and run at
the host rate divided to 64 kHz or under. Broadband level and decay are
covered across rates. Each late line now preserves its accepted 48 kHz
absorption decay at DC and 8 kHz (fs/4 below a 32 kHz internal rate); the
48 kHz reference coefficients are unchanged. A one-pole mapping cannot
match the complete continuous-frequency curve at every sample rate.
The two-microphone, earlier-onset room experiment was rejected by the listener;
the [room report](room-2026-10-10.md) records that retained tradeoff.
It renders the same at any block
size, costs about 4% of the engine's time while it sounds and 148 KB per
instance, and at 0 is an exact no-op once it has rung out. The engine's own
default is 0, so the benchmark, the demos and every test that does not ask
for it are the dry instrument. Output scales the dry microphones and the
existing room tail together; it does not change the room send or leave old
room energy behind at the previous output level.

### Outputs

| Output | Channels | What it carries |
| --- | --- | --- |
| **Main** (`Output`) | stereo, always on | Whatever Capture selects, with the piezo beneath it at Piezo Mix. |
| **Piezo** | mono, optional | The under-saddle piezo and its preamp, whatever Capture selects. |

The piezo observes the same vibrating instrument in the same pass, so it can be
recorded, processed or blended separately while Main keeps following Capture.
Both outputs are seven samples later than the strings, at every rate and for
every Capture: the piezo chain's output is seven samples behind its input
(its preamp stages), and since 2026-09-30 the microphones are held back by
the same seven samples, so a Capture crossfade and Piezo Mix sum the two
sensors as the instrument moved them. Before, the blend summed them 146 us
apart at 48 kHz and 73 us apart at 96 kHz, a comb whose notches moved with
the rate; now the blend's spectrum at 48 and 96 kHz agrees within 0.2-0.4 dB
on average (OutputBusTests). `AcustraEngine::outputLatencySamples()` is that
fixed latency; `Performer::latencySamples()` adds the Gather Chords window
when it is on, and the plug-in reports the sum to the host (7 samples, or
1447 at 48 kHz while gathering).
It plays at the level Main has when Capture selects Piezo (the same Output and
construction reference) and has its own copy of the headroom-only safety
limiter, so with Capture on Piezo it is either side of Main sample for sample.
Enabling it never changes Main by a bit, and an idle instrument is exact
silence on both.

In the VST3 plug-in, Piezo is an optional auxiliary output bus, off by
default, so hosts and saved sessions that know only the stereo output are
unchanged; enable it in the host's output/bus configuration. Audio Units have
no disabled buses, so an AU host sees both outputs; Main is still the first.
A disabled bus is not rendered; an enabled one adds a limiter and a store a
sample, since the piezo is computed for Main anyway.

The plug-in plays at host rates from 8 kHz to 384 kHz
(`AcustraEngine::maximumSupportedSampleRate`). At any other rate the engine
would run at a rate other than the host's (it clamps a finite rate to the
nearer bound and uses 48 kHz for a zero, negative or non-finite rate), so
every note would play off pitch; both outputs stay silent instead and the
editor's status line reads UNSUPPORTED RATE.

In code, `AcustraEngine::process(left, right, buses, numSamples)` and
`Performer::beginBlock(left, right, buses, numSamples)` take an
`AcustraEngine::OutputBuses { piezo }` whose null pointer means "not wanted";
the two-pointer forms render Main alone and are the same samples.

**An idle instance sleeps.** With no string played (held, sustained,
damping or still to come in a strum), Main and Piezo under -120 dBFS for
half a second and a two-second strum rest since the last struck key (the
Rack Extension's rule), and the engine's own residue cleared, the plug-in
stops computing the strings. The engine clears what still rings under the
threshold once everything has stayed under 1e-11 for 80 ms
(`AcustraEngine::suspend`), so an instance never played sleeps after half
a second and a played one, in the cases measured, 7 to 10 s after its
output fell under -120 dBFS, 12 to 17 s after its last key. Its blocks
are then exact zeros, at about 0.5 µs instead of about 62 µs per 64-frame
block at 48 kHz (rough figures from an unquiet shared build machine; a
playing instance is unchanged within measurement noise). The engine's
clocks run on while it sleeps, so the next note meets the picking hand,
vibrato and strum direction where it would have: whatever the block size,
the output is bit for bit that of an instance that never slept, in every
case tested. A struck key wakes it for its block; controllers, bend,
keyswitches and RPNs reach it asleep, and a parameter change wakes it for
the half second its glides need. A bypassed instance sleeps too. See
[the decision](../decisions.md).

### Why this architecture

The 2026
[measurement-informed nonlinear guitar model](https://dafx26.mit.edu/assets/papers/DAFx26_paper_40.pdf)
motivates Acustra's measured bridge compliance and measured force-to-pressure
body radiation (its EJ45 string data served the nylon strings, removed on
2026-09-29); its one-way treatment of idle
strings has been replaced by a two-way six-string junction. Acustra keeps a
waveguide string solver for present realtime use; it
does not yet implement that paper's full geometrically exact nonlinear string.

The 2026
[differentiable Karplus--Strong study](https://dafx26.mit.edu/assets/papers/DAFx26_paper_38.pdf)
shows how delay, frequency-dependent loss and pluck geometry can be matched with
multi-scale spectral objectives. Acustra applies that idea offline to bounded
physical parameters while retaining its stiff-string and measured bridge/body
solver at runtime.

The 2010
[passive admittance-matrix guitar model](https://www.dafx.de/paper-archive/2010/DAFx10/BankKarjalainen_DAFx10_P60.pdf)
motivates a future measured 2-D bridge. Acustra does not invent the missing
cross-axis measurement: a passive rotated scalar approximation improved a
coarse aggregate score but worsened held-out two-stage decay, so it is not in
the shipping topology.

The 2024
[real-time nonlinear guitar study](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_50.pdf)
demonstrates a stable geometrically nonlinear string with fretboard, fret and
stopping-finger contact. Those mechanisms inform the roadmap, but Acustra does
not mislabel its present bounded pitch glide as that solver. A 2025
[comparison of acoustic, electro-acoustic and optical sensors](https://www.mdpi.com/1424-8220/25/21/6514)
examines the different waveforms those sensors observe on a plucked and struck
string. It does not identify a reciprocal 2-D guitar-bridge transfer matrix.
The second axis Acustra radiates is the part of that matrix the archive does
measure - the rocking a sideways crown force drives, through the published
crown height - not the in-plane translation, which remains unmeasured.

The 2025
[robotic plucking-depth study, posted in 2026](https://arxiv.org/abs/2606.24356)
separates small depth changes from pick speed: deeper strokes generally increase
level and strengthen low harmonics within a mounting, while remounting changes
the result substantially. It supplies no universal MIDI-velocity brightness
law. Release depth, speed and contact geometry therefore remain separate
physical questions in the excitation work.

[`ModalPreloadProbe.cpp`](../../Tools/ModalPreloadProbe.cpp) and
[`PrototypeModalPreload.py`](../../Tools/PrototypeModalPreload.py) provide a reproducible
mechanical reference for a string held before release, using
[Woodhouse's constrained modal construction](https://euphonics.org/wp-content/uploads/2022/03/Guitar_I.pdf).
It couples one low-E string to the Original bridge and all six fixed tail
anchors, solves static equilibrium under a stated 1 N point force, then releases
it with zero initial velocity. At 128 string modes, static displacement is
within 0.20% of the infinite pinned stiff-string reference;
free-release energy balance passes at 44.1, 48 and 96 kHz. The tool reports modal
convergence and time-integration error separately. It also observes the force
on the free body, including the fixed anchors, and checks separate string,
body and anchor work balances. Its finite-mode release-force jump is quantified
as a truncation effect, not a measured contact transient. It has no acoustic radiation,
MIDI-force calibration or runtime role; it is a reference for future source
changes, not a validated audible improvement. Compile/export/run instructions
are in the two files.

### Recordings are calibration references only

The separate `AcustraReferenceBank` contains 280 CC0 regions over MIDI 38--84:

- 272 Shinyguitar acoustic-microphone regions for steel: 17 roots, four
  velocity layers and four round robins;
- 8 Eastman E1D finger-plucked anchors for reproducible evaluation.

Its 41 classical-guitar regions, the nylon strings' references, were removed
with the nylon strings on 2026-09-29.

Only the offline physical-fit renderer and reference-bank tests link this
target. The plug-in links `AcustraDSP` alone, so none of the recorded audio is
present in or playable by the runtime instrument.

Training uses 54 of those regions and development validation 14, and the eight
Eastman E1D regions are a third split that is rendered, scored and reported but
never fitted and never used to gate anything. They matter because of what they
are: the only flat-top steel acoustic in the bank, while the whole steel
calibration is fitted against a miked archtop. Their absolute score is not
comparable with the other two splits — different rows, as always — but the same
eight rows scored on two builds are comparable with each other, which is the
use. Two caveats travel with them. Eight rows at one captured dynamic leave the
dynamics term empty, and the guitar was tuned about four cents sharp of A440
across them, from -0.5 to +10.3 cents by its own settled-H1 roots, so part of
that split's tuning term is the recording's tuning rather than the model's.

The fitter renders the physical engine deterministically and compares it with
those references using seven weighted robust terms: attack spectra, harmonic
envelopes, settled tuning and inharmonicity, early pitch trajectory,
partial/band decay, body spectrum and velocity dynamics. The decay term
compares decay rates in dB/s rather than log2(T60), because what a decay error
does to a note is a level difference and after t seconds that difference is
exactly the rate error times t; the relative form scored the same 1.4x error
identically whether it meant 25 dB or 3 dB after one second. The trajectory
term measures common H1--H8 pitch movement over 20--100, 80--200 and
180--400 ms relative to settled partials. The calibration stores 25 bounded
values, in the order of `NAMES` in
[`OptimizePhysicalModel.py`](../../Tools/OptimizePhysicalModel.py). Only the supported
shared-body, steel-string and shared-body-refine stages remain; `BY_EAR` records
the fixed listener choices. The cleanup removes the 13 controls for rejected
or disabled direct-radiation, axial, end-correction, impact and contact-noise
experiments. Existing active values and their relative order are retained.
Older 37-value and nylon-era 48-value vectors are refused rather than silently
reinterpreted. The fitter does not copy audio or fit an arbitrary playback filter.

That loss reduces each partial to one robust log2(T60), which is why
[`Tools/AuditDampingCurve.py`](../../Tools/AuditDampingCurve.py) exists alongside it:
it pools per-partial early decay rates by absolute frequency and reports dB/s,
so a damping error can be located on the frequency axis rather than averaged
into one number per partial.

The following table is the frozen 2026-09-04 refit comparison; its Shipping
column names the model released then, and its training and development rows
include the classical recordings the benchmark held then. The current force/moment body retains
that string/bridge calibration and replaces four body factors with the neutral
values auditioned on 2026-09-05. These historical dry-note scores have not been
recomputed for that integration. Lower is better in the robust descriptor score:

| Split | Rows | Neutral baseline | Fitted | Shipping | Change |
| --- | ---: | ---: | ---: | ---: | ---: |
| Training | 83 | 8.299141 | 5.662377 | 6.111540 | −26.36% |
| Development validation | 24 | 7.597316 | 5.893771 | 6.072344 | −20.07% |
| Flat top (reported only) | 8 | 9.315695 | 6.913712 | 8.073304 | −13.34% |

The one split that answers whether the refit generalises is the frozen test
split, which no fit, screen or refit has ever rendered: 188 archtop regions on
the sixteen roots the fit uses. It is scored once per release and never fitted.
The refit moves it from 6.324463 to 5.728151, 9.4% better, and every one of its
seven terms improves — attack 8.110 to 6.870, harmonics 8.205 to 8.122, tuning
3.019 to 2.311, pitch trajectory 0.315 to 0.275, decay 4.421 to 3.929, body
11.506 to 10.504, dynamics 0.0945 to 0.0188. That is the evidence against
reading the flat top's 1.6% regression as overfitting: on the larger unseen
split the refit is better everywhere, and the flat top is eight rows of one
guitar tuned about four cents sharp.

The shipping column is not the fitted one. The Fitted column is what the
bounded pattern search of 2026-09-04 converged on by itself; the user heard it
against the calibration that shipped before it, level-matched with the letters
unread, and chose it by ear, which is what licensed the direction. It did not
license the vector: the search had run two values onto bounds that contradict
what this repository has measured, and both are pinned back in what ships —
`bridgeMobilityScale` to the archive's own measured mobility, and the
saddle-to-anchor length to DAFx-26's published 3.25 mm stub (retired on
2026-10-10, when the strings came to end on the bridge). That pinning is
what the two columns differ by, and it costs 7.9% on training, 3.0% on
development validation and 16.8% on the eight flat-top rows. The five values
an earlier listener moved off the fit's answer on 2026-08-31 — the body's
low-mode gain and residue tilt, steel's fundamental T60 and frequency loss,
and the plate conductance floor — were frozen at that verdict during the
search, as was the axial resonators' gain switched off by ear on 2026-09-01,
so the search never saw any of the six. `Docs/decisions.md` records these
verdicts and the subsequent force/moment body selection.
Read the whole table as a snapshot rather than a constant: every column moves
on every engine change, and the comparison that means anything is paired,
same-note and same-engine.

The neutral column moves with the engine, not only with the calibration, so it
is re-rendered rather than carried: the two-point bridge, nylon's measured
classical body and the lossless fractional-delay read all landed while it
stood at 8.768535, 8.145301 and 9.309315, which is what it read on the engine
of 2026-09-02. On that engine the constant six-string anchor scored 8.768535
and 8.145301 where the played-subset anchor scored 9.671509 and 9.277751 on
the same rows; `Docs/decisions.md` records that trade, the gate clause it
fails, and why the change ships regardless.

Read those figures as a paired measurement, not as constants. Halving the decay
descriptor's analysis window, which no result should depend on, moves the
development-validation score by 0.9% and its decay term by 6.2%. The shift is
common-mode, so differences between two builds scored the same way survive it —
the plate conductance floor wins its ablation at both resolutions and from both
starting points — but an absolute score is only comparable against another taken
over identical rows with identical settings, which is why development validation
stays at the same 24 rows even when the training corpus grows.

The neutral baseline uses unity scales, direct branch off, velocity-brightness
depths zero, aperture exponent one, low-body gain one, steel nonlinear
displacement off, the legacy +0.018 steel fret-decay slope, a shared high-loss
cutoff scale of one, a plate conductance floor of zero and a 20 mm
saddle-to-anchor length. It is re-rendered on
the current runtime and re-scored under the current descriptors, so it is
comparable with the fitted column beside it rather than with the earlier
26-mode-bridge figures. Every term improves against it on both splits. These
measurements establish a repeatable movement toward the reference corpus, not
perceptual equivalence to a guitar.
The held-out split is
a development validation set used to gate promotions; it is not an untouched
final test. A new captured blind set and controlled listening remain necessary.

The [real dry-note benchmark](real-dry-note-benchmark.md) compares the fit with
real recordings, and [verification](../validation/verification.md) records the
measured checks behind these claims.
