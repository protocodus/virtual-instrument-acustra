# Flagship audit, 1.9.66 to 1.9.68 — 2026-11-26

A pass over the shipping instrument at the owner's request: sound first (no
artifact, consistency, balance), then playability (fingering, fret noise,
natural imperfections), then CPU and seamless operation. Source 1.9.66
(`9a459ca`), then 1.9.67 and 1.9.68; 48 kHz, 256-frame blocks, Release, gcc 13.3 unless a
line says otherwise, through `acustra::Performer`, on one shared development
machine (a 2.1 GHz Xeon guest). Nobody listened: every figure below is a
signal measurement, and what it cannot say is listed at the end. The probes
were one-off programs and are not in the repository, except where a test or
tool is named.

## Found and fixed

| Version | Found | Fix | Evidence |
| --- | --- | --- | --- |
| 1.9.67 | A line played legato, each key held a moment into the next, was thrown across the neck: a note never took a string whose key was down, so each overlapped note went a string across and three to five frets up. A two-octave C major scale from C3, keys overlapping 20 ms, reached the 20th fret of the A and low E strings (F4, C4); detached it stays under the 8th. | Once the playing has shown a legato overlap, the next note takes the last note's string when no free string is under the hand ([decision](../decisions.md)). | `Acustra.HandAllocator`: `testLegatoLineKeepsItsString`, `testNotesLeftToRingKeepTheirStrings`. The eight factory demos render bit for bit as 1.9.66. |
| 1.9.68 | A chord played live, its keys a few milliseconds apart, ended beyond one hand: the first key is placed before the rest are known, and a note that had sounded kept its string (figures below). | The chord still forming is refretted as one shape, and a sounded note that must move is plucked again; the owner chose it from an A-B set ([decision](../decisions.md)). | `Acustra.HandAllocator`: `testRolledTriadIsOneHandShape`, `testHandTimedStrums`, `testLiveChordReach` (0 of 214 live, 63 with attacks kept). |

## Chords played live, as measured on 1.9.67

A chord whose keys arrive on one sample is always fingered in one hand.
Played live on 1.9.67, the first key was placed before the rest were known
and a note that had sounded kept its string (since 2026-10-02). Over 24
chord types on every root from E2 (683 chords that have a one-hand shape, a
fresh hand each): bottom-up, keys 8 or 20 ms apart, 391 (57%) ended in a
shape no hand holds and 183 more than on one sample lost a held key's
string; in random order 3-25 ms apart 323 (47%); top-down 5 (0.7%). In
twelve-key progressions with the hand's memory (903 such chords): 332 (37%)
bottom-up, 275 (30%) in random order, 26 (2.9%) top-down, none on one
sample. An exhaustive search of every rule that neither looks ahead nor
moves a sounded note, with the first key placed as on 1.9.67, still misses
50% of the bottom-up set, so no placement rule closes it. 1.9.68 moves the
just-sounded notes inside the 30 ms window, as before 2026-10-02: none of
either set misses in any key order, and each moved note is plucked twice.

## Found, and left

- **A Picking change under a ringing chord** steps its level over the 20 ms
  glide: −5.2 dB from Finger to Pick, −5.7 dB from Thumb to Pick, +3.9 dB from
  Pick to Finger, against the chord's own decay. The construction loudness
  table is applied at the output, and its Pick cells stand 6 dB under the
  Finger's since 1.9.63 moved the Pick back to 60 mm (1.25 dB when the step
  was first noted, 2026-09-29). No other switch or control jump left a tick:
  after a Shape, Wood, Capture or Tuning change no band from 300 Hz up rises
  more than 0.9 dB over the chord's level before it, and a String Age jump
  moves the band above 8 kHz at −82 dBFS.

## Sound: every combination

Shape x Wood x Picking x Capture (108 combinations) at the default controls,
eight phrases each (single notes E2-B5, a velocity ladder, six strummed
chords, eighth-note strumming, palm-muted picking, tremolo, bend, vibrato and
overlapped notes, and an arpeggio under the pedal): 864 renders, 75 s each
combination.

- No non-finite sample; DC under 4.4e-5 on every file.
- No tick away from a pluck: above 6 kHz, no 2 ms window stands 8 times over
  the preceding 100 ms except at note-ons and, 72-86 dB under full scale, at
  key-ups and pedal lifts (the damping hand, Release Noise 10%).
- Hammer-ons and pull-offs (CC65 + CC84), octave slides on a 12-semitone
  wheel, 20 notes a second under the pedal, the top fret at velocity 127, the
  strum keys and the six noise keys, on three presets: the same, and every
  file returns under −95 dBFS.
