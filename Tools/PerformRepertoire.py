#!/usr/bin/env python3
"""Write expressive performances of public-domain guitar repertoire for
AcustraRenderRepertoire: Tárrega, the Renaissance, the Baroque and Bach, solo
and for ensembles of differently built guitars.

What is taken from the sources is the composition alone -- which pitch
sounds when and for how long -- read from Mutopia Project MIDI files (pinned
by md5 below).  Every source is either public domain or CC BY-SA, whose terms
cover an edition's engraving, not the composer's notes; no engraving,
fingering or editorial marking is read.  Mutopia's MIDI is quantised and
mostly at one velocity, and it plays repeats once, so everything that makes a
performance -- the form taken, the phrasing, the dynamics, the tempo, the
timing of every hand -- is authored here.  None of it enters the model.

The performance model
---------------------

A score is unfolded into the form a player would take (repeats, D.C., which
ending), and each section is divided into phrases and periods.  Then:

* Tempo.  A tempo map in seconds per beat is built on a 1/48-quarter grid
  from the product of: the section's own tempo plan (accelerando, più mosso,
  meno mosso -- up to half or one and a half times the base tempo); a phrase
  arch at every level, pressing forward into the middle of a phrase and
  relaxing out of it (Todd 1992, JASA 91:3540); a ritardando into every
  phrase end, deeper at a period and deeper again at a section end; the
  final ritardando of Friberg and Sundberg (1999, JASA 105:1469), v(x) =
  (1 + (w^q - 1) x)^(1/q), into the last chord, which is then held as a
  fermata; a breath (a short lift) before a new section; and a slow random
  wander of a few per cent.

* Dynamics.  Each section has a dynamic plan (pp ... ff, steps and hairpins)
  on the engine's velocity scale, spaced for even loudness steps across
  about 80% of its range.  On top of it: the phrase arch again, louder where
  the phrase moves forward (Todd's coupling of loudness and tempo); the
  voice that sings is played above the accompaniment and the bass a little
  above the inner voices; metrical weight on the downbeat; a melody that
  climbs grows; and a small per-note scatter.

* Timing of the hands.  Every player has a correlated timing drift, an
  AR(1) process of ~10 ms, as measured human timing is correlated rather
  than white (Hennig et al. 2011, PLoS ONE 6:e26457), plus a few ms of
  independent jitter.  Chords are rarely struck as one: two notes are
  pinched with the thumb a little ahead; larger chords are rolled from the
  bass, by more when soft or at a cadence, and struck almost together in
  fast passages.  A tremolo's three fingers are neither equally loud nor
  equally spaced.  A dance can lean on a beat (a mazurka's second).  In an
  ensemble every guitarist has their own drift, so the parts are together
  only as closely as players are (tens of milliseconds).

* Articulation.  Melody is joined legato; a dance's longer notes can be
  lifted; a bass can ring on until the next bass note; long melody notes in
  a Romantic piece take a left-hand vibrato after they have sounded; and
  tone colour moves between sections (nearer the fingerboard for dolce,
  nearer the bridge for brillante).

Every random draw comes from a generator seeded by the piece's name, so a
performance file is reproducible.

Usage:

    python3 Tools/PerformRepertoire.py --midi-dir DIR --download --out DIR
    ./build-dsp/AcustraRenderRepertoire --out Docs/audio/repertoire DIR/*.txt

--download fetches any missing source from mutopiaproject.org into
--midi-dir and verifies its md5; without it the files must already be there.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import math
import random
import struct
import urllib.request
import zipfile
import zlib
from dataclasses import dataclass, field
from pathlib import Path

MUTOPIA = "https://www.mutopiaproject.org/ftp/"

# name: (Mutopia path, md5, member inside a zip or None)
SOURCES = {
    "lagrima-duo.mid": ("TarregaF/lagrima-duo/lagrima-duo.mid",
                        "0b20164983fe93c99a0afc689d55f86b", None),
    "adelita.mid": ("TarregaF/adelita/adelita.mid",
                    "285c484833d24aa8df4294d1cba5f370", None),
    "recuerdos.mid": ("TarregaF/recuerdos/recuerdos.mid",
                      "b91ef372bc2f64e383be1a539f033f62", None),
    "capricho-arabe.mid": ("TarregaF/capricho-arabe/capricho-arabe.mid",
                           "2b25caffdfc2e50878774b5fe26c2629", None),
    "milan-pavan2.mid": ("MilanL/milan-pavan2/milan-pavan2.mid",
                         "ce11d33de7e745af2dee993c3ab926d6", None),
    "saltarello.mid": ("GalileiV/saltarello/saltarello.mid",
                       "368e9f4fe4100ecd5f6ce7ab3d06be2c", None),
    "sanz-1.mid": ("SanzG/sanz-1/sanz-1.mid",
                   "65ae99a7d75772faa60f738e212aaae6", None),
    "Bach_Prelude_BWV999.mid": (
        "BachJS/BWV999/Bach_Prelude_BWV999/Bach_Prelude_BWV999.mid",
        "4bc83fb2491c05ccfa2da06741465a90", None),
    "bwv997-03sarabande.mid": (
        "BachJS/BWV997/bwv997-03sarabande/bwv997-03sarabande.mid",
        "f1a0e4383c7db74abaf9ff9c7a0b6a87", None),
    "bwv-1006a_3g.mid": ("BachJS/BWV1006a/bwv-1006a_3g/bwv-1006a_3g.mid",
                         "cf06d8635100f43d8b6a2f9f2a6d9afa", None),
    "ComeAgain.mid": ("DowlandJ/ALS17/ComeAgain/ComeAgain.mid",
                      "100e59e1fb70c6c59d207ff4f75eec92", None),
    "belle.mid": ("ArbeauT/Orch/belle/belle.mid",
                  "9b28ec9489a0a72e5ceb7e8b14f527a6", None),
    "canon_per_3_violini_e_basso.mid": (
        "PachelbelJ/Canon_per_3_Violini_e_Basso/"
        "Canon_per_3_Violini_e_Basso-mids.zip",
        "250f5e8c484c8332a9d8fa5c597bde15", "canon_per_3_violini_e_basso.mid"),
    "bwv-988-guitar-1.mid": ("BachJS/BWV988/bwv-988-guitar/bwv-988-guitar-mids.zip",
                             "c7c412d3b1b2a8e334fe1cb89e741867",
                             "bwv-988-guitar-1.mid"),
    "bach_air_bmv_1068.mid": (
        "BachJS/BWV1068/bach_air_bmv_1068/bach_air_bmv_1068.mid",
        "f4ac65ba4ad09297b972ccf6a47b57bb", None),
}

# The engine's velocity is 0-1 (its own map starts at 0.1). Measured on the
# Bellido at the default Output, a single note's early RMS rises about 15 dB
# from 0.12 to 0.96, flattening above 0.8 while the tone keeps brightening,
# so the levels are spaced for roughly even loudness steps, and pp to ff
# spans about 80% of the scale.
LEVELS = {"ppp": 0.14, "pp": 0.20, "p": 0.29, "mp": 0.40, "mf": 0.52,
          "f": 0.66, "ff": 0.80, "fff": 0.92}
VELOCITY_FLOOR, VELOCITY_CEILING = 0.12, 0.96
GRID = 48  # tempo-map cells per quarter note
LEAD_IN = 0.4  # seconds of silence before the first note


# --------------------------------------------------------------------------
# MIDI

def _read_var(data: bytes, i: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[i]
        i += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, i


@dataclass
class MidiFile:
    division: int
    tracks: list  # per track: list of (on tick, off tick, pitch)
    timesigs: list  # (tick, numerator, denominator)
    tempo: float  # first tempo, quarter notes per minute


def parse_midi(data: bytes) -> MidiFile:
    if data[:4] != b"MThd":
        raise SystemExit("not a MIDI file")
    header = struct.unpack(">I", data[4:8])[0]
    _, count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise SystemExit("SMPTE time division is not supported")
    i = 8 + header
    tracks, timesigs, tempos = [], [], []
    for _ in range(count):
        while data[i:i + 4] != b"MTrk":
            i += 8 + struct.unpack(">I", data[i + 4:i + 8])[0]
        length = struct.unpack(">I", data[i + 4:i + 8])[0]
        j, end = i + 8, i + 8 + length
        tick, running, sounding, notes = 0, None, {}, []
        while j < end:
            delta, j = _read_var(data, j)
            tick += delta
            status = data[j]
            if status & 0x80:
                running, j = status, j + 1
            else:
                status = running
            if status == 0xFF:
                meta = data[j]
                size, j = _read_var(data, j + 1)
                if meta == 0x51:
                    tempos.append((tick, int.from_bytes(data[j:j + size], "big")))
                elif meta == 0x58:
                    timesigs.append((tick, data[j], 2 ** data[j + 1]))
                j += size
            elif status in (0xF0, 0xF7):
                size, j = _read_var(data, j)
                j += size
            elif status & 0xF0 in (0xC0, 0xD0):
                j += 1
            else:
                first, second = data[j], data[j + 1]
                j += 2
                key = (status & 0x0F, first)
                if status & 0xF0 == 0x90 and second > 0:
                    sounding.setdefault(key, []).append(tick)
                elif status & 0xF0 in (0x80, 0x90) and sounding.get(key):
                    notes.append((sounding[key].pop(0), tick, first))
        tracks.append(sorted(notes))
        i = end
    tempos.sort()
    timesigs = sorted(set(timesigs)) or [(0, 4, 4)]
    tempo = 6e7 / tempos[0][1] if tempos else 120.0
    return MidiFile(division, tracks, timesigs, tempo)


def load_source(name: str, directory: Path, download: bool) -> bytes:
    path_in_mutopia, digest, member = SOURCES[name]
    local = directory / name
    if not local.exists():
        if not download:
            raise SystemExit(f"{local}: missing (use --download, or fetch "
                             f"{MUTOPIA}{path_in_mutopia})")
        directory.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(MUTOPIA + path_in_mutopia, timeout=60) as reply:
            payload = reply.read()
        if member is not None:
            payload = zipfile.ZipFile(io.BytesIO(payload)).read(member)
        local.write_bytes(payload)
    data = local.read_bytes()
    actual = hashlib.md5(data).hexdigest()
    if actual != digest:
        raise SystemExit(f"{local}: md5 {actual}, expected {digest}")
    return data


# --------------------------------------------------------------------------
# Piece descriptions

@dataclass
class Guitar:
    """One player's instrument and where it sits; see RenderRepertoire.cpp."""
    model: str = "original"
    shape: str = "auditorium"
    material: str = "spruce"
    tuning: str = "standard"
    picking: str = "finger"
    age: float = 0.12
    pluck: float = 0.30
    touch: float = 0.35
    body: float = 0.86
    width: float = 0.85
    output: float = 0.30
    release: float = 0.30
    pan: float = 0.0
    gain: float = 1.0
    # Player: velocity offset for this part's line, timing lead (s, negative
    # is early) and how loose the hand is (scales drift and jitter).
    balance: float = 0.0
    lead: float = 0.0
    looseness: float = 1.0
    # In an ensemble: loudness while playing, dB from the others (the bodies
    # differ in how loud they are, so balance is set here, not by chance).
    level: float | None = None

    def line(self, name: str) -> str:
        return (f"part {name} model={self.model} shape={self.shape} "
                f"material={self.material} tuning={self.tuning} "
                f"picking={self.picking} age={self.age:g} pluck={self.pluck:g} "
                f"touch={self.touch:g} body={self.body:g} width={self.width:g} "
                f"output={self.output:g} release={self.release:g} "
                f"pan={self.pan:g} gain={self.gain:g}"
                + ("" if self.level is None else f" level={self.level:g}"))


