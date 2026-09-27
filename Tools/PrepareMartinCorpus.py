#!/usr/bin/env python3
"""Cut Jeff Learman's CC0 Martin HD28 samples into an isolated-note corpus.

Protocol (deterministic; no audio enters the repository):

1. Source. Program 026 "Acoustic Guitar (steel)" of the Discord SFZ GM Bank,
   https://github.com/sfzinstruments/Discord-SFZ-GM-Bank, pinned to commit
   COMMIT below. Only ``Discord GM/Melodic/026-Acoustic Guitar (steel).sfz``,
   the repository README (license policy) and the 15 WAV files the SFZ
   references are fetched, from raw.githubusercontent.com at that commit.
   Every file must match both its git blob SHA-1 in that commit's tree and
   the SHA-256 pinned here, or the run aborts before any analysis. The SFZ
   header must still read ``// License: Creative Commons CC0``.
2. Mapping. The SFZ is parsed (file-level, <global>, <master>, <group> and
   <region> opcodes inherited in that order). Each region contributes one
   file, one note, with its pitch_keycenter, lokey/hikey and every amp/pitch
   EG, tune, transpose, offset and volume opcode recorded. There is one
   velocity layer and one round robin: the bank simulates dynamics with a
   velocity-tracked one-pole low-pass that is fully open at velocity 127.
3. Raw audio only. The WAVs are 44.1 kHz 16-bit mono PCM with an ``smpl``
   chunk (unity note = pitch_keycenter) holding one forward sustain loop that
   ends exactly 100 samples before the end of the file. The SFZ sets
   ``ampeg_hold`` to the loop start time and ``ampeg_decay`` 8-22 s with no
   ``loop_mode``, so a player sustains the loop (SFZ default
   loop_continuous) and fabricates the rest of the decay at playback. The
   files themselves end abruptly 31-43 dB below the attack peak with no fade,
   and the loop regions show the lag-L self-similarity of a loop edit. The
   natural recording is therefore taken to end at the loop start
   (``--end-at loop-start``, the default); ``--end-at file-end`` keeps the
   loop region instead. The ampeg envelope is never applied.
4. Onset. Tools/FitPhysicalModel.py's own ``_onset`` rule (1 ms energy
   follower, first sample at -26 dB of the 250 ms window peak), iterated on
   the exact target window, so the scorer measures a 20 ms latency on every
   target. The files are trimmed inside the attack (in 11 of 15 the first
   0.5 ms is already within 9-25 dB of the peak; at most 94 samples precede the rule's
   onset, and for six files the rule lands up to 20 samples before frame 0,
   reported as a negative ``onset_seconds_in_source``). Each target's 20 ms
   pre-roll is therefore completed with leading zeros, counted in ``notes``.
5. Isolation. There is never 100 ms of audio before the onset, so the shared
   rule cannot be applied as written. isolation_db is instead the sample peak
   of the first 300 ms after the onset minus a broadband background level:
   the mean power density over 12-16 kHz in the last 300 ms of the natural
   recording, extended as white noise to the full band. Anything the decayed
   note still contributes there only raises that level, so the value is a
   lower bound on isolation. In these files it is the 16-bit dither floor.
6. Pitch. A Hann-windowed 0.40-1.20 s (the scorer's settled-tuning window,
   truncated at the natural end; 0.08 s onwards when that leaves < 0.25 s)
   mono spectrum, zero-padded to >= 2^18 points. Up to eight partials are
   found by log-parabolic peak interpolation within +-40 cents of their
   stretched positions and must stand 20 dB above the local median level.
   f0 and B are fitted to f_h = h f0 sqrt(1 + B h^2), weighted by partial
   power.
7. Tuning. The global offset is the median, over all regions, of the cents
   between the fitted f0 and the nearest equal-tempered note (A = 440 Hz).
   ``midi`` is the nearest note after removing that offset (it must equal
   pitch_keycenter); ``cents_from_midi`` is the raw deviation from 440 Hz
   equal temperament, so it still contains the offset.
8. Selection. clean_seconds = min(natural end, onset + 4.2 s) - onset. A row
   needs clean_seconds >= 1.25 s and isolation_db >= 30 dB.
9. Targets. float32 little-endian mono at the native 44.1 kHz from 20 ms
   before the onset to onset + clean_seconds, a 60 ms terminal half-cosine
   fade, no gain change. Split ``martin-hd28``, material steel, velocity 91
   (one layer), round_robin 0, dynamic_group/dynamic_marking null. picking
   is null: neither the SFZ, the README nor the project wiki says whether
   the notes were picked or finger-plucked.

Outputs in --out: rows.json (shared corpus row schema), targets/<id>.f32,
regions.json (every region, accepted or not, with its measurements) and
SOURCE.md (provenance, license evidence, hashes, region table).

    python3 Tools/PrepareMartinCorpus.py \
        --downloads "$S/downloads/martin-hd28" --out "$S/corpora/martin-hd28"

Add ``--offline`` to refuse network access and use only verified local files.
Downloads use urllib, falling back to ``curl`` when Python has no CA bundle.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import struct
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import warnings
from pathlib import Path
from typing import Any

import numpy as np
from scipy.io import wavfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import FitPhysicalModel as scorer  # noqa: E402

CORPUS = "martin-hd28"
REPOSITORY = "sfzinstruments/Discord-SFZ-GM-Bank"
COMMIT = "7a9c478fe331f94f246d33332f0adedb25bbbe27"
RAW = f"https://raw.githubusercontent.com/{REPOSITORY}/{COMMIT}/"
TREE = f"https://github.com/{REPOSITORY}/tree/{COMMIT}/"
PROGRAM = "Discord GM/Melodic/026-Acoustic Guitar (steel).sfz"
SAMPLE_DIR = "Discord GM/Melodic/026-Acoustic Guitar (steel)"
SOURCE_URL = TREE + urllib.parse.quote(SAMPLE_DIR)
LICENSE = "CC0-1.0"
LICENSE_URL = "https://creativecommons.org/publicdomain/zero/1.0/"
LICENSE_LINE = "// License: Creative Commons CC0"

# repository path -> (local name, git blob SHA-1 at COMMIT, SHA-256)
FILES = {
    PROGRAM: ("026-Acoustic Guitar (steel).sfz",
              "85dab9fd1fb79326963226121f31e974105a3cb8",
              "aa1f7d8b2d52a35295cf38bfad22fedb76a85150c1b3664cc5df0118b410e42f"),
    "README.md": ("README.md",
                  "7dea7338cbf5c7b227636ade4b6b818284e4c118",
                  "e5408403ebc7fe23514fcc45d6865851885db760bba5d24fd1c48fd5a7bd2463"),
}
WAVS = {
    "MartinGM2_040__E2_1.wav": ("252091bc8e44bb0972f5e68ee2b2c12173507c3f",
        "3d80eb25aa8cbe8d1d2b5971d42a63df279c5429437df6267422ceef1c75b50f"),
    "MartinGM2_043__G2_1.wav": ("d48b82a4c7ddbeb4455c4a1a4fdfe23fb3a2c578",
        "574f9b934a6965cfbb9eee1db6918a7bc47ea8577c321ac98ff20872e8da42ed"),
    "MartinGM2_046_Bb2_1.wav": ("3364ed1643283b75d46c92e036b3dbdaf209f9dd",
        "aadf19ca38c73473373a22947f49317dbee0c2b58e040e0115801fe57b76d3e6"),
    "MartinGM2_049_Db3_1.wav": ("9c25a8afb8dc1600dfba6e4a5b0038eb849d3339",
        "bf57a94a18e63c6c7f90c84c6bf08e998a1ea7e0b8bf1a6a09d31bed77e4a2c7"),
    "MartinGM2_052__E3_1.wav": ("a9b10b4ea08881140672df5fb3de7de48539e97b",
        "e2ee2b611260e25bd6dacf00dbe5b3b8c7e5f69cc2cc252461af98025dd284fe"),
    "MartinGM2_055__G3_1.wav": ("58cf4e5b5ffa5356d2f2a49bc5a0845afc81b236",
        "694679709136628e7b807a8059a10e4264af8123bf5fc819e9761c4e0e563f29"),
    "MartinGM2_058_Bb3_1.wav": ("c523c5768784894783afd6a583ca09066626c915",
        "cdacb0918cbe4c2e8da3324f559d94b2be11c3614be6c5da0b7654373e842080"),
    "MartinGM2_061_Db4_1.wav": ("47d2d7e25205200da0058cbd440fb59e29cb2c86",
        "81c1ea3bdd795d7e91b369f47001757ab3e6f82da0aeb4486da9032451cae958"),
    "MartinGM2_064__E4_1.wav": ("5284d358aad34708f8282e12d3f4c705887fa7c8",
        "1ce5ce765892d3f557cd3c47c7fbba9ac866765d10fe342cb03e00b8be0ee412"),
    "MartinGM2_068_Ab4_1.wav": ("6b17f3ee2454c200f02be0e7ab280197f2d029e0",
        "85595b30c7d2fcca30186a3539ca401c4e551e4ca6506c72c26c291a8d333701"),
    "MartinGM2_071__B4_1.wav": ("6a17cf650e538430acdbc0983451604cd6306605",
        "5278514d5a99ba1871c2a5035335219740d759cc6553a06eb54d82742ae052f3"),
    "MartinGM2_074__D5_1.wav": ("a2ff038e718286d9203652e075a205efedff51b7",
        "272f34deb22bc1ddc17f01c65b34016a6bc519bacf912aa57aecad827ff2f0da"),
    "MartinGM2_077__F5_1.wav": ("177404840211633847cbb8d6260f57ae8d0221b3",
        "981d8956ffde9ec5b045a3ddf563d3802957e6845079540cf4958797060c415a"),
    "MartinGM2_080_Ab5_1.wav": ("eea778214cfbfcda5af83ec96716e02904f8bd4f",
        "1dfca9cf6adb0f6e4a37ac7ab3818c62c5bab560e6db13373255db7cef23de2e"),
    "MartinGM2_083__B5_1.wav": ("eece19a83c69222a5c9e44d30b9329f8a7439801",
        "da4133e56cc7e73f7284f91fd098a0120190deac4c9dbdd59c47f4a08c4a70c5"),
}
for _name, (_blob, _digest) in WAVS.items():
    FILES[f"{SAMPLE_DIR}/{_name}"] = (f"wav/{_name}", _blob, _digest)

PRE_ROLL = 0.020
MAX_AFTER = 4.2
FADE = 0.060
MIN_CLEAN = 1.25
MIN_ISOLATION = 30.0
VELOCITY = 91
NOTE_NAMES = ("C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B")
RECORDED_OPCODES = re.compile(
    r"^(pitch_keycenter|lokey|hikey|key|lovel|hivel|tune|transpose|offset|end|"
    r"volume|amplitude|pan|loop_\w+|ampeg_\w+|pitcheg_\w+|fil\w*|cutoff\w*|"
    r"resonance\w*|amp_\w+|sw_\w+|seq_\w+|trigger)$"
)


# --------------------------------------------------------------------------
# Download and verification


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def git_blob(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


def fetch(downloads: Path, offline: bool) -> dict[str, dict[str, str]]:
    verified: dict[str, dict[str, str]] = {}
    for repo_path, (local, blob, digest) in FILES.items():
        path = downloads / local
        if not path.exists():
            if offline:
                raise SystemExit(f"missing {path} (--offline)")
            url = RAW + urllib.parse.quote(repo_path)
            print(f"download {url}", file=sys.stderr)
            try:
                with urllib.request.urlopen(url, timeout=60) as response:
                    payload = response.read()
            except urllib.error.URLError:
                # python.org builds often lack a CA bundle; the hashes below,
                # not the transport, are what authenticate the bytes.
                payload = subprocess.run(["curl", "-fsSL", "--max-time", "120", url],
                                         check=True, capture_output=True).stdout
            path.parent.mkdir(parents=True, exist_ok=True)
            partial = path.with_name(path.name + ".part")
            partial.write_bytes(payload)
            partial.replace(path)
        data = path.read_bytes()
        if git_blob(data) != blob or sha256(data) != digest:
            raise SystemExit(f"{path}: bytes differ from {repo_path} at {COMMIT}")
        verified[repo_path] = {"local": str(path), "git_blob_sha1": blob,
                               "sha256": digest, "bytes": str(len(data))}
    return verified


# --------------------------------------------------------------------------
# SFZ and WAV parsing


def parse_sfz(text: str) -> tuple[list[str], dict[str, str], list[dict[str, Any]]]:
    """Return header comments, file-level opcodes and regions with inherited opcodes."""
    comments = [line.strip() for line in text.splitlines() if line.strip().startswith("//")]
    body = "\n".join(line.split("//", 1)[0] for line in text.splitlines())
    tokens = re.split(r"<(\w+)>", body)
    opcode = re.compile(r"(\w+)=")

    def opcodes(chunk: str) -> dict[str, str]:
        found = list(opcode.finditer(chunk))
        result = {}
        for index, match in enumerate(found):
            stop = found[index + 1].start() if index + 1 < len(found) else len(chunk)
            result[match.group(1)] = chunk[match.end():stop].strip()
        return result

    file_level = opcodes(tokens[0])
    scopes = {"global": {}, "master": {}, "group": {}}
    regions: list[dict[str, Any]] = []
    for header, chunk in zip(tokens[1::2], tokens[2::2]):
        values = opcodes(chunk)
        if header == "global":
            scopes = {"global": values, "master": {}, "group": {}}
        elif header == "master":
            scopes["master"], scopes["group"] = values, {}
        elif header == "group":
            scopes["group"] = values
        elif header == "region":
            merged = {**file_level, **scopes["global"], **scopes["master"],
                      **scopes["group"], **values}
            regions.append({"opcodes": merged, "region_opcodes": values,
                            "group_opcodes": dict(scopes["group"])})
    return comments, file_level, regions


def read_wav(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    chunks: list[tuple[str, int]] = []
    fmt = smpl = None
    position = 12
    while position + 8 <= len(data):
        name = data[position:position + 4].decode("latin-1")
        size = struct.unpack("<I", data[position + 4:position + 8])[0]
        payload = data[position + 8:position + 8 + size]
        chunks.append((name, size))
        if name == "fmt ":
            fmt = struct.unpack("<HHIIHH", payload[:16])
        elif name == "smpl":
            header = struct.unpack("<9I", payload[:36])
            loops = [struct.unpack("<6I", payload[36 + 24 * i:60 + 24 * i])
                     for i in range(header[7])]
            smpl = {"unity_note": header[3], "pitch_fraction": header[4],
                    "loops": [{"type": loop[1], "start": loop[2], "end": loop[3],
                               "play_count": loop[5]} for loop in loops]}
        position += 8 + size + (size & 1)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", wavfile.WavFileWarning)
        rate, samples = wavfile.read(path)
    if fmt is None or fmt[0] != 1 or fmt[5] != 16 or samples.ndim != 1:
        raise SystemExit(f"{path}: expected 16-bit mono PCM")
    return {"rate": int(rate), "audio": samples.astype(np.float64) / 32768.0,
            "chunks": chunks, "smpl": smpl, "bits": fmt[5], "channels": fmt[1]}


# --------------------------------------------------------------------------
# Analysis


def db(value: float) -> float:
    return 20.0 * math.log10(max(value, 1.0e-12))


def note_name(midi: int) -> str:
    return f"{NOTE_NAMES[midi % 12]}{midi // 12 - 1}"


def midi_hz(midi: float) -> float:
    return 440.0 * 2.0 ** ((midi - 69.0) / 12.0)


def target_window(audio: np.ndarray, onset: int, end: int, rate: int) -> np.ndarray:
    pre = round(PRE_ROLL * rate)
    start = onset - pre
    window = audio[max(0, start):end]
    if start < 0:
        window = np.concatenate([np.zeros(-start), window])
    return window


def refine_onset(audio: np.ndarray, rate: int, end: int) -> int:
    """Make the scorer's own onset rule land exactly PRE_ROLL into the target."""
    pre = round(PRE_ROLL * rate)
    onset = scorer._onset(audio, rate)
    for _ in range(8):
        detected = scorer._onset(target_window(audio, onset, end, rate), rate)
        if detected == pre:
            break
        onset += detected - pre
    return onset


