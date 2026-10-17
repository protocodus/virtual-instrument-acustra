# Performer research and baseline audit — 2026-10-16

Audited canonical commit `7b2b4c2659952f32560133a7eab21e48ae5779c1`, also frozen
under `out/realism-20261016/baseline`. Sections 1–3 preserve the original gap
audit and its evidence limits. Implementation and validation are recorded at
the end; none of these measurements establish listener preference.

## 1. Hammer-ons need externally supplied finger energy

**Observed gap.** `AcustraEngine::transitionNote` (baseline lines 9395–9529)
correctly retains the waves and transfers ownership, but scales its contact
pulse to `0.02 * fingerVelocity^2 * currentSlopeEnergy`. A quiet held string
therefore cannot be sounded by the finger, however firmly the player requests
the next hammer-on. The current tests deliberately enforce this dependency
(`Tests/LegatoRealismTests.cpp:150`). They are a regression contract for the old
model, not physical evidence for that limit.

**Primary research.** [Bilbao, Russo, Webb and Ducceschi, Real-Time Guitar
Synthesis, DAFx 2024](https://www.dafx.de/paper-archive/2024/papers/DAFx24_paper_50.pdf),
§2.5, equations 9–10, models the finger with its own mass, displacement and
external force. Its energy balance in §2.6, equation 13i, includes finger work
as an independent power input. This directly supports a bounded excitation
that can add energy to a quiet string. It does not supply a MIDI velocity map,
a universal contact duration, or a measured amplitude for Acustra.

**Quick implementation direction.** Keep the retained waves, explicit trigger,
smooth finite pulse, local travel, friction budget and ownership. Add an
independent, finite finger-work budget based on the existing force/velocity
scale and contact geometry, rather than deriving the whole pulse from the
remaining wave energy. Retain a strict bound and smooth endpoints. Label the
velocity-to-work magnitude as authored; measure it in sustained-note context
and a repeated trill, rather than claiming it is a measured hand force.

**A/B witness.** On string 3, sound G3 (MIDI 55) at velocity 102; keep its key
down for 0.1, 2, or 8 seconds, then explicitly hammer to A3 (57) at the same
finger velocity. Include a musical six-second trill after one initial pluck.
The candidate must articulate a deliberate late hammer without restarting the
picking noise, while ordinary overlapping notes remain ordinary plucks.

## 2. A pull-off is a fretting-hand pluck, not the same downward impact

**Observed gap.** The same function gives descending transitions the same
sin-squared pulse with a 3 ms rather than 2 ms duration, a sign change, and the
same residual-energy ceiling. Both directions inject only into the normal
plane (`AcustraEngine.cpp:12163–12178`); queued old transients do the same
(`7954–7968`). Thus the model has no distinct sideways release or independent
pluck work for the lifting finger. Merely increasing the common contact gain
would leave this distinction unmodeled.

**Direct performance evidence.** [Kim Perlak's Berklee guitar course excerpt](https://online.berklee.edu/takenote/guitar-techniques-hammer-ons-pull-offs-and-slides/)
explicitly distinguishes striking the higher fret for a hammer from plucking
the lower note with the finger that leaves the higher fret for a pull-off.
This is first-party instrumental instruction, not a numerical parameter source.
[Evangelista and Smith, DAFx 2010](https://www.dafx.de/paper-archive/2010/DAFx10/EvangelistaSmith_DAFx10_P21.pdf)
provides a passive scattering framework for compliant guitar pluck contacts.
It supports treating release as a contact process, but does not identify the
pull-off angle or work of this player.

**Quick implementation direction.** Preserve the old string waves, then add a
small independently bounded release from the departing finger. Place it at the
old fret within the newly longer speaking length; this normalized location is
`L_old / L_target = 2^(-interval/12)` from the bridge. Treat the normal/parallel
split as an explicit authored performance choice, not a universal measured
angle. Do not regenerate a complete ordinary-picking attack or move the hand
to the usual picking position. All live, queued and retained-tail copies of
the new excitation state must agree, including damping after immediate key-up.

**A/B witness.** On string 3, explicitly descend 57→55 and 55→50 (the open D3),
with quiet and firm finger velocities, after short and long holds. A musical
descending run and trill should keep the lower note audible without adding a
right-hand pick transient. Compare this mechanism independently from the
hammer improvement, even if they share a helper.

## Baseline numerical witness for both connected gestures

An off-tree C++17/O2 probe compiled the frozen baseline engine, at 48 kHz,
128-sample blocks, stock controls and string-per-channel mode. It played MIDI
55→57 for hammer and 57→55 for pull on channel 3, with initial velocity 0.8.
All transitions were accepted. The table is the contact-source slope energy
and stereo RMS of the first 500 ms after a transition at finger velocity 0.8:

| Gesture | Prior hold | Contact slope energy | Post-transition RMS |
| --- | ---: | ---: | ---: |
| Hammer | 0.1 s | 1.49233e-4 | 2.85050e-2 |
| Hammer | 2 s | 1.17072e-6 | 3.98600e-3 |
| Hammer | 8 s | 6.99668e-12 | 1.58369e-5 |
| Hammer | 16 s | 9.96484e-17 | 2.42417e-8 |
| Pull | 0.1 s | 3.52454e-6 | 2.16947e-2 |
| Pull | 2 s | 4.87685e-6 | 4.02395e-3 |
| Pull | 8 s | 1.92858e-10 | 4.11987e-5 |
| Pull | 16 s | 9.20503e-16 | 3.26614e-8 |

The source energy at velocity 0.8 is exactly 16 times the result at 0.2,
as the quadratic law requires, but almost no energy remains after a long hold.
Post-transition RMS includes residual string/body sound and is not isolated
injected work or a realism score. Different short-time phases mean stronger
finger velocity need not monotonically increase every 500 ms output window.

Probe: `/tmp/acustra-performer-research-20261016/probe.cpp`, SHA-256
`10ed8e19b083532342981e5434ce85bcee86cd29043f939a75a6e9276f90edc2`.
CSV: `/tmp/acustra-performer-research-20261016/probe.csv`, SHA-256
`acffb1ce7449a68ff339bf9e47142fde7167c5f9d5c401b6d2f173ef8e4f843d`.

The actual public trigger for either gesture is: CC65=127 on the source
channel, CC84=held source MIDI pitch, then exactly one destination Note On on
that channel. CC84 is consumed once. Send new CC84 before every next trill
note. Old-source Note Off is harmless after transfer. Ordinary overlap must
remain unchanged. The README says the current Rack wrapper cannot forward
these explicit gestures, so an offline Performer test does not establish a
playable RE articulation route.

## 3. Finger pairs can receive their own bounded timing model

**Observed gap.** `Performer::flushNoteGroup`, lines 459–465, treats two-note
groups as timed strokes only under Pick; the Finger pinch branch requires
three or four strings. Same-sample Finger pairs therefore sound together
even though three/four-note chords receive independent measured timing.

**Existing measured evidence.** The project's
`Docs/validation/playing-physics-2026-10-13/two-string-strokes-report.json`, made
by `Tools/MeasureTwoStringStrokes.py`, holds 293 adjacent-string Finger pairs:
absolute lag median 11.156 ms; quartiles 5.986 and 16.122 ms; 7.85% within
2 ms; 42.32% lower first and 49.83% higher first. The source dataset is
[Xi et al., GuitarSet, ISMIR 2018](https://archives.ismir.net/ismir2018/paper/000188.pdf),
which uses per-string hexaphonic audio and rich annotations. These numbers
come from our existing secondary analysis, not a table in the paper. No new
corpus measurement was run in this audit.

**Critical limitation.** The earlier playing-physics report explicitly deferred
this change because its onset groups do not establish whether the player meant
a simultaneous pinch or two sequential fingers. That uncertainty remains. A
default same-sample MIDI timing rule is an authored performance decision,
supported by the distribution, not newly proven by it. Explicitly sequenced
notes, controller-owned strings and latched strum intent should retain their
existing meaning. Do not describe it as a newly discovered missing feature.

**Quick candidate.** A signed uniform pair separation of ±22.312 ms gives an
absolute median 11.156 ms, quartiles 5.578/16.734 ms and 8.96% within 2 ms,
close to this corpus slice. Choose direction through that signed draw, put the
earlier member on the requested sample, and delay only the later member. This
needs no host latency or new UI. This is a proposed simple distribution, not
an evaluated fit, and it needs repeated-pair musical A/B against the unchanged
baseline, a schedule-distribution test, and per-string attack readback.

## Existing work that should not be counted again

Current main already has explicit legato contact/friction, pressure-biased
vibrato, one-sided tension vibrato, release-velocity damping, measured fretted
release fall, continuous wound-string slide noise, three/four-note Finger
chords, coherent picking gestures, repeated-pluck force variation, Pick pairs,
small-stroke timing, strum scatter, dead strokes, edge weighting and alternate
picking. Papers on those mechanisms alone do not establish another new win.

The historical AG-PT-set hammer audit in README lines 654–685 reports real
steel-string level ratios, with unknown finger velocities and onset inference.
It does not identify independent injected finger energy and its old engine
comparison predates the currently explicit legato implementation. Reusing
those levels as a direct numerical calibration would overstate the evidence.

## Implemented candidate and parameter provenance

`hammerWork`, `pullOffPluck` and `fingerPairs` independently enable the three
changes and default to true. They add no user controls or host latency. Each
disabled branch retains the old calculation and random streams. Ordinary
overlapping notes still pluck independently unless the existing explicit
Gather Chords option owns their timing.

The contact-work map is an authored synthesis law, **not measured force in
newtons**. With normalized finger velocity `v`, its reference displacement is
`A = 0.24 v² s [L p (1-p) / (0.648 × 0.16 × 0.84)] × tensionRatio`, where
`s` is 0.35 for a hammer and 0.30 for a pull, `L` is speaking length in metres,
and `p` is the fret-contact position from the bridge. The 0.24 magnitude and
0.16 open-length reference relate its scale to the existing picking model;
the quadratic velocity law is authored separately from the calibrated
picking-velocity exponent. The finite new slope-energy budget is
`A² / [N p (1-p)]`, with `N` the **physical** round-trip period in samples.
Phase compensation in the loop filter must not change this work. The old
`0.02 v² × residualSlopeEnergy` term remains; smooth contact and friction
share the combined budget rather than adding separate unbounded noise.

Hammer work remains normal to the fretboard and keeps the existing 2 ms
smooth pulse. Pull work uses the longer target string, with the old fret at
`p = 2^(-interval/12)`, and the existing 3 ms total duration. Its authored
profile loads over two thirds and releases over the final third; both ends
have zero displacement and slope. The pull's normal/parallel gains are
0.5 and sqrt(0.75), a unit-norm 25/75 energy split. These durations, force
fractions, velocity map, profile and polarization are authored choices;
the cited papers and instruction support independent work and a departing
finger's pluck, not these numerical values. No fresh ordinary-pick noise is
started. Live contact, a retained tail, and queued same-note replucks carry
the same waveform, two-plane direction and propagation history.

The pair law is a separate deterministic signed uniform draw in ±22.312 ms.
The earlier physical string starts at the grouped event's sample and the
later string receives the absolute separation. Existing three/four-note
chord draws are independent. It applies to automatic Finger pairs with a
complete two-string plan, including pairs explicitly collected by Gather
Chords. With Gather off, even 10 ms separation is preserved exactly. Gather
already intentionally replaces sub-30 ms key timing with one delayed chord;
that existing declared ownership also applies to the new pair law. Latched
stroke direction, string-per-channel ownership, Thumb and Pick keep their
previous meaning.

`Tests/PerformerContactTests.cpp` checks quiet and naturally decayed held
strings, velocity/work scaling, physical work across 8–384 kHz, smooth
endpoints, signed direction, unit-norm polarization, open/fretted pull
locations, independent ablations, public CC65+CC84 ownership and host block
partition invariance. It also checks same-note queued packets and refret
tails at five contact ages, damping in both planes, panic clearance, pair
distribution, exact per-string scheduling, random-stream independence,
prepare repeatability, panic continuation, and the explicit-timing/Gather cases above. Existing
`LegatoRealismTests` keeps its residual-only ≤2% contract by disabling the
two new work flags in that specific audit; its other contracts remain active.

The standalone contact suite compiled and passed on 2026-10-16 with C++17,
`-O2 -DNDEBUG -Wall -Wextra -Wpedantic`, with no warnings. Its pass covers all
checks listed above. Integrated regression results and full-score audio A/B
are recorded separately by the parent validation run.

### Preliminary public-score activation

The parent's first twelve-scene render of `Tools/RenderRealismQuickWins.cpp`
reported the following fixed-score witnesses on 2026-10-16. These are
preliminary objective activation measurements, **not a human preference
result or a new calibration target**. The renderer exercises the public
Performer/MIDI route and independent flags.

| Witness | Baseline | Candidate | Meaning |
| --- | ---: | ---: | --- |
| Late hammer, held 5.8 s; RMS over 6.0–6.6 s | −74.42 dBFS | −29.60 dBFS | The finger can sound a nearly quiet held string. |
| Late pull to open D; corresponding RMS window | −74.83 dBFS | −34.60 dBFS | The departing finger supplies audible work to the retained open string. |
| Connected phrase, overall RMS | reference | +3.53 dB relative | The phrase contains added articulation energy; louder alone does not establish realism. |
| Finger pairs, difference RMS relative to baseline RMS | reference | +0.78 dB | Independent timing materially changes this score's waveform; phase changes make this unsuitable as a quality score. |

The finite work law and its tests remain the authored bounds above. These
renders are listening witnesses rather than grounds for relaxing those bounds.
