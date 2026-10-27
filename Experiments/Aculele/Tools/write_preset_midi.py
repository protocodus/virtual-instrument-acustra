#!/usr/bin/env python3
"""Write the preset demo phrases to Docs/audio/midi/<NN>-<slug>.mid.

One phrase per preset in Source/DSP/AculelePresets.h, written for what that
preset is for. Each phrase's last event falls 5.5-9.5 s in, so with the demo
renderer's 2.5 s tail every demo is 8-12 s long.

Most phrases are written as ukulele tab: Mono Mode On (CC126 = 4) on
channel 1 makes channels 1-4 the G, C, E and A strings, so a chord names the
string of every note and can double a pitch, as G (0232) and Am (2000) do.
A group of notes on the string channels at one moment is one stroke, timed
across the strings it names. Strum directions are latched with the
keyswitches C0 (down) and C#0 (up) before each stroke, as a player's hand
keeps its down-up motion through the skipped strokes. The picked melody
plays on one channel and lets the fretting hand choose its strings.

    python3 Tools/write_preset_midi.py [output-dir]
"""

import pathlib
import re
import struct
import sys

PPQ = 480
ROOT = pathlib.Path(__file__).resolve().parent.parent

DOWN, UP = 24, 25  # Performer::keyswitchDownstroke, keyswitchUpstroke

# Open strings, G C E A as channels 0-3, and chord shapes as frets.
STANDARD = (67, 60, 64, 69)
LOW_G = (55, 60, 64, 69)
D_TUNING = (69, 62, 66, 71)
SHAPES = {
    "C": (0, 0, 0, 3), "Am": (2, 0, 0, 0), "F": (2, 0, 1, 0), "G": (0, 2, 3, 2),
    "G7": (0, 2, 1, 2), "Em": (0, 4, 3, 2), "Dm": (2, 2, 1, 0), "D7": (2, 2, 2, 3),
    "C7": (0, 0, 0, 1), "E7": (1, 2, 0, 2),
}


class Phrase:
    def __init__(self, bpm, tab=True):
        self.bpm = bpm
        self.events = []  # (tick, order, bytes); note-offs sort first
        if tab:
            self.events.append((0, 0, bytes([0xB0, 126, 4])))

    def ticks(self, beats):
        return round(beats * PPQ)

    def note(self, start, length, note, velocity, channel=0):
        self.events.append((self.ticks(start), 1, bytes([0x90 | channel, note, velocity])))
        self.events.append((self.ticks(start + length), 0, bytes([0x80 | channel, note, 0])))

    def keyswitch(self, at, key):
        self.events.append((self.ticks(at), 1, bytes([0x90, key, 100])))
        self.events.append((self.ticks(at) + 1, 0, bytes([0x80, key, 0])))

    def cc(self, at, number, value):
        self.events.append((self.ticks(at), 0, bytes([0xB0, number, value])))

    def stroke(self, start, length, tuning, shape, velocity, direction=None, strings=range(4)):
        """One stroke of a chord shape across the named strings."""
        if direction is not None:
            self.keyswitch(start, direction)
        for string in strings:
            self.note(start, length, tuning[string] + SHAPES[shape][string], velocity, string)

    def pluck(self, start, length, tuning, string, fret, velocity):
        self.note(start, length, tuning[string] + fret, velocity, string)

    def smf(self):
        tempo = round(60_000_000 / self.bpm)
        track = bytearray()
        track += b"\x00\xff\x51\x03" + tempo.to_bytes(3, "big")
        track += b"\x00\xff\x58\x04\x04\x02\x18\x08"
        last = 0
        for tick, _, data in sorted(self.events, key=lambda e: (e[0], e[1])):
            track += vlq(tick - last) + data
            last = tick
        track += b"\x00\xff\x2f\x00"
        return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
                + b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