def settled_pitch(audio: np.ndarray, rate: int, onset: int, end: int,
                  nominal: float) -> dict[str, Any]:
    begin, stop = onset + round(0.40 * rate), min(end, onset + round(1.20 * rate))
    if stop - begin < round(0.25 * rate):
        begin = onset + round(0.08 * rate)
    segment = audio[begin:stop] - np.mean(audio[begin:stop])
    segment = segment * np.hanning(segment.size)
    size = max(1 << 18, 1 << int(math.ceil(math.log2(segment.size * 8))))
    power = np.abs(np.fft.rfft(segment, size)) ** 2
    level = 10.0 * np.log10(np.maximum(power, 1.0e-30))
    hz = rate / size

    def peak(expected: float, cents: float) -> tuple[float, float] | None:
        low = int(expected * 2 ** (-cents / 1200.0) / hz)
        high = int(math.ceil(expected * 2 ** (cents / 1200.0) / hz))
        if high + 2 >= level.size or low < 2:
            return None
        index = low + int(np.argmax(level[low:high + 1]))
        if index in (low, high):
            return None
        a, b, c = level[index - 1], level[index], level[index + 1]
        offset = 0.5 * (a - c) / (a - 2.0 * b + c)
        local = level[max(0, index - round(0.5 * expected / hz)):
                      index + round(0.5 * expected / hz)]
        excess = b - float(np.median(local))
        return (index + offset) * hz, excess

    first = peak(nominal, 80.0)
    if first is None or first[1] < 20.0:
        raise SystemExit("no settled fundamental near the region's keycenter")
    f0, inharmonicity = first[0], 0.0
    partials = {1: first}
    for harmonic in range(2, 9):
        stretched = harmonic * f0 * math.sqrt(1.0 + inharmonicity * harmonic ** 2)
        if stretched > 0.45 * rate:
            break
        found = peak(stretched, 40.0)
        if found is None or found[1] < 20.0:
            continue
        partials[harmonic] = found
        if len(partials) >= 3:  # refit so higher partials follow the stretch
            h = np.array(sorted(partials), dtype=float)
            f = np.array([partials[int(k)][0] for k in h])
            w = np.array([10.0 ** (partials[int(k)][1] / 10.0) for k in h])
            slope, intercept = np.polyfit(h * h, (f / h) ** 2, 1, w=np.sqrt(w))
            f0, inharmonicity = math.sqrt(intercept), max(0.0, slope / intercept)
    return {"f0_hz": f0, "inharmonicity_b": inharmonicity,
            "first_partial_hz": first[0], "partials_used": sorted(partials),
            "window_seconds": [(begin - onset) / rate, (stop - onset) / rate]}


