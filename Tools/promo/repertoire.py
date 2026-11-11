#!/usr/bin/env python3
"""The promo's three performed pieces, each arranged for a quartet of steel-string guitars.

    python3 Tools/promo/repertoire.py <out-dir>   # writes <out-dir>/<piece>/<part>.mid

Every part is one Acustra set to a factory patch, and each piece takes four
of the eight, so the promo plays all of them; `pan` (-1 left .. +1 right)
and `level` (dB against the melody, on matched loudness) are the part's place
in the mix promo_audio.py makes. The pieces, the accompaniments and the way
they are played follow Aculyre's lyre promo (virtual-instrument-aculyre
Tools/promo/repertoire.py, 313396e), re-voiced for guitars:

- range: standard tuning runs from E2 to C6 (the high E string's 20th fret),
  so the lyres' parts above C6 move down and their bass can use the low E
  and, in DADGAD and Open G, the low D;
- six strings: a guitar sounds at most six notes, so the closing chords are
  guitar voicings, rolled across the strings as a slow stroke;
- keys: the guitar has no sharping levers, so a cut may come from F sharp,
  and The Butterfly goes back to its own E minor, the guitar's home key;
- the lyre harp's glissando becomes a run up the scale into the last chord.

Played as people play, on performance.py's Performance, with fixed seeds:

- tempo: the players share one clock that breathes with the phrases, holds
  back at the cadences, wanders a little from bar to bar, and in the jigs
  pushes on as a session does;
- timing: each player has a hand of its own (onsets a few milliseconds off
  the grid, a slow drift ahead of and behind the others), the accompanists
  sit a touch behind the tune, and the jigs lilt: the middle quaver of each
  three comes late and light;
- velocity: every part follows the phrase's rise and fall, the tune leans on
  the bar and the pulse and plays its high notes stronger, and no two notes
  land alike;
- ornament: the jigs' repeated notes are cut with a grace note from above, as
  Irish players part them, and the minuet's cadence notes take one too.

The tunes are public domain; the other parts are written here.

- Minuet in G, BWV Anh. 114 (Christian Petzold, Notebook for Anna Magdalena
  Bach, 1725), the first strain twice at its own pitch: the melody on the
  parlor guitar and Petzold's bass under the thumb of the mahogany
  auditorium begin alone; the dreadnought joins with the harmony, held and
  then broken across the bar; for the last eight bars the guitar in open G
  plays the nearest chord note under the tune on each beat (the lyres took
  the melody an octave up, above the guitar's top C).
- The Kesh, Irish jig (traditional; The Session, tune 55, setting 1), in G:
  the tune an octave down, as a guitarist flatpicks it, on the jumbo, alone
  with its own bass strings the first time; then the palm-muted dreadnought
  keeps the jig's bass and off-beat chords, the DADGAD guitar joins the tune
  at its written octave in the B part, and the parlor rings chord notes
  above the last time through; the bass runs down into each new part.
- The Butterfly, Irish slip jig (traditional; The Session, tune 10, setting
  1), in E minor at its written pitch: the tune on the dreadnought, broken
  chords on the DADGAD guitar from its low D, the mahogany guitar's thumb
  marking the pulse, and on each part's repeat the piezo guitar a chord note
  under the tune; the third part first falls back to the tune over held
  chords, then everyone plays it out.
"""
import collections
import math
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from performance import Performance, breathe, n  # noqa: E402

Part = collections.namedtuple("Part", "name patch pan level p")
# Reason's song tempo for each piece, quarter notes a minute: the engine
# times its key-up join and its stroke alternation by it. The jigs' written
# beat is the quaver.
TEMPO = {"minuet": 116, "kesh": 153, "butterfly": 168}