@dataclass
class Segment:
    """A stretch of the score as it is played this time through.

    bars: first and last score bar, 1-based and inclusive.
    dyn: [(bar offset in the segment, level name or velocity)], linear
         between points; two points at one offset make a step.
    tempo: [(bar offset, factor of the base tempo)], likewise.
    tone: {part: (pluck position, touch)} or (pluck, touch) for every part.
    phrases: phrase lengths in bars (the piece's phrase length by default).
    breath: seconds of lift before the segment begins.
    close: how strongly the segment's end is marked: 'phrase', 'period',
           'section' or 'final'.
    mute: parts silent in this segment.
    """
    bars: tuple
    dyn: list = field(default_factory=lambda: [(0, "mf")])
    tempo: list = field(default_factory=lambda: [(0, 1.0)])
    tone: object = None
    phrases: list | None = None
    breath: float = 0.0
    close: str = "section"
    mute: tuple = ()


@dataclass
class Style:
    """How a period's player shapes things; see the module docstring."""
    tempo_arch: float = 0.08    # phrase arch depth (fraction of tempo)
    dyn_arch: float = 0.08      # phrase arch depth (velocity)
    rit: dict = field(default_factory=lambda: {
        "phrase": (0.18, 1.0), "period": (0.30, 1.5), "section": (0.42, 2.5)})
    final_rit: tuple = (0.52, 2.0, 2.5)  # end tempo ratio, bars, curve q
    final_hold: float = 5.0     # seconds the last chord is left ringing
    wander: float = 0.035       # slow random tempo wander
    drift: float = 0.010        # AR(1) timing drift, seconds
    jitter: float = 0.004       # independent timing jitter, seconds
    roll: float = 0.040         # spread of a rolled four-note chord, seconds
    block: float = 0.30         # chance a chord is struck almost together
    pinch_lead: float = 0.012   # thumb ahead of a pinched melody note
    metric: tuple = (0.035, 0.012, -0.012)  # downbeat, beat, off-beat
    melody: float = 0.07        # melody above the accompaniment
    bass: float = 0.02
    inner: float = -0.06
    contour: float = 0.004      # per semitone above the phrase's mean
    scatter: float = 0.022      # per-note velocity scatter
    legato: float = 1.0         # fraction of the written length sounded
    lift: float = 1.0           # ...for notes of a beat or longer
    bass_ring: bool = True      # a bass note rings until the next bass
    # Chord tones left ringing: a note sounds on until the harmony moves
    # ('bar', 'half' or 'beat') or a note a tone or less away takes over, as a
    # guitarist leaves a held shape ringing -- so an arpeggio rings and a
    # stepwise melody stays joined; None leaves every note its written length.
    ring: str | None = None
    ring_max: float = 3.0       # seconds
    rest_ring: float = 0.35     # how far a note rings into a following rest, s
    vibrato: float = 0.0        # left-hand vibrato depth on long notes
    beat_lean: tuple = ()       # timing lean per beat, fraction of a beat
    tremolo_cycle: tuple = ()   # (velocity, timing in s) per tremolo finger


ROMANTIC = Style(tempo_arch=0.12, dyn_arch=0.10, roll=0.050, block=0.22,
                 drift=0.012, vibrato=0.45, wander=0.03, ring="bar")
RENAISSANCE = Style(tempo_arch=0.08, dyn_arch=0.08, roll=0.035, block=0.35,
                    drift=0.010, metric=(0.04, 0.015, -0.015),
                    final_rit=(0.55, 2.0, 2.5), ring="half")
BAROQUE = Style(tempo_arch=0.07, dyn_arch=0.08, roll=0.030, block=0.40,
                drift=0.008, jitter=0.0035, metric=(0.045, 0.018, -0.018),
                rit={"phrase": (0.12, 0.75), "period": (0.25, 1.25),
                     "section": (0.40, 2.0)},
                lift=0.90, final_rit=(0.55, 1.5, 2.5), ring="beat")


@dataclass
class Piece:
    name: str
    title: str
    source: str
    guitars: dict            # part -> Guitar
    tracks: dict | None      # MIDI track -> part (None: every track -> the one part)
    form: list               # [Segment]
    bpm: float               # base tempo, quarter notes per minute
    style: Style
    transpose: int = 0       # semitones, every part
    part_transpose: dict = field(default_factory=dict)
    fold: tuple = (0, 127)   # notes outside are moved by octaves into range
    phrase_bars: int = 4
    period_phrases: int = 2
    tail: float = 2.5


def ensemble(*specs) -> dict:
    return {name: guitar for name, guitar in specs}


# Instruments. The Bellido 1978 is the measured classical body (strung with
# steel, the instrument's only strings); the others are the constructed
# flat-tops, whose shapes and woods differ in size, air resonance and colour.
def bellido(**kw):
    base = dict(model="bellido1978", shape="auditorium", material="mahogany",
                age=0.08, pluck=0.40, touch=0.14, body=0.90, width=0.55)
    base.update(kw)
    return Guitar(**base)


PIECES: list[Piece] = []

# ---------------------------------------------------------------- Tárrega
PIECES.append(Piece(
    name="01-tarrega-lagrima",
    title="Francisco Tarrega - Lagrima (preludio), A A B B A",
    source="lagrima-duo.mid", tracks=None, bpm=58, style=ROMANTIC,
    guitars={"guitar": bellido(pluck=0.44, touch=0.10)},
    form=[
        Segment((1, 8), dyn=[(0, "mp"), (4, "mf"), (6, "mp"), (8, "p")],
                tone=(0.42, 0.12), close="section"),
        Segment((1, 8), dyn=[(0, "p"), (4, "mp"), (6, "p"), (8, "pp")],
                tone=(0.56, 0.06), tempo=[(0, 0.94), (8, 0.92)],
                breath=0.35, close="section"),
        Segment((9, 16), dyn=[(0, "mf"), (2, "f"), (3, "ff"), (4, "mf"),
                              (5, "mf"), (6, "f"), (8, "p")],
                tone=(0.34, 0.20), tempo=[(0, 1.04), (3, 1.12), (4, 0.96),
                                         (8, 0.95)],
                breath=0.55, close="section"),
        Segment((9, 16), dyn=[(0, "p"), (2, "mf"), (3, "f"), (4, "mp"),
                              (6, "mp"), (8, "pp")],
                tone=(0.48, 0.10), tempo=[(0, 0.96), (3, 1.06), (4, 0.92)],
                breath=0.45, close="section"),
        Segment((1, 8), dyn=[(0, "p"), (4, "mp"), (6, "pp"), (8, "ppp")],
                tone=(0.58, 0.05), tempo=[(0, 0.90), (8, 0.86)],
                breath=0.7, close="final"),
    ]))

