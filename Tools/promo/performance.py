"""A person's timing for the promo's parts: a breathing tempo curve, onsets
and velocities scattered by a seeded hand, written as standard MIDI files in
the performance's own time. Copied from Aculyre's Tools/write_preset_midi.py
(Performance, breathe and n, at virtual-instrument-aculyre 313396e), where
the three-dance promo this one follows was first written.
"""
import math
import random
import struct

PPQ = 480
LEAD_IN = 0.3  # seconds

NOTE = {}
for octave in range(0, 8):
    for name, offset in (("C", 0), ("D", 2), ("E", 4), ("F", 5), ("G", 7), ("A", 9), ("B", 11)):
        NOTE[f"{name}{octave}"] = 12 * (octave + 1) + offset


def n(name):
    if "#" in name:
        return NOTE[name.replace("#", "")] + 1
    return NOTE[name]


def variable(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.append(0x80 | (value & 0x7F))
        value >>= 7
    return bytes(reversed(out))


class Performance:
    def __init__(self, tempo, seed, timing=0.007, dynamics=4.0):
        self.tempo = sorted(tempo)  # [(beat, bpm)], linear between, held past the ends
        self.random = random.Random(seed)
        self.timing = timing        # seconds, the onset scatter's standard deviation
        self.dynamics = dynamics    # velocity steps, likewise
        self.events = []            # (seconds, order, bytes, voice); note-offs sort first
        self.voice = None           # the part being written; each is its own file

    def bpm(self, beat):
        points = self.tempo
        if beat <= points[0][0]:
            return points[0][1]
        for (b0, t0), (b1, t1) in zip(points, points[1:]):
            if beat <= b1:
                return t0 + (t1 - t0) * (beat - b0) / (b1 - b0)
        return points[-1][1]

    def seconds(self, beat):
        """Clock time of a written beat: the integral of 60 / bpm, after a
        moment's silence before the first note."""
        total, previous = LEAD_IN, 0.0
        for b, _ in self.tempo[1:] + [(math.inf, 0)]:
            end = min(beat, b)
            if end > previous:
                t0, t1 = self.bpm(previous), self.bpm(end)
                slope = (t1 - t0) / (end - previous)
                total += (60.0 * (end - previous) / t0 if abs(slope) < 1e-9
                          else 60.0 / slope * math.log(t1 / t0))
                previous = end
            if beat <= b:
                break
        return total

    def scatter(self, scale=1.0):
        deviation = self.timing * scale
        return max(-2.5 * deviation, min(2.5 * deviation, self.random.gauss(0.0, deviation)))

    def velocity(self, value):
        return max(1, min(127, round(value + self.random.gauss(0.0, self.dynamics))))

    def at(self, beat, lean=0.0, scale=1.0):
        """A played onset: the written beat on the breathing clock, plus a
        deliberate lean in seconds and the hand's scatter."""
        return max(0.0, self.seconds(beat) + lean + self.scatter(scale))

    def message(self, seconds, data, order=1):
        self.events.append((seconds, order, bytes(data), self.voice))

    def play(self, onset, beats_held, beat, note, velocity, channel=0):
        """One note struck at `onset` seconds and let go `beats_held` written
        beats after `beat`, with the release scattered too."""
        release = max(onset + 0.04, self.seconds(beat + beats_held) + self.scatter(0.6))
        self.message(onset, (0x90 | channel, note, self.velocity(velocity)))
        self.message(release, (0x80 | channel, note, 0), 0)

    def smf(self, voice=None):
        """One part as a format 0 file at the piece's opening tempo, so it
        sits roughly on a DAW's bars; the performance's own time is in the
        ticks."""
        bpm = self.tempo[0][1]
        events = sorted((e[:3] for e in self.events if e[3] == voice), key=lambda e: (e[0], e[1]))
        # A key struck again before its last release lets go just before.
        struck = {}
        trimmed = []
        for index in range(len(events) - 1, -1, -1):
            seconds, order, data = events[index]
            key = (data[0] & 0x0F, data[1])
            if data[0] & 0xF0 == 0x90:
                struck[key] = seconds
            elif data[0] & 0xF0 == 0x80 and key in struck and seconds >= struck[key]:
                seconds = struck[key] - 0.004
            trimmed.append((seconds, order, data))
        events = sorted(trimmed, key=lambda e: (e[0], e[1]))
        ticks_per_second = PPQ * bpm / 60.0
        tempo = round(60_000_000 / bpm)
        track = bytearray(b"\x00\xff\x51\x03" + tempo.to_bytes(3, "big"))
        last = 0
        for seconds, _, data in events:
            tick = max(last, round(seconds * ticks_per_second))
            track += variable(tick - last) + data
            last = tick
        track += b"\x00\xff\x2f\x00"
        return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ)
                + b"MTrk" + struct.pack(">I", len(track)) + bytes(track))


def breathe(bpm, phrases, swell=0.03, settle=0.07, last=0.22):
    """A tempo curve over phrases of (first beat, length in beats): each
    pushes on a little towards its middle and settles at its end; the last
    one broadens into the close."""
    points = []
    for index, (start, length) in enumerate(phrases):
        final = index == len(phrases) - 1
        # After a breath the hand takes the tempo up again within a beat.
        points += [(start if index == 0 else start + 0.75, bpm), (start + 0.55 * length, bpm * (1 + swell)),
                   (start + 0.85 * length, bpm), (start + length, bpm * (1 - (last if final else settle)))]
    return points