class Player(Performance):
    """One player of the quartet: Performance's hand, plus what playing with
    others adds. `phrase` is the written phrase length in beats, whose rise
    and fall every part's velocity follows (`swell` steps); `lilt` delays the
    middle note of each three beats by that share of a beat and plays it
    lighter (the jigs' swing); `behind` seats the player that many seconds
    after the clock; the drift is a slow wander either side of it."""

    def __init__(self, tempo, seed, timing, phrase, swell, lilt=0.0, behind=0.0):
        super().__init__(tempo, seed=seed, timing=timing)
        self.phrase, self.swell, self.lilt, self.behind = phrase, swell, lilt, behind
        own = random.Random(seed + 7)
        # two slow sines, about 5 and 3 ms, periods of several seconds
        self.wander = [(amount * own.uniform(0.7, 1.3), own.uniform(*period), own.uniform(0, 2 * math.pi))
                       for amount, period in ((0.005, (7.0, 11.0)), (0.003, (2.5, 4.0)))]

    def middle(self, beat):
        return self.lilt and abs(beat % 3 - 1) < 1e-6

    def at(self, beat, lean=0.0, scale=1.0):
        clock = self.seconds(beat)
        drift = sum(a * math.sin(2 * math.pi * clock / period + phase) for a, period, phase in self.wander)
        late = self.lilt * 60.0 / self.bpm(beat) if self.middle(beat) else 0.0
        return max(0.0, clock + lean + self.behind + drift + late + self.scatter(scale))

    def play(self, onset, beats_held, beat, note, velocity, channel=0):
        x = (beat % self.phrase) / self.phrase
        # up to the phrase's peak past its middle, then away to its end
        rise = math.sin(math.pi * x ** 0.85) - 0.55 - 0.45 * max(0.0, (x - 0.85) / 0.15)
        super().play(onset, beats_held, beat, note, velocity + self.swell * rise - (5 if self.middle(beat) else 0), channel)


def wobble(points, step, amount, seed):
    """The tempo curve with a little wander from one `step` of beats to the next."""
    curve, rng = Performance(points, seed=0), random.Random(seed)
    first, last = points[0][0], points[-1][0]
    marks = sorted({b for b, _ in points} | {first + step * k for k in range(1, int((last - first) / step))})
    return [(b, curve.bpm(b) * (1.0 if b in (first, last) else 1.0 + max(-2, min(2, rng.gauss(0, 1))) * amount)) for b in marks]


def push(points, by):
    """The tempo curve pushed on by the share `by` from its first beat to its last."""
    first, last = points[0][0], points[-1][0]
    return [(b, bpm * (1.0 + by * (b - first) / (last - first))) for b, bpm in points]


def players(tempo, timing, seed, phrase, swell, lilt, *specs):
    """One Player a part, on the piece's shared tempo: (name, patch, pan, level).
    The first carries the tune; the others sit 4 to 9 ms behind it."""
    return [Part(*spec, Player(tempo, seed + 101 * k, timing, phrase, swell, lilt, behind=0.0 if k == 0 else 0.004 + 0.0025 * (k % 3)))
            for k, spec in enumerate(specs)]


G_MAJOR = (7, 9, 11, 0, 2, 4, 6)   # and E minor


def melody(p, start, notes, loud, bar, lean=0.012, shift=0, scale=G_MAJOR, cuts=0.0):
    """(name, length in beats) from `start`, in bars of `bar` beats: the bar's
    first note strongest, the first of each other three beats a little
    stronger, as a player leans on the pulse, and the bar's high notes
    stronger than its low ones. A name written "~X" takes a grace note, the
    scale's next note above, just before it (the tune's own octave only);
    with `cuts` (seconds) so does every note that repeats the one before, as
    an Irish player cuts a repeated note to part it from the last."""
    pitches = [n(name.lstrip("~")) for name, _ in notes if name]
    centre = sum(pitches) / max(1, len(pitches))
    at, before = start, None
    for name, length in notes:
        if name:
            pitch = n(name.lstrip("~")) + shift
            lead = 3 if abs(at % bar) < 1e-6 else 0
            strong = abs(at / 3 - round(at / 3)) < 1e-6
            contour = max(-4.0, min(5.0, 0.5 * (pitch - shift - centre)))
            onset = p.at(at, lean=lean)
            grace = 0.07 if name.startswith("~") and not shift else cuts if pitch == before else 0.0
            above = next(x for x in range(pitch + 1, pitch + 4) if x % 12 in scale)
            if grace and above <= 84:   # C6, the top of the guitar
                p.message(onset - grace, (0x90, above, p.velocity(loud - 12)))
                p.message(onset - 0.006, (0x80, above, 0), 0)
            p.play(onset, length * (0.96 if length > 1 or not p.lilt else 0.9), at, pitch, loud + (4 + lead if strong else 0) + contour)
            before = pitch
        at += length
    return at


def roll(p, beat, names, held, loud, spread=0.03, lean=-0.01):
    for k, name in enumerate(names):
        p.play(p.at(beat, lean=lean + spread * k, scale=0.6), held, beat, n(name) if isinstance(name, str) else name, loud - 2 * k)


