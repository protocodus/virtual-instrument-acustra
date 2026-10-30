#!/usr/bin/env python3
"""Measure the embedded Shinyguitar round robins and Acustra renders alike.

    AcustraExportReferenceBank bank/
    compare_reference_takes.py --write-events takes.txt
    AcustraRenderEventList 3 takes.txt jumbo.wav 257
    compare_reference_takes.py bank/ jumbo=jumbo.wav [...]

    compare_reference_takes.py --write-events notes.txt --once
    AcustraRenderEventList 3 notes.txt jumbo-once.wav 65
    compare_reference_takes.py --beating bank/ jumbo=jumbo-once.wav [...]
    compare_reference_takes.py --decay bank/ jumbo=jumbo-once.wav [...]

The bank's SteelPicked zones are Karoryfer's Shinyguitar: a picked steel
flat-top, four round robins of every third semitone in four velocity
layers. The 16 roots from E2 to C6 are measured, one layer at a time
(--layer, the layer's lowest velocity: 1, 33, 65 or 97). The events play the
same roots at velocity 80, four times each (once with --once), 4 s apart and
held 3.5 s. Nothing from the bank leaves the build directory.

Takes (the default) compares each note's round robins with each other, so the
microphone, the same for every take, drops out. Over 30-330 ms after the
onset each take gives:
  - its level (RMS) and brightness (power above 2.5 kHz against all of it),
    their SD within a note, the median over notes;
  - partials 1-12, each found from the last two partials' spacing and dropped
    50 dB under the note's strongest. Their deviation from the note's mean is
    fitted with a + b log2(n). b, the tilt in dB per octave of partial number,
    is what a sharper or duller release changes; its SD over all takes is
    printed. What the line leaves, its RMS the median over takes, is the comb
    scatter: a force or a release angle moves every partial together, and a
    pluck point that moves shifts the comb |sin(n pi p)| under the partials
    one by one.

--beating measures each note's partials 1-4: the envelope in dB from 0.1 s to
2 s while it stays 20 dB above the file's floor, less a quadratic fit, and
prints the share of notes whose residual spans more than 6 dB, with the
onset glide (the fundamental over 15-115 ms against 0.8-1.6 s, in cents).

--decay measures each note's partials 1-12 over its first 2 s, each
envelope's slope in dB/s fitted over 30-300 ms (early) and 0.3-1.5 s (late)
while it stays 15 dB above the file's floor, and prints the median over notes
for H1, H2-4, H5-8 and H9-12. A string whose planes drain alike decays in one
stage; a recorded one snaps first (early faster than late).

The measures are Aculele's Tools/compare_sample_packs.py (2026-10-29), so
the guitar and the ukulele are read the same way. Needs NumPy, SciPy and
soundfile.
"""

import math
import sys

import numpy as np
import soundfile
from scipy import signal

ROOTS = [40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84]
TAKES, SPACING, HOLD, START, VELOCITY = 4, 4.0, 3.5, 0.2, 80