def strum_bars(p, tuning, chords, pattern, start=0.0):
    """pattern: (eighth, direction, velocity) per stroke; each stroke rings
    until just before the next one, as a strummed chord does."""
    strokes = [(start + 4 * bar + 0.5 * eighth, name, direction, velocity)
               for bar, name in enumerate(chords) for eighth, direction, velocity in pattern]
    for index, (at, name, direction, velocity) in enumerate(strokes):
        following = strokes[index + 1][0] if index + 1 < len(strokes) else start + 4 * len(chords)
        p.stroke(at, following - at - 0.05, tuning, name, velocity, direction)
    return start + 4 * len(chords)


# The island strum: D, D-U, U-D-U, the hand moving down-up on every eighth.
ISLAND = [(0, DOWN, 100), (2, DOWN, 92), (3, UP, 70), (5, UP, 74), (6, DOWN, 90), (7, UP, 70)]


def concert_strum():
    # The default sound: the island strum on the four chords every player
    # learns first, with the hand's own down-up timing.
    p = Phrase(120)
    end = strum_bars(p, STANDARD, ["C", "Am", "F", "G7"], ISLAND)
    p.stroke(end, 2.0, STANDARD, "C", 104, DOWN)
    return p


def soprano_d_tuning():
    # The soprano's old "my dog has fleas" tuning a tone up, brighter and
    # tighter: a quick calypso strum, D-D-U-U-D-U, in D.
    p = Phrase(124)
    calypso = [(0, DOWN, 104), (2, DOWN, 96), (3, UP, 76), (5, UP, 80), (6, DOWN, 94), (7, UP, 74)]
    # In D tuning the C shape is D, F is G, G7 is A7 and Am is Bm.
    end = strum_bars(p, D_TUNING, ["C", "F", "G7", "C"], calypso)
    p.stroke(end, 3.0, D_TUNING, "C", 108, DOWN)
    return p


def ring(p, tuning, events, longest):
    """Plucks (beat, string, fret, velocity); each note rings until its own
    string is played again, or for at most longest beats."""
    for index, (beat, string, fret, velocity) in enumerate(events):
        later = [b for b, s, _, _ in events[index + 1:] if s == string and b > beat]
        length = min(later[0] - beat - 0.05, longest) if later else longest
        p.pluck(beat, length, tuning, string, fret, velocity)


def tenor_fluorocarbon_fingerstyle():
    # Campanella: a melody spread across the re-entrant strings so each note
    # rings into the next, the ukulele's harp-like fingerstyle.
    p = Phrase(92)
    # (beat, string, fret) per eighth; strings G C E A are 0-3.
    line = [
        (0.0, 1, 0), (0.5, 0, 0), (1.0, 2, 0), (1.5, 3, 3), (2.0, 0, 0), (2.5, 3, 0), (3.0, 2, 0), (3.5, 0, 0),
        (4.0, 1, 0), (4.5, 0, 2), (5.0, 2, 0), (5.5, 3, 0), (6.0, 0, 2), (6.5, 3, 2), (7.0, 2, 3), (7.5, 0, 2),
        (8.0, 1, 2), (8.5, 0, 0), (9.0, 2, 1), (9.5, 3, 2), (10.0, 0, 0), (10.5, 3, 0), (11.0, 2, 3),
    ]
    ring(p, STANDARD, [(beat, string, fret, 92 if beat % 2 == 0 else 78)
                       for beat, string, fret in line], 2.5)
    p.stroke(12, 2.4, STANDARD, "C", 88)
    return p


def low_g_tenor():
    # Chord melody over the low G string's bass: the thumb and fingers pinch
    # the chord, then the melody moves on the top strings above it.
    p = Phrase(96)
    events = [
        # G: G3 D4 G4 B4, melody D5 C5 B4 A4
        (0, 0, 0, 96), (0, 1, 2, 80), (0, 2, 3, 82), (0, 3, 2, 92),
        (1, 3, 5, 90), (2, 0, 0, 86), (2, 3, 3, 88), (3, 3, 2, 86), (3.5, 3, 0, 80),
        # Em: G3 E4 G4 B4, melody C5 B4 G4 F#4
        (4, 0, 0, 94), (4, 1, 4, 80), (4, 2, 3, 82), (4, 3, 2, 90),
        (5, 3, 3, 88), (6, 0, 0, 84), (6, 3, 2, 86), (7, 2, 3, 84), (7.5, 2, 2, 78),
        # C over its root on the low G string, then D7 over A3
        (8, 0, 5, 96), (8, 1, 0, 80), (8, 2, 0, 82), (8, 3, 3, 92), (9, 2, 3, 84),
        (10, 0, 2, 92), (10, 1, 2, 80), (10, 2, 2, 82), (10, 3, 3, 90),
        (11, 3, 2, 86), (11.5, 3, 0, 80),
        # G
        (12, 0, 0, 98), (12, 1, 2, 82), (12, 2, 3, 84), (12, 3, 2, 92),
    ]
    ring(p, LOW_G, events, 2.5)
    return p