def below(pitch, chord):
    """The nearest note of the chord (pitch classes) a third or more under `pitch`."""
    return max(x for x in range(pitch - 9, pitch - 2) if x % 12 in chord)


def minuet():
    # (name, quarters) per bar, the first strain; bars 9-14 repeat 1-6
    head = [[("D5", 1), ("G4", .5), ("A4", .5), ("B4", .5), ("C5", .5)], [("D5", 1), ("G4", 1), ("G4", 1)],
            [("E5", 1), ("C5", .5), ("D5", .5), ("E5", .5), ("F#5", .5)], [("~G5", 1), ("G4", 1), ("G4", 1)],
            [("C5", 1), ("D5", .5), ("C5", .5), ("B4", .5), ("A4", .5)], [("B4", 1), ("C5", .5), ("B4", .5), ("A4", .5), ("G4", .5)]]
    tune = head + [[("F#4", 1), ("G4", .5), ("A4", .5), ("B4", .5), ("G4", .5)], [("~A4", 3)]] \
        + head + [[("A4", 1), ("B4", .5), ("A4", .5), ("G4", .5), ("F#4", .5)]]
    bass = [[("G3", 2), ("A3", 1)], [("B3", 3)], [("C4", 3)], [("B3", 3)], [("A3", 3)], [("G3", 3)],
            [("D4", 1), ("B3", 1), ("G3", 1)], [("D4", 1), ("D3", .5), ("C4", .5), ("B3", .5), ("A3", .5)],
            [("B3", 2), ("A3", 1)], [("G3", 1), ("B3", 1), ("G3", 1)], [("C4", 3)], [("B3", 1), ("C4", .5), ("B3", .5), ("A3", .5), ("G3", .5)],
            [("A3", 2), ("F#3", 1)], [("G3", 2), ("B3", 1)], [("C4", 1), ("D4", 1), ("D3", 1)]]
    # the harmony a beat at a time, between the bass and the melody; where the
    # bass moves under one bar's chord (7, 13, 15) the notes follow it
    G, C, Am, D = ("B3", "D4", "G4"), ("C4", "E4", "G4"), ("A3", "C4", "E4"), ("A3", "D4", "F#4")
    inner = [G, G, C, G, Am, G, ("A3", "D4", "B3"), D, G, G, C, G, ("A3", "C4", "D4"), G, ("E4", "D4", "C4")]
    # each bar's chord as pitch classes, for the line under the tune (D with its seventh)
    chord = {"G": (7, 11, 2), "C": (0, 4, 7), "Am": (9, 0, 4), "D": (2, 6, 9, 0)}
    harmony = ["G", "G", "C", "G", "Am", "G", "D", "D", "G", "G", "C", "G", "D", "G", "D"]
    passes = [(0, 66), (48, 80)]
    # a dance at an easy walk: each eight bars swell and give way at their cadence, the last broadly
    tempo = wobble(breathe(116, [(s + 24 * h, 24) for s, _ in passes for h in range(2)][:-1] + [(72, 24)], swell=0.05, settle=0.11, last=0.34),
                   3, 0.012, 1725)
    parts = players(tempo, 0.009, 1725, 24, 9.0, 0.0, ("melody", "Parlor Steel Fingerstyle", 0.0, 0.0),
                    ("bass", "Mahogany Steel Thumb", 0.55, -6.0), ("harmony", "Dreadnought Steel", -0.60, -8.0),
                    ("under", "Open G Slide-ready", 0.25, -12.0))
    lead, low, mid, under = (part.p for part in parts)
    for start, loud in passes:
        last = start == passes[-1][0]
        held = 6.0 if last else 2.9   # the cadence, the last one held long
        at = start
        for bar in tune:
            at = melody(lead, at, bar, loud, 3)
        melody(lead, at, [("~G4", held)], loud, 3)
        at = start
        for bar in bass:
            at = melody(low, at, bar, loud - 6, 3, lean=-0.008)
        roll(low, at, ["G2", "G3", "D4"] if last else ["G3", "D4"], held, loud - (12 if last else 8), 0.06 if last else 0.03)
        # The quartet gathers: melody and bass begin alone, the harmony joins for the
        # strain's second half, breaks its chords across the bar the second time,
        # and the open-G guitar walks under the tune for the last eight bars.
        for bar, notes in enumerate(inner):
            beat = start + 3 * bar
            if last:
                for k, name in enumerate(notes):
                    mid.play(mid.at(beat + k, lean=-0.004), 2.9 - k, beat + k, n(name), loud - 14 + 2 * k)
            elif bar >= 8:
                roll(mid, beat, notes[:2], 2.8, loud - 12)
        roll(mid, at, G, held, loud - (15 if last else 12), 0.07 if last else 0.03, lean=0.02)
        if last:
            for bar in range(8, len(tune)):
                beat, sounding = start + 3 * bar, []
                for name, length in tune[bar]:   # the tune's note on each beat
                    if abs(beat - round(beat)) < 1e-6:
                        sounding.append((beat, n(name.lstrip("~"))))
                    beat += length
                for k, (beat, pitch) in enumerate(sounding):
                    under.play(under.at(beat, lean=0.006), 0.95, beat, below(pitch, chord[harmony[bar]]), loud - 9 + (3 if k == 0 else 0))
            roll(under, at, ["B3", "D4", "G4", "B4"], held, loud - 12, 0.07, lean=0.2)
    return parts