PIECES.append(Piece(
    name="02-tarrega-adelita",
    title="Francisco Tarrega - Adelita (mazurka), A A B B A",
    source="adelita.mid", tracks=None, bpm=66,
    style=Style(**{**ROMANTIC.__dict__, "beat_lean": (0.0, 0.0, 0.06),
                   "metric": (0.02, 0.035, -0.01)}),
    guitars={"guitar": bellido(pluck=0.42, touch=0.12)},
    form=[
        Segment((1, 8), dyn=[(0, "p"), (2, "mp"), (3, "mf"), (4, "p"),
                             (6, "mp"), (7, "mf"), (8, "p")],
                tone=(0.44, 0.10), close="section"),
        Segment((1, 8), dyn=[(0, "pp"), (2, "p"), (3, "mp"), (4, "pp"),
                             (6, "p"), (7, "mp"), (8, "pp")],
                tone=(0.58, 0.05), tempo=[(0, 0.93)], breath=0.3,
                close="section"),
        Segment((9, 16), dyn=[(0, "mf"), (2, "f"), (3, "ff"), (4, "mf"),
                              (6, "f"), (7, "mf"), (8, "mp")],
                tone=(0.32, 0.22), tempo=[(0, 1.10), (3, 1.18), (4, 1.0),
                                          (7, 0.92)],
                breath=0.5, close="section"),
        Segment((9, 16), dyn=[(0, "mp"), (2, "mf"), (3, "f"), (4, "mp"),
                              (6, "mf"), (8, "p")],
                tone=(0.40, 0.14), tempo=[(0, 1.02), (3, 1.10), (4, 0.95)],
                breath=0.35, close="section"),
        Segment((1, 8), dyn=[(0, "p"), (3, "mp"), (4, "pp"), (7, "p"),
                             (8, "ppp")],
                tone=(0.56, 0.06), tempo=[(0, 0.90)], breath=0.8,
                close="final"),
    ]))

PIECES.append(Piece(
    name="03-tarrega-recuerdos-de-la-alhambra",
    title="Francisco Tarrega - Recuerdos de la Alhambra (tremolo study)",
    source="recuerdos.mid", tracks=None, bpm=72,
    style=Style(**{**ROMANTIC.__dict__, "roll": 0.02, "block": 0.6,
                   "vibrato": 0.3, "tempo_arch": 0.10,
                   "tremolo_cycle": ((0.02, 0.004), (-0.05, -0.003),
                                     (-0.01, 0.002))}),
    guitars={"guitar": bellido(pluck=0.38, touch=0.10)},
    form=[
        # A minor: bars 1-20.
        Segment((1, 20), dyn=[(0, "p"), (4, "mp"), (8, "mf"), (10, "f"),
                              (12, "mf"), (14, "mf"), (16, "mp"), (18, "p"),
                              (20, "pp")],
                tone=(0.40, 0.10), phrases=[4, 4, 4, 4, 4], close="section"),
        # A major: body 21-35, then the second ending (37), not the first (36).
        Segment((21, 35), dyn=[(0, "pp"), (4, "p"), (5, "mp"), (8, "mf"),
                               (9, "f"), (11, "ff"), (12, "f"), (13, "mf"),
                               (15, "mp")],
                tone=(0.50, 0.08), tempo=[(0, 0.96), (8, 1.04), (11, 1.08),
                                          (12, 1.0), (15, 0.94)],
                phrases=[4, 4, 4, 3], breath=0.6, close="period"),
        Segment((37, 58), dyn=[(0, "mp"), (1, "p"), (4, "mp"), (8, "mf"),
                               (10, "f"), (12, "mf"), (15, "mp"), (18, "p"),
                               (19, "pp"), (22, "pp")],
                tone=(0.44, 0.10), tempo=[(0, 0.97), (10, 1.03), (14, 0.97),
                                          (18, 0.90)],
                phrases=[1, 4, 4, 4, 4, 5], close="final"),
    ]))

PIECES.append(Piece(
    name="04-tarrega-capricho-arabe",
    title="Francisco Tarrega - Capricho arabe (serenata), in Drop D",
    source="capricho-arabe.mid", tracks=None, bpm=84,
    style=Style(**{**ROMANTIC.__dict__, "vibrato": 0.5, "roll": 0.055}),
    guitars={"guitar": bellido(tuning="drop_d", pluck=0.40, touch=0.14)},
    form=[
        # Introduction, 3/4: free, recitative-like.
        Segment((1, 12), dyn=[(0, "p"), (2, "mp"), (4, "mf"), (6, "f"),
                              (8, "mf"), (10, "p"), (12, "pp")],
                tempo=[(0, 0.72), (2, 0.86), (4, 1.04), (6, 1.12),
                       (8, 0.86), (10, 0.70), (12, 0.60)],
                tone=(0.46, 0.10), phrases=[4, 4, 4], close="section"),
        # The serenata, 4/4.
        Segment((13, 36), dyn=[(0, "p"), (4, "mp"), (6, "mf"), (8, "p"),
                               (12, "mp"), (14, "f"), (16, "mf"), (20, "mp"),
                               (22, "f"), (24, "p")],
                tempo=[(0, 0.82), (6, 0.90), (8, 0.84), (14, 0.94),
                       (16, 0.86), (22, 0.92), (24, 0.80)],
                tone=(0.40, 0.14), breath=0.9, close="section"),
        # Major-mode middle section, more animated.
        Segment((37, 56), dyn=[(0, "mf"), (4, "f"), (6, "ff"), (8, "mf"),
                               (12, "f"), (14, "ff"), (16, "f"), (18, "mf"),
                               (20, "mp")],
                tempo=[(0, 0.98), (4, 1.10), (6, 1.18), (8, 1.02),
                       (12, 1.12), (14, 1.20), (16, 1.04), (20, 0.86)],
                tone=(0.30, 0.24), breath=0.6, close="section"),
        # Return and coda.
        Segment((57, 73), dyn=[(0, "p"), (4, "mf"), (6, "f"), (8, "mp"),
                               (12, "p"), (15, "pp"), (17, "ppp")],
                tempo=[(0, 0.84), (4, 0.94), (6, 1.0), (8, 0.86),
                       (12, 0.80), (17, 0.72)],
                tone=(0.48, 0.08), breath=0.8, close="final"),
    ]))

# ------------------------------------------------------------ Renaissance
PIECES.append(Piece(
    name="05-milan-pavana-ii",
    title="Luis Milan - Pavana II (El Maestro, 1536), twice through",
    source="milan-pavan2.mid", tracks=None, bpm=108, style=RENAISSANCE,
    phrase_bars=4,
    guitars={"guitar": Guitar(shape="parlor", material="mahogany", age=0.30,
                              pluck=0.22, touch=0.30, body=0.84)},
    form=[
        Segment((1, 24), dyn=[(0, "mf"), (4, "mp"), (8, "mf"), (12, "f"),
                              (16, "mf"), (20, "mp"), (24, "mp")],
                tone=(0.24, 0.30), close="section"),
        Segment((1, 24), dyn=[(0, "p"), (4, "pp"), (8, "p"), (12, "mf"),
                              (16, "ff"), (20, "mf"), (24, "p")],
                tone=(0.40, 0.16), tempo=[(0, 0.95), (12, 1.02), (20, 1.0)],
                breath=0.6, close="final"),
    ]))

PIECES.append(Piece(
    name="06-galilei-saltarello",
    title="Vincenzo Galilei - Saltarello, in Drop D",
    source="saltarello.mid", tracks=None, bpm=100,
    style=Style(**{**RENAISSANCE.__dict__, "lift": 0.82, "final_hold": 3.5}),
    phrase_bars=4,
    guitars={"guitar": Guitar(shape="parlor", material="maple", tuning="drop_d",
                              age=0.20, pluck=0.20, touch=0.40, body=0.84)},
    form=[
        Segment((1, 8), dyn=[(0, "f"), (4, "mf"), (8, "mf")], tone=(0.22, 0.40),
                close="period"),
        Segment((9, 16), dyn=[(0, "p"), (4, "mp"), (8, "p")], tone=(0.42, 0.18),
                tempo=[(0, 0.97)], close="period"),
        Segment((17, 32), dyn=[(0, "f"), (2, "p"), (4, "f"), (6, "p"),
                               (8, "mf"), (10, "p"), (12, "f"), (14, "ff"),
                               (16, "f")],
                tone=(0.24, 0.40),
                tempo=[(0, 1.0), (8, 1.08), (12, 1.18), (15, 1.24),
                       (16, 1.0)],
                breath=0.2, close="section"),
        Segment((33, 35), dyn=[(0, "pp"), (2, "pp"), (2.01, "f")],
                tempo=[(0, 0.62)], tone=(0.30, 0.30), breath=0.4,
                phrases=[3], close="final"),
    ]))