def soprano_thumb():
    # A slow ballad brushed with the thumb, every stroke a soft downstroke:
    # an accented stroke and a lighter one on each chord.
    p = Phrase(84)
    for index, name in enumerate(["C", "Em", "Am", "F"]):
        p.stroke(2 * index, 0.95, STANDARD, name, 92)
        p.stroke(2 * index + 1, 0.95, STANDARD, name, 72)
    p.stroke(8, 4.0, STANDARD, "C", 96)
    return p


def tenor_picked_melody():
    # A single-note line with a felt pick on low-G tenor: the fretting hand
    # chooses the strings, as on a guitar.
    p = Phrase(108, tab=False)
    line = [(0, 55), (0.5, 59), (1, 62), (1.5, 64), (2, 67), (3, 66), (3.5, 64), (4, 62),
            (5, 59), (5.5, 62), (6, 64), (6.5, 67), (7, 69), (7.5, 71), (8, 74), (9, 71),
            (9.5, 69), (10, 67)]
    for index, (beat, note) in enumerate(line):
        following = line[index + 1][0] if index + 1 < len(line) else beat + 3
        p.note(beat, following - beat - 0.08, note, 100 if beat % 1 == 0 else 84)
    p.note(12, 3.0, 55, 96)
    p.note(12, 3.0, 62, 90)
    p.note(12, 3.0, 67, 90)
    p.note(12, 3.0, 71, 90)
    return p


def concert_piezo_stage():
    # A syncopated pop strum through the saddle pickup, the off-beats choked
    # by the strumming hand's palm (CC2, the bridge hand) as a "chuck".
    p = Phrase(112)
    t = 0.0
    for name in ["Am", "F", "G"]:
        for eighth, direction, velocity, chuck in [(0, DOWN, 104, False), (2, DOWN, 96, True),
                                                   (3, UP, 74, False), (5, UP, 78, False),
                                                   (6, DOWN, 96, True), (7, UP, 74, False)]:
            at = t + 0.5 * eighth
            p.cc(at, 2, 110 if chuck else 0)
            p.stroke(at, 0.2 if chuck else 0.45, STANDARD, name, velocity, direction)
        t += 4
    p.cc(t, 2, 0)
    p.stroke(t, 2.5, STANDARD, "Am", 104, DOWN)
    return p


PHRASES = {
    "concert-strum": concert_strum,
    "soprano-d-tuning": soprano_d_tuning,
    "tenor-fluorocarbon-fingerstyle": tenor_fluorocarbon_fingerstyle,
    "low-g-tenor": low_g_tenor,
    "soprano-thumb": soprano_thumb,
    "tenor-picked-melody": tenor_picked_melody,
    "concert-piezo-stage": concert_piezo_stage,
}


def preset_slugs():
    header = (ROOT / "Source/DSP/AculelePresets.h").read_text(encoding="utf-8")
    return re.findall(r'\{ "[^"]+", "([a-z0-9-]+)",', header)


def main():
    out = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "Docs/audio/midi"
    slugs = preset_slugs()
    if sorted(slugs) != sorted(PHRASES):
        sys.exit(f"error: phrases {sorted(PHRASES)} do not match the bank {sorted(slugs)}")
    out.mkdir(parents=True, exist_ok=True)
    for number, slug in enumerate(slugs, 1):
        path = out / f"{number:02d}-{slug}.mid"
        path.write_bytes(PHRASES[slug]().smf())
        print(path)


if __name__ == "__main__":
    main()