def kesh():
    # (name, eighths); ABC (The Session 55, setting 1) an octave down
    A = [[("G3", 3), ("G3", 1), ("A3", 1), ("B3", 1)], [("A3", 3), ("A3", 1), ("B3", 1), ("D4", 1)],
         [("E4", 1), ("D4", 1), ("D4", 1), ("G4", 1), ("D4", 1), ("D4", 1)], [("E4", 1), ("D4", 1), ("B3", 1), ("D4", 1), ("B3", 1), ("A3", 1)],
         [("G3", 1), ("A3", 1), ("G3", 1), ("G3", 1), ("A3", 1), ("B3", 1)], [("A3", 3), ("A3", 1), ("B3", 1), ("D4", 1)],
         [("E4", 1), ("D4", 1), ("D4", 1), ("G4", 1), ("D4", 1), ("B3", 1)], [("A3", 1), ("G3", 1), ("F#3", 1), ("G3", 3)]]
    B = [[("B3", 1), ("A3", 1), ("B3", 1), ("D4", 1), ("B3", 1), ("D4", 1)], [("E4", 1), ("G4", 1), ("E4", 1), ("D4", 1), ("B3", 1), ("D4", 1)],
         [("B3", 1), ("A3", 1), ("B3", 1), ("D4", 1), ("B3", 1), ("G3", 1)], [("A3", 1), ("B3", 1), ("A3", 1), ("A3", 1), ("G3", 1), ("A3", 1)],
         [("B3", 1), ("A3", 1), ("B3", 1), ("D4", 1), ("B3", 1), ("D4", 1)], [("E4", 1), ("G4", 1), ("E4", 1), ("D4", 1), ("B3", 1), ("D4", 1)],
         [("G4", 1), ("F#4", 1), ("G4", 1), ("A4", 1), ("G4", 1), ("A4", 1)], [("B4", 1), ("G4", 1), ("F#4", 1), ("G4", 3)]]
    # one chord a dotted quarter
    chordsA = ["G G", "D D", "C C", "G D", "G G", "D D", "C C", "D G"]
    chordsB = ["G G", "C C", "G G", "D D", "G G", "C C", "G D", "D G"]
    # the jumbo's own bass strings under the tune when it plays alone: a root
    # on the bar and a fifth or fourth on each half, all under the tune's G3
    dyad = {"G": ("G2", "D3"), "D": ("A2", "D3"), "C": ("C3", "G2")}
    # the backing's bass, on the bar and on the half bar, and its off-beat chord
    boom = {"G": ("G2", "D3"), "D": ("D3", "A2"), "C": ("C3", "G2")}
    chop = {"G": ("B3", "D4", "G4"), "D": ("A3", "D4", "F#4"), "C": ("C4", "E4", "G4")}
    # chord notes above the tune, at most the guitar's top C
    bell = {"G": ("B5", "G5"), "D": ("A5", "F#5"), "C": ("C6", "G5")}
    form = [(A, chordsA, 74), (A, chordsA, 80), (B, chordsB, 84), (B, chordsB, 88)]
    # a session's jig: steady in the bar, 4 % faster by the end, a breath at each turn of the tune
    tempo = wobble(push(breathe(306, [(48 * i, 48) for i in range(4)], swell=0.025, settle=0.03, last=0.16), 0.04), 6, 0.008, 55)
    parts = players(tempo, 0.007, 55, 24, 7.0, 0.09, ("melody", "Jumbo Steel Pick", 0.04, 0.0), ("backing", "Palm Muted Steel Pick", -0.58, -5.0),
                    ("octave", "DADGAD Mono Mic", 0.55, -4.0), ("bells", "Parlor Steel Fingerstyle", 0.30, -11.0))
    lead, back, upper, bells = (part.p for part in parts)
    for index, (part, chords, loud) in enumerate(form):
        start = 48 * index
        at = start
        for bar in part:
            at = melody(lead, at, bar, loud, 6, cuts=0.045)
        if index >= 2:
            at = start
            for bar in part:
                at = melody(upper, at, bar, loud - 4, 6, lean=0.014, shift=12, cuts=0.045)
        for bar, pair in enumerate(chords):
            for half, chord in enumerate(pair.split()):
                beat = start + 6 * bar + 3 * half
                final = index == len(form) - 1 and bar == 7 and half == 1
                if index == 0:
                    low, high = dyad[chord]
                    if half == 0:
                        lead.play(lead.at(beat, lean=-0.01), 2.8, beat, n(low), loud - 16)
                    lead.play(lead.at(beat, lean=-0.004), 2.8, beat, n(high), loud - 20)
                    continue
                if final:   # the close: a G chord stroked slowly up from the bass and left to ring
                    roll(back, beat, ["G2", "B2", "D3", "G3", "B3", "G4"], 5.0, loud - 4, 0.035)
                    roll(bells, beat, ["G5", "B5"], 5.0, loud - 10, 0.08, lean=0.15)
                    continue
                if bar == 7 and half == 1:   # the turn of the tune: the bass runs down to the next part's G
                    for k, name in enumerate(["G3", "E3", "D3"]):
                        back.play(back.at(beat + k, lean=-0.006), 0.9, beat + k, n(name), loud - 4 + 3 * k)
                    continue
                back.play(back.at(beat, lean=-0.008), 1.6, beat, n(boom[chord][half]), loud + (2 if half == 0 else -3))
                roll(back, beat + 2, chop[chord], 0.7, loud - 10, 0.008, lean=0.0)
                if index == 3:
                    bells.play(bells.at(beat, lean=0.004), 2.9, beat, n(bell[chord][half]), loud - 12)
    return parts