# ---------------------------------------------------------------- Baroque
PIECES.append(Piece(
    name="07-sanz-preludio",
    title="Gaspar Sanz - Preludio (Instruccion de musica, 1674)",
    source="sanz-1.mid", tracks=None, bpm=104, transpose=-12, style=BAROQUE,
    guitars={"guitar": Guitar(shape="parlor", material="spruce", age=0.18,
                              pluck=0.24, touch=0.36, body=0.86)},
    form=[
        Segment((1, 8), dyn=[(0, "mf"), (4, "f"), (6, "mf"), (8, "mp")],
                tone=(0.26, 0.36), close="period"),
        Segment((9, 16), dyn=[(0, "p"), (4, "mp"), (6, "mf"), (8, "p")],
                tone=(0.44, 0.20), tempo=[(0, 0.96), (6, 1.04)],
                breath=0.25, close="period"),
        Segment((17, 30), dyn=[(0, "mf"), (4, "f"), (8, "ff"), (10, "f"),
                               (12, "mf"), (14, "mp")],
                tone=(0.28, 0.34), tempo=[(0, 1.02), (8, 1.08), (10, 1.0)],
                breath=0.2, close="final"),
    ]))

# ------------------------------------------------------------------- Bach
PIECES.append(Piece(
    name="08-bach-prelude-bwv999",
    title="J. S. Bach - Prelude in D minor, BWV 999",
    source="Bach_Prelude_BWV999.mid", tracks=None, bpm=84,
    style=Style(**{**BAROQUE.__dict__, "metric": (0.05, 0.0, -0.02),
                   "lift": 1.0, "ring": "bar"}),
    guitars={"guitar": bellido(pluck=0.34, touch=0.18)},
    form=[
        Segment((1, 16), dyn=[(0, "mp"), (4, "mf"), (8, "mp"), (12, "mf"),
                              (16, "mp")],
                tone=(0.36, 0.18), close="period"),
        Segment((17, 32), dyn=[(0, "p"), (4, "mp"), (8, "mf"), (12, "f"),
                               (16, "f")],
                tone=(0.30, 0.22), tempo=[(0, 0.97), (8, 1.04), (12, 1.10),
                                          (16, 1.12)],
                close="period"),
        # The dominant pedal, building, then the cadenza into the close.
        Segment((33, 43), dyn=[(0, "f"), (3, "ff"), (6, "fff"), (8, "f"),
                               (10, "mf"), (11, "mp")],
                tone=(0.26, 0.28), tempo=[(0, 1.10), (6, 1.16), (8, 0.92),
                                          (10, 0.80)],
                phrases=[4, 4, 3], close="final"),
    ]))

PIECES.append(Piece(
    name="09-bach-sarabande-bwv997",
    title="J. S. Bach - Sarabande from the Lute Suite BWV 997",
    source="bwv997-03sarabande.mid", tracks=None, bpm=52,
    style=Style(**{**BAROQUE.__dict__, "tempo_arch": 0.09, "roll": 0.055,
                   "block": 0.25, "beat_lean": (0.0, 0.0, 0.05),
                   "metric": (0.03, 0.03, -0.015),
                   "vibrato": 0.2, "lift": 0.96}),
    guitars={"guitar": Guitar(shape="auditorium", material="mahogany",
                              age=0.14, pluck=0.44, touch=0.16, body=0.90)},
    form=[
        Segment((1, 15), dyn=[(0, "p"), (4, "mp"), (8, "mf"), (10, "f"),
                              (12, "mf"), (15, "mp")],
                tone=(0.46, 0.14), phrases=[4, 4, 4, 3], close="phrase"),
        Segment((17, 17), dyn=[(0, "p")], phrases=[1], close="section"),
        Segment((18, 32), dyn=[(0, "mp"), (4, "mf"), (6, "f"), (8, "ff"),
                               (10, "f"), (12, "mf"), (14, "p")],
                tone=(0.40, 0.20), tempo=[(0, 1.0), (8, 1.06), (12, 0.96)],
                phrases=[4, 4, 4, 3], breath=0.5, close="phrase"),
        Segment((34, 34), dyn=[(0, "pp")], phrases=[1], close="final"),
    ]))

PIECES.append(Piece(
    name="10-bach-gavotte-en-rondeau-bwv1006a",
    title="J. S. Bach - Gavotte en rondeau from BWV 1006a",
    source="bwv-1006a_3g.mid", tracks=None, bpm=132, transpose=-12,
    style=Style(**{**BAROQUE.__dict__, "lift": 0.86, "bass_ring": False}),
    phrase_bars=4,
    guitars={"guitar": Guitar(shape="dreadnought", material="spruce", age=0.10,
                              pluck=0.26, touch=0.38, body=0.86)},
    form=[
        # Rondeau theme and its episodes, as the score writes them out.
        Segment((1, 8), dyn=[(0, "f"), (4, "mf"), (8, "f")], tone=(0.26, 0.38),
                close="section"),
        Segment((9, 24), dyn=[(0, "p"), (4, "mp"), (8, "mf"), (12, "mp"),
                              (16, "mp")], tone=(0.40, 0.24),
                tempo=[(0, 0.97)], close="section"),
        Segment((25, 32), dyn=[(0, "f"), (4, "mf"), (8, "f")], tone=(0.26, 0.38),
                close="section"),
        Segment((33, 48), dyn=[(0, "mp"), (4, "mf"), (8, "f"), (12, "ff"),
                              (16, "mf")], tone=(0.30, 0.34),
                tempo=[(0, 1.0), (10, 1.06), (16, 0.98)], close="section"),
        Segment((49, 56), dyn=[(0, "p"), (4, "pp"), (8, "p")], tone=(0.50, 0.14),
                tempo=[(0, 0.94)], close="section"),
        Segment((57, 92), dyn=[(0, "mp"), (8, "mf"), (16, "f"), (20, "mp"),
                               (28, "f"), (32, "ff"), (36, "f")],
                tone=(0.30, 0.32), tempo=[(0, 1.0), (16, 1.07), (20, 0.97),
                                          (32, 1.08)],
                close="section"),
        Segment((93, 100), dyn=[(0, "f"), (4, "mf"), (6, "f"), (8, "ff")],
                tone=(0.24, 0.40), close="final"),
    ]))

# ------------------------------------------------------------- Ensembles
# Each voice is a different guitar, set in the stereo field like players on
# a stage. A cantus or a leading line is played a touch louder and a touch
# ahead; the bass sits a touch behind the beat.
PIECES.append(Piece(
    name="11-dowland-come-again-guitar-quartet",
    title="John Dowland - Come again, sweet love doth now invite "
          "(4 guitars: Bellido, Parlor maple, Auditorium mahogany, Jumbo spruce)",
    source="ComeAgain.mid", tracks={1: "cantus", 3: "altus", 5: "tenor",
                                    7: "bassus"},
    bpm=132, style=Style(**{**RENAISSANCE.__dict__, "bass_ring": False,
                            "lift": 0.92, "legato": 0.97, "ring": None,
                            "rest_ring": 0.8}),
    phrase_bars=2,
    guitars=ensemble(
        ("cantus", bellido(pan=-0.10, level=0.0, balance=0.07, lead=-0.006, pluck=0.40,
                           touch=0.16)),
        ("altus", Guitar(shape="parlor", material="maple", pan=-0.55, level=-3.0,
                         balance=-0.04, pluck=0.30, touch=0.30, age=0.20)),
        ("tenor", Guitar(shape="auditorium", material="mahogany", pan=0.50, level=-3.0,
                         balance=-0.04, pluck=0.34, touch=0.24, age=0.16)),
        ("bassus", Guitar(shape="jumbo", material="spruce", pan=0.20, level=-1.5,
                          balance=0.02, lead=0.006, pluck=0.30, touch=0.26,
                          age=0.24))),
    form=[
        # Verse 1: the opening, then the repeated second half twice.
        Segment((1, 7), dyn=[(0, "mp"), (4, "mf"), (7, "mp")], close="period",
                tone={"cantus": (0.40, 0.16)}),
        Segment((8, 14), dyn=[(0, "mf"), (3, "f"), (5, "ff"), (6, "f"),
                              (7, "mf")],
                tempo=[(0, 1.0), (3, 1.06), (5, 0.84), (7, 0.9)],
                close="section"),
        Segment((8, 14), dyn=[(0, "p"), (3, "mf"), (5, "f"), (6, "mp"),
                              (7, "p")],
                tempo=[(0, 0.96), (3, 1.04), (5, 0.80), (7, 0.86)],
                breath=0.3, close="section"),
        # Verse 2: quieter, then the climb to "to die" at full voice.
        Segment((1, 7), dyn=[(0, "p"), (4, "mp"), (7, "p")], breath=0.9,
                tempo=[(0, 0.96)], close="period",
                tone={"cantus": (0.52, 0.08), "altus": (0.42, 0.20),
                      "tenor": (0.46, 0.14), "bassus": (0.42, 0.18)}),
        Segment((8, 14), dyn=[(0, "mp"), (3, "f"), (5, "fff"), (6, "f"),
                              (7, "mf")],
                tempo=[(0, 1.0), (3, 1.10), (5, 0.78), (7, 0.9)],
                close="section",
                tone={"cantus": (0.36, 0.22), "altus": (0.28, 0.34),
                      "tenor": (0.30, 0.28), "bassus": (0.28, 0.30)}),
        Segment((8, 14), dyn=[(0, "p"), (3, "mp"), (5, "f"), (6, "p"),
                              (7, "pp")],
                tempo=[(0, 0.94), (3, 1.0), (5, 0.76), (7, 0.80)],
                breath=0.4, close="final",
                tone={"cantus": (0.50, 0.10), "altus": (0.44, 0.18),
                      "tenor": (0.46, 0.14), "bassus": (0.44, 0.16)}),
    ]))

