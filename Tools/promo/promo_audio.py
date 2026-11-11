"""Acustra promo: the last strains of three performed pieces, each to its
final chord, mixed from its players' full-piece renders, joined and mastered
as one track.

Run in the work directory holding out/<piece>/<part>.wav and .csv from
SongRender, one pair for each part repertoire.py writes. The renders already
carry each patch's own room; nothing else is added: no reverb, EQ or
compression beyond this mastering. The mix is level and place only: each
player's stereo picture is narrowed to WIDTH of the patch's own and set at
the part's `pan` by a constant-power balance, and its level is the part's
`level` in dB against the melody, on the excerpt's integrated BS.1770
loudness, so a balance is a loudness difference whatever the patches' own
volumes. Each excerpt
starts a little before its first downbeat (a 40 ms fade-in) and runs to the
end of its tail (the last 0.9 s faded); excerpts are matched on integrated
BS.1770 loudness, overlapped by 0.35 s, then the whole is brought to -16 LUFS
with true peak <= -1.15 dBTP (4x) by a 5 ms look-ahead limiter. Writes
out/<set>/<set>.wav, timeline.json and report.json; <set> is the film
(promo) or one short.

    python3 promo_audio.py promo | short-minuet | short-kesh | short-butterfly
"""
import json
import os
import pathlib
import sys

import numpy as np
import pyloudnorm as pyln
import soundfile as sf
from scipy.ndimage import minimum_filter1d
from scipy.signal import resample_poly

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from repertoire import PIECES  # noqa: E402

sr = 48000
meter = pyln.Meter(sr)
VERSION = '1.9.41d2'
# piece, title, credit line, the consort in a line
MIN = dict(piece='minuet', title='Minuet in G', sub='Christian Petzold · 1725', how='parlor tune · thumb bass · open G')
KES = dict(piece='kesh', title='The Kesh', sub='Irish jig · traditional', how='flatpicked tune · palm-muted · DADGAD')
BUT = dict(piece='butterfly', title='The Butterfly', sub='Irish slip jig · traditional', how='dreadnought tune · DADGAD · piezo')
# name -> ([excerpt with its first beat], lead-in s); a short is one excerpt under 30 s
SETS = {
    'promo': ([dict(MIN, beat=48), dict(KES, beat=48), dict(BUT, beat=72)], 0.6),
    'short-minuet': ([dict(MIN, beat=72)], 0.1),
    'short-kesh': ([dict(KES, beat=96)], 0.1),
    'short-butterfly': ([dict(BUT, beat=108)], 0.1),
}
OVERLAP, FADE_IN, FADE_OUT, TAIL, PRE = 0.35, 0.04, 0.9, 2.6, 0.12
WIDTH = 0.7


def tp(sig):
    return 20 * np.log10(np.abs(resample_poly(sig, 4, 1, axis=0)).max())


def notes_of(performance):
    """[onset, release, pitch, velocity] from the performance's own events."""
    on, out = {}, []
    for seconds, order, data, _ in sorted(performance.events, key=lambda e: (e[0], e[1])):
        if data[0] & 0xF0 == 0x90:
            on[data[1]] = (seconds, data[2])
        elif data[1] in on:
            start, vel = on.pop(data[1])
            out.append([round(start, 4), round(max(seconds, start + 0.04), 4), data[1], vel])
    return sorted(out)


def placed(x, pan):
    """A stereo render narrowed to WIDTH and balanced to `pan` (-1 .. 1),
    constant power. A guitar's microphone pair hears its two sides unequally
    (the louder side by up to 3.7 dB in these renders), so `pan` places the
    part's energy, not its channels: each side is set so that their power
    splits as the constant-power law at `pan` splits it."""
    mid, side = x.mean(1), WIDTH * (x[:, 0] - x[:, 1]) / 2
    left, right = mid + side, mid - side
    pl, pr = (left ** 2).mean() + 1e-20, (right ** 2).mean() + 1e-20
    angle = (pan + 1) * np.pi / 4
    gl, gr = np.cos(angle) * np.sqrt((pl + pr) / pl), np.sin(angle) * np.sqrt((pl + pr) / pr)
    return np.stack([left * gl, right * gr], 1)


def trace_of(path):
    """SongRender's 60 Hz trace: each string's open note, then per frame its
    level as dB re 1 (rounded), its state (0 quiet or ringing in sympathy
    only, 1 played, 2 key down) and the note it is stopped at."""
    lines = pathlib.Path(path).read_text().split('\n')
    strings = [int(x) for x in lines[0].split(',')[1:]]
    cells = [[c.split(':') for c in r.split(',')[1:]] for r in lines[1:] if r]
    level = [[round(20 * np.log10(float(c[0]) + 1e-12)) for c in r] for r in cells]
    state = [[int(c[1]) for c in r] for r in cells]
    note = [[int(c[3]) for c in r] for r in cells]
    return strings, level, state, note


