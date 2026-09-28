#!/usr/bin/env python3
"""Prepare the University of Iowa MIS classical guitar as a nylon target corpus.

Source: https://theremin.music.uiowa.edu/MISguitar.html (University of Iowa
Electronic Music Studios, Musical Instrument Samples, Lawrence Fritts). The
site's usage statement, on https://theremin.music.uiowa.edu/MIS.html, says the
recordings "may be downloaded and used for any projects, without
restrictions". The tool fetches both pages, checks that this sentence is still
there and records it verbatim in SOURCE.md with each page's SHA-256 at fetch
time. The site has no separate terms-of-use page.

Recording, as the guitar page's table states it: Raimundo 118 (a Spanish
classical guitar), Brian Penkrot, December 11, 2011, anechoic chamber, 5 feet,
Earthworks QTC40, Metric Halo interface. MIS.html adds that since 2011 the
centre microphone of a three-QTC40 Decca tree was downsampled to 16-bit 44.1
kHz mono chromatic scales. The pages do not say how the strings were plucked
(finger, thumb or nail), so rows say picking "finger" and note that the source
does not state the technique.

Protocol. The thresholds were set while inspecting the runs' envelopes,
spectra and onsets. No model render or scorer residual was consulted.

1. Format. Guitar.mono.1644.1.zip is the smallest faithful format on offer:
   120 MB, holding 45 16-bit 44.1 kHz mono AIFF chromatic runs, one per
   dynamic, string and range. (The individually linked *.mono.aif files are
   24-bit 48 kHz and add up to about 1 GB.) The zip URL, byte count and
   SHA-256 are pinned. Members are read from the zip in memory and their
   SHA-256 values recorded. A built-in PCM AIFF reader decodes them, with
   ffmpeg as the fallback.
2. Conditioning. The Earthworks QTC40 is flat to a few hertz, and the runs
   carry chamber and handling rumble: 1-60 Hz, up to -4 dBFS, and up to 27 dB
   above a soft note in the scorer's 20-500 ms level window. It raises that
   level by more than 0.5 dB on 220 of 350 notes, and it moves the scorer's
   onset by 10 ms on the median note. Every run is therefore high-passed
   (--high-pass-hz, default 60) by a causal 8th-order Butterworth filter
   starting from rest. This is the only processing. See condition() for why
   the filter is causal and what its group delay costs.
3. Note onsets, on the conditioned run. The 10 ms RMS level is taken on a 1
   ms hop. A candidate is a frame whose largest level in the next 80 ms is at
   least ONSET_RISE_DB above the energy mean of the previous 150 ms, while
   that 150 ms is quiet: at most QUIET_ABOVE_NOISE_DB above the run's noise
   floor, which is the 2nd percentile of its 50 ms levels, about -89 dBFS.
   Chained candidates merge, and a group is anchored at its largest rise.
   Groups peaking NOTE_PEAK_SPREAD_DB below the run's median note are
   dropped. The onset is FitPhysicalModel._onset (the scorer's own
   detector), run from 100 ms before the anchor.
4. Clean region. After onset + 0.2 s, the region ends at the first sudden
   rise in any of five STFT bands (80 Hz to 12 kHz). A rise is at least
   DISTURBANCE_RISE_DB from the previous 200 ms energy mean to the largest
   level in the next 30 ms, and reaches DISTURBANCE_ABOVE_NOISE_DB above that
   band's floor. Such rises are thumps, finger or hand noise, and re-touches.
   The region also ends at the next onset, at a 100 ms run within
   GAP_ABOVE_NOISE_DB of the noise floor (the edit gap, or a note that has
   fully decayed), or at the end of the file. clean_seconds runs from onset
   to that point.
5. Target: conditioned audio from onset - 20 ms to min(end - 20 ms, onset +
   4.2 s), with a 60 ms terminal half-cosine fade. It is float32 LE at the
   native rate and channel count. A run's first note starts within 0-60 ms of
   the file's first sample, and the missing pre-roll is digital silence
   (noted per row). A written target is deleted unless the scorer's onset on
   it, at 48 kHz, lands at 20 +- 1 ms.
6. Rejections. A note is rejected when any of these hold:
   - clean_seconds is under 1.25 s.
   - isolation_db is under 30 dB. isolation_db is the sample peak minus the
     RMS of the (up to) 100 ms of the file before onset.
   - No settled f0 is found.
   - Its pitch disagrees with the file name. Where the onset count equals the
     name's range, the measured MIDI must be the name's note at that
     position. Otherwise it must lie in the range, in chromatic order.
   - The onset is more than MAX_ONSET_LEAD ahead of the >200 Hz onset (the
     string's release), which means low-frequency rumble or handling before
     the attack.
   - Energy below 70 Hz left after the high-pass exceeds MAX_RESIDUAL_LF_DB
     relative to the note in the scorer's level window.
7. Pitch. YIN runs on 80-400 ms, on a further 75 Hz high-pass. Late in a soft
   high note the chamber noise and open strings ringing in sympathy (the A
   string at 108/216 Hz, the B string at 242 Hz) can outweigh the note. Two
   octave checks follow; see settled_f0. The estimate is then refined to the
   H1 peak of the scorer's own 400-1200 ms spectrum (_spectrum/_peak). When
   H1 is more than 35 dB below the strongest partial, the median of H2-H4
   divided by their harmonic numbers is used instead. The global tuning
   offset is the circular mean of every usable note's deviation from its
   nearest semitone. midi is round(69 + (cents(f0 re A440) - offset) / 100).
   cents_from_midi is measured against A440 equal temperament, as the
   scorer's tuning term sees it.
8. Rows. split "uiowa-nylon", material "nylon", picking "finger" (the source
   does not state it), round_robin 0, dynamic_marking pp/mf/ff. velocity comes
   from --velocity-map (default pp=40,mf=80,ff=112). dynamic_group is
   "<string>-<midi>" (strings E6 A5 D4 G3 B2 E1), with an optional
   --group-prefix. The scorer's dynamics term on this corpus compares the
   recorded pp/mf/ff levels with model levels rendered at these mapped
   velocities, so it depends on the mapping.
9. Dynamics law. dynamics_law.json holds the recorded velocity-to-timbre law
   per string/note group, summarised by register. It is computed on the
   written targets at 48 kHz from the scorer's onset. Level is the RMS over
   20-500 ms, the scorer's level window. The spectral and harmonic centroids
   over 0-40 ms and 80-900 ms (see _centroids) are given as mf/pp and ff/pp
   ratios in cents.

No EQ beyond the rumble high-pass, and no denoising, loudness normalisation
or manual alignment, is applied. Audio is never copied into the repository.

Usage:
  python3 Tools/PrepareIowaGuitarCorpus.py \
      --download-dir "$S/downloads/uiowa-nylon" --output "$S/corpora/uiowa-nylon"
  python3 Tools/PrepareIowaGuitarCorpus.py --self-test

--cafile names a CA bundle for hosts whose chain the system store lacks.
theremin.music.uiowa.edu chains to Sectigo Public Server Authentication Root
R46. By default certifi's bundle is used when it is installed.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import io
import json
import math
import re
import ssl
import struct
import subprocess
import sys
import time
import urllib.request
import zipfile
from pathlib import Path
from typing import Any

import numpy as np
from scipy.signal import butter, sosfilt, stft

sys.path.insert(0, str(Path(__file__).resolve().parent))
import FitPhysicalModel as features  # noqa: E402


CORPUS = "uiowa-nylon"
SITE = "https://theremin.music.uiowa.edu"
GUITAR_PAGE = f"{SITE}/MISguitar.html"
USAGE_PAGE = f"{SITE}/MIS.html"
ARCHIVE_URL = f"{SITE}/sound%20files/MIS/Piano_Other/guitar/Guitar.mono.1644.1.zip"
ARCHIVE_NAME = "Guitar.mono.1644.1.zip"
ARCHIVE_SHA256 = "42b24075a939a33d6eab12758bca763a24944fcd32b642ceb4f9290999c894dd"
ARCHIVE_BYTES = 119980148
USAGE_SENTENCE = (
    "Since 1997, these recordings have been freely available on this website "
    "and may be downloaded and used for any projects, without restrictions."
)
LICENSE = ("University of Iowa Electronic Music Studios usage statement: "
           "\"" + USAGE_SENTENCE + "\" (" + USAGE_PAGE + ")")
MEMBER = re.compile(
    r"1644mono/Guitar\.(pp|mf|ff)\.(sulE|sulA|sulD|sulG|sulB|sul_E)\."
    r"([A-G]b?[0-9])([A-G]b?[0-9])?\.aif")
STRINGS = {"sulE": "E6", "sulA": "A5", "sulD": "D4",
           "sulG": "G3", "sulB": "B2", "sul_E": "E1"}
PITCH = {"C": 0, "Db": 1, "D": 2, "Eb": 3, "E": 4, "F": 5, "Gb": 6,
         "G": 7, "Ab": 8, "A": 9, "Bb": 10, "B": 11}
DEFAULT_VELOCITY_MAP = "pp=40,mf=80,ff=112"

PRE_ROLL = 0.020
MAX_AFTER_ONSET = 4.2
FADE = 0.060
MIN_CLEAN = 1.25
MIN_ISOLATION_DB = 30.0
ONSET_RISE_DB = 25.0
QUIET_ABOVE_NOISE_DB = 30.0
NOTE_PEAK_SPREAD_DB = 20.0
MIN_NOTE_SPACING = 1.0
DISTURBANCE_START = 0.200
DISTURBANCE_RISE_DB = 12.0
DISTURBANCE_ABOVE_NOISE_DB = 15.0
DISTURBANCE_BANDS = ((80.0, 300.0), (300.0, 800.0), (800.0, 2000.0),
                     (2000.0, 5000.0), (5000.0, 12000.0))
GAP_ABOVE_NOISE_DB = 4.0
HIGH_PASS_HZ = 60.0
HIGH_PASS_ORDER = 8
REFERENCE_ONSET_HIGH_PASS_HZ = 200.0
MAX_ONSET_LEAD = 0.003
PITCH_HIGH_PASS_HZ = 75.0
RUMBLE_BELOW_HZ = 50.0
RESIDUAL_LF_BELOW_HZ = 70.0
NOTE_BAND_FROM_HZ = 78.0
MAX_RESIDUAL_LF_DB = -15.0
SCORER_ONSET_TOLERANCE = 0.001
GAP_SECONDS = 0.100
REGISTERS = ((40, 51, "E2-D#3"), (52, 63, "E3-D#4"),
             (64, 75, "E4-D#5"), (76, 88, "E5-E6"))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def note_number(name: str) -> int:
    match = re.fullmatch(r"([A-G]b?)([0-9])", name)
    if match is None:
        raise ValueError(f"bad note name {name}")
    return 12 * (int(match.group(2)) + 1) + PITCH[match.group(1)]


def nominal_sequence(first: str, last: str | None) -> list[int]:
    low = note_number(first)
    high = note_number(last) if last else low
    return list(range(low, high + 1))


# --------------------------------------------------------------------------
# Download


def _ssl_context(cafile: str | None) -> ssl.SSLContext:
    if cafile:
        return ssl.create_default_context(cafile=cafile)
    try:
        import certifi  # type: ignore
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def fetch(url: str, path: Path, context: ssl.SSLContext,
          expected_bytes: int | None = None, attempts: int = 8) -> bytes:
    """Download url to path, resuming with Range after a truncated read.

    The server often closes a response early, and urllib reads that as a
    normal end of file, so a download counts as complete only at the size the
    caller pins or, failing that, the size the server declares (Content-Length,
    or the total of a 206's Content-Range). Only complete files reach path.
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    partial = path.with_suffix(path.suffix + ".part")
    if path.exists() and (expected_bytes is None
                          or path.stat().st_size == expected_bytes):
        return path.read_bytes()
    declared: int | None = None
    for attempt in range(attempts):
        have = partial.stat().st_size if partial.exists() else 0
        request = urllib.request.Request(url, headers={
            "User-Agent": "Acustra-corpus-preparation/1",
            **({"Range": f"bytes={have}-"} if have else {})})
        try:
            with urllib.request.urlopen(request, context=context,
                                        timeout=120) as response:
                if have and response.status != 206:
                    have = 0
                    partial.unlink(missing_ok=True)
                total = response.headers.get("Content-Range", "").rpartition("/")[2]
                length = response.headers.get("Content-Length")
                if response.status == 206 and total.isdigit():
                    declared = int(total)
                elif response.status != 206 and length and length.isdigit():
                    declared = int(length)
                with partial.open("ab") as stream:
                    while True:
                        block = response.read(1 << 20)
                        if not block:
                            break
                        stream.write(block)
        except Exception as error:  # noqa: BLE001 - retried below
            print(f"  {url}: {error!r}; retry {attempt + 1}", file=sys.stderr)
            time.sleep(min(30, 2 ** attempt))
            continue
        size = partial.stat().st_size
        target = expected_bytes if expected_bytes is not None else declared
        if target is None or size == target:
            partial.replace(path)
            return path.read_bytes()
        if size > target:
            partial.unlink()
        print(f"  {url}: {size} of {target} bytes; retry {attempt + 1}",
              file=sys.stderr)
    raise RuntimeError(f"could not download {url}")


def page_text(raw: bytes) -> str:
    text = raw.decode("latin-1")
    text = re.sub(r"<script.*?</script>", " ", text, flags=re.S | re.I)
    text = html.unescape(re.sub(r"<[^>]*>", " ", text))
    return re.sub(r"\s+", " ", text).strip()


def recording_table(text: str) -> dict[str, str]:
    """Return the guitar page's metadata row, keyed by its column names."""
    header = ("Instrument Model Performer Date Location Technician Distance "
              "Microphone Interface Format")
    start = text.find(header)
    stop = text.find("Stereo Guitar.")
    if start < 0 or stop < 0:
        return {}
    row = text[start + len(header):stop].strip()
    match = re.fullmatch(
        r"(Guitar) (.+?) (Brian Penkrot) (\w+ \d+, \d{4}) (Anechoic Chamber) "
        r"(.+?) (\d+ feet) (.+?) (Metric Halo) (.+)", row)
    if match is None:
        return {"row": row}
    return dict(zip(header.split(), match.groups()))


# --------------------------------------------------------------------------
# Audio


def _extended(data: bytes) -> float:
    exponent = ((data[0] & 0x7F) << 8) | data[1]
    mantissa = int.from_bytes(data[2:10], "big")
    if exponent == 0 and mantissa == 0:
        return 0.0
    return mantissa * 2.0 ** (exponent - 16383 - 63)


def read_aiff(data: bytes) -> tuple[int, np.ndarray, int]:
    """Return rate, float64 frames x channels and bit depth of PCM AIFF."""
    if data[:4] != b"FORM" or data[8:12] not in (b"AIFF", b"AIFC"):
        raise ValueError("not an AIFF file")
    position, common, sound = 12, None, None
    compression = b"NONE"
    while position + 8 <= len(data):
        kind = data[position:position + 4]
        size = struct.unpack(">I", data[position + 4:position + 8])[0]
        body = data[position + 8:position + 8 + size]
        if kind == b"COMM":
            channels, frames, bits = struct.unpack(">hIh", body[:8])
            common = (channels, frames, bits, _extended(body[8:18]))
            if data[8:12] == b"AIFC":
                compression = body[18:22]
        elif kind == b"SSND":
            offset = struct.unpack(">I", body[:4])[0]
            sound = body[8 + offset:]
        position += 8 + size + (size & 1)
    if common is None or sound is None:
        raise ValueError("AIFF lacks COMM or SSND")
    channels, frames, bits, rate = common
    if compression not in (b"NONE", b"twos") or bits not in (16, 24):
        raise ValueError(f"unsupported AIFF encoding {compression!r}/{bits}")
    width = bits // 8
    raw = np.frombuffer(sound[:frames * channels * width], dtype=np.uint8)
    if raw.size != frames * channels * width:
        raise ValueError("AIFF sound data is truncated")
    if bits == 16:
        values = raw.view(">i2").astype(np.float64) / 32768.0
    else:
        triples = raw.reshape(-1, 3).astype(np.int32)
        packed = (triples[:, 0] << 16) | (triples[:, 1] << 8) | triples[:, 2]
        packed = np.where(packed >= 1 << 23, packed - (1 << 24), packed)
        values = packed.astype(np.float64) / float(1 << 23)
    if abs(rate - round(rate)) > 1.0e-6:
        raise ValueError(f"non-integer sample rate {rate}")
    return int(round(rate)), values.reshape(frames, channels), bits


def decode(data: bytes) -> tuple[int, np.ndarray, int]:
    try:
        return read_aiff(data)
    except ValueError:
        probe = subprocess.run(
            ["ffprobe", "-v", "error", "-show_entries",
             "stream=sample_rate,channels,bits_per_raw_sample,bits_per_sample",
             "-of", "json", "-i", "pipe:0"],
            input=data, capture_output=True, check=True)
        stream = json.loads(probe.stdout)["streams"][0]
        rate, channels = int(stream["sample_rate"]), int(stream["channels"])
        decoded = subprocess.run(
            ["ffmpeg", "-v", "error", "-i", "pipe:0", "-f", "f32le",
             "-acodec", "pcm_f32le", "pipe:1"],
            input=data, capture_output=True, check=True).stdout
        audio = np.frombuffer(decoded, dtype="<f4").astype(np.float64)
        bits = int(stream.get("bits_per_raw_sample") or
                   stream.get("bits_per_sample") or 0)
        return rate, audio.reshape(-1, channels), bits


def _db(energy: np.ndarray | float) -> np.ndarray | float:
    return 10.0 * np.log10(np.maximum(energy, 1.0e-20))


def frame_energy(signal: np.ndarray, rate: int, window: float,
                 hop: float) -> tuple[np.ndarray, int]:
    total = np.concatenate(([0.0], np.cumsum(signal * signal)))
    width, step = round(window * rate), round(hop * rate)
    starts = np.arange(0, signal.size - width + 1, step)
    return (total[starts + width] - total[starts]) / width, step


def _window_mean(values: np.ndarray, back: int, stop: int) -> np.ndarray:
    """Mean of values[i-back:i-stop] for every i (clamped at the start)."""
    total = np.concatenate(([0.0], np.cumsum(values)))
    index = np.arange(values.size)
    low = np.clip(index - back, 0, values.size)
    high = np.clip(index - stop, 1, values.size)
    high = np.maximum(high, low + 1)
    return (total[high] - total[low]) / (high - low)


def _forward_max(values: np.ndarray, length: int) -> np.ndarray:
    padded = np.concatenate((values, np.full(length - 1, values.min())))
    view = np.lib.stride_tricks.sliding_window_view(padded, length)
    return view.max(axis=1)


def detect_notes(signal: np.ndarray, rate: int) -> dict[str, Any]:
    """Return the plucked notes of one conditioned (high-passed) run.

    The onset of each accepted candidate is FitPhysicalModel._onset run from
    100 ms before it, on the same conditioned audio the target carries.
    """
    energy, step = frame_energy(signal, rate, 0.010, 0.001)
    level = _db(energy)
    slow, _ = frame_energy(signal, rate, 0.050, 0.010)
    noise_db = float(np.percentile(_db(slow), 2.0))
    # Before the first sample the file is digital silence, not unknown audio.
    before = _db(_window_mean(energy, 150, 10))
    before[:150] = np.minimum(before[:150], noise_db)
    after = _forward_max(level, 80)
    rise = after - before
    candidates = np.flatnonzero(
        (rise >= ONSET_RISE_DB) & (before <= noise_db + QUIET_ABOVE_NOISE_DB))
    events: list[list[int]] = []
    for frame in candidates:
        if events and frame - events[-1][-1] <= MIN_NOTE_SPACING * 1000:
            events[-1].append(int(frame))
        else:
            events.append([int(frame)])
    rough = []
    for group in events:
        # A weak precursor (finger contact, or the file's first sample) can
        # chain into the note's own candidates: anchor at the largest rise.
        anchor = group[int(np.argmax(rise[group]))]
        peak = float(level[anchor:anchor + 400].max())
        rough.append((anchor * step, peak))
    if not rough:
        return {"noise_db": noise_db, "notes": [], "rejected_candidates": []}
    median_peak = float(np.median([peak for _, peak in rough]))
    notes, rejected = [], []
    for sample, peak in rough:
        if peak < median_peak - NOTE_PEAK_SPREAD_DB:
            rejected.append({"seconds": sample / rate, "peak_db": peak,
                             "why": f"peak more than {NOTE_PEAK_SPREAD_DB} dB "
                                    "below the run's median note peak"})
            continue
        start = max(0, sample - round(0.100 * rate))
        stop = start + round(0.500 * rate)
        notes.append({
            "onset": start + features._onset(signal[start:stop], rate),
            "candidate_peak_db": peak})
    return {"noise_db": noise_db, "notes": notes,
            "rejected_candidates": rejected, "median_peak_db": median_peak}


class BandLevels:
    """Five-band short-time levels used for disturbance and gap detection."""

    def __init__(self, signal: np.ndarray, rate: int):
        size, self.hop = 1024, 220
        frequency, times, spectrum = stft(
            signal, fs=rate, window="hann", nperseg=size,
            noverlap=size - self.hop, nfft=size, boundary=None, padded=False)
        power = np.abs(spectrum) ** 2
        self.times = times  # frame centres
        self.rate = rate
        self.energy = np.stack([
            power[(frequency >= low) & (frequency < high)].sum(axis=0)
            for low, high in DISTURBANCE_BANDS])
        self.level = _db(self.energy)
        self.noise = np.percentile(self.level, 2.0, axis=1)
        back = round(0.200 * rate / self.hop)
        stop = round(0.020 * rate / self.hop)
        ahead = max(1, round(0.030 * rate / self.hop))
        self.rise = np.stack([
            _forward_max(self.level[band], ahead)
            - _db(_window_mean(self.energy[band], back, stop))
            for band in range(len(DISTURBANCE_BANDS))])
        self.after = np.stack([
            _forward_max(self.level[band], ahead)
            for band in range(len(DISTURBANCE_BANDS))])

    def first_event(self, begin: float, end: float) -> tuple[float, str, float]:
        """First disturbance time in [begin, end) seconds, its band and rise."""
        selected = np.flatnonzero((self.times >= begin) & (self.times < end))
        if selected.size == 0:
            return end, "", 0.0
        rise = self.rise[:, selected]
        audible = self.after[:, selected] >= (
            self.noise[:, None] + DISTURBANCE_ABOVE_NOISE_DB)
        hit = (rise >= DISTURBANCE_RISE_DB) & audible
        columns = np.flatnonzero(hit.any(axis=0))
        if columns.size == 0:
            return end, "", float(np.max(np.where(audible, rise, -99.0)))
        column = int(columns[0])
        band = int(np.argmax(np.where(hit[:, column], rise[:, column], -99.0)))
        # Frame centre minus half a window is where the new energy can start.
        seconds = float(self.times[selected[column]]) - 512 / self.rate
        low, high = DISTURBANCE_BANDS[band]
        return max(begin, seconds), f"{low:.0f}-{high:.0f} Hz", float(
            rise[band, column])


def first_gap(signal: np.ndarray, rate: int, noise_db: float,
              begin: int, end: int) -> int:
    """First sample in [begin, end) that starts a GAP_SECONDS near-noise run."""
    energy, step = frame_energy(signal[begin:end], rate, 0.020, 0.005)
    quiet = _db(energy) <= noise_db + GAP_ABOVE_NOISE_DB
    run = round(GAP_SECONDS / 0.005)
    if quiet.size < run:
        return end
    counts = np.convolve(quiet.astype(int), np.ones(run, dtype=int), "valid")
    hits = np.flatnonzero(counts == run)
    return begin + int(hits[0]) * step if hits.size else end


def yin(segment: np.ndarray, rate: int, low: float = 70.0,
        high: float = 1200.0, threshold: float = 0.12) -> float:
    longest = int(math.ceil(rate / low)) + 2
    shortest = max(2, int(rate / high))
    width = segment.size - longest
    if width < 4 * longest:
        return math.nan
    x = segment - np.mean(segment)
    size = 1 << int(math.ceil(math.log2(x.size + width)))
    spectrum = np.fft.rfft(x, size)
    head = np.fft.rfft(x[:width], size)
    correlation = np.fft.irfft(spectrum * np.conj(head), size)[:longest]
    squares = np.concatenate(([0.0], np.cumsum(x * x)))
    lags = np.arange(longest)
    difference = squares[width] + squares[lags + width] - squares[lags] \
        - 2.0 * correlation
    difference[0] = 0.0
    cumulative = np.cumsum(difference[1:])
    normalised = np.ones(longest)
    normalised[1:] = difference[1:] * lags[1:] / np.maximum(cumulative, 1e-30)
    lag = -1
    for tau in range(shortest, longest - 1):
        if normalised[tau] < threshold:
            while tau + 1 < longest - 1 and normalised[tau + 1] < normalised[tau]:
                tau += 1
            lag = tau
            break
    if lag < 0:
        lag = shortest + int(np.argmin(normalised[shortest:longest - 1]))
    a, b, c = normalised[lag - 1:lag + 2]
    denominator = a - 2.0 * b + c
    offset = 0.5 * (a - c) / denominator if abs(denominator) > 1e-12 else 0.0
    return rate / (lag + float(np.clip(offset, -0.5, 0.5)))


def settled_f0(signal: np.ndarray, rate: int, onset: int) -> dict[str, Any]:
    """Coarse YIN, octave-checked and refined on the scorer's own 400-1200 ms
    spectrum (FitPhysicalModel._spectrum/_peak).

    Late in a soft high note, residual chamber noise at 50-110 Hz can outweigh
    the note itself, so YIN runs on 80-400 ms (the note dominates there) with
    a further 75 Hz high-pass. The settled spectrum is only searched near
    multiples of the YIN estimate. Octave checks, on 80-400 ms: halve when
    the partials at 0.5, 1.5 and 2.5 times the estimate have a median within
    12 dB of the strongest of its first three (open-string sympathetic peaks
    sit 20-30 dB down); multiply by k when every partial that is not a
    multiple of k is 20 dB below the multiples.
    """
    begin, end = onset + round(0.080 * rate), onset + round(0.400 * rate)
    sos = butter(HIGH_PASS_ORDER, PITCH_HIGH_PASS_HZ, "highpass", fs=rate,
                 output="sos")
    first_sample = max(0, begin - rate // 2)
    segment = sosfilt(sos, signal[first_sample:end])[begin - first_sample:]
    coarse = yin(segment, rate)
    result: dict[str, Any] = {"yin_hz": coarse, "f0_hz": math.nan,
                              "method": "none", "octave_fix": None}
    if not math.isfinite(coarse):
        return result
    # Octave checks on the 80-400 ms spectrum, where the note dominates.
    frequency, power = features._spectrum(signal, rate, onset, 0.080, 0.400)

    def levels(fundamental: float, multiples) -> np.ndarray:
        return np.array([features._peak(frequency, power, k * fundamental,
                                        40.0)[1]
                         if k * fundamental < 0.46 * rate else math.nan
                         for k in multiples])

    harmonics = levels(coarse, range(1, 13))
    strongest = float(np.nanmax(harmonics[:3]))
    halves = levels(coarse, (0.5, 1.5, 2.5))
    if (coarse / 2.0 >= 75.0 and np.all(np.isfinite(halves))
            and np.median(halves) >= strongest - 12.0):
        coarse, result["octave_fix"] = coarse / 2.0, "halved"
    else:
        # YIN locks onto a common period of the note and an open string
        # ringing in sympathy (for example A4 with the A string's H2 at
        # 216 Hz): then only multiples of k are real partials.
        for factor in (6, 5, 4, 3, 2):
            multiples = harmonics[factor - 1::factor]
            others = np.delete(harmonics, np.arange(factor - 1, 12, factor))
            if (np.any(np.isfinite(multiples)) and np.any(np.isfinite(others))
                    and np.nanmax(multiples) - np.nanmax(others) >= 20.0):
                coarse, result["octave_fix"] = coarse * factor, f"x{factor}"
                break
    frequency, power = features._spectrum(signal, rate, onset, 0.400, 1.200)
    partials = [features._peak(frequency, power, h * coarse, 40.0)
                for h in range(1, 7)]
    finite = [level for _, level in partials if math.isfinite(level)]
    first, first_level = partials[0]
    if math.isfinite(first) and finite and first_level >= max(finite) - 35.0:
        result.update(f0_hz=first, method="settled H1 peak")
    else:
        estimates = [found / h for h, (found, _) in enumerate(partials[1:4], 2)
                     if math.isfinite(found)]
        if estimates:
            result.update(f0_hz=float(np.median(estimates)),
                          method="median of settled H2-H4 / h")
    result["partial_levels_db"] = [
        None if not math.isfinite(level) else round(level, 2)
        for _, level in partials]
    return result


def cents(frequency: float, reference: float) -> float:
    return 1200.0 * math.log2(frequency / reference)


def midi_hz(midi: float) -> float:
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


def write_target(path: Path, frames: np.ndarray, rate: int) -> None:
    """Write frames x channels as interleaved float32 LE with the end fade."""
    shaped = frames.astype(np.float64, copy=True).reshape(frames.shape[0], -1)
    count = min(round(FADE * rate), shaped.shape[0])
    ramp = 0.5 * (1.0 + np.cos(np.pi * np.arange(count) / max(count - 1, 1)))
    shaped[shaped.shape[0] - count:] *= ramp[:, None]
    shaped.astype("<f4").tofile(path)


# --------------------------------------------------------------------------
# Corpus


def parse_velocity_map(text: str) -> dict[str, int]:
    mapping = {}
    for item in text.split(","):
        key, _, value = item.partition("=")
        velocity = int(value)
        if key.strip() not in ("pp", "mf", "ff") or not 1 <= velocity <= 127:
            raise ValueError(f"bad velocity map entry {item!r}")
        mapping[key.strip()] = velocity
    if set(mapping) != {"pp", "mf", "ff"}:
        raise ValueError("velocity map needs pp, mf and ff")
    return mapping


def condition(frames: np.ndarray, rate: int, cutoff: float) -> np.ndarray:
    """Causal (minimum-phase) Butterworth high-pass of every channel.

    The Earthworks QTC40 is flat to a few hertz, so the runs carry chamber and
    handling rumble below 60 Hz that is up to 27 dB above a soft note in the
    scorer's level window and moves the scorer's onset by ~10 ms on the median
    note. A causal filter behaves like an ordinary microphone's low-frequency
    roll-off: it cannot ring before an attack, which a zero-phase filter does
    (it moved 52 of 350 onsets by more than 2 ms). Its price is group delay
    near the cutoff: 9.5 ms at 82 Hz, 4.6 ms at 110 Hz, 1.3 ms at 200 Hz.
    The filter starts from rest at the file's first sample.
    """
    if cutoff <= 0.0:
        return frames.astype(np.float64, copy=True)
    sos = butter(HIGH_PASS_ORDER, cutoff, "highpass", fs=rate, output="sos")
    return sosfilt(sos, frames.astype(np.float64), axis=0)


def reference_onset(signal: np.ndarray, rate: int, onset: int) -> int:
    """Scorer onset of the >200 Hz part of the note: the string's release."""
    start = max(0, onset - round(0.100 * rate))
    stop = start + round(0.500 * rate)
    sos = butter(4, REFERENCE_ONSET_HIGH_PASS_HZ, "highpass", fs=rate,
                 output="sos")
    segment = sosfilt(sos, signal[max(0, start - rate // 2):stop])
    segment = segment[start - max(0, start - rate // 2):]
    return start + features._onset(segment, rate)


def scorer_level(signal: np.ndarray, rate: int, onset: int) -> float:
    """The scorer's dynamics level: RMS over 20-500 ms after onset, in dB."""
    segment = signal[onset + round(0.020 * rate):onset + round(0.500 * rate)]
    return 20.0 * math.log10(max(math.sqrt(float(np.mean(segment ** 2))), 1e-12))


def band_ratio_db(signal: np.ndarray, rate: int, onset: int,
                  below: float) -> float:
    """Energy below `below` Hz relative to the note band, in the scorer's
    20-500 ms level window (Hann spectrum, as the scorer's _spectrum)."""
    frequency, power = features._spectrum(signal, rate, onset, 0.020, 0.500)
    low = float(np.sum(power[frequency < below]))
    note = float(np.sum(power[frequency >= NOTE_BAND_FROM_HZ]))
    return 10.0 * math.log10(max(low, 1e-30) / max(note, 1e-30))


def analyse_member(name: str, data: bytes, cutoff: float) -> dict[str, Any]:
    match = MEMBER.fullmatch(name)
    if match is None:
        raise ValueError(f"unexpected member {name}")
    dynamic, string_tag, first, last = match.groups()
    rate, raw_frames, bits = decode(data)
    channels = raw_frames.shape[1]
    raw = raw_frames.mean(axis=1)
    frames = condition(raw_frames, rate, cutoff)
    signal = frames.mean(axis=1)
    found = detect_notes(signal, rate)
    bands = BandLevels(signal, rate)
    onsets = [note["onset"] for note in found["notes"]]
    nominal = nominal_sequence(first, last)
    notes = []
    for index, onset in enumerate(onsets):
        next_onset = onsets[index + 1] if index + 1 < len(onsets) else signal.size
        horizon = next_onset  # clean_seconds is measured to the next note
        event_seconds, event_band, event_rise = bands.first_event(
            onset / rate + DISTURBANCE_START, horizon / rate)
        event = min(round(event_seconds * rate), horizon)
        gap = first_gap(signal, rate, found["noise_db"],
                        onset + round(DISTURBANCE_START * rate), event)
        reason = ("next note onset" if event >= next_onset
                  else "file end" if event >= signal.size
                  else f"disturbance {event_band} +{event_rise:.1f} dB"
                  if event_band else "next note onset")
        if gap < event:
            event, reason = gap, "edit gap / decayed to noise floor"
        clean = (event - onset) / rate
        begin = onset - round(PRE_ROLL * rate)
        stop = min(event - round(PRE_ROLL * rate),
                   onset + round(MAX_AFTER_ONSET * rate))
        pre = signal[max(0, onset - round(0.100 * rate)):onset]
        pre_rms = math.sqrt(float(np.mean(pre * pre))) if pre.size else 0.0
        body = frames[max(0, begin):max(begin + 1, stop)]
        peak = float(np.max(np.abs(body))) if body.size else 0.0
        pitch = settled_f0(signal, rate, onset) if clean >= 1.2 else {
            "f0_hz": math.nan, "method": "too short", "yin_hz": math.nan,
            "octave_fix": None}
        release = reference_onset(signal, rate, onset)
        notes.append({
            "order": index,
            "onset": onset,
            "onset_lead_seconds": (release - onset) / rate,
            "begin": begin,
            "stop": stop,
            "clean_seconds": clean,
            "clean_end_reason": reason,
            "peak_dbfs": 20.0 * math.log10(max(peak, 1e-12)),
            "pre_onset_seconds_available": pre.size / rate,
            "pre_rms_dbfs": 20.0 * math.log10(max(pre_rms, 1e-12)),
            "isolation_db": 20.0 * math.log10(max(peak, 1e-12))
                            - 20.0 * math.log10(max(pre_rms, 1e-12)),
            "raw_rumble_db": band_ratio_db(raw, rate, onset, RUMBLE_BELOW_HZ),
            "residual_lf_db": band_ratio_db(signal, rate, onset,
                                            RESIDUAL_LF_BELOW_HZ),
            **pitch,
        })
    return {
        "member": name, "dynamic": dynamic, "string_tag": string_tag,
        "string": STRINGS[string_tag], "rate": rate, "channels": channels,
        "bits": bits, "frames": int(frames.shape[0]), "nominal": nominal,
        "noise_dbfs": found["noise_db"], "notes": notes,
        "rejected_candidates": found["rejected_candidates"],
        "signal": frames,
    }


def circular_offset(deviations: list[float]) -> float:
    angles = np.exp(2j * np.pi * np.asarray(deviations) / 100.0)
    return float(np.angle(np.mean(angles)) * 100.0 / (2.0 * np.pi))


def _centroids(signal: np.ndarray, rate: int, onset: int, f0: float,
               begin: float, end: float) -> dict[str, float]:
    """Two noise-robust brightness measures of one window.

    spectral: power centroid over 60 Hz-10 kHz of the scorer's Hann spectrum,
    counting only bins within 60 dB of the window's maximum, so a soft note's
    noise floor cannot pull it up. harmonic: amplitude-weighted centroid of
    the partial peaks at h*f0 up to 10 kHz (the scorer's _peak, +-40 cents),
    as the Timbre Toolbox's harmonic spectral centroid.
    """
    frequency, power = features._spectrum(signal, rate, onset, begin, end)
    band = (frequency >= 60.0) & (frequency <= 10_000.0)
    weights = power[band]
    weights = np.where(weights >= weights.max() * 1e-6, weights, 0.0)
    spectral = float(np.sum(frequency[band] * weights) / np.sum(weights))
    partials = [h * f0 for h in range(1, 121) if h * f0 <= 10_000.0]
    levels = np.array([features._peak(frequency, power, value, 40.0)[1]
                       for value in partials])
    finite = np.isfinite(levels)
    amplitude = 10.0 ** (levels[finite] / 20.0)
    harmonic = (float(np.sum(np.asarray(partials)[finite] * amplitude)
                      / np.sum(amplitude)) if amplitude.size else math.nan)
    return {"spectral_hz": spectral, "harmonic_hz": harmonic}


def summarise_dynamics(rows: list[dict[str, Any]], output: Path) -> dict[str, Any]:
    """Recorded velocity-to-timbre law, on targets at the scorer's rate."""
    rate = 48_000
    windows = {"0_40ms": (0.000, 0.040), "80_900ms": (0.080, 0.900)}
    per_row = {}
    for row in rows:
        path = output / row["target"]["path"]
        audio = np.fromfile(path, dtype="<f4").astype(np.float64)
        if row["target"]["channels"] > 1:
            audio = audio.reshape(-1, row["target"]["channels"]).mean(axis=1)
        signal = features._resample(audio, row["target"]["sample_rate"], rate)
        onset = features._onset(signal, rate)
        entry = {"scorer_onset_seconds": onset / rate,
                 "level_db": scorer_level(signal, rate, onset)}
        for name, (begin, end) in windows.items():
            for kind, value in _centroids(signal, rate, onset,
                                          row["measured_f0_hz"], begin,
                                          end).items():
                entry[f"{kind}_centroid_{name}"] = value
        per_row[row["id"]] = entry
    groups: dict[str, dict[str, dict[str, Any]]] = {}
    for row in rows:
        groups.setdefault(row["dynamic_group"], {})[row["dynamic_marking"]] = row
    measures = [key for key in next(iter(per_row.values())) if "centroid" in key]
    table = []
    for group, members in sorted(groups.items(), key=lambda item: (
            next(iter(item[1].values()))["midi"], item[0])):
        if "pp" not in members:
            continue
        pp = per_row[members["pp"]["id"]]
        entry: dict[str, Any] = {"dynamic_group": group,
                                 "midi": members["pp"]["midi"],
                                 "markings": sorted(members)}
        for marking in ("mf", "ff"):
            if marking not in members:
                continue
            other = per_row[members[marking]["id"]]
            entry[f"{marking}_minus_pp_level_db"] = other["level_db"] - pp["level_db"]
            for measure in measures:
                if math.isfinite(other[measure]) and math.isfinite(pp[measure]):
                    entry[f"{marking}_over_pp_{measure}_cents"] = cents(
                        other[measure], pp[measure])
        table.append(entry)
    keys = sorted({key for entry in table for key in entry
                   if key.endswith("_db") or key.endswith("_cents")})
    registers = []
    for low, high, label in REGISTERS + ((0, 127, "all"),):
        chosen = [entry for entry in table if low <= entry["midi"] <= high]
        summary: dict[str, Any] = {"register": label, "midi": [low, high]}
        for key in keys:
            values = np.array([entry[key] for entry in chosen if key in entry])
            summary[key] = None if values.size == 0 else {
                "n": int(values.size), "median": float(np.median(values)),
                "p25": float(np.percentile(values, 25)),
                "p75": float(np.percentile(values, 75)),
                "fraction_positive": float(np.mean(values > 0.0))}
        registers.append(summary)
    return {"level_window": "RMS 20-500 ms after FitPhysicalModel._onset at 48 kHz "
                            "(the scorer's dynamics level)",
            "centroids": _centroids.__doc__.strip(),
            "pairing": "same string and MIDI note (dynamic_group), mf or ff "
                       "relative to pp; centroid changes are 1200*log2(ratio)",
            "per_row": per_row, "groups": table, "registers": registers}


def build(args: argparse.Namespace) -> dict[str, Any]:
    velocity_map = parse_velocity_map(args.velocity_map)
    downloads, output = Path(args.download_dir), Path(args.output)
    context = _ssl_context(args.cafile)
    pages = {}
    for url in (USAGE_PAGE, GUITAR_PAGE):
        name = url.rsplit("/", 1)[1]
        raw = fetch(url, downloads / "pages" / name, context)
        pages[url] = {"sha256": sha256(raw), "bytes": len(raw),
                      "text": page_text(raw)}
    usage_found = USAGE_SENTENCE in pages[USAGE_PAGE]["text"]
    if not usage_found and not args.allow_missing_usage:
        raise RuntimeError("usage statement not found on " + USAGE_PAGE)
    guitar_text = pages[GUITAR_PAGE]["text"]
    if ARCHIVE_NAME not in guitar_text:
        raise RuntimeError(f"{ARCHIVE_NAME} is no longer linked from the page")
    table = recording_table(guitar_text)
    archive = fetch(ARCHIVE_URL, downloads / ARCHIVE_NAME, context, ARCHIVE_BYTES)
    if sha256(archive) != ARCHIVE_SHA256:
        raise RuntimeError(f"{ARCHIVE_NAME} SHA-256 mismatch")

    output.mkdir(parents=True, exist_ok=True)
    targets = output / "targets"
    targets.mkdir(exist_ok=True)
    for stale in targets.glob("*.f32"):
        stale.unlink()

    members = []
    with zipfile.ZipFile(io.BytesIO(archive)) as bundle:
        names = sorted(name for name in bundle.namelist()
                       if name.startswith("1644mono/") and name.endswith(".aif"))
        if len(names) != 45:
            raise RuntimeError(f"expected 45 AIFF members, found {len(names)}")
        for name in names:
            data = bundle.read(name)
            analysed = analyse_member(name, data, args.high_pass_hz)
            analysed["sha256"] = sha256(data)
            analysed["bytes"] = len(data)
            members.append(analysed)
            print(f"{name}: {len(analysed['notes'])} onsets, "
                  f"{len(analysed['nominal'])} nominal", file=sys.stderr)

    deviations = []
    for member in members:
        for note in member["notes"]:
            if (note["clean_seconds"] >= MIN_CLEAN
                    and note["isolation_db"] >= MIN_ISOLATION_DB
                    and math.isfinite(note["f0_hz"])):
                semitones = 69.0 + cents(note["f0_hz"], 440.0) / 100.0
                deviations.append(100.0 * (semitones - round(semitones)))
    offset = circular_offset(deviations)

    rows, rejected, file_report = [], [], []
    for member in members:
        count_ok = len(member["notes"]) == len(member["nominal"])
        measured = [round(69.0 + (cents(note["f0_hz"], 440.0) - offset) / 100.0)
                    if math.isfinite(note["f0_hz"]) else None
                    for note in member["notes"]]
        file_report.append({
            "member": member["member"], "sha256": member["sha256"],
            "bytes": member["bytes"], "sample_rate": member["rate"],
            "channels": member["channels"], "bits": member["bits"],
            "seconds": member["frames"] / member["rate"],
            "noise_floor_dbfs_50ms": member["noise_dbfs"],
            "nominal_midi": member["nominal"],
            "onsets_found": len(member["notes"]),
            "count_matches_nominal": count_ok,
            "rejected_candidates": member["rejected_candidates"]})
        for note in member["notes"]:
            f0 = note["f0_hz"]
            midi = measured[note["order"]]
            nominal = (member["nominal"][note["order"]]
                       if count_ok else None)
            reasons = []
            if note["clean_seconds"] < MIN_CLEAN:
                reasons.append(f"clean_seconds {note['clean_seconds']:.3f} < {MIN_CLEAN}")
            if note["isolation_db"] < MIN_ISOLATION_DB:
                reasons.append(f"isolation {note['isolation_db']:.1f} dB < {MIN_ISOLATION_DB}")
            if midi is None:
                reasons.append("no settled f0")
            elif count_ok and midi != nominal:
                reasons.append(f"measured MIDI {midi} but the file-name "
                               f"chromatic sequence puts MIDI {nominal} here")
            elif not count_ok and (
                    midi not in member["nominal"]
                    or any(other in member["nominal"] and other >= midi
                           for other in measured[:note["order"]])
                    or any(other in member["nominal"] and other <= midi
                           for other in measured[note["order"] + 1:])):
                reasons.append(f"measured MIDI {midi} is outside the file-name "
                               "range or out of chromatic order")
            if note["onset_lead_seconds"] > MAX_ONSET_LEAD:
                reasons.append(
                    f"scorer onset is {1000 * note['onset_lead_seconds']:.1f} ms "
                    f"ahead of the >{REFERENCE_ONSET_HIGH_PASS_HZ:.0f} Hz release "
                    "(low-frequency rumble or handling before the attack)")
            if note["residual_lf_db"] > MAX_RESIDUAL_LF_DB:
                reasons.append(
                    f"residual energy below {RESIDUAL_LF_BELOW_HZ:.0f} Hz after "
                    f"the high-pass is {note['residual_lf_db']:.1f} dB re the "
                    f"note in the scorer's level window (> {MAX_RESIDUAL_LF_DB})")
            context_note = {
                "member": member["member"], "order": note["order"],
                "onset_seconds_in_source": note["onset"] / member["rate"],
                "clean_seconds": note["clean_seconds"],
                "clean_end_reason": note["clean_end_reason"],
                "isolation_db": note["isolation_db"],
                "measured_f0_hz": f0, "midi": midi, "nominal_midi": nominal}
            if reasons:
                rejected.append({**context_note, "reasons": reasons})
                continue
            string = member["string"]
            identifier = f"{CORPUS}-{string}-{midi}-{member['dynamic']}"
            path = f"targets/{identifier}.f32"
            if any(row["id"] == identifier for row in rows):
                rejected.append({**context_note,
                                 "reasons": [f"duplicate id {identifier}"]})
                continue
            pad = max(0, -note["begin"])
            frames = member["signal"][max(0, note["begin"]):note["stop"]]
            if pad:
                frames = np.concatenate(
                    (np.zeros((pad, member["channels"])), frames))
            write_target(output / path, frames, member["rate"])
            written = np.fromfile(output / path, dtype="<f4").astype(np.float64)
            written = written.reshape(-1, member["channels"]).mean(axis=1)
            scorer_onset = features._onset(
                features._resample(written, member["rate"], 48_000),
                48_000) / 48_000
            if abs(scorer_onset - PRE_ROLL) > SCORER_ONSET_TOLERANCE:
                (output / path).unlink()
                rejected.append({**context_note, "reasons": [
                    f"scorer onset on the written target is "
                    f"{1000 * scorer_onset:.2f} ms, not {1000 * PRE_ROLL:.0f} ms"]})
                continue
            pitch_note = (
                f"f0 {note['method']} (80-400 ms YIN {note['yin_hz']:.2f} Hz"
                + (f", octave {note['octave_fix']}" if note["octave_fix"] else "")
                + ")"
                + ("" if nominal is not None else
                         "; run's onset count differs from its file name, "
                         "pitch checked against the name's range and order"))
            padding = (f"; {1000 * pad / member['rate']:.1f} ms of the 20 ms "
                       "pre-roll is digital silence before the file's first "
                       "sample" if pad else "")
            rows.append({
                "id": identifier,
                "split": CORPUS,
                "material": "nylon",
                "picking": "finger",
                "midi": int(midi),
                "velocity": velocity_map[member["dynamic"]],
                "round_robin": 0,
                "dynamic_group": f"{args.group_prefix}{string}-{midi}",
                "dynamic_marking": member["dynamic"],
                "target": {"path": path, "sample_rate": member["rate"],
                           "channels": member["channels"]},
                "onset_seconds_in_source": round(note["onset"] / member["rate"], 6),
                "isolation_db": round(note["isolation_db"], 2),
                "clean_seconds": round(note["clean_seconds"], 4),
                "measured_f0_hz": round(f0, 4),
                "cents_from_midi": round(cents(f0, midi_hz(midi)), 2),
                "peak_dbfs": round(note["peak_dbfs"], 2),
                "notes": (
                    f"{member['member']} note {note['order'] + 1}/"
                    f"{len(member['notes'])}, string {string} "
                    f"({member['string_tag']}); picking not stated by source "
                    f"(recorded as finger); clean region ends at "
                    f"{note['clean_end_reason']}; {pitch_note}; global tuning "
                    f"offset {offset:+.2f} cents removed for MIDI assignment; "
                    f"causal Butterworth high-pass {HIGH_PASS_ORDER}th order "
                    f"{args.high_pass_hz:g} Hz applied "
                    f"(raw energy below {RUMBLE_BELOW_HZ:.0f} Hz was "
                    f"{note['raw_rumble_db']:+.1f} dB re the note in 20-500 ms)"
                    f"; isolation from "
                    f"{1000 * note['pre_onset_seconds_available']:.0f} ms of "
                    f"pre-onset audio{padding}"),
            })

    rows.sort(key=lambda row: (row["midi"], row["dynamic_group"],
                               row["velocity"]))
    corpus = {"corpus": CORPUS, "license": LICENSE, "source": GUITAR_PAGE,
              "rows": rows}
    (output / "rows.json").write_text(json.dumps(corpus, indent=1) + "\n")
    law = summarise_dynamics(rows, output)
    (output / "dynamics_law.json").write_text(json.dumps(law, indent=1) + "\n")
    report = {
        "global_tuning_offset_cents": offset,
        "velocity_map": velocity_map,
        "rows": len(rows),
        "rows_by_marking": {marking: sum(row["dynamic_marking"] == marking
                                         for row in rows)
                            for marking in ("pp", "mf", "ff")},
        "rejected": rejected,
        "files": file_report,
        "pitch_rejections": sum(any("MIDI" in reason for reason in item["reasons"])
                                for item in rejected),
        "rejections_by_reason": {
            name: sum(any(key in reason for reason in item["reasons"])
                      for item in rejected)
            for name, key in (("clean_seconds", "clean_seconds"),
                              ("isolation", "isolation"),
                              ("no_f0", "no settled f0"),
                              ("pitch_vs_file_name", "MIDI"),
                              ("onset_lead", "ahead of"),
                              ("residual_lf", "residual energy"),
                              ("scorer_onset", "scorer onset on the written"))},
        "scorer_onset_ms": {
            "min": 1000 * min(item["scorer_onset_seconds"]
                              for item in law["per_row"].values()),
            "max": 1000 * max(item["scorer_onset_seconds"]
                              for item in law["per_row"].values())},
        "parameters": {name: value for name, value in globals().items()
                       if name.isupper() and isinstance(value, (int, float))},
    }
    (output / "preparation_report.json").write_text(
        json.dumps(report, indent=1) + "\n")
    write_source(output / "SOURCE.md", pages, usage_found, table,
                 file_report, offset, velocity_map, rows, rejected, args)
    return report


def write_source(path: Path, pages: dict[str, Any], usage_found: bool,
                 table: dict[str, str], files: list[dict[str, Any]],
                 offset: float, velocity_map: dict[str, int],
                 rows: list[dict[str, Any]], rejected: list[dict[str, Any]],
                 args: argparse.Namespace) -> None:
    fetched = time.strftime("%Y-%m-%d", time.gmtime())
    lines = [
        f"# {CORPUS}", "",
        "University of Iowa Electronic Music Studios, Musical Instrument "
        "Samples (MIS), guitar set (Lawrence Fritts).", "",
        "## Source", "",
        f"- Instrument page: {GUITAR_PAGE}",
        f"- Usage page: {USAGE_PAGE}",
        f"- Archive used: {ARCHIVE_URL}",
        f"  - {ARCHIVE_BYTES} bytes, SHA-256 `{ARCHIVE_SHA256}`",
        f"- Pages fetched {fetched} (UTC); page SHA-256 at fetch time:",
    ]
    for url, page in pages.items():
        lines.append(f"  - {url}: `{page['sha256']}` ({page['bytes']} bytes)")
    lines += [
        "", "## License / usage statement (verbatim)", "",
        f"From {USAGE_PAGE} (found on the page at fetch time: "
        f"{'yes' if usage_found else 'NO'}):", "",
        f"> {USAGE_SENTENCE}", "",
        "No separate terms-of-use page exists: none is linked from "
        "index.html, MIS.html or MISguitar.html, and MIS.html's statement is "
        "the site's only usage text. It is an explicit no-restriction "
        "statement, not a CC licence. The page also asks users to let the "
        "director know when the recordings helped a project.", "",
        "## Recording (guitar page table, verbatim fields)", "",
    ]
    for key, value in table.items():
        lines.append(f"- {key}: {value}")
    lines += [
        "- Plucking technique: not stated by the source (rows say picking "
        "\"finger\"; finger, thumb or nail is unknown).",
        "- MIS.html: since 2011, three Earthworks QTC-40 mics in a Decca tree "
        "(left, centre, right, 12 in apart, 5 ft from the performer), "
        "recorded at 24/96; the centre mic was downsampled to the 16/44.1 mono "
        "chromatic scales used here. Wendell Johnson Speech and Hearing Center "
        "anechoic chamber.", "",
        "## Format choice", "",
        f"{ARCHIVE_NAME} holds the 16-bit 44.1 kHz mono chromatic runs, the "
        "smallest faithful format offered. The separately linked "
        "`Guitar.*.mono.aif` files are 24-bit 48 kHz (about 1 GB in total), and "
        "the stereo and 24/96 zips are larger still. Members are read from the "
        "zip in memory.", "",
        "## Processing", "",
        "Tool: `Tools/PrepareIowaGuitarCorpus.py`. Its docstring holds the "
        "protocol.", "",
        f"- Only processing: a causal {HIGH_PASS_ORDER}th-order Butterworth "
        f"high-pass at {args.high_pass_hz:g} Hz, which removes chamber and "
        "handling rumble (1-60 Hz, up to -4 dBFS, up to 27 dB above a soft "
        "note). Group delay is 9.5 ms at 82 Hz, 4.6 ms at 110 Hz and 1.3 ms "
        "at 200 Hz. No other EQ, denoising or normalisation is applied.",
        f"- Global tuning offset: {offset:+.2f} cents re A440, removed only "
        "for MIDI assignment. cents_from_midi is re A440 equal temperament.",
        f"- Velocity map: {velocity_map}. The scorer's dynamics term on this "
        "corpus depends on it.",
        f"- dynamic_group prefix: {args.group_prefix!r}.",
        f"- Rows written: {len(rows)}. Notes rejected: {len(rejected)}. "
        "preparation_report.json lists each rejection with its reason.",
        "- dynamics_law.json holds the recorded pp/mf/ff level and centroid "
        "law.", "",
        "## Member files (SHA-256)", "",
        "| member | bytes | rate | bits | ch | seconds | onsets/nominal |",
        "|---|---|---|---|---|---|---|",
    ]
    for item in files:
        lines.append(
            f"| {item['member'].split('/')[-1]} `{item['sha256']}` | "
            f"{item['bytes']} | {item['sample_rate']} | {item['bits']} | "
            f"{item['channels']} | {item['seconds']:.2f} | "
            f"{item['onsets_found']}/{len(item['nominal_midi'])} |")
    lines += ["", "Audio stays out of the repository. Targets are float32 LE at "
              "the native 44.1 kHz, mono.", ""]
    path.write_text("\n".join(lines))


# --------------------------------------------------------------------------
# Self-test


def self_test() -> None:
    rate = 44_100
    rng = np.random.default_rng(3)
    signal = rng.normal(0.0, 10 ** (-89 / 20), round(15.0 * rate))
    starts = [0.5, 5.5, 10.2]
    for index, start in enumerate(starts):
        frequency = 110.0 * 2 ** (index / 12)
        t = np.arange(round(4.0 * rate)) / rate
        note = sum(np.sin(2 * np.pi * h * frequency * t) / h *
                   np.exp(-t * (0.8 + 0.4 * h)) for h in range(1, 6))
        first = round(start * rate)
        signal[first:first + note.size] += 0.3 * note
    burst = round(7.8 * rate)
    signal[burst:burst + 400] += 0.05 * rng.normal(size=400)
    found = detect_notes(condition(signal[:, None], rate, HIGH_PASS_HZ)[:, 0], rate)
    onsets = [note["onset"] for note in found["notes"]]
    assert len(onsets) == 3, found
    for onset, start in zip(onsets, starts):
        assert abs(onset / rate - start) < 0.003, (onset / rate, start)
    bands = BandLevels(signal, rate)
    event, band, _ = bands.first_event(5.5 + DISTURBANCE_START, 10.2)
    # The 30 ms look-ahead and half an STFT window make events early, never
    # late: a clean region ends before the disturbance, not inside it.
    assert -0.060 < event - 7.8 < 0.005 and band, (event, band)
    event, band, _ = bands.first_event(0.5 + DISTURBANCE_START, 5.5)
    assert -0.060 < event - 5.5 < 0.005, (event, band)  # the next attack
    pitch = settled_f0(signal, rate, onsets[0])
    assert abs(cents(pitch["f0_hz"], 110.0)) < 1.0, pitch
    assert parse_velocity_map(DEFAULT_VELOCITY_MAP) == {"pp": 40, "mf": 80, "ff": 112}
    assert nominal_sequence("C5", "Gb5") == [72, 73, 74, 75, 76, 77, 78]
    assert abs(abs(circular_offset([49.0, -49.0, 48.0])) - 49.33) < 0.5  # wraps
    print("self-test passed")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--download-dir", help="cache for the zip and pages")
    parser.add_argument("--output", help="corpus directory (rows.json, targets/)")
    parser.add_argument("--velocity-map", default=DEFAULT_VELOCITY_MAP,
                        help="marking=velocity list; the scorer's dynamics "
                             "term on this corpus depends on it "
                             f"(default {DEFAULT_VELOCITY_MAP})")
    parser.add_argument("--group-prefix", default="",
                        help="prefix for dynamic_group, to keep groups unique "
                             "when merged with other corpora")
    parser.add_argument("--high-pass-hz", type=float, default=HIGH_PASS_HZ,
                        help="causal 8th-order Butterworth high-pass applied "
                             "to the targets to remove chamber rumble; 0 "
                             f"disables (default {HIGH_PASS_HZ:g})")
    parser.add_argument("--cafile", help="CA bundle for HTTPS")
    parser.add_argument("--allow-missing-usage", action="store_true",
                        help="continue if the usage sentence changed")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.download_dir or not args.output:
        parser.error("--download-dir and --output are required")
    report = build(args)
    print(json.dumps({key: report[key] for key in (
        "global_tuning_offset_cents", "rows", "rows_by_marking",
        "rejections_by_reason", "scorer_onset_ms")}, indent=1))
    print(f"rejected notes: {len(report['rejected'])}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