PIECES.append(Piece(
    name="12-arbeau-belle-qui-tiens-ma-vie-guitar-quartet",
    title="Thoinot Arbeau - Belle qui tiens ma vie, pavane (Orchesographie, "
          "1589), 4 guitars in D minor",
    source="belle.mid", tracks={2: "superius", 3: "contratenor", 4: "tenor",
                                5: "bassus"},
    bpm=112, transpose=-5,
    style=Style(**{**RENAISSANCE.__dict__, "bass_ring": False, "lift": 0.94,
                   "metric": (0.05, 0.0, -0.02), "ring": None}),
    phrase_bars=4,
    guitars=ensemble(
        ("superius", Guitar(shape="auditorium", material="maple", pan=0.12, level=0.0,
                            balance=0.06, lead=-0.005, pluck=0.32,
                            touch=0.28)),
        ("contratenor", bellido(pan=-0.50, level=-3.5, balance=-0.03, pluck=0.42,
                                touch=0.14)),
        ("tenor", Guitar(shape="parlor", material="mahogany", pan=0.55, level=-3.5,
                         balance=-0.04, pluck=0.30, touch=0.26, age=0.28)),
        ("bassus", Guitar(shape="dreadnought", material="spruce",
                          tuning="drop_d", pan=-0.15, level=-1.5, balance=0.03,
                          lead=0.005, pluck=0.30, touch=0.24, age=0.2))),
    form=[
        # First time: melody and bass alone, softly.
        Segment((1, 32), dyn=[(0, "p"), (8, "mp"), (16, "p"), (24, "mp"),
                              (32, "p")],
                mute=("contratenor", "tenor"), close="section",
                tone={"superius": (0.44, 0.18), "bassus": (0.44, 0.16)}),
        Segment((1, 32), dyn=[(0, "mf"), (8, "f"), (16, "mf"), (24, "f"),
                              (32, "mf")],
                tempo=[(0, 1.02)], breath=0.4, close="section",
                tone={"superius": (0.30, 0.32), "contratenor": (0.36, 0.20),
                      "tenor": (0.28, 0.30), "bassus": (0.30, 0.26)}),
        Segment((1, 32), dyn=[(0, "f"), (8, "ff"), (16, "mf"), (24, "p"),
                              (28, "pp"), (32, "pp")],
                tempo=[(0, 1.0), (16, 0.96), (24, 0.92)], breath=0.5,
                close="final",
                tone={"superius": (0.40, 0.20), "contratenor": (0.46, 0.12),
                      "tenor": (0.40, 0.18), "bassus": (0.40, 0.18)}),
    ]))

PIECES.append(Piece(
    name="13-pachelbel-canon-guitar-quartet",
    title="Johann Pachelbel - Canon in D (3 guitars in canon over a Drop D bass)",
    source="canon_per_3_violini_e_basso.mid",
    tracks={1: "first", 2: "second", 3: "third", 4: "bass"},
    bpm=60, fold=(0, 84),
    style=Style(**{**BAROQUE.__dict__, "bass_ring": False, "lift": 0.92,
                   "final_rit": (0.50, 2.0, 2.5), "ring": None}),
    phrase_bars=2, period_phrases=4,
    guitars=ensemble(
        ("first", bellido(pan=-0.55, level=0.0, pluck=0.40, touch=0.16, balance=0.02,
                          lead=-0.004)),
        ("second", Guitar(shape="auditorium", material="maple", pan=0.55, level=0.0,
                          pluck=0.30, touch=0.30, age=0.10)),
        ("third", Guitar(shape="parlor", material="spruce", pan=0.0, level=0.0,
                         pluck=0.30, touch=0.30, age=0.16, balance=-0.01)),
        ("bass", Guitar(shape="jumbo", material="mahogany", tuning="drop_d",
                        pan=0.0, level=-1.0, pluck=0.34, touch=0.22, age=0.30,
                        balance=0.0, lead=0.006, looseness=0.8))),
    form=[
        Segment((1, 57), dyn=[(0, "p"), (2, "pp"), (6, "p"), (10, "mp"),
                              (18, "mf"), (22, "mp"), (26, "f"), (30, "mf"),
                              (34, "p"), (38, "mp"), (42, "f"), (46, "ff"),
                              (50, "f"), (52, "mf"), (54, "mp"), (56, "p")],
                tempo=[(0, 0.92), (10, 1.0), (18, 1.06), (22, 0.98),
                       (26, 1.12), (30, 1.02), (34, 0.94), (42, 1.10),
                       (46, 1.16), (50, 1.04), (56, 0.96)],
                close="final"),
    ]))

PIECES.append(Piece(
    name="14-bach-goldberg-aria-guitar-trio",
    title="J. S. Bach - Aria from the Goldberg Variations, BWV 988 "
          "(3 guitars; the bass in Drop D)",
    source="bwv-988-guitar-1.mid", tracks={1: "treble", 2: "middle", 3: "bass"},
    bpm=54,
    style=Style(**{**BAROQUE.__dict__, "tempo_arch": 0.09, "roll": 0.04,
                   "beat_lean": (0.0, 0.0, 0.04), "vibrato": 0.2,
                   "lift": 0.96, "ring": None}),
    guitars=ensemble(
        ("treble", bellido(pan=-0.45, level=0.0, pluck=0.44, touch=0.12, balance=0.06,
                           lead=-0.005)),
        ("middle", Guitar(shape="auditorium", material="spruce", pan=0.45, level=-3.0,
                          pluck=0.36, touch=0.22, balance=-0.04)),
        ("bass", Guitar(shape="dreadnought", material="mahogany",
                        tuning="drop_d", pan=0.05, level=-1.5, pluck=0.36, touch=0.20,
                        age=0.2, balance=0.0, lead=0.004))),
    form=[
        Segment((1, 16), dyn=[(0, "p"), (4, "mp"), (8, "mf"), (12, "mp"),
                              (16, "p")],
                tone=None, close="section"),
        Segment((17, 32), dyn=[(0, "mp"), (4, "mf"), (8, "ff"), (10, "mf"),
                               (12, "mp"), (14, "p"), (16, "pp")],
                tempo=[(0, 1.0), (8, 1.05), (12, 0.97)], breath=0.5,
                close="final"),
    ]))

PIECES.append(Piece(
    name="15-bach-air-bwv1068-guitar-duo",
    title="J. S. Bach - Air from the Orchestral Suite No. 3, BWV 1068 "
          "(2 guitars)",
    source="bach_air_bmv_1068.mid", tracks={1: "melody", 2: "continuo"},
    bpm=58,
    style=Style(**{**BAROQUE.__dict__, "tempo_arch": 0.08, "vibrato": 0.35,
                   "roll": 0.035, "lift": 1.0, "bass_ring": True,
                   "ring": None}),
    guitars=ensemble(
        ("melody", bellido(pan=-0.35, level=0.0, pluck=0.46, touch=0.12, balance=0.08,
                           lead=-0.006)),
        ("continuo", Guitar(shape="jumbo", material="spruce", pan=0.40, level=-2.5,
                            pluck=0.40, touch=0.18, age=0.18,
                            balance=-0.03))),
    # The guitar line is written an octave above where it sounds.
    part_transpose={"continuo": -12},
    form=[
        # A (bars 1-5) with its first ending, again with its second (7),
        # then B (8-19) twice.
        Segment((1, 6), dyn=[(0, "p"), (2, "mp"), (4, "mf"), (6, "mp")],
                close="section"),
        Segment((1, 5), dyn=[(0, "pp"), (2, "p"), (4, "mp"), (5, "p")],
                tempo=[(0, 0.96)], breath=0.3, close="phrase",
                tone={"melody": (0.56, 0.06), "continuo": (0.50, 0.12)}),
        Segment((7, 7), dyn=[(0, "p")], phrases=[1], close="section"),
        Segment((8, 19), dyn=[(0, "mp"), (2, "mf"), (4, "f"), (6, "mf"),
                              (8, "ff"), (9, "f"), (10, "mf"), (12, "mp")],
                tempo=[(0, 1.0), (4, 1.05), (8, 1.06), (10, 0.98)],
                breath=0.5, close="section",
                tone={"melody": (0.42, 0.16), "continuo": (0.36, 0.22)}),
        Segment((8, 19), dyn=[(0, "p"), (2, "mp"), (4, "mf"), (6, "mp"),
                              (8, "f"), (10, "mp"), (12, "pp")],
                tempo=[(0, 0.97), (8, 1.04), (10, 0.95)], breath=0.5,
                close="final",
                tone={"melody": (0.52, 0.08), "continuo": (0.46, 0.14)}),
    ]))