def isolation(audio: np.ndarray, rate: int, onset: int, natural_end: int) -> dict[str, float]:
    first = max(0, onset)  # the rule may place the onset just before frame 0
    peak = float(np.max(np.abs(audio[first:onset + round(0.300 * rate)])))
    tail = audio[max(first, natural_end - round(0.300 * rate)):natural_end]
    tail = (tail - np.mean(tail)) * np.hanning(tail.size)
    spectrum = np.abs(np.fft.rfft(tail)) ** 2 / (rate * float(np.sum(np.hanning(tail.size) ** 2)))
    frequency = np.fft.rfftfreq(tail.size, 1.0 / rate)
    band = (frequency >= 12_000.0) & (frequency < 16_000.0)
    background = 10.0 * math.log10(2.0 * float(np.mean(spectrum[band])) * rate / 2.0)
    pre = audio[max(0, onset - round(0.100 * rate)):first]
    return {"peak_300ms_dbfs": db(peak), "background_dbfs": background,
            "isolation_db": db(peak) - background, "pre_onset_samples": int(pre.size)}


def loop_join(audio: np.ndarray, rate: int, start: int, end: int) -> dict[str, float]:
    """Level of x[n] - x[n - L] relative to x[n] (4 ms windows), L = loop length.

    A loop edit makes the audio just before the loop end repeat the audio just
    before the loop start; natural decay does not. The control is the same
    measure 300 ms before the loop start.
    """
    length, width = end - start, round(0.004 * rate)

    def residual(stop: int) -> float:
        a = audio[stop - width:stop]
        b = audio[stop - width - length:stop - length]
        return 10.0 * math.log10(float(np.sum((a - b) ** 2)) / max(float(np.sum(a * a)), 1e-30))

    near = [residual(end + 1 - round(k * rate)) for k in (0.0, 0.005, 0.010)]
    control = start - round(0.300 * rate)
    return {"join_residual_db": float(np.mean(near)),
            "control_residual_db": residual(control) if control - width - length >= 0 else math.nan}


