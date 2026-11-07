# Coupled tuning: what the string is tuned to, 1.9.26 — 2026-11-07

**Question.** The owner asked again for the physical model to be reviewed,
suspecting the recent rounds, and for every playable note at two velocities
to be checked for outliers in tone, level, resonance and tuning. The 1.9.25
round checked level and tone; this one found the tuning wrong and fixed it.

## Tools

- `Tests/ModalTuningTests.cpp` (the oracle): reconstructs the digital bridge,
  all twelve string loops and the microphone observer independently of the
  engine's tuning helper, finds the poles of the whole coupled system by
  Newton from sixteen starts, and reports the radiated-energy-weighted centre
  and the dominant pole of each fixture's fundamental, in cents from the
  nominal.
- `Tools/MeasureFretboardSweep.py` gained attack and settled pitch and the
  inharmonicity coefficient per note, and `AcustraPerformanceRenderer
  --exact-intonation 1` renders without the 1.9.21 fretting-pitch draw
  (±3 cents fretted, ±1 open), so a sweep measures the instrument's tuning
  and not the performance's.
- Probes in the working directory (not committed): the engine's own detune,
  the full root list of a note with each root's heard energy, the detune and
  loop delays through a vibrato, and the solve's cost per block and per
  note-on.

Baseline 1.9.25 (`e60a6bb`); candidate the working tree of 1.9.26. 48 kHz,
Release, GCC, default controls, Room 0, exact intonation.

## Found wrong

### 1. The string was tuned to the member nobody hears

Every string has two transverse polarisations, and through the rocking
saddle they form a pair of coupled modes a few cents apart, each a mix of
the two planes. Since 1.8 the engine lengthens both loops by one detune so
that the pair is heard at the requested pitch (`coupledPolarisationDetune`).
1.9.25 chose which member of the pair to tune by the energy each would have
left after three seconds. The member that moves the bridge radiates and
decays fast; the one that hardly moves it rings long. After three seconds
the long, quiet member always wins, so the string was tuned to the member
that is not heard, and the heard member landed wherever the pair's split put
it.

Measured by the oracle at 48 kHz, before and after:

| Note | 1.9.25 centre | 1.9.26 centre | dominant pole |
| --- | ---: | ---: | ---: |
| Jumbo, open E2 | −10.1 c | −0.26 c | −0.30 c |
| Auditorium, open E2 | −10.5 c | within 2 c | |
| DADGAD, open D2 | −12.6 c | within 2 c | |
| Open-G tuning, G3 on the G string | +15.2 c | +1.56 c | +1.68 c |
| Dreadnought, E3 on the low E | | +0.51 c | −1.00 c |
| Dreadnought, D3 on the A string | | −1.04 c | −1.88 c |
| Dreadnought, E3 on the D string | | +0.27 c | +5.20 c |

The split is widest where the fundamental sits on a body mode, which is
where a guitar's tuning matters most and where 1.9.25 was 8-15 cents flat
(or sharp, when the pair sat the other way round).

### 2. What the solve now does

The poles of the two coupled loops are the roots of det(I − E M(s)): E the
planes' common delay, M the saddle's reflection with the body and the five
resting strings folded into it (the Schur complement the junction itself
uses). They are found in s by Newton (a difference slope at the seed, the
secant after it) from seeds at each eigenvalue's phase; where the note meets
a partial of a resting string, which every fifth fret does with the next
open string, eight more seeds 7 and 14 cents either side at the string's
decay and four times it find the in-phase member of the unison, which the
eigenvalue seeds miss.

Each pole is weighed by its loudness over its life. Its radiated energy is
the squared residue of the loop's response to the pluck's wave (22-30 % in
the normal plane by Touch, `pluckNormalShare`), by Jacobi's formula
|adj(I−EM)·u|² / (|F′|·|tr adj|), times the fraction of its loss that leaves
through the bridge, (loss per period − the string's own) / loss; its
loudness is that energy's power to the 0.3 (Stevens' law, the sone),
integrated over its decay: (2αE)^0.3 / (0.6α). The detune is then solved so
that the weighted centre of the family sits on the nominal: a secant in the
detune, the pole family followed from one evaluation to the next (each pole
from where it was and from where a free pole would move to), the seeds
tried again only when a pole is lost. The twelve wide seeds (7, 14 and 21
cents either way at the string's decay and four times it) are always
tried: a note on a body mode has a third member the eigenvalue seeds miss
the same way a unison does.

Why loudness and not energy: by total energy a wolf's fast member (F3 on
the low E, 14 cents flat, gone in 0.45 s) outweighs the member that rings
10 s and carries the note, and the sweep read the low E's 12th-15th frets
sustaining 11-19 cents sharp. Loudness over life halves that: on the same
notes the attack reads 3-9 cents flat and the sustain 4-5 sharp. Neither
rule can land both members of a 12-22 cent pair; the three rules were
rendered as a listening set (`Docs/decisions.md`, 2026-11-07).

A plain note's centre is its heard member. A unison's members veer apart as
the string is detuned, trading energy; no detune puts one of them alone on
the nominal, so the pair is centred, which is the oracle's criterion too.
On E3 on the D string the members are 46 % and 54 % of the heard energy and
12 cents apart, so the dominant pole reads +5.2 cents while the centre is
within 0.3; the oracle's dominant-pole check is relaxed only where no member
holds two thirds of the energy.