def write_events(path, takes):
    with open(path, "w") as out:
        for k in range(len(ROOTS) * takes):
            t = k * SPACING + START
            root = ROOTS[k // takes]
            out.write(f"{t:.4f} 144 {root} {VELOCITY}\n{t + HOLD:.4f} 128 {root} 0\n")


def spectrum(segment, rate, size):
    window = np.hanning(len(segment))
    return np.abs(np.fft.rfft(segment * window, size)), np.fft.rfftfreq(size, 1 / rate)


def peak_near(magnitude, bins, frequency, cents):
    index = np.where((bins >= frequency * 2 ** (-cents / 1200))
                     & (bins <= frequency * 2 ** (cents / 1200)))[0]
    if len(index) < 3:
        return frequency, -300.0
    k = index[np.argmax(magnitude[index])]
    a, b, c = np.log(magnitude[k - 1:k + 2] + 1e-30)
    curvature = a - 2 * b + c
    shift = 0.5 * (a - c) / curvature if curvature else 0.0
    return bins[k] + shift * (bins[1] - bins[0]), 20 * math.log10(magnitude[k] + 1e-30)


def onset(x, rate):
    envelope = np.abs(signal.hilbert(x))
    width = int(0.001 * rate)
    smooth = np.convolve(envelope, np.ones(width) / width, mode="same")
    k = np.argmax(smooth > 0.1 * smooth.max())
    while k > 0 and smooth[k] > 0.02 * smooth.max():
        k -= 1
    return k


def partial_envelope_db(x, rate, frequency, n, hop):
    width = min(1 / 12, 0.4 / n)
    sos = signal.butter(4, [frequency * 2 ** -width, min(frequency * 2 ** width, 0.45 * rate)],
                        btype="bandpass", fs=rate, output="sos")
    envelope = np.abs(signal.hilbert(signal.sosfiltfilt(sos, x)))[::int(hop * rate)]
    return 20 * np.log10(envelope + 1e-12)


def take(x, rate, midi):
    x = x[onset(x, rate):]
    nominal = 440 * 2 ** ((midi - 69) / 12)
    long_magnitude, long_bins = spectrum(x[int(0.3 * rate):int(1.5 * rate)], rate, 1 << 20)
    segment = x[int(0.03 * rate):int(0.33 * rate)]
    magnitude, bins = spectrum(segment, rate, 1 << 19)
    found = [peak_near(long_magnitude, long_bins, nominal, 60)[0]]
    while len(found) < 12:
        # A stiff string's spacing widens, which a guess from n f1 misses.
        guess = 2 * found[-1] - (found[-2] if len(found) > 1 else 0.0)
        if guess > 9000:
            break
        found.append(peak_near(long_magnitude, long_bins, guess, 40)[0])
    partials = np.full(12, np.nan)
    partials[:len(found)] = [peak_near(magnitude, bins, f, 25)[1] for f in found]
    power = magnitude ** 2
    return dict(level=10 * math.log10(np.mean(segment ** 2)),
                brightness=10 * math.log10(power[bins > 2500].sum() / power.sum()),
                partials=partials)


def beating(x, rate, midi):
    x = x[onset(x, rate):][:int(3.0 * rate)]
    nominal = 440 * 2 ** ((midi - 69) / 12)
    f1 = peak_near(*spectrum(x[int(0.2 * rate):int(1.2 * rate)], rate, 1 << 20), nominal, 60)[0]
    long_magnitude, long_bins = spectrum(x[int(0.08 * rate):int(1.0 * rate)], rate, 1 << 20)
    spans = []
    for n in range(1, 5):
        fn = peak_near(long_magnitude, long_bins, n * f1 * math.sqrt(1 + 2e-5 * n * n), 40)[0]
        db = partial_envelope_db(x[:int(2.2 * rate)], rate, fn, n, 0.01)
        times = np.arange(len(db)) * 0.01
        part, times = db[10:200], times[10:200]
        audible = np.where(part > np.percentile(db, 5) + 20)[0]
        if len(audible) < 40:
            spans.append(math.nan)
            continue
        part, times = part[:audible[-1]], times[:audible[-1]]
        residual = part - np.polyval(np.polyfit(times, part, 2), times)
        spans.append(residual.max() - residual.min())

    def pitch(start, stop):
        return peak_near(*spectrum(x[int(start * rate):int(stop * rate)], rate, 1 << 20), f1, 60)[0]

    return spans, 1200 * math.log2(pitch(0.015, 0.115) / pitch(0.8, 1.6))


def decay(x, rate, midi):
    x = x[onset(x, rate):][:int(2.0 * rate)]
    nominal = 440 * 2 ** ((midi - 69) / 12)
    magnitude, bins = spectrum(x[int(0.08 * rate):int(1.0 * rate)], rate, 1 << 20)
    f1 = peak_near(magnitude, bins, nominal, 60)[0]
    slopes = np.full((12, 2), np.nan)
    if len(x) < int(1.6 * rate):
        return slopes
    for n in range(1, 13):
        fn = peak_near(magnitude, bins, n * f1 * math.sqrt(1 + 2e-5 * n * n), 40)[0]
        if fn > 0.4 * rate:
            break
        db = partial_envelope_db(x, rate, fn, n, 0.01)
        times = np.arange(len(db)) * 0.01
        floor = np.percentile(db, 5)
        for k, (start, stop) in enumerate(((0.03, 0.3), (0.3, 1.5))):
            use = (times >= start) & (times < stop) & (db > floor + 15)
            if use.sum() > 8:
                slopes[n - 1, k] = -np.polyfit(times[use], db[use], 1)[0]
    return slopes


def bank_notes(directory, layer):
    notes = []
    for line in open(f"{directory}/index.tsv"):
        file, bank, _, root, low, *_ = line.rstrip("\n").split("\t")
        if bank == "0" and int(root) in ROOTS and int(low) == layer:
            x, rate = soundfile.read(f"{directory}/{file}")
            notes.append((int(root), x, rate))
    return notes


def render_notes(path, takes):
    x, rate = soundfile.read(path, always_2d=True)
    x = x.mean(axis=1)
    return [(ROOTS[k // takes], x[int((k * SPACING + START) * rate) - 200:int((k * SPACING + START + HOLD) * rate)], rate)
            for k in range(len(ROOTS) * takes)]


def report_takes(label, notes):
    groups = {}
    for midi, x, rate in notes:
        groups.setdefault(midi, []).append(take(x, rate, midi))
    spreads, tilts, scatter = [], [], []
    order = np.log2(np.arange(1, 13))
    for takes in groups.values():
        spreads.append([np.std([t[k] for t in takes], ddof=1) for k in ("level", "brightness")])
        levels = np.array([t["partials"] for t in takes])
        levels[levels < np.nanmax(levels) - 50] = np.nan
        for deviation in levels - np.nanmean(levels, axis=0):
            use = np.isfinite(deviation)
            if use.sum() >= 6:
                slope, offset = np.polyfit(order[use], deviation[use], 1)
                tilts.append(slope)
                scatter.append(np.sqrt(np.mean((deviation[use] - offset - slope * order[use]) ** 2)))
    level, brightness = np.median(spreads, axis=0)
    print(f"{label:14s} {len(tilts):5d} | {level:8.2f} {brightness:9.2f} | {np.std(tilts):7.2f} {np.median(scatter):12.2f}")


def report_beating(label, notes):
    rows = [beating(x, rate, midi) for midi, x, rate in notes]
    spans = np.array([r[0] for r in rows])
    shares = [np.nanmean(spans[:, n] > 6) * 100 for n in range(4)]
    print(f"{label:14s} {len(rows):5d} | {np.nanmedian([r[1] for r in rows]):+6.1f} | "
          + " ".join(f"{s:3.0f}" for s in shares))


def report_decay(label, notes):
    slopes = np.array([decay(x, rate, midi) for midi, x, rate in notes])
    cells = []
    for group in (slice(0, 1), slice(1, 4), slice(4, 8), slice(8, 12)):
        early, late = (np.nanmedian(slopes[:, group, k]) for k in (0, 1))
        cells.append(f"{early:5.1f} {late:5.1f}")
    print(f"{label:14s} {len(notes):5d} | " + " | ".join(cells))


def main():
    arguments = sys.argv[1:]
    if arguments[:1] == ["--write-events"]:
        write_events(arguments[1], 1 if "--once" in arguments else TAKES)
        return
    mode = "--beating" if "--beating" in arguments else "--decay" if "--decay" in arguments else None
    layers = [1, 33, 65, 97]
    if "--layer" in arguments:
        at = arguments.index("--layer")
        layers = [int(arguments[at + 1])]
        del arguments[at:at + 2]
    arguments = [a for a in arguments if not a.startswith("--")]
    if not arguments:
        sys.exit(__doc__)
    directory, renders = arguments[0], [a.split("=", 1) for a in arguments[1:]]
    if mode == "--decay":
        print(f"{'':14s} notes | dB/s early late: H1 | H2-4 | H5-8 | H9-12")
        for layer in layers:
            report_decay(f"shinyguitar {layer}", bank_notes(directory, layer))
        for label, path in renders:
            report_decay(label, render_notes(path, 1))
    elif mode:
        print(f"{'':14s} notes | glide | beating % H1 H2 H3 H4")
        for layer in layers:
            report_beating(f"shinyguitar {layer}", bank_notes(directory, layer))
        for label, path in renders:
            report_beating(label, render_notes(path, 1))
    else:
        print(f"{'':14s} takes | level SD bright SD | tilt SD comb scatter")
        for layer in layers:
            report_takes(f"shinyguitar {layer}", bank_notes(directory, layer))
        for label, path in renders:
            report_takes(label, render_notes(path, TAKES))


if __name__ == "__main__":
    main()