# --------------------------------------------------------------------------
# Unfolding the score

@dataclass
class Note:
    part: str
    pitch: int
    start: float      # timeline quarters
    end: float
    segment: int
    velocity: float = 0.0
    on: float = 0.0   # seconds
    off: float = 0.0
    role: str = "line"
    tremolo: int = -1  # position in a tremolo run


class BarGrid:
    """Bar starts in score quarters, from the MIDI's time signatures."""

    def __init__(self, midi: MidiFile, end_quarters: float):
        division = midi.division
        changes = [(tick / division, num, den) for tick, num, den in midi.timesigs]
        self.starts, self.lengths, self.beats = [], [], []
        position, index = 0.0, 0
        while position < end_quarters - 0.0625:
            while index + 1 < len(changes) and changes[index + 1][0] <= position + 1e-6:
                index += 1
            _, num, den = changes[index]
            length = num * 4.0 / den
            if den == 8 and num % 3 == 0 and num > 3:
                beat = 1.5
            elif den == 2:
                beat = 2.0
            else:
                beat = 4.0 / den
            self.starts.append(position)
            self.lengths.append(length)
            self.beats.append(beat)
            position += length

    def span(self, first: int, last: int) -> tuple[float, float]:
        return self.starts[first - 1], self.starts[last - 1] + self.lengths[last - 1]


@dataclass
class Boundary:
    at: float        # timeline quarters
    level: str       # phrase, period, section, final
    beat: float      # beat length there, in quarters
    arrival: float = -1.0  # the last onset before it, where a ritardando lands
    silent: float = -1.0   # where the arrival's written sound ends


