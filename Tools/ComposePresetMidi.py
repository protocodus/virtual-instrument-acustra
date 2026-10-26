#!/usr/bin/env python3
"""Write the factory preset demo phrases to Docs/audio/midi/<NN>-<slug>.mid.

One phrase per preset in Source/DSP/AcustraPresets.h, written for what that
preset is for. The .mid files are the committed sources the demo renderer
reads; rerun this only to change a phrase on purpose, in its own commit.
Each phrase's last event falls 5.5-9.5 s in, so with the renderer's 2.5 s
tail every demo is 8-12 s long (G3c).

Acustra reads MIDI as a guitarist plays: notes that start on one sample are
one strummed chord (a dotted eighth or less after the previous strum is the
upstroke), single notes are plucked, a key-up damps the string, CC2 is the
bridge hand (palm mute) and the RPN 0 range sets how far pitch bend slides.

    python3 Tools/ComposePresetMidi.py [output-dir]
"""

import pathlib
import re
import struct
import sys

PPQ = 480
ROOT = pathlib.Path(__file__).resolve().parent.parent

# Open-position voicings, low string to high, as MIDI notes.
STANDARD = {
    "G": [43, 47, 50, 55, 59, 67],
    "C": [48, 52, 55, 60, 64],
    "D": [50, 57, 62, 66],
    "Em": [40, 47, 52, 55, 59, 64],
    "Am": [45, 52, 57, 60, 64],
    "F": [41, 48, 53, 57, 60, 65],
    "A": [45, 52, 57, 61, 64],
    "E": [40, 47, 52, 56, 59, 64],
    "Bm": [47, 54, 59, 62, 66],
    "F#m": [42, 49, 54, 57, 61, 66],
    "Cadd9": [48, 52, 55, 62, 67],
    "Dsus4": [50, 57, 62, 67],
}


class Phrase:
    def __init__(self, bpm):
        self.bpm = bpm
        self.events = []  # (tick, order, bytes); note-offs sort first

    def ticks(self, beats):
        return round(beats * PPQ)

    def note(self, start, length, notes, velocity, channel=0):
        for n in ([notes] if isinstance(notes, int) else notes):
            self.events.append((self.ticks(start), 1, bytes([0x90 | channel, n, velocity])))
            self.events.append((self.ticks(start + length), 0, bytes([0x80 | channel, n, 0])))

    def cc(self, at, number, value, channel=0):
        self.events.append((self.ticks(at), 0, bytes([0xB0 | channel, number, value])))

    def bend(self, at, semitones, bend_range, channel=0):
        value = max(0, min(16383, round(8192 + semitones / bend_range * 8192)))
        self.events.append((self.ticks(at), 0, bytes([0xE0 | channel, value & 0x7F, value >> 7])))

    def bend_range(self, at, semitones, channel=0):
        for number, value in ((101, 0), (100, 0), (6, semitones), (38, 0), (101, 127), (100, 127)):
            self.cc(at, number, value, channel)

    def glide(self, start, length, begin, end, bend_range, channel=0, steps=12):
        for step in range(steps + 1):
            self.bend(start + length * step / steps,
                      begin + (end - begin) * step / steps, bend_range, channel)

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


def strum_bars(p, chords, pattern, start=0.0, velocity=(96, 72)):
    """Strum each chord for one bar. pattern: (beat, length, accented) per stroke."""
    for bar, name in enumerate(chords):
        shape = STANDARD[name]
        for beat, length, accent in pattern:
            p.note(start + 4 * bar + beat, length, shape, velocity[0] if accent else velocity[1])
    return start + 4 * len(chords)


def dreadnought_steel():
    # The default sound: a folk/pop campfire strum with ringing open chords.
    p = Phrase(104)
    pattern = [(0, 0.95, True), (1, 0.45, True), (1.5, 0.45, False),
               (2.5, 0.45, False), (3, 0.45, True), (3.5, 0.45, False)]
    end = strum_bars(p, ["G", "Cadd9", "D"], pattern)
    p.note(end, 4.0, STANDARD["G"], 100)
    return p


def parlor_steel_fingerstyle():
    # Travis picking: alternating thumb bass under a picked melody.
    p = Phrase(92)
    bars = [("C", 48, 43, [64, 67, 64, 72]), ("F", 41, 48, [65, 69, 72, 69]),
            ("G", 43, 50, [62, 67, 71, 74])]
    for bar, (_, bass, alt, melody) in enumerate(bars):
        t = 4 * bar
        for beat, n in ((0, bass), (1, alt), (2, bass), (3, alt)):
            p.note(t + beat, 0.95, n, 100)
        for i, n in enumerate(melody):
            p.note(t + 0.5 + i, 0.9, n, 96 + (10 if i == 0 else 0))
    p.note(12, 2.0, STANDARD["C"], 90)
    return p