def decay_profile(audio: np.ndarray, rate: int, onset: int, natural_end: int) -> dict[str, float]:
    block = round(0.050 * rate)
    onset = max(0, onset)
    count = (natural_end - onset) // block
    frames = audio[onset:onset + count * block].reshape(count, block)
    levels = 10.0 * np.log10(np.maximum(np.mean(frames * frames, axis=1), 1e-30))
    times = (np.arange(count) + 0.5) * block / rate
    peak = db(float(np.max(np.abs(audio))))

    def slope(first: float, last: float) -> float:
        chosen = (times >= first) & (times < last)
        return float(np.polyfit(times[chosen], levels[chosen], 1)[0]) if chosen.sum() >= 3 else math.nan

    span = times[-1] if count else 0.0
    return {"decay_db_per_s_0p3_to_end": slope(0.3, span + 1.0),
            "decay_db_per_s_last_0p5": slope(span - 0.5, span + 1.0),
            "natural_end_rms_db_re_peak": float(levels[-1] - peak) if count else math.nan,
            "file_end_rms_db_re_peak": db(float(np.sqrt(np.mean(audio[-block:] ** 2)))) - peak}


def write_target(path: Path, audio: np.ndarray, onset: int, end: int, rate: int) -> int:
    window = target_window(audio, onset, end, rate).astype(np.float64)
    fade = round(FADE * rate)
    window[-fade:] *= 0.5 * (1.0 + np.cos(np.pi * np.arange(fade) / fade))
    path.parent.mkdir(parents=True, exist_ok=True)
    window.astype("<f4").tofile(path)
    return int(window.size)


