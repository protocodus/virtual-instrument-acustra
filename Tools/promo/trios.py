#!/usr/bin/env python3
"""More demos: Irish tunes and John Dowland, each arranged for three steel-string guitars.

    python3 Tools/promo/trios.py <out-dir> <dowland-dir>   # writes <out-dir>/<piece>/<part>.mid

Every part is one Acustra on a factory patch, played on repertoire.py's
Player (the shared breathing tempo, each hand's own timing, the phrase's rise
and fall); `pan` and `level` are its place in the mix demo_audio.py makes.

The Irish tunes are traditional (The Session's first settings, written here
as ABC bars); their accompaniments are written here.

- Morrison's Jig (The Session 71), E dorian: the tune an octave down, as a
  guitarist flatpicks it, on the jumbo; the palm-muted guitar keeps the bass
  and the off-beat chords, open fifths without a third, as a jig is backed;
  the parlor joins the tune at its written octave in the second part and
  plays a chord note under it when the first part returns.
- Drowsy Maggie (The Session 27), reel, E dorian turning to D: the tune an
  octave down on the dreadnought, the DADGAD guitar's bass and chops, the
  jumbo doubling at the written octave from the second time through.
- Sí Bheag Sí Mhór (Turlough O'Carolan, c. 1691; The Session 449), D major,
  at its written pitch: the tune on the parlor, the DADGAD guitar breaking
  each bar's chord from its low strings, and on the repeats the dreadnought
  a chord note under each long note of the tune.

The Dowland pieces are played as written, voice for voice, read from Laura
Conrad's editions of the original prints (serpentpublications.org,
~lconrad/music/dowland/<piece>/score.mid) and Peter Chubb's Mutopia edition
of Come Again (ftp/DowlandJ/ALS17/ComeAgain/ComeAgain.mid). Those files are
fetched into <dowland-dir> and never committed. Guitar 1 has the cantus,
guitar 2 the inner voices and guitar 3 the bass (and, in the five-part
consort pieces, a middle voice above it); the bass goes to the DADGAD guitar
where it runs below the low E string.

- Come Again (First Booke of Songes, 1597), four voices, two verses: the
  first with cantus, tenor and bass, the second with every voice.
- Now, O now, I needs must part (the Frog Galliard; First Booke, 1597).
- The Earl of Essex Galliard and Lachrimae Antiquae (Lachrimae, 1604), five
  parts.
"""
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from performance import breathe, n  # noqa: E402
from repertoire import below, melody, players, push, roll, wobble  # noqa: E402

# Reason's song tempo for each piece, quarter notes a minute.
TEMPO = {"morrisons": 160, "drowsy-maggie": 192, "si-bheag": 104,
         "come-again": 168, "now-o-now": 216, "essex-galliard": 208, "lachrimae": 96}
D_MAJOR = (2, 4, 6, 7, 9, 11, 1)   # and E dorian
SHARP = "C C# D D# E F F# G G# A A# B".split()
TONES = {"Em": (4, 7, 11), "D": (2, 6, 9), "G": (7, 11, 2), "A": (9, 1, 4), "Bm": (11, 2, 6)}


def name(pitch):
    return f"{SHARP[pitch % 12]}{pitch // 12 - 1}"


def abc(bars):
    """ABC bars in two sharps, L:1/8, as [[(name, eighths)]]: the letters,
    their octave marks and lengths, and `(3` triplets; ornaments are left to
    the player."""
    out = []
    for bar in bars.split("|"):
        notes, triplet = [], 0
        for three, letter, marks, length in re.findall(r"(\(3)|([A-Ga-g])([,']*)(\d*)", bar.replace("~", "")):
            if three:
                triplet = 3
                continue
            pitch = {"C": 61, "D": 62, "E": 64, "F": 66, "G": 67, "A": 69, "B": 71}[letter.upper()]
            pitch += 12 * (letter.islower() + marks.count("'") - marks.count(","))
            notes.append((name(pitch), float(length or 1) * (2 / 3 if triplet else 1)))
            triplet = max(0, triplet - 1)
        if notes:
            out.append(notes)
    return out


