#!/usr/bin/env python3
"""Measure the notes in an AculeleProbe render.

    analyse_probe.py <render.wav> <note>:<start-seconds> [...] [--span S]

For each note (a MIDI number played alone from start-seconds), reports the
sounding fundamental, the first eight partials' frequencies (as cents of
inharmonicity against n f1), their levels against the fundamental at 50 ms,
and each partial's T60 from a straight-line fit to its envelope in dB
between 0.1 s and the earlier of the span's end or the envelope 40 dB down.
The mono sum of both channels is analysed; the file may be 24-bit PCM or
32-bit float, as the probe writes it.
"""

import argparse
import math
import struct
import sys

import numpy as np
from scipy import signal


def read_wav(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        sys.exit(f"{path}: not a WAV file")
    offset, fmt, rate, channels, bits, frames = 12, None, 0, 0, 0, None
    while offset + 8 <= len(data):
        tag, size = data[offset:offset + 4], struct.unpack("<I", data[offset + 4:offset + 8])[0]
        body = data[offset + 8:offset + 8 + size]
        if tag == b"fmt ":
            fmt, channels, rate = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
        elif tag == b"data":
            frames = body
        offset += 8 + size + (size & 1)
    if fmt == 3 and bits == 32:
        samples = np.frombuffer(frames, dtype="<f4").astype(np.float64)
    elif fmt == 1 and bits == 24:
        raw = np.frombuffer(frames, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        value = raw[:, 0] | (raw[:, 1] << 8) | (raw[:, 2] << 16)
        value = np.where(value >= 1 << 23, value - (1 << 24), value)
        samples = value / 8388607.0
    else:
        sys.exit(f"{path}: unsupported format {fmt}/{bits}")
    return rate, samples.reshape(-1, channels)


def partial_envelope(x, rate, frequency, hop=0.01):
    """The partial's amplitude every hop seconds: a band-pass two semitones
    wide around it, then the analytic signal's magnitude."""
    low, high = frequency * 2 ** (-1 / 12), min(frequency * 2 ** (1 / 12), 0.45 * rate)
    sos = signal.butter(4, [low, high], btype="bandpass", fs=rate, output="sos")
    band = signal.sosfiltfilt(sos, x)
    magnitude = np.abs(signal.hilbert(band))
    step = int(hop * rate)
    return magnitude[::step], hop


def measure(x, rate, note, start, span):
    segment = x[int(start * rate):int((start + span) * rate)]
    nominal = 440.0 * 2 ** ((note - 69) / 12)
    # The fundamental from a long window past the attack, refined by
    # parabolic interpolation of the log spectrum.
    window = segment[int(0.2 * rate):int(min(span, 1.5) * rate)]
    size = 1 << 20
    spectrum = np.abs(np.fft.rfft(window * np.hanning(len(window)), size))
    bins = np.fft.rfftfreq(size, 1 / rate)

    def peak_near(frequency, width_cents=60):
        lo, hi = frequency * 2 ** (-width_cents / 1200), frequency * 2 ** (width_cents / 1200)
        index = np.where((bins >= lo) & (bins <= hi))[0]
        if len(index) < 3:
            return frequency, -math.inf
        k = index[np.argmax(spectrum[index])]
        a, b, c = np.log(spectrum[k - 1:k + 2] + 1e-30)
        shift = 0.5 * (a - c) / (a - 2 * b + c)
        return bins[k] + shift * (bins[1] - bins[0]), 20 * math.log10(spectrum[k] + 1e-30)

    f1, _ = peak_near(nominal, 80)
    rows = []
    for n in range(1, 9):
        fn, _ = peak_near(n * f1, 40)
        if fn > 0.45 * rate:
            break
        envelope, hop = partial_envelope(segment, rate, fn)
        db = 20 * np.log10(envelope + 1e-12)
        t = np.arange(len(db)) * hop
        early = db[int(0.05 / hop)]
        first = int(0.1 / hop)
        fit_end = len(db) - int(0.15 / hop)
        below = np.where(db[first:fit_end] < db[first] - 40)[0]
        if len(below):
            fit_end = first + below[0]
        slope = np.polyfit(t[first:fit_end], db[first:fit_end], 1)[0] if fit_end - first > 10 else 0.0
        t60 = -60.0 / slope if slope < 0 else math.inf
        rows.append((n, fn, 1200 * math.log2(fn / (n * f1)), early, t60))
    peak = np.max(np.abs(segment))
    return f1, nominal, rows, peak


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("wav")
    parser.add_argument("notes", nargs="+", help="midi:start-seconds")
    parser.add_argument("--span", type=float, default=3.8)
    args = parser.parse_args()
    rate, frames = read_wav(args.wav)
    if not np.all(np.isfinite(frames)):
        sys.exit("non-finite samples in the render")
    x = frames.mean(axis=1)
    print(f"{args.wav}: {len(x) / rate:.2f} s, peak {20 * math.log10(np.max(np.abs(frames)) + 1e-30):.1f} dBFS")
    for item in args.notes:
        note, start = item.split(":")
        f1, nominal, rows, peak = measure(x, rate, int(note), float(start), args.span)
        print(f"  MIDI {note}: f1 {f1:.2f} Hz ({1200 * math.log2(f1 / nominal):+.1f} cents), "
              f"peak {20 * math.log10(peak + 1e-30):.1f} dBFS")
        reference = rows[0][3]
        for n, fn, cents, level, t60 in rows:
            print(f"    H{n}: {fn:8.1f} Hz {cents:+6.1f} c  {level - reference:+6.1f} dB  "
                  f"T60 {t60:5.2f} s")


if __name__ == "__main__":
    main()