# --------------------------------------------------------------------------
# Output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--downloads", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--end-at", choices=("loop-start", "file-end"), default="loop-start")
    parser.add_argument("--offline", action="store_true")
    args = parser.parse_args()

    verified = fetch(args.downloads, args.offline)
    sfz_text = (args.downloads / FILES[PROGRAM][0]).read_text(encoding="utf-8")
    readme_text = (args.downloads / "README.md").read_text(encoding="utf-8")
    comments, file_level, regions = parse_sfz(sfz_text)
    if LICENSE_LINE not in comments:
        raise SystemExit(f"SFZ header no longer contains {LICENSE_LINE!r}")
    samples = [Path(region["opcodes"]["sample"]).name for region in regions]
    if sorted(samples) != sorted(WAVS):
        raise SystemExit(f"SFZ regions reference {samples}, expected the pinned WAV set")

    analysed: list[dict[str, Any]] = []
    audio_by_name: dict[str, tuple[np.ndarray, int]] = {}
    for region in regions:
        opcodes = region["opcodes"]
        name = Path(opcodes["sample"]).name
        wav = read_wav(args.downloads / "wav" / name)
        audio, rate = wav["audio"], wav["rate"]
        keycenter = int(opcodes["pitch_keycenter"])
        loops = wav["smpl"]["loops"] if wav["smpl"] else []
        if not wav["smpl"] or wav["smpl"]["unity_note"] != keycenter or len(loops) != 1:
            raise SystemExit(f"{name}: expected one smpl loop with unity note {keycenter}")
        loop = loops[0]
        natural_end = loop["start"] if args.end_at == "loop-start" else audio.size
        onset = refine_onset(audio, rate, min(natural_end, audio.size))
        end = min(natural_end, onset + round(MAX_AFTER * rate))
        pitch = settled_pitch(audio, rate, onset, end, midi_hz(keycenter))
        audio_by_name[name] = (audio, rate)
        analysed.append({
            "file": name, "rate": rate, "bits": wav["bits"], "channels": wav["channels"],
            "frames": int(audio.size), "seconds": audio.size / rate,
            "chunks": wav["chunks"], "smpl": wav["smpl"],
            "pitch_keycenter": keycenter, "lokey": int(opcodes["lokey"]),
            "hikey": int(opcodes["hikey"]),
            "sfz_opcodes": {key: value for key, value in opcodes.items()
                            if RECORDED_OPCODES.match(key)},
            "onset_frame": onset, "natural_end_frame": natural_end, "end_frame": end,
            "clean_seconds": (end - onset) / rate,
            "loop_start_seconds": loop["start"] / rate,
            "loop_end_seconds": loop["end"] / rate,
            "frames_after_loop_end": int(audio.size - 1 - loop["end"]),
            "loop_join": loop_join(audio, rate, loop["start"], loop["end"]),
            "decay": decay_profile(audio, rate, onset, natural_end),
            "isolation": isolation(audio, rate, onset, natural_end),
            "pitch": pitch,
            "peak_dbfs": db(float(np.max(np.abs(audio[max(0, onset):end])))),
            # Files are trimmed inside the attack: level of the first 0.5 ms.
            "start_level_db_re_peak": db(float(np.max(np.abs(audio[:round(0.0005 * rate)]))))
                                      - db(float(np.max(np.abs(audio)))),
        })

    deviations = []
    for item in analysed:
        semitones = 69.0 + 12.0 * math.log2(item["pitch"]["f0_hz"] / 440.0)
        deviations.append(100.0 * (semitones - round(semitones)))
    offset = float(np.median(deviations))
    for item in analysed:
        semitones = 69.0 + 12.0 * math.log2(item["pitch"]["f0_hz"] / 440.0)
        midi = int(round(semitones - offset / 100.0))
        if midi != item["pitch_keycenter"]:
            raise SystemExit(f"{item['file']}: measured MIDI {midi} != keycenter "
                             f"{item['pitch_keycenter']}")
        item["midi"] = midi
        item["cents_from_midi"] = 100.0 * (semitones - midi)
        reasons = []
        if item["clean_seconds"] < MIN_CLEAN:
            reasons.append(f"clean_seconds {item['clean_seconds']:.3f} < {MIN_CLEAN}")
        if item["isolation"]["isolation_db"] < MIN_ISOLATION:
            reasons.append(f"isolation {item['isolation']['isolation_db']:.1f} dB < {MIN_ISOLATION}")
        item["rejected"] = "; ".join(reasons) or None
        item["id"] = f"{CORPUS}-m{midi}-{note_name(midi)}"

    targets = args.out / "targets"
    targets.mkdir(parents=True, exist_ok=True)
    for stale in targets.glob("*.f32"):
        stale.unlink()
    rows = []
    pre = round(PRE_ROLL * analysed[0]["rate"])
    for item in analysed:
        if item["rejected"]:
            continue
        audio, rate = audio_by_name[item["file"]]
        path = targets / f"{item['id']}.f32"
        frames = write_target(path, audio, item["onset_frame"], item["end_frame"], rate)
        check = np.fromfile(path, dtype="<f4").astype(np.float64)
        item["target_onset_frame"] = scorer._onset(check, rate)
        padding = max(0, pre - item["onset_frame"])
        opcodes = " ".join(f"{k}={v}" for k, v in item["sfz_opcodes"].items()
                           if k.startswith(("ampeg", "pitcheg", "tune", "transpose",
                                            "offset", "volume", "loop")))
        rows.append({
            "id": item["id"], "split": CORPUS, "material": "steel", "picking": None,
            "midi": item["midi"], "velocity": VELOCITY, "round_robin": 0,
            "dynamic_group": None, "dynamic_marking": None,
            "target": {"path": f"targets/{item['id']}.f32", "sample_rate": rate,
                       "channels": 1},
            "onset_seconds_in_source": round(item["onset_frame"] / rate, 6),
            "isolation_db": round(item["isolation"]["isolation_db"], 2),
            "clean_seconds": round(item["clean_seconds"], 4),
            "measured_f0_hz": round(item["pitch"]["f0_hz"], 4),
            "cents_from_midi": round(item["cents_from_midi"], 2),
            "peak_dbfs": round(item["peak_dbfs"], 2),
            "notes": (
                f"{item['file']} (pitch_keycenter={item['pitch_keycenter']} "
                f"lokey={item['lokey']} hikey={item['hikey']}; {opcodes}); raw WAV only, "
                f"no SFZ envelope or filter; smpl loop {item['smpl']['loops'][0]['start']}-"
                f"{item['smpl']['loops'][0]['end']} of {item['frames']} frames; target ends "
                f"{item['end_frame'] / rate:.3f} s in source ("
                f"{'onset + 4.2 s' if item['end_frame'] < item['natural_end_frame'] else args.end_at.replace('-', ' ')}"
                f") with 60 ms fade; "
                f"{padding} leading zero samples complete the 20 ms pre-roll "
                f"(file starts {item['isolation']['pre_onset_samples']} samples before onset); "
                f"isolation = 300 ms peak minus late 12-16 kHz background "
                f"({item['isolation']['background_dbfs']:.1f} dBFS, a lower bound); "
                f"first 0.5 ms of the file already at {item['start_level_db_re_peak']:.1f} dB re peak "
                f"(trimmed inside the attack); "
                f"global tuning offset {offset:+.2f} cents; f0 fitted to partials "
                f"{item['pitch']['partials_used']} (first partial "
                f"{item['pitch']['first_partial_hz']:.3f} Hz), B={item['pitch']['inharmonicity_b']:.2e}; "
                f"picking undocumented; one velocity layer"),
        })
        if item["target_onset_frame"] != pre:
            raise SystemExit(f"{item['id']}: scorer onset {item['target_onset_frame']} != {pre}")
        if frames != pre + item["end_frame"] - item["onset_frame"]:
            raise SystemExit(f"{item['id']}: unexpected target length {frames}")

    document = {"corpus": CORPUS, "license": LICENSE, "source": SOURCE_URL, "rows": rows}
    (args.out / "rows.json").write_text(json.dumps(document, indent=1) + "\n")
    (args.out / "regions.json").write_text(json.dumps({
        "corpus": CORPUS, "commit": COMMIT, "end_at": args.end_at,
        "global_tuning_offset_cents": offset, "file_level_opcodes": file_level,
        "regions": analysed}, indent=1) + "\n")
    write_source(args.out, verified, comments, readme_text, analysed, offset, args.end_at)
    print(f"{len(rows)} rows, {sum(1 for item in analysed if item['rejected'])} rejected; "
          f"global tuning offset {offset:+.2f} cents -> {args.out}")
    return 0