def dance(tempo, seed, lilt, specs, form, bar, half, close, cuts=0.045):
    """A jig or a reel for tune, backing and a second tune player. `form` is
    [(bars, chords, loud, layers)]: one chord each `half` bar ("Em D"), and
    `layers` names who joins the tune: "bass" (the backing's roots only),
    "back" (bass and off-beat chords), "octave" (the third guitar on the tune
    at its written octave) or "under" (a chord note under each tune note)."""
    parts = players(tempo, 0.007, seed, 4 * bar, 7.0, lilt, *specs)
    lead, back, third = (part.p for part in parts)
    start = 0
    for index, (bars, chords, loud, layers) in enumerate(form):
        at = start
        for b, notes in enumerate(bars):
            at = melody(lead, at, notes, loud, bar, shift=-12, scale=D_MAJOR, cuts=cuts, pulse=half)
            if "octave" in layers:
                melody(third, start + bar * b, notes, loud - 4, bar, lean=0.014, scale=D_MAJOR, cuts=cuts, pulse=half)
            if "under" in layers:
                beat = start + bar * b
                for pitch, length in notes:
                    chord = TONES[chords[b].split()[min(1, int((beat - start - bar * b) // half))]]
                    third.play(third.at(beat, lean=0.014), length * 0.9, beat, below(n(pitch), chord), loud - 9)
                    beat += length
        for b, pair in enumerate(chords):
            for h, chord in enumerate(pair.split()):
                beat = start + bar * b + half * h
                root = 40 + (TONES[chord][0] - 4) % 12   # from the low E string up
                if not ({"bass", "back"} & set(layers)):
                    continue
                # the root on the bar, its fifth on the half bar
                back.play(back.at(beat, lean=-0.008), half * 0.55, beat, root + (7 if h else 0), loud + (2 if h == 0 else -3))
                if "back" in layers:   # the off-beat chord: fifth, octave and the fifth above, no third
                    for off in ((2,) if half == 3 else (2, 3)):
                        roll(back, beat + off, [root + 7, root + 12, root + 19], 0.7, loud - 10 - 3 * (off == 3), 0.008, lean=0.0)
        start += bar * len(bars)
    # The close: after the tune's last bar, the home chord stroked slowly and left to ring.
    end = start
    low, tones = close
    roll(back, end, low, 6.0, form[-1][2] - 4, 0.035)
    roll(lead, end, tones[:3], 6.0, form[-1][2] - 6, 0.05, lean=0.1)
    roll(third, end, tones[3:], 6.0, form[-1][2] - 10, 0.08, lean=0.2)
    return parts


def morrisons():
    A = abc("E3 B3|EBE AFD|EDE B3|dcB AFD|E3 B3|EBE AFD|G3 FGA|dAG FED")
    B = abc("Bee fee|aee fee|Bee fee|a2g fed|Bee fee|aee fee|gfe d2A|BAG FGA|"
            "Bee fee|aee fee|Bee fee|faf def|g3 gfe|def g2d|edc d2A|BAG FED")
    chA = ["Em Em", "Em D", "Em Em", "Bm D", "Em Em", "Em D", "G D", "D D"]
    chB = ["Em Em", "Em Em", "Em Em", "D D", "Em Em", "Em Em", "G D", "G D",
           "Em Em", "Em Em", "Em Em", "D D", "G G", "D G", "A D", "G D"]
    form = [(A, chA, 74, ["bass"]), (A, chA, 80, ["back"]), (B, chB, 86, ["back", "octave"]),
            (A, chA, 82, ["back", "under"]), (A, chA, 86, ["back", "under"]), (B, chB, 90, ["back", "octave"])]
    beats = [6 * len(f[0]) for f in form]
    tempo = wobble(push(breathe(320, [(sum(beats[:i]), b) for i, b in enumerate(beats)], swell=0.025, settle=0.03, last=0.14), 0.04), 6, 0.008, 71)
    return dance(tempo, 71, 0.09, [("melody", "Jumbo Steel Pick", 0.0, 0.0), ("backing", "Palm Muted Steel Pick", -0.58, -5.0),
                                   ("second", "Parlor Steel Fingerstyle", 0.55, -5.0)],
                 form, 6, 3, (["E2", "B2", "E3", "B3"], ["E3", "G3", "B3", "E4", "B4"]))


def drowsy_maggie():
    A = abc("E2BE dEBE|E2BE AFDF|E2BE dEBE|BABc dAFD")
    B = abc("d2fd c2ec|defg afge|d2fd c2ec|BABc dAFA|d2fd c2ec|defg afge|afge fded|BABc dAFD")
    chA = ["Em Em", "Em D", "Em Em", "Bm D"]
    chB = ["D A", "D A", "D A", "Bm D", "D A", "D A", "A D", "Bm D"]
    form = []
    for time, loud in enumerate((76, 84, 90)):
        second = ["octave"] if time else []
        form += [(A, chA, loud, ["back" if time else "bass"]), (A, chA, loud + 2, ["back"] + second), (B, chB, loud + 4, ["back"] + second)]
    beats = [8 * len(f[0]) for f in form]
    tempo = wobble(push(breathe(384, [(sum(beats[:i]), b) for i, b in enumerate(beats)], swell=0.02, settle=0.02, last=0.12), 0.05), 8, 0.007, 27)
    return dance(tempo, 27, 0.0, [("melody", "Dreadnought Steel", -0.10, 0.0), ("backing", "DADGAD Mono Mic", 0.58, -5.0),
                                  ("second", "Jumbo Steel Pick", -0.55, -4.0)],
                 form, 8, 4, (["E2", "B2", "E3", "B3"], ["E3", "G3", "B3", "E4", "B4"]))


def si_bheag():
    A = abc("f3e d2|d2 de d2|B4 A2|F4 A2|BA Bc d2|e4 de|f2 f2 e2|d4 f2|B4 e2|A4 d2|F4 E2|D4 e2|B4 e2|A4 dc|d6|d4 de")
    B = abc("f2 fe d2|ed ef a2|b4 a2|f4 ed|e4 a2|f4 e2|d4 B2|A4 BA|F4 E2|D4 f2|B4 e2|A4 a2|ba gf ed|e4 dc|d6|d4 de")
    chA = "D D G D G A D D G D D D G A D D".split()
    chB = "D D G D A D Bm A D D G D G A D D".split()
    form = [(A, chA, 70, False), (A, chA, 76, True), (B, chB, 82, False), (B, chB, 80, True)]
    tempo = wobble(breathe(208, [(48 * i, 48) for i in range(8)], swell=0.04, settle=0.08, last=0.3), 6, 0.01, 449)
    parts = players(tempo, 0.009, 449, 48, 9.0, 0.0, ("melody", "Parlor Steel Fingerstyle", -0.10, 0.0),
                    ("chords", "DADGAD Mono Mic", 0.55, -7.0), ("under", "Dreadnought Steel", -0.55, -9.0))
    lead, harp, under = (part.p for part in parts)
    melody(lead, -2, [("D5", 1), ("E5", 1)], 68, 6, scale=D_MAJOR, pulse=2)   # the pickup
    root = {"D": 38, "G": 43, "A": 45, "Bm": 47}
    for index, (bars, chords, loud, repeat) in enumerate(form):
        start = 96 * index
        last = index == len(form) - 1
        for b, notes in enumerate(bars):
            beat = start + 6 * b
            if last and b == 15:   # the close: the tune's D over a D chord stroked from the low string
                lead.play(lead.at(beat, lean=0.012), 10.0, beat, n("D5"), loud)
                roll(harp, beat, ["D2", "A2", "D3", "A3", "D4", "F#4"], 10.0, loud - 6, 0.06)
                roll(under, beat, ["F#4", "A4"], 10.0, loud - 12, 0.09, lean=0.3)
                break
            melody(lead, beat, notes, loud, 6, scale=D_MAJOR, pulse=2)
            r = root[chords[b]]
            # A's third would sit a semitone under the tune's passing D: its upper fifth instead
            tenth = r + {"Bm": 15, "A": 19}.get(chords[b], 16)
            for k, (pitch, quiet) in enumerate([(r, 9), (r + 7, 15), (tenth, 13)]):   # root, fifth and tenth, a quarter each
                harp.play(harp.at(beat + 2 * k, lean=-0.006), 5.8 - 2 * k, beat + 2 * k, pitch, loud - quiet)
            if repeat:
                at = beat
                for pitch, length in notes:
                    if length >= 2:
                        under.play(under.at(at, lean=0.014), length * 0.95, at, below(n(pitch), TONES[chords[b]]), loud - 9)
                    at += length
    return parts


def voices(path):
    """A format 1 file's tracks as [[(beat, beats, pitch)]], in quarter notes."""
    data = pathlib.Path(path).read_bytes()
    division = struct.unpack(">H", data[12:14])[0]
    out, at = [], 14
    while at < len(data):
        length = struct.unpack(">I", data[at + 4:at + 8])[0]
        track, at = data[at + 8:at + 8 + length], at + 8 + length
        i, tick, status, on, notes = 0, 0, 0, {}, []
        while i < len(track):
            delta = 0
            while True:
                delta, byte, i = delta << 7 | track[i] & 0x7F, track[i], i + 1
                if byte < 0x80:
                    break
            tick += delta
            if track[i] == 0xFF:
                size = track[i + 2]
                i += 3 + size
                continue
            if track[i] & 0x80:
                status, i = track[i], i + 1
            kind = status & 0xF0
            a, b = track[i], track[i + 1] if kind not in (0xC0, 0xD0) else 0
            i += 1 if kind in (0xC0, 0xD0) else 2
            if kind == 0x90 and b:
                on[a] = tick
            elif kind in (0x80, 0x90) and a in on:
                first = on.pop(a)
                notes.append((first / division, round((tick - first) / division * 48) / 48, a))
        if notes:
            out.append(sorted(notes))
    return out


def consort(path, tempo_bpm, seed, hands, low_guitar, verses=((),), bar=6, phrase=24):
    """A Dowland piece, voice for voice. `hands` gives each guitar its voices
    (indices, top voice first); `verses` is one entry a time through, naming
    the voices left out of it."""
    parts_in = voices(path)
    length = max(s + d for v in parts_in for s, d, _ in v)
    length = bar * round(length / bar)
    total = length * len(verses)
    phrases = [(b, min(phrase, total - b)) for b in range(0, int(total), phrase)]
    tempo = wobble(breathe(tempo_bpm, phrases, swell=0.03, settle=0.06, last=0.3), bar, 0.01, seed)
    parts = players(tempo, 0.009, seed, phrase, 8.0, 0.0, ("cantus", "Parlor Steel Fingerstyle", 0.0, 0.0),
                    ("inner", "Dreadnought Steel", -0.55, -6.0), ("bass", low_guitar, 0.55, -6.0))
    for part, mine in zip(parts, hands):
        p = part.p
        for verse, silent in enumerate(verses):
            sounding = set()
            for k in mine:
                if k in silent:
                    continue
                top, lowest = k == 0, k == len(parts_in) - 1
                centre = sum(x[2] for x in parts_in[k]) / len(parts_in[k])
                for start, held, pitch in parts_in[k]:
                    beat = verse * length + start
                    if (beat, pitch) in sounding:   # two voices of one guitar on a unison
                        continue
                    sounding.add((beat, pitch))
                    final = verse == len(verses) - 1 and start + held >= length - 1e-6
                    loud = (82 if top else 72 if lowest else 66) + 4 * verse + (3 if beat % bar < 1e-6 else 0)
                    loud += max(-4.0, min(5.0, 0.5 * (pitch - centre))) if top else 0.0
                    lean = 0.012 if top else -0.008 if lowest else 0.0
                    p.play(p.at(beat, lean=lean), (held + (6 if final else 0)) * 0.97, beat, pitch, loud)
    return parts


def dowland(folder):
    return {
        "come-again": lambda: consort(folder / "ComeAgain.mid", 168, 1597, [(0,), (1, 2), (3,)], "Mahogany Steel Thumb",
                                      verses=((1,), ()), bar=4, phrase=16),
        "now-o-now": lambda: consort(folder / "nowonow.mid", 216, 1598, [(0,), (1, 2), (3,)], "Mahogany Steel Thumb"),
        "essex-galliard": lambda: consort(folder / "essex.mid", 208, 1604, [(0,), (1, 3), (2, 4)], "DADGAD Mono Mic"),
        "lachrimae": lambda: consort(folder / "antiquae.mid", 96, 1605, [(0,), (1, 2), (3, 4)], "DADGAD Mono Mic", bar=4, phrase=16),
    }


def pieces(folder):
    return {"morrisons": morrisons, "drowsy-maggie": drowsy_maggie, "si-bheag": si_bheag, **dowland(pathlib.Path(folder))}


if __name__ == "__main__":
    out = pathlib.Path(sys.argv[1])
    for piece, build in pieces(sys.argv[2]).items():
        (out / piece).mkdir(parents=True, exist_ok=True)
        for part in build():
            (out / piece / f"{part.name}.mid").write_bytes(part.p.smf())
            print(f"{out / piece / part.name}.mid\t{part.patch}\t{TEMPO[piece]}")