def unfold(piece: Piece, midi: MidiFile):
    division = midi.division
    parts = list(piece.guitars)
    raw = []
    for index, track in enumerate(midi.tracks):
        if piece.tracks is None:
            part = parts[0]
        else:
            part = piece.tracks.get(index)
            if part is None:
                continue
        for on, off, pitch in track:
            raw.append((on / division, off / division,
                        pitch + piece.transpose + piece.part_transpose.get(part, 0),
                        part))
    end_quarters = max(r[1] for r in raw)
    grid = BarGrid(midi, end_quarters)

    notes: list[Note] = []
    boundaries: list[Boundary] = []
    segments = []   # (timeline start, timeline end, score start, Segment, bar starts)
    cursor = 0.0
    for number, segment in enumerate(piece.form):
        first, last = segment.bars
        score_start, score_end = grid.span(first, last)
        offset = cursor - score_start
        bar_starts = [grid.starts[b - 1] + offset for b in range(first, last + 1)]
        bar_beats = [grid.beats[b - 1] for b in range(first, last + 1)]
        bar_lengths = [grid.lengths[b - 1] for b in range(first, last + 1)]
        segments.append(dict(start=cursor, end=score_end + offset, segment=segment,
                             bar_starts=bar_starts, bar_beats=bar_beats,
                             bar_lengths=bar_lengths, number=number))
        for start, end, pitch, part in raw:
            if score_start - 1e-6 <= start < score_end - 1e-6 and part not in segment.mute:
                low, high = piece.fold
                while pitch > high:
                    pitch -= 12
                while pitch < low:
                    pitch += 12
                notes.append(Note(part, pitch, start + offset,
                                  min(end, score_end) + offset, number))
        # Phrase and period boundaries inside the segment.
        lengths = segment.phrases or []
        if not lengths:
            count = last - first + 1
            lengths = [piece.phrase_bars] * (count // piece.phrase_bars)
            if count % piece.phrase_bars:
                lengths.append(count % piece.phrase_bars)
        bar = 0
        for k, length in enumerate(lengths[:-1]):
            bar += length
            if bar >= len(bar_starts):
                break
            level = "period" if (k + 1) % piece.period_phrases == 0 else "phrase"
            boundaries.append(Boundary(bar_starts[bar], level, bar_beats[bar - 1]))
        cursor = score_end + offset
        boundaries.append(Boundary(cursor, segment.close, bar_beats[-1]))
    notes.sort(key=lambda n: (n.start, n.pitch))
    return notes, boundaries, segments, cursor


# --------------------------------------------------------------------------
# Expression

def interpolate(points, x, convert=lambda v: v):
    points = [(p, convert(v)) for p, v in points]
    if x <= points[0][0]:
        return points[0][1]
    for (x0, y0), (x1, y1) in zip(points, points[1:]):
        if x0 <= x < x1:
            return y0 if x1 - x0 < 1e-9 else y0 + (y1 - y0) * (x - x0) / (x1 - x0)
    return points[-1][1]


def level(value):
    return LEVELS[value] if isinstance(value, str) else float(value)


def arch(x: float) -> float:
    """A phrase's shape: rises to a peak 60% of the way through, falls to 0."""
    x = min(max(x, 0.0), 1.0)
    return math.sin(math.pi * x ** (math.log(0.5) / math.log(0.6)))


ARCH_MEAN = sum(arch((i + 0.5) / 1000) for i in range(1000)) / 1000


class Timeline:
    """Everything that depends on where a moment is in the unfolded form."""

    def __init__(self, piece: Piece, boundaries, segments, end: float, rng,
                 onsets, spans):
        self.piece, self.style = piece, piece.style
        self.segments, self.end = segments, end
        self.boundaries = sorted(boundaries, key=lambda b: b.at)
        for boundary in self.boundaries:
            before = [q for q in onsets if q < boundary.at - 1e-6]
            boundary.arrival = before[-1] if before else boundary.at
            ends = [e for q, e in spans if boundary.arrival - 1e-6 <= q < boundary.at - 1e-6]
            boundary.silent = min(max(ends), boundary.at) if ends else boundary.at
        # Nested phrase spans for each level.
        rank = {"phrase": 0, "period": 1, "section": 2, "final": 3}
        self.spans = {}
        for name, minimum in (("phrase", 0), ("period", 1), ("section", 2)):
            cuts = [0.0] + [b.at for b in self.boundaries if rank[b.level] >= minimum]
            cuts = sorted(set(round(c, 6) for c in cuts))
            self.spans[name] = list(zip(cuts, cuts[1:]))
        # Slow wander: a few random sinusoids over the piece.
        self.wander = [(rng.uniform(0.15, 0.6), rng.uniform(0, 2 * math.pi),
                        rng.uniform(0.4, 1.0)) for _ in range(3)]

    def segment_at(self, q: float):
        for info in self.segments:
            if info["start"] - 1e-9 <= q < info["end"] - 1e-9:
                return info
        return self.segments[-1]

    def bar_position(self, q: float):
        info = self.segment_at(q)
        starts = info["bar_starts"]
        bar = max(0, max(i for i, s in enumerate(starts) if s <= q + 1e-9))
        into = q - starts[bar]
        return info, bar, into, info["bar_beats"][bar], info["bar_lengths"][bar]

    def span_fraction(self, name: str, q: float):
        for a, b in self.spans[name]:
            if a - 1e-9 <= q < b - 1e-9:
                return (q - a) / max(b - a, 1e-9), a, b
        return 1.0, self.end, self.end

    def tempo_factor(self, q: float) -> float:
        style = self.style
        info, bar, into, beat, length = self.bar_position(q)
        segment = info["segment"]
        bar_offset = bar + into / length
        factor = interpolate(segment.tempo, bar_offset)
        # Phrase arches at three levels: press into the middle, relax out.
        for name, weight in (("phrase", 1.0), ("period", 0.6), ("section", 0.4)):
            x, _, _ = self.span_fraction(name, q)
            factor *= 1.0 + style.tempo_arch * weight * (arch(x) - ARCH_MEAN)
        # Ritardando into each boundary: it lands on the arrival (the last
        # onset before it), which is then held a little broader, and whatever
        # rest follows is not stretched further.
        for boundary in self.boundaries:
            if boundary.level == "final":
                continue
            depth, beats = style.rit[boundary.level]
            window = beats * boundary.beat
            arrival = max(boundary.arrival, boundary.at - window)
            if arrival - window <= q < arrival:
                s = (q - (arrival - window)) / window
                factor *= 1.0 - depth * s * s
            elif arrival <= q < max(boundary.silent, arrival):
                factor *= 1.0 - 0.45 * depth
            elif arrival <= q < boundary.at:
                # A written rest after the arrival has sounded: the sound
                # rings on through it, and it is not stretched further.
                factor *= 1.1
        # Slow wander.
        position = q / max(self.end, 1e-9)
        factor *= 1.0 + style.wander * sum(
            amplitude * math.sin(2 * math.pi * rate * position * 8 + phase)
            for rate, phase, amplitude in self.wander) / 2.0
        return factor

    def dynamic(self, q: float) -> float:
        style = self.style
        info, bar, into, beat, length = self.bar_position(q)
        base = interpolate(info["segment"].dyn, bar + into / length, level)
        shaped = 0.0
        for name, weight in (("phrase", 1.0), ("period", 0.7), ("section", 0.5)):
            x, _, _ = self.span_fraction(name, q)
            shaped += style.dyn_arch * weight * (arch(x) - ARCH_MEAN)
        return base + shaped

    def metric(self, q: float) -> tuple[float, int]:
        """Metrical weight and which beat of the bar (-1 off the beat)."""
        info, bar, into, beat, length = self.bar_position(q)
        downbeat, onbeat, offbeat = self.style.metric
        if abs(into) < 1e-6:
            return downbeat, 0
        ratio = into / beat
        if abs(ratio - round(ratio)) < 1e-6:
            return onbeat, int(round(ratio))
        return offbeat, -1


def build_clock(piece: Piece, timeline: Timeline, notes: list[Note]):
    """Seconds at every grid point of the unfolded form."""
    style = piece.style
    final_onset = max(n.start for n in notes)
    cells = int(math.ceil(timeline.end * GRID)) + 1
    seconds = [0.0] * (cells + 1)
    base = 60.0 / piece.bpm / GRID  # seconds per cell at the base tempo
    ratio, bars, curve = style.final_rit
    info, bar, _, _, length = timeline.bar_position(final_onset)
    window = bars * length
    rit_start = final_onset - window
    breaths = {round(info["start"] * GRID): info["segment"].breath
               for info in timeline.segments if info["segment"].breath > 0}
    held = max(timeline.end - final_onset, 1e-6)
    factors = []
    for cell in range(cells):
        q = (cell + 0.5) / GRID
        if q >= final_onset:
            step = style.final_hold / held / GRID
        else:
            factor = timeline.tempo_factor(q)
            if q >= rit_start:
                x = (q - rit_start) / window
                factor *= (1.0 + (ratio ** curve - 1.0) * x) ** (1.0 / curve)
            factor = min(max(factor, 0.45), 1.55)
            factors.append(factor)
            step = base / factor
        seconds[cell + 1] = seconds[cell] + step
        if cell + 1 in breaths:
            seconds[cell + 1] += breaths[cell + 1]

    def clock(q: float) -> float:
        position = min(max(q * GRID, 0.0), cells)
        low = int(position)
        if low >= cells:
            return seconds[cells]
        return seconds[low] + (seconds[low + 1] - seconds[low]) * (position - low)
    clock.range = (min(factors), max(factors))
    # The renderer uses this same piecewise clock for the key-up join grace.
    # Include breaths and the final hold: each cell advances 1/48 quarter,
    # even when it takes longer than the surrounding musical pulse.
    clock.tempos = [(seconds[cell], 60.0 / GRID
                     / (seconds[cell + 1] - seconds[cell]))
                    for cell in range(cells)]
    return clock


def assign_roles(notes: list[Note]):
    """melody / bass / inner within each part; tremolo positions."""
    by_part = {}
    for note in notes:
        by_part.setdefault(note.part, []).append(note)
    for part, group in by_part.items():
        group.sort(key=lambda n: (n.start, n.pitch))
        # A tremolo: a run of one pitch repeated at a thirty-second or
        # shorter, each note starting where the last ended.
        short = [n for n in group if n.end - n.start <= 0.1876]
        runs, current = [], []
        for note in short:
            if current and note.pitch == current[-1].pitch \
                    and abs(note.start - current[-1].end) < 1e-6:
                current.append(note)
            else:
                if len(current) >= 2:
                    runs.append(current)
                current = [note]
        if len(current) >= 2:
            runs.append(current)
        for run in runs:
            for index, note in enumerate(run):
                note.tremolo = index
        polyphonic = any(a.start < b.end and b.start < a.end and a is not b
                         for a, b in zip(group, group[1:]))
        if not polyphonic:
            for note in group:
                note.role = "line"
            continue
        for note in group:
            sounding = [n.pitch for n in group
                        if n.start < note.end - 1e-6 and n.end > note.start + 1e-6]
            if note.tremolo >= 0 or note.pitch == max(sounding):
                note.role = "melody"
            elif note.pitch == min(sounding) and note.pitch < 55:
                note.role = "bass"
            else:
                note.role = "inner"
        # A single note in a guitar texture that is the highest at its onset
        # but under a held melody is accompaniment, handled above; the
        # tremolo's thumb is the bass.
        for note in group:
            if note.role == "melody" and note.tremolo < 0 and note.pitch < 52:
                note.role = "bass"


def perform(piece: Piece, midi: MidiFile, sample_rate: int):
    """The performance file's text, the notes as played, and the clock."""
    rng = random.Random(zlib.crc32(piece.name.encode()))
    notes, boundaries, segments, end = unfold(piece, midi)
    timeline = Timeline(piece, boundaries, segments, end, rng,
                        sorted(set(n.start for n in notes)),
                        [(n.start, n.end) for n in notes])
    clock = build_clock(piece, timeline, notes)
    style = piece.style
    assign_roles(notes)

    # At a section's or period's end the last sounds ring on through a
    # written rest into what follows, as a guitar's do, instead of being
    # damped into silence for the breath.
    for info in segments:
        if info["segment"].close not in ("section", "period"):
            continue
        last_bar = info["bar_starts"][-1]
        for part in piece.guitars:
            own = [n for n in notes if n.part == part and n.segment == info["number"]]
            if not own:
                continue
            for note in own:
                if note.end >= last_bar - 1e-6 and note.end < info["end"] - 1e-6 \
                        and not any(o.start >= note.end - 1e-6 for o in own):
                    note.end = info["end"]

    # Mean melody pitch per phrase, for the contour term.
    phrase_mean = {}
    for note in notes:
        if note.role in ("melody", "line"):
            _, a, _ = timeline.span_fraction("phrase", note.start)
            phrase_mean.setdefault((note.part, a), []).append(note.pitch)
    phrase_mean = {k: sum(v) / len(v) for k, v in phrase_mean.items()}

    events = []   # (seconds, order, text)
    order = 0

    def emit(seconds, text):
        nonlocal order
        events.append((LEAD_IN + max(-LEAD_IN, seconds), order, text))
        order += 1

    last_onset = max(n.start for n in notes)
    t_final = clock(last_onset) + 0.05
    for part, guitar in piece.guitars.items():
        own = [n for n in notes if n.part == part]
        if not own:
            continue
        groups = {}
        for note in own:
            groups.setdefault(round(note.start, 6), []).append(note)
        onsets = sorted(groups)
        drift = 0.0
        rho = 0.85
        sigma = style.drift * guitar.looseness
        previous_time = -1.0
        group_time = {}
        for index, q in enumerate(onsets):
            group = sorted(groups[q], key=lambda n: n.pitch)
            # One note per pitch.
            unique = {}
            for note in group:
                if note.pitch not in unique or note.end > unique[note.pitch].end:
                    unique[note.pitch] = note
            group = sorted(unique.values(), key=lambda n: n.pitch)[-6:]
            groups[q] = group

            drift = rho * drift + sigma * math.sqrt(1 - rho * rho) * rng.gauss(0, 1)
            info, bar, into, beat, length = timeline.bar_position(q)
            lean = 0.0
            if style.beat_lean:
                ratio = into / beat
                if abs(ratio - round(ratio)) < 1e-6:
                    k = int(round(ratio)) % len(style.beat_lean)
                    lean = style.beat_lean[k] * beat * 60.0 / piece.bpm
            t = clock(q) + drift + lean + guitar.lead \
                + rng.gauss(0, style.jitter * guitar.looseness)
            # Tremolo fingers: a-m-i are not evenly spaced.
            tremolo = [n for n in group if n.tremolo >= 0]
            if tremolo and style.tremolo_cycle:
                finger = style.tremolo_cycle[tremolo[0].tremolo % len(style.tremolo_cycle)]
                t += finger[1]
            if q >= last_onset - 1e-9:
                t = clock(q) + rng.gauss(0, 0.004)
            # Keep successive onsets of one player in order.
            gap = (clock(q) - clock(onsets[index - 1])) if index else 1.0
            t = max(t, previous_time + min(0.4 * gap, 0.03))
            previous_time = t
            group_time[q] = t

            # Velocities.
            dyn = timeline.dynamic(q)
            weight, _ = timeline.metric(q)
            velocities = []
            for note in group:
                v = dyn + weight + guitar.balance
                v += {"melody": style.melody, "bass": style.bass,
                      "inner": style.inner, "line": 0.0}[note.role]
                if note.role in ("melody", "line"):
                    _, a, _ = timeline.span_fraction("phrase", q)
                    mean = phrase_mean.get((part, a), note.pitch)
                    v += style.contour * (note.pitch - mean)
                if note.tremolo >= 0 and style.tremolo_cycle:
                    v += style.tremolo_cycle[note.tremolo % len(style.tremolo_cycle)][0]
                v += rng.gauss(0, style.scatter)
                note.velocity = min(max(v, VELOCITY_FLOOR), VELOCITY_CEILING)
                velocities.append(note.velocity)

            # How the chord is struck.
            delays = [0.0] * len(group)
            if len(group) == 2:
                lead = style.pinch_lead + rng.gauss(0, 0.006)
                delays = [0.0, max(0.0, lead)]
            elif len(group) > 2:
                next_gap = (clock(onsets[index + 1]) - clock(q)) \
                    if index + 1 < len(onsets) else 2.0
                if rng.random() < style.block or next_gap < 0.22:
                    spread = abs(rng.gauss(0.010, 0.004))
                else:
                    softness = max(0.0, 0.6 - dyn) / 0.4
                    cadence = any(abs(b.at - q) < beat + 1e-6 and b.level != "phrase"
                                  for b in timeline.boundaries) or q >= last_onset - 1e-9
                    spread = style.roll * (len(group) - 1) / 3.0 \
                        * (1.0 + 0.6 * softness) * (1.8 if cadence else 1.0) \
                        * rng.uniform(0.7, 1.3)
                spread = min(spread, 0.35 * next_gap + 0.02)
                steps = [rng.uniform(0.6, 1.4) for _ in group[1:]]
                total = sum(steps)
                acc = 0.0
                for k, step in enumerate(steps, start=1):
                    acc += step
                    delays[k] = spread * acc / total
            for note, delay in zip(group, delays):
                note.on = t + delay
            if len(group) == 1:
                emit(t, f"{part} on {group[0].pitch} {group[0].velocity:.3f}")
            else:
                members = " ".join(f"{n.pitch}:{n.velocity:.3f}:{d:.4f}"
                                   for n, d in zip(group, delays))
                emit(t, f"{part} chord {members}")

        # Ends of notes: where the written end falls in this player's time.
        def time_at(q):
            later = [o for o in onsets if o >= q - 1e-9]
            if later and abs(later[0] - q) < 1e-6:
                return group_time[later[0]]
            return clock(q) + drift_at(q)

        def drift_at(q):
            before = [o for o in onsets if o <= q]
            if not before:
                return 0.0
            o = before[-1]
            return group_time[o] - clock(o)

        flat = sorted((n for g in groups.values() for n in g), key=lambda n: n.start)
        # A key-up must come before the same pitch is taken again.
        next_same, upcoming = {}, {}
        for note in reversed(flat):
            next_same[id(note)] = upcoming.get(note.pitch)
            upcoming[note.pitch] = group_time[round(note.start, 6)]
        bass_starts = sorted(group_time[round(n.start, 6)] for n in flat
                             if n.role == "bass")
        starts = sorted(set(round(n.start, 6) for n in flat))

        def harmony_end(q):
            info, bar, into, beat, length = timeline.bar_position(q)
            start = info["bar_starts"][bar]
            if style.ring == "beat":
                return start + (math.floor(into / beat + 1e-6) + 1) * beat
            if style.ring == "half" and length >= 2 * beat - 1e-6:
                half = length / 2.0
                return start + (half if into < half - 1e-6 else length)
            return start + length

        for note in flat:
            # The written length, as this style articulates it.
            end = time_at(note.end)
            if note.end - note.start >= 1.0 - 1e-6:
                end = note.on + (end - note.on) * style.lift
            end = note.on + (end - note.on) * style.legato
            # Left ringing: on to the harmony's change or a neighbouring note.
            if style.ring and note.role != "bass" and note.tremolo < 0:
                target = harmony_end(note.start)
                near = [o.start for o in flat
                        if o.start > note.start + 1e-6 and abs(o.pitch - note.pitch) <= 2]
                if near:
                    target = min(target, near[0])
                if target > note.end + 1e-6:
                    end = max(end, min(time_at(target), note.on + style.ring_max))
            # Into a rest that follows: players seldom damp on the dot.
            following = [q for q in starts if q >= note.end - 1e-6]
            if not following or following[0] > note.end + 1e-6:
                rest_end = time_at(following[0]) if following else end + 1.0
                end = max(end, min(time_at(note.end) + style.rest_ring, rest_end))
            if note.role == "bass" and style.bass_ring:
                later = [b for b in bass_starts if b > note.on + 1e-6]
                if later:
                    end = max(end, min(later[0], note.on + 4.0))
            if note.start >= last_onset - 1e-9:
                end = t_final + style.final_hold
            limit = next_same.get(id(note))
            if limit is not None:
                end = min(end, limit - 0.002)
            end = max(end, note.on + 0.05)
            note.off = end
            emit(end, f"{part} off {note.pitch} {rng.uniform(0.35, 0.65):.2f}")

        # Left-hand vibrato on long, sung notes.
        if style.vibrato > 0:
            singers = [n for n in flat if n.role in ("melody", "line")]
            for note in singers:
                emit(note.on - 0.0005, f"{part} vibrato 0")
                duration = note.off - note.on
                if duration >= 0.55 and note.tremolo < 0:
                    depth = style.vibrato * min(1.0, duration / 1.4) \
                        * rng.uniform(0.7, 1.2)
                    emit(note.on + rng.uniform(0.12, 0.25),
                         f"{part} vibrato {min(depth, 1.0):.2f}")

        # Tone colour per segment, varied a little per phrase.
        for info in timeline.segments:
            tone = info["segment"].tone
            if isinstance(tone, dict):
                tone = tone.get(part)
            if tone is None:
                tone = (guitar.pluck, guitar.touch)
            for a, b in timeline.spans["phrase"]:
                if info["start"] - 1e-9 <= a < info["end"] - 1e-9:
                    pluck = min(max(tone[0] + rng.gauss(0, 0.02), 0.05), 0.95)
                    touch = min(max(tone[1] + rng.gauss(0, 0.02), 0.0), 1.0)
                    emit(clock(a) - 0.02, f"{part} tone {pluck:.3f} {touch:.3f}")

    events.sort()
    lines = ["ACUSTRA_REPERTOIRE_V1", f"title {piece.title}",
             f"sample_rate {sample_rate}", f"tail {piece.tail:g}"]
    for name, guitar in piece.guitars.items():
        lines.append(guitar.line(name))
    # The tempo map, one line per quarter note: "# clock <quarter> <seconds>".
    for k in range(int(timeline.end) + 1):
        lines.append(f"# clock {k} {LEAD_IN + clock(k):.4f}")
    # Keep the authored note/control events unchanged. Tempo applies to all
    # guitars at its exact clock-cell boundary; the first cell also covers
    # the lead-in and any player's early first attack.
    for index, (seconds, bpm) in enumerate(clock.tempos):
        when = 0.0 if index == 0 else LEAD_IN + seconds
        lines.append(f"tempo {when:.9f} {bpm:.12g}")
    for seconds, _, text in events:
        lines.append(f"e {seconds:.5f} {text}")
    performed = [n for n in notes if n.off > 0.0]
    return "\n".join(lines) + "\n", performed, clock


def summary(piece: Piece, notes, clock) -> str:
    velocities = sorted(n.velocity for n in notes)
    slowest, fastest = clock.range
    pick = lambda xs, f: xs[min(len(xs) - 1, int(f * len(xs)))]
    return (f"{piece.name}: {len(notes)} notes, {max(n.off for n in notes):.0f} s; "
            f"velocity {velocities[0]:.2f}-{velocities[-1]:.2f} "
            f"(5-95% {pick(velocities, .05):.2f}-{pick(velocities, .95):.2f}); "
            f"tempo {slowest:.2f}-{fastest:.2f} x {piece.bpm:g} per quarter "
            f"(before breaths and the last chord's hold)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--midi-dir", required=True, type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--rate", type=int, default=96000)
    parser.add_argument("--only", nargs="*", default=None,
                        help="names (or name prefixes) of pieces to write")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for piece in PIECES:
        if args.only and not any(piece.name.startswith(o) for o in args.only):
            continue
        midi = parse_midi(load_source(piece.source, args.midi_dir, args.download))
        text, notes, clock = perform(piece, midi, args.rate)
        (args.out / f"{piece.name}.txt").write_text(text)
        print(summary(piece, notes, clock))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