def strings_of(notes, state, note, strings):
    """The string each note sounded on: the one stopped at its pitch and
    played within 80 ms of its onset; failing that (a cut too short for the
    trace), the highest string whose open note is at or below it."""
    out = []
    for onset, _, pitch, _ in notes:
        i0 = max(0, int(onset * 60))
        k = next((k for i in range(i0, min(len(note), i0 + 5)) for k in range(len(strings))
                  if note[i][k] == pitch and state[i][k] >= 1), None)
        out.append(k if k is not None else max([j for j, o in enumerate(strings) if o <= pitch] or [0]))
    return out


def master(mix):
    """Brings `mix` to -16 LUFS in place, true peak <= -1.15 dBTP (4x) by a
    5 ms look-ahead limiter; returns what it did."""
    total = len(mix)
    mix *= 10 ** ((-16 - meter.integrated_loudness(mix)) / 20)
    before = round(float(tp(mix)), 2)
    up = np.abs(resample_poly(mix, 4, 1, axis=0)).max(1).reshape(-1, 4).max(1)[:total]
    need = np.minimum(1, 10 ** (-1.15 / 20) / np.maximum(up, 1e-9))
    la = int(.005 * sr)
    target = minimum_filter1d(need, size=2 * la + 1, origin=-la)
    gain, rel, cur = np.ones(total), np.exp(-1 / (0.12 * sr)), 1.0
    for i in range(total):
        cur = target[i] if target[i] < cur else min(target[i], cur + (1 - cur) * (1 - rel))
        gain[i] = cur
    mix *= gain[:, None]
    return dict(true_peak_before_limit=before, limiter_max_gr_db=round(float(-20 * np.log10(gain.min())), 2),
                lufs=round(float(meter.integrated_loudness(mix)), 2), true_peak=round(float(tp(mix)), 2), duration=round(total / sr, 3))


def main():
    name = sys.argv[1] if len(sys.argv) > 1 else 'promo'
    segments, lead_in = SETS[name]
    clips, report, at = [], dict(segments=[]), lead_in
    for seg in segments:
        parts = PIECES[seg['piece']]()
        stems = [sf.read(f"out/{seg['piece']}/{part.name}.wav")[0] for part in parts]
        players = [dict(name=part.name, patch=part.patch, pan=part.pan, notes=notes_of(part.p)) for part in parts]
        s0 = parts[0].p.seconds(seg['beat']) - PRE
        s1 = min(min(len(x) for x in stems) / sr, max(n[1] for pl in players for n in pl['notes']) + TAIL)
        a, b = int(s0 * sr), int(s1 * sr)
        clip, mixed = np.zeros((b - a, 2)), []
        for part, x in zip(parts, stems):
            stem = placed(x[a:b], part.pan)
            trim = -20 + part.level - meter.integrated_loudness(stem)
            clip += stem * 10 ** (trim / 20)
            mixed.append(dict(part=part.name, patch=part.patch, pan=part.pan, level_db=part.level, trim_db=round(float(trim), 2)))
        env = np.ones(b - a)
        fi, fo = int(FADE_IN * sr), int(FADE_OUT * sr)
        env[:fi] = np.linspace(0, 1, fi) ** 2
        env[-fo:] *= np.linspace(1, 0, fo) ** 2
        clip *= env[:, None]
        g = 10 ** ((-16 - meter.integrated_loudness(clip)) / 20)
        clip *= g
        clips.append((at, clip))
        report['segments'].append(dict(title=seg['title'], parts=mixed, piece_s=[round(s0, 3), round(s1, 3)],
                                       promo_s=[round(at, 3), round(at + (b - a) / sr, 3)], match_gain_db=round(20 * np.log10(g), 2)))
        seg.update(start=at, end=at + (b - a) / sr, src0=s0, players=players)
        at += (b - a) / sr - OVERLAP
    os.makedirs(f'out/{name}', exist_ok=True)
    total = int((segments[-1]['end'] + (1.2 if len(segments) > 1 else 0.0)) * sr)
    mix = np.zeros((total, 2))
    for s, c in clips:
        i = int(round(s * sr))
        mix[i:i + len(c)] += c[:total - i]
    report.update(master(mix))
    sf.write(f'out/{name}/{name}.wav', mix, sr, subtype='PCM_24')

    # timeline for promo.mjs: each excerpt's players, their notes and their strings' 60 Hz trace, in piece time
    tl = dict(duration=total / sr, fps=30, version=VERSION, segments=[])
    for seg in segments:
        for player in seg['players']:
            player['strings'], player['energy'], player['state'], note = trace_of(f"out/{seg['piece']}/{player['name']}.csv")
            # the note each string is stopped at, as semitones above its open note
            player['fret'] = [[x - o if x else 0 for x, o in zip(row, player['strings'])] for row in note]
            player['on'] = strings_of(player['notes'], player['state'], note, player['strings'])
        tl['segments'].append({k: seg[k] for k in ('title', 'sub', 'how', 'start', 'end', 'src0', 'players')})
    json.dump(tl, open(f'out/{name}/timeline.json', 'w'))
    json.dump(report, open(f'out/{name}/report.json', 'w'), indent=1)
    print(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