- 162 corners of the nine continuous controls (each at 0 and 100%, all low,
  all high, random corners with random Shape, Wood and Tuning) for every
  Picking on the microphones and the piezo, twelve velocity-127 strums a
  second, a hard tremolo and a chord held 16 s: every peak at or under the
  −1.50 dBFS ceiling, every held chord under −130 dBFS by 18 s.
- `Tools/CalibrateConstructionLoudness.py --check`: all 36 cells within
  ±1 LU on every capture (stereo −0.90 to +0.74, mono −0.73 to +0.77, piezo
  −0.47 to +0.74). Its Pick headroom rule still fails on all 12 Pick cells
  (hardest case up to +1.31 dBFS before the output stage), as on 1.9.65.
- Drop D, DADGAD and Open G on the default construction and Jumbo Mahogany
  Pick: no fault; loudness −0.05, −0.68 and −0.52 LU (medians) from Standard.
- 22.05, 44.1, 88.2, 96 and 192 kHz against 48 kHz, three constructions, four
  phrases: integrated loudness within 0.52 LU. On chords and strums from
  44.1 kHz up every third octave to 8 kHz is within 2.0 dB, except
  Dreadnought Finger's 8 kHz band, 3.5-5.8 dB lower at 88.2-192 kHz (it lies
  64 dB under full scale at 48 kHz).

What the same renders say about balance, all of it already under README
Known gaps and none of it changed:

- Note to note (every semitone E2-C6, velocity 80, a fresh instrument each,
  stereo width 100%, the 36 Shape x Wood x Picking cells): the level against
  three neighbours each side spreads 1.75-3.30 dB RMS. The deepest holes are
  C6 on Dreadnought Mahogany (−9.8 dB, Thumb), F#5 on Parlor Maple (−8.9,
  Finger), E5 on Parlor Mahogany (−8.6) and D5 on Auditorium Maple (−8.5);
  the tallest note is A5 on Parlor Maple (+8.2). The piezo spreads
  0.80-1.95 dB. They follow the measured body's 490-600 Hz trough as Shape
  moves it.
- One note's two microphones differ by −10.4 to +12.2 dB at full width.
- The fundamental sits −13.8 to +7.5 cents from its fret near the body's
  modes (F#2 and E3 on Dreadnought Spruce); a fresh instrument's first note
  on a string always takes the same draw of the ±3 cent fretting scatter.
- With a Pick at the default controls one line holds a tenth of a strum's
  power on three constructions: 1318 Hz on Jumbo Spruce (−9.6 dB of
  70 Hz-11 kHz), 882 Hz on Parlor Maple (−9.7), 1176 Hz on Dreadnought Spruce
  (−10.7).
- Velocity 24 to 120 spans 27 dB with Finger and 33 dB with Pick, as 1.9.58
  set it toward released 1.3.3.

## Playability

- Detached scales, chromatic runs, arpeggios and leaps move no more than
  four frets between notes, and held C, Am and F arpeggios are fingered
  within a hand. Held arpeggios whose
  root and third share a string (D and A from the open string) keep a
  seven-fret shape, by the rule that a held note keeps its string.
- Release Noise at the presets' 10%: the damping hand and the fingers'
  slides add a signal 34-38 dB under a phrase of position shifts and barre
  changes; single events lie 35-39 dB under the music at that moment (median)
  and the loudest 25 dB under. At 50% the median is 23 dB under, at 100% 19.
- A legato line's first overlap is placed as before 1.9.67, because nothing
  has yet shown a line: E4 to G4 overlapped goes to the B string's 8th fret
  and E5 to G5 to its 20th.

## CPU and robustness

- The eight presets on the eight phrases, the fastest of five passes per
  block: 4.1-9.3% of one core on average, the 99th percentile block at most
  13.9% and the worst 18.1% (a six-string chord's first block), on this
  machine, which is not Reason's.
- libFuzzer under ASan and UBSan (`Acustra.DspFuzz`): 204 138 inputs in 30
  minutes on 1.9.66, 57 159 in 10 minutes on 1.9.67 and 36 326 in 7 minutes
  on 1.9.68, no finding.
- `ctest`: 130 of 130 on 1.9.66, 1.9.67 and 1.9.68 (`unit`, `audio` and
  `perf` tiers, gcc 13.3, Release). On 1.9.68 `Acustra.Performer` and
  `Acustra.VoicingRealism` first failed on the checks that pinned the rule of
  2026-10-02 (a sounded note never moves); those checks now hold the chosen
  rule and, with the switch off, the former one. The engine and Performer
  also build as C++17 under Clang 18 with `-Wall -Wextra -Wshadow -Werror`
  (`AcustraDSPCxx17`).

## What this did not establish

No listening. A clean signal check does not say a combination sounds good,
and the balance figures above are differences a listener may or may not
mind. The plug-in wrapper, its editor and the host validators were not
built here; the Rack device's own suite ran without the SDK.