### 3. The detune followed the vibrato

The solve ran at the frequency the loss is designed at, which carries the
vibrato, a member channel's bend and the fretting draw, so it re-ran every
32-sample control period of a vibrato. With the new solve that cost 100-900
µs per 64-sample block (1.9.25: 6 µs), and on a unison the family was found
in one period and a member missed in the next, moving the detune by 3.8
cents at the vibrato's rate (the Engine suite read the vibrato at 11.4 Hz
instead of its 4 Hz). The solve now runs once per contact, at the pitch
the finger stops: the fret and the slide the wheel holds when the string is
struck or re-plucked. A player tunes the string where the finger lands and
bends and shakes it by ear, so a later slide, the vibrato, the bend and the
fretting error ride on the detune of the stop they leave from, and a note
struck under a held wheel is tuned exactly as the fret it lands on
(`testSlideAttackMatchesItsPhysicalFret`). Under vibrato and under a slide
the detune is constant and the extra cost is 3 µs per block. The solve costs 139 determinant evaluations on average over every
string and fret, 255 at most; note-on plus its first block measures 306 µs
mean and 532 µs worst (1.9.25: 179 and 919).

### 4. The tuning depended on the sympathetic-strings switch

The solve left the resting strings out when the switch was off, so the two
renders the sympathetic test compares differed in tuning as well as in the
idle strings' sound, and on a unison by cents. The tuning is now the whole
guitar's with every idle string on its bridge; the switch takes their sound
out of the mix.

## Wolf notes, kept

F3 (174.6 Hz) on the low E's 13th fret and the D string's 3rd sits on the
dreadnought's top resonance. Its pair is a wolf: at zero detune one member
at −14 cents with a T60 of 0.45 s, the other at +9.6 cents ringing 9.7 s.
Tuned on its loudness centre the long member sits at +3.4 and the fast one
at −18.6, so the attack reads flat and the tail a little sharp, which is
what a wolf note does on the instrument. No detune makes both members land,
and choosing one would mistune the other; the centre is kept, and the sweep
reports the note. Whether a steel-string bridge's rocking really couples
this hard is the open question (README, Known gaps).

## Fixtures re-derived

- `Tests/ModalTuningTests.cpp`: weights by loudness over life, not the
  energy left after three seconds; the dominant-pole bound relaxed where no
  member holds three quarters of the loudness (the unison above, the
  jumbo's open E at 72 %); fixtures render with exact intonation.
- `Tests/ChorusRegressionTests.cpp`: references re-observed on 1.9.26. In
  tune, E3's fifth partial (824 Hz) sits 4 cents under a dreadnought body
  mode that answers 19 dB down and beats at 2 Hz by 2.6 dB; the modulation
  bound is 3.0 where it was 1.0, which the mistuned 1.9.25 passed by being
  off the mode.
- `Tests/FingerLookaheadTests.cpp`: the shift F#2→G#2 read 12.5 dB above
  its plain render on 1.9.25 and 11.8 dB since the low E's fretted notes
  moved 8-10 cents up; the bound is 3.5× where it was 4.0×.
- `Tests/AcustraEngineTests.cpp`: see the entries in the file, each beside
  its bound.

## Sweep

`Tools/MeasureFretboardSweep.py`, eight presets, six strings, frets 0-20,
velocities 48 and 112 (2016 notes), exact intonation, 48 kHz. Settled pitch
is the FFT peak 0.3-0.9 s after the pluck, attack pitch 0-0.3 s, both in
cents from equal temperament; on a two-member note the peak reads whichever
member is momentarily stronger, so these are the heard trade, not the poles.

| Rule | settled RMS | settled > 2 c | > 5 c | > 10 c | attack > 5 c |
| --- | ---: | ---: | ---: | ---: | ---: |
| A: 1.9.25, energy left after 3 s | 2.31 c | 337 | 99 | 21 | 364 |
| B: total radiated energy | 2.65 c | 228 | 95 | 48 | 293 |
| C: loudness over life (ships) | 1.92 c | 244 | 50 | 17 | 312 |

Settled notes beyond 5 cents per preset, A / B / C: Dreadnought 8 / 13 / 8,
Jumbo 9 / 15 / 3, Mahogany Thumb 12 / 15 / 8, Open G 15 / 11 / 6, Parlor
19 / 12 / 7, Piezo Stage 18 / 12 / 5, Palm Muted 9 / 8 / 6, DADGAD 9 / 9 /
7. Level, attack level, peak and inharmonicity are unchanged within the
measurement (sustain level ±0.3 dB sd, inharmonicity ±0.001); the
fundamental's T60 and the sustain centroid move only where a pair's
members changed places.

What is left beyond 10 cents under C is the unisons at velocity 48 (E3 on
the A string's 7th fret and the D string's 2nd, −18 and −15 settled, where
the pair beats through the window), F3 on the D string (the wolf, −14),
G3 on the low E (+11 to +12, its fast member) and the jumbo's 11th fret
(+11). None of these is a tuning error of the loop: each is a pair 12-22
cents wide whose members trade places with velocity and time.

## Not changed

The width of the pairs. Whether a steel-string bridge's rocking couples the
parallel plane this hard (22 cents at F3 on the low E) is the open
question; a measured rocking admittance would settle it (README, Known
gaps). The attack pitch's own spread (RMS 6 cents on every rule) is the
Kirchhoff-Carrier glide and the release transient, not the tuning.