def mahogany_steel_thumb():
    # A thumb-played walking bass and warm chord stabs, a slow country blues in
    # E through the I, IV and V before the final chord.
    p = Phrase(84)
    line = [40, 44, 47, 49, 45, 49, 52, 54, 47, 51, 54, 56]
    chords = [[56, 59, 64], [57, 61, 64], [59, 63, 66]]
    for i, n in enumerate(line):
        p.note(i, 0.9, n, 112 if i % 4 == 0 else 100)
        if i % 2 == 1:
            p.note(i + 0.5, 0.45, chords[i // 4], 88)
    p.note(12, 1.0, STANDARD["E"], 104)
    return p


def jumbo_steel_pick():
    # Big, bright sixteenth-note pick strumming on open chords.
    p = Phrase(112)
    pattern = [(0, 0.45, True), (0.75, 0.2, False), (1, 0.45, True), (1.5, 0.2, False),
               (1.75, 0.2, False), (2.5, 0.2, False), (2.75, 0.2, False),
               (3, 0.45, True), (3.5, 0.45, False)]
    end = strum_bars(p, ["D", "G", "A"], pattern, velocity=(110, 82))
    p.note(end, 4.0, STANDARD["D"], 112)
    return p


def palm_muted_steel_pick():
    # Chugging palm-muted eighths (CC2 is the bridge hand), opening up for the
    # accents and the last chord.
    p = Phrase(124)
    mute, open_ = 72, 0
    riff = [([40, 47], 6), ([43, 50], 2), ([45, 52], 6), ([38, 45], 2)]
    t = 0.0
    # The riff, an open accent, then the riff's first half into the last chord.
    for segments, accent in ((riff, True), (riff[:2], False)):
        for shape, eighths in segments:
            for i in range(eighths):
                p.cc(t + 0.5 * i, 2, mute)
                p.note(t + 0.5 * i, 0.4, shape, 96 if i % 2 == 0 else 80)
            t += 0.5 * eighths
        if accent:
            p.cc(t, 2, open_)
            p.note(t, 1.9, STANDARD["E"], 110)
            p.cc(t + 2, 2, mute)
            t += 2
    p.cc(t, 2, open_)
    p.note(t, 4.0, STANDARD["E"], 112)
    return p


def dadgad_mono_mic():
    # Celtic-style DADGAD: open strings ringing against a modal melody.
    p = Phrase(96)
    drone = [38, 45, 50]
    p.note(0, 7.8, drone, 104)
    melody = [(0.5, 62), (1, 64), (1.5, 66), (2, 69), (3, 67), (3.5, 66), (4, 64),
              (5, 62), (5.5, 57), (6, 62), (7, 55)]
    for beat, n in melody:
        p.note(beat, 1.8, n, 104)
    p.note(8, 6.0, [38, 45, 50, 55, 57, 62], 108)
    for i, n in enumerate([69, 67, 66, 64, 62, 64, 66, 62]):
        p.note(8.5 + 0.5 * i, 1.5, n, 100)
    return p


def open_g_slide_ready():
    # Bottleneck slide in open G: the slide plays on its own channel with a
    # 12-semitone bend range, so its glides leave the open drone in tune.
    p = Phrase(80)
    rng, slide = 12, 1
    p.bend_range(0, rng, slide)
    p.note(0, 3.9, [43, 50], 100)
    # (start beat, held MIDI note, slide from, slide to, length)
    phrases = [(0.5, 62, -2, 0, 1.4), (2, 67, -1, 0, 1.9), (4, 67, 0, 2, 1.8),
               (6, 71, -1, 0, 1.4), (7.5, 67, 0, -5, 0.4)]
    p.note(4, 3.9, [43, 50, 55], 100)
    for start, n, a, b, length in phrases:
        p.bend(start, a, rng, slide)
        p.note(start, length, n, 112, slide)
        p.glide(start, 0.35, a, b, rng, slide)
    p.bend(8, 0, rng, slide)
    p.note(8, 4.0, [38, 43, 50, 55, 59, 62], 108)
    return p


def piezo_stage_steel():
    # A syncopated pop strum through the stage pickup.
    p = Phrase(118)
    pattern = [(0, 0.45, True), (1, 0.45, True), (1.5, 0.45, False), (2.5, 0.45, False),
               (3, 0.2, True), (3.5, 0.45, False)]
    end = strum_bars(p, ["A", "E", "D"], pattern, velocity=(104, 80))
    p.note(end, 4.0, STANDARD["A"], 106)
    return p


PHRASES = {
    "dreadnought-steel": dreadnought_steel,
    "parlor-steel-fingerstyle": parlor_steel_fingerstyle,
    "mahogany-steel-thumb": mahogany_steel_thumb,
    "jumbo-steel-pick": jumbo_steel_pick,
    "palm-muted-steel-pick": palm_muted_steel_pick,
    "dadgad-mono-mic": dadgad_mono_mic,
    "open-g-slide-ready": open_g_slide_ready,
    "piezo-stage-steel": piezo_stage_steel,
}


def preset_slugs():
    header = (ROOT / "Source/DSP/AcustraPresets.h").read_text(encoding="utf-8")
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