def write_source(out: Path, verified: dict[str, dict[str, str]], comments: list[str],
                 readme: str, analysed: list[dict[str, Any]], offset: float, end_at: str) -> None:
    licenses = readme.split("## Licenses", 1)[1].split("##", 1)[0].strip()
    scorer_path = Path(scorer.__file__).resolve()
    lines = [
        "# martin-hd28 corpus source",
        "",
        "Generated by `Tools/PrepareMartinCorpus.py`; do not edit by hand.",
        "",
        f"- Instrument: 2017 Martin HD28 Vintage Series steel-string dreadnought, "
        f"sampled by Jeff Learman for the Discord SFZ GM Bank (program 026).",
        f"- Repository: https://github.com/{REPOSITORY}, commit `{COMMIT}`.",
        f"- Program: {TREE}{urllib.parse.quote(PROGRAM)}",
        f"- Samples: {SOURCE_URL}",
        f"- License: {LICENSE} ({LICENSE_URL}), from the SFZ header. The repository has "
        f"no LICENSE file (GitHub reports no license); its README delegates to each .sfz.",
        f"- Target end: `--end-at {end_at}`. Global tuning offset {offset:+.2f} cents "
        f"(median, re A = 440 Hz); `cents_from_midi` includes it.",
        f"- Onset rule: `_onset` from {scorer_path.name} "
        f"(SHA-256 `{sha256(scorer_path.read_bytes())}` when generated).",
        "",
        "## License evidence (verbatim)",
        "",
        f"SFZ header, `{PROGRAM}`:",
        "",
        "```",
        *comments[:4],
        "```",
        "",
        "Repository README.md, section Licenses:",
        "",
        "```",
        licenses,
        "```",
        "",
        "Wiki Conventions page (https://github.com/sfzinstruments/Discord-SFZ-GM-Bank/wiki/"
        "Conventions) asks contributors for a `// License:` comment naming the license and "
        "the licensor. Picking technique is not documented in the SFZ, README or wiki; "
        "`picking` is null. The wiki GM table links an off-GitHub Google Drive original "
        "(not used, not verified).",
        "",
        "## Files (verified by git blob SHA-1 and SHA-256)",
        "",
        "| repository path | bytes | git blob SHA-1 | SHA-256 |",
        "|---|---:|---|---|",
    ]
    for repo_path, info in verified.items():
        lines.append(f"| `{repo_path}` | {info['bytes']} | `{info['git_blob_sha1']}` | "
                     f"`{info['sha256']}` |")
    lines += [
        "",
        "## Regions",
        "",
        "Every region: 44.1 kHz 16-bit mono PCM, chunks fmt/data/smpl, one forward smpl "
        "loop ending 100 frames before the file end, `ampeg_hold` = loop start, "
        "`ampeg_sustain=0`, no loop_mode/tune/transpose/offset/volume opcodes. "
        "File-level opcodes: `fil_type=lpf_1p cutoff=22050 cutoff_oncc131=-6500 "
        "cutoff_curvecc131=2` (velocity-tracked low-pass, open at velocity 127). "
        "Join/control: lag-L residual 0-10 ms before the loop end vs 300 ms before the "
        "loop start (lower = more self-similar; a loop edit). Decay: broadband 50 ms RMS "
        "slope from 0.3 s to the natural end, and in its last 0.5 s.",
        "",
        "| file | key | lo-hi | ampeg hold/decay/release s | length s | onset frame | loop s "
        "| start/end level dB re peak | join/control dB | decay dB/s (last 0.5 s) | f0 Hz (first partial) | cents | "
        "B | isolation dB | clean s | status |",
        "|---|---:|---|---|---:|---:|---|---:|---|---|---:|---:|---:|---:|---:|---|",
    ]
    for item in analysed:
        op = item["sfz_opcodes"]
        decay = item["decay"]
        join = item["loop_join"]
        lines.append(
            f"| {item['file']} | {item['pitch_keycenter']} | {item['lokey']}-{item['hikey']} | "
            f"{op.get('ampeg_hold')}/{op.get('ampeg_decay')}/{op.get('ampeg_release')} | "
            f"{item['seconds']:.3f} | {item['onset_frame']} | "
            f"{item['loop_start_seconds']:.3f}-{item['loop_end_seconds']:.3f} | "
            f"{item['start_level_db_re_peak']:.1f}/{decay['file_end_rms_db_re_peak']:.1f} | "
            f"{join['join_residual_db']:.1f}/{join['control_residual_db']:.1f} | "
            f"{decay['decay_db_per_s_0p3_to_end']:.1f} ({decay['decay_db_per_s_last_0p5']:.1f}) | "
            f"{item['pitch']['f0_hz']:.3f} ({item['pitch']['first_partial_hz']:.3f}) | "
            f"{item['cents_from_midi']:+.1f} | "
            f"{item['pitch']['inharmonicity_b']:.1e} | {item['isolation']['isolation_db']:.1f} | "
            f"{item['clean_seconds']:.3f} | {item['rejected'] or 'row ' + item['id']} |")
    lines += [
        "",
        "## Regenerate",
        "",
        "```",
        "python3 Tools/PrepareMartinCorpus.py --downloads \"$S/downloads/martin-hd28\" "
        f"--out \"$S/corpora/martin-hd28\" --end-at {end_at}",
        "```",
        "",
    ]
    (out / "SOURCE.md").write_text("\n".join(lines))


if __name__ == "__main__":
    raise SystemExit(main())