def butterfly():
    # (name, eighths); ABC (The Session 10, setting 1), E minor
    A = [[("B4", 2), ("E4", 1), ("G4", 2), ("E4", 1), ("F#4", 3)], [("B4", 2), ("E4", 1), ("G4", 2), ("E4", 1), ("F#4", 1), ("E4", 1), ("D4", 1)],
         [("B4", 2), ("E4", 1), ("G4", 2), ("E4", 1), ("F#4", 3)], [("B4", 2), ("D5", 1), ("D5", 2), ("B4", 1), ("A4", 1), ("F#4", 1), ("D4", 1)]]
    B = [[("B4", 2), ("D5", 1), ("E5", 2), ("F#5", 1), ("G5", 3)], [("B4", 2), ("D5", 1), ("G5", 2), ("E5", 1), ("D5", 1), ("B4", 1), ("A4", 1)],
         [("B4", 2), ("D5", 1), ("E5", 2), ("F#5", 1), ("G5", 2), ("A5", 1)], [("B5", 2), ("A5", 1), ("G5", 2), ("E5", 1), ("D5", 1), ("B4", 1), ("A4", 1)]]
    C = [[("B4", 3), ("B4", 2), ("A4", 1), ("G4", 2), ("A4", 1)], [("B4", 3), ("B4", 1), ("A4", 1), ("B4", 1), ("D5", 1), ("B4", 1), ("A4", 1)],
         [("B4", 3), ("B4", 2), ("A4", 1), ("G4", 2), ("A4", 1)], [("B4", 2), ("D5", 1), ("G5", 2), ("E5", 1), ("D5", 1), ("B4", 1), ("A4", 1)]]
    harmony = {"A": ["Em Em Bm", "Em Em D", "Em Em Bm", "G G D"],
               "B": ["G Em G", "G C D", "G Em C", "Em C D"],
               "C": ["Em Em G", "Em Em D", "Em Em G", "G C D"]}
    # the DADGAD guitar's roots, from its low D string
    root = {"Em": "E2", "G": "G2", "D": "D2", "Bm": "B2", "C": "C3"}
    tones = {"Em": (4, 7, 11), "G": (7, 11, 2), "D": (2, 6, 9), "Bm": (11, 2, 6), "C": (0, 4, 7)}
    # the thumb's pulse, a low note on the beat and one above it before the next
    pulse = {"Em": ("E3", "B3"), "G": ("G3", "D4"), "D": ("D3", "A3"), "Bm": ("B2", "F#3"), "C": ("C3", "G3")}
    # the third part falls back to the tune over held chords, then everyone plays it out
    form = [("A", A, 78), ("A", A, 82), ("B", B, 86), ("B", B, 91), ("C", C, 80), ("C", C, 96)]
    # a slip jig that lifts through its second part and broadens into the close
    tempo = wobble(push(breathe(336, [(36 * i, 36) for i in range(len(form))] + [(36 * len(form), 9)], swell=0.03, settle=0.04, last=0.5), 0.03),
                   9, 0.008, 10)
    parts = players(tempo, 0.006, 10, 18, 7.0, 0.07, ("melody", "Dreadnought Steel", -0.02, 0.0), ("chords", "DADGAD Mono Mic", 0.55, -7.0),
                    ("pulse", "Mahogany Steel Thumb", -0.60, -9.0), ("under", "Piezo Stage Steel", 0.25, -11.0))
    lead, harp, thumb, under = (part.p for part in parts)
    for index, (name, part, loud) in enumerate(form):
        start = 36 * index
        at = start
        for bar, notes in enumerate(part):
            at = melody(lead, at, notes, loud, 9, cuts=0.045)
            if index % 2:   # the repeat: under each note of the tune, the bar's nearest chord note a third or more below
                beat = start + 9 * bar
                for pitch, length in notes:
                    chord = tones[harmony[name][bar].split()[min(2, int(beat - start - 9 * bar) // 3)]]
                    under.play(under.at(beat, lean=0.014), length * 0.96, beat, below(n(pitch), chord), loud - 8)
                    beat += length
        for bar, chords in enumerate(harmony[name]):
            for third, chord in enumerate(chords.split()):
                beat = start + 9 * bar + 3 * third
                # root, fifth and tenth broken across the dotted quarter
                r = n(root[chord])
                tenth = r + (15 if chord in ("Em", "Bm") else 16)
                if index == 4:   # held: one rolled chord where the harmony changes
                    if third == 0 or chord != chords.split()[third - 1]:
                        roll(harp, beat, [r, r + 7, tenth], 2.9, loud - 8, 0.03)
                    continue
                for k, (pitch, quiet) in enumerate([(r, 10), (r + 7, 16), (tenth, 14)]):
                    harp.play(harp.at(beat + k, lean=-0.006), 2.9 - k, beat + k, pitch, loud - quiet)
                if index:
                    low, high = pulse[chord]
                    thumb.play(thumb.at(beat, lean=-0.008), 1.8, beat, n(low), loud - 6)
                    thumb.play(thumb.at(beat + 2, lean=0.0), 0.9, beat + 2, n(high), loud - 14)
    # The close: a run up the scale into E minor, stroked slowly across all six strings.
    end = 36 * len(form)
    run = [n(x) for x in ("E4", "F#4", "G4", "A4", "B4", "D5", "E5")]
    t0 = lead.at(end - 0.6, scale=0.3)
    for k, pitch in enumerate(run):
        lead.message(t0 + 0.05 * k, (0x90, pitch, max(48, 74 - 2 * k)))
        lead.message(t0 + 0.05 * k + 0.3, (0x80, pitch, 0), 0)
    for k, name in enumerate(["E2", "B2", "E3", "G3", "B3", "E4"]):
        lead.play(lead.at(end, lean=0.33 + 0.07 * k, scale=0.4), 8.0, end, n(name), 86 - 3 * k)
    roll(harp, end, ["E2", "B2", "G3"], 8.0, 80, 0.09, lean=0.3)
    roll(thumb, end, ["E3", "B3"], 8.0, 80, 0.09, lean=0.34)
    roll(under, end, ["E5", "B5"], 8.0, 74, 0.1, lean=0.7)
    return parts


PIECES = {"minuet": minuet, "kesh": kesh, "butterfly": butterfly}

if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1])
    for name, build in PIECES.items():
        (out / name).mkdir(parents=True, exist_ok=True)
        for part in build():
            (out / name / f"{part.name}.mid").write_bytes(part.p.smf())
            print(f"{out / name / part.name}.mid\t{part.patch}\t{TEMPO[name]}")
