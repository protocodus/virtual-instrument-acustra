#!/usr/bin/env python3
"""Validate and package RenderNaturalPerformance's deterministic ablations.

Native floats and source/build evidence are retained. Listening derivatives
use explicitly recorded whole-passage RMS matching, without EQ or limiting.
The optional mix audition uses a separately retained synthetic bass/drum bed;
it is neither a recorded performance nor an independent realism reference.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
from pathlib import Path

import numpy as np
from scipy.io import wavfile


VARIANTS = ("baseline", "contact", "hand", "damping", "body", "combined")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def rms(audio: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(audio, dtype=np.float64))))


def write_wave(path: Path, rate: int, audio: np.ndarray) -> dict:
    if not np.all(np.isfinite(audio)) or np.max(np.abs(audio)) > 0.999:
        raise ValueError(f"invalid listening derivative: {path}")
    wavfile.write(path, rate, np.asarray(audio, dtype=np.float32))
    return {"file": path.name, "sha256": sha256(path), "frames": len(audio),
            "peak": float(np.max(np.abs(audio))), "rms": rms(audio)}


def collect_sources(source: Path, build: Path, binary: Path, output: Path) -> dict:
    """Copy the supplied frozen sources; no working-tree Git ID substitutes for them."""
    destination = output / "source-evidence"
    destination.mkdir()
    paths = sorted((source / "Source" / "DSP").rglob("*.h"))
    paths += sorted((source / "Source" / "DSP").glob("*.cpp"))
    paths += [source / "CMakeLists.txt"]
    hashes = {}
    for path in paths:
        relative = path.relative_to(source)
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        hashes[str(relative)] = sha256(path)
    for tool_name in ("RenderNaturalPerformance.cpp", "PackageNaturalPerformance.py"):
        path = source / "Tools" / tool_name
        if not path.is_file():
            # The old DSP snapshot cannot contain a newly authored renderer.
            # Explicitly retain the actual current harness in this case.
            path = Path(__file__).resolve().with_name(tool_name)
        target = destination / "Tools" / tool_name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        hashes[f"Tools/{tool_name}"] = sha256(path)
    build_hashes = {}
    for relative in ("CMakeCache.txt", "compile_commands.json", "CMakeFiles/AcustraDSP.dir/flags.make"):
        path = build / relative
        if path.is_file():
            target = destination / "build" / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, target)
            build_hashes[relative] = sha256(path)
    if not build_hashes:
        raise ValueError("build provenance is missing")
    return {"source_root": str(source.resolve()), "source_sha256": hashes,
            "build_root": str(build.resolve()), "build_sha256": build_hashes,
            "renderer_binary": str(binary.resolve()), "renderer_sha256": sha256(binary)}


def inspect_renders(root: Path) -> tuple[dict, dict[str, dict[str, dict]], list[dict]]:
    manifest = json.loads((root / "manifest.json").read_text())
    if manifest["schema"] != 1 or manifest["channels"] != 2 or manifest["post_gain"] != 1:
        raise ValueError("unsupported native render schema")
    groups: dict[str, dict[str, dict]] = {}
    reports = []
    for render in manifest["renders"]:
        raw_path, wav_path = root / render["raw"], root / render["wav"]
        audio = np.fromfile(raw_path, dtype="<f4").reshape(-1, 2)
        rate, wave = wavfile.read(wav_path)
        if rate != manifest["sample_rate"] or len(audio) != render["frames"]:
            raise ValueError(f"duration or sample-rate mismatch: {raw_path}")
        if not np.array_equal(audio.view("u4"), wave.view("u4")):
            raise ValueError(f"WAV does not preserve raw float bits: {wav_path}")
        if not np.all(np.isfinite(audio)) or rms(audio) == 0 or render["dropped_events"] != 0:
            raise ValueError(f"invalid rendering: {raw_path}")
        measured_peak = float(np.max(np.abs(audio)))
        if not math.isclose(measured_peak, render["peak"], rel_tol=1e-8, abs_tol=1e-12):
            raise ValueError(f"manifest peak mismatch: {raw_path}")
        if not math.isclose(rms(audio), render["rms"], rel_tol=1e-8, abs_tol=1e-12):
            raise ValueError(f"manifest RMS mismatch: {raw_path}")
        records = groups.setdefault(render["case"], {})
        if render["variant"] in records:
            raise ValueError(f"duplicate case and variant: {raw_path}")
        record = dict(render, raw_sha256=sha256(raw_path), wav_sha256=sha256(wav_path),
                      score_sha256=sha256(root / render["score"]))
        records[render["variant"]] = record
        reports.append(record)
    for name, group in groups.items():
        reference = group["baseline"]
        for record in group.values():
            for key in ("parameters", "frames", "score_sha256"):
                if record[key] != reference[key]:
                    raise ValueError(f"variant input confound: {name}: {key}")
    return manifest, groups, reports


def compare_previous(root: Path, groups: dict, previous: Path) -> dict:
    manifest, old_groups, _ = inspect_renders(previous)
    checked = []
    for case, group in groups.items():
        if case not in old_groups:
            raise ValueError(f"prior baseline is missing {case}")
        current, old = group["baseline"], old_groups[case]["baseline"]
        for key in ("parameters", "frames", "score_sha256"):
            if current[key] != old[key]:
                raise ValueError(f"old-API comparison input mismatch: {case}: {key}")
        if current["raw_sha256"] != old["raw_sha256"]:
            a = np.fromfile(root / current["raw"], dtype="<f4")
            b = np.fromfile(previous / old["raw"], dtype="<f4")
            raise ValueError(f"disabled-current versus old baseline differs: {case}; "
                             f"max absolute sample delta {float(np.max(np.abs(a-b)))}")
        checked.append(case)
    return {"all_disabled_matches_previous_bytes": True, "cases": checked,
            "previous_root": str(previous.resolve()),
            "previous_manifest_sha256": sha256(previous / "manifest.json"),
            "sample_rate": manifest["sample_rate"]}


def synthetic_bed(frames: int, rate: int) -> np.ndarray:
    """Deterministic musical context only: no external samples or room effects."""
    bed = np.zeros((frames, 2), np.float64)
    rng = np.random.default_rng(20261007)

    def add(at: float, mono: np.ndarray, pan: float = 0.0) -> None:
        start = round(at * rate)
        available = min(len(mono), frames - start)
        if available <= 0:
            return
        gains = np.sqrt([(1.0 - pan) * 0.5, (1.0 + pan) * 0.5])
        bed[start:start + available] += mono[:available, None] * gains[None, :]

    for bar, pitch in enumerate((45, 41, 48, 43)):
        for beat in range(6):
            at = 0.2 + bar * 3.0 + beat * 0.5
            t = np.arange(round(0.43 * rate)) / rate
            frequency = 440.0 * 2.0 ** ((pitch - 12 - 69) / 12.0)
            bass = (np.sin(2 * np.pi * frequency * t) + 0.12 * np.sin(4 * np.pi * frequency * t))
            bass *= (1 - np.exp(-t / 0.008)) * np.exp(-t / 0.15)
            bass[-min(len(bass), round(rate * 0.015)):] *= np.linspace(1, 0, min(len(bass), round(rate * 0.015)))
            add(at, 0.6 * bass)
            if beat % 2 == 0:
                t = np.arange(round(0.22 * rate)) / rate
                phase = 2 * np.pi * (47 * t + 32 * 0.025 * (1 - np.exp(-t / 0.025)))
                add(at, 0.7 * np.sin(phase) * np.exp(-t / 0.043) * (1 - np.exp(-t / 0.001)))
            else:
                t = np.arange(round(0.18 * rate)) / rate
                noise = rng.standard_normal(len(t))
                add(at, 0.13 * noise * np.exp(-t / 0.028) * (1 - np.exp(-t / 0.001)), 0.1)
            for half in range(2):
                t = np.arange(round(0.07 * rate)) / rate
                noise = rng.standard_normal(len(t) + 1)
                hat = np.diff(noise) * np.exp(-t / 0.009) * (1 - np.exp(-t / 0.0005))
                add(at + half * 0.25, 0.025 * hat, -0.25)
    return bed


def make_listening(root: Path, manifest: dict, groups: dict, output: Path) -> list[dict]:
    rate = manifest["sample_rate"]
    gap = np.zeros((rate, 2))
    derivatives = []
    for case, group in sorted(groups.items()):
        if set(group) != set(VARIANTS):
            continue
        before = np.fromfile(root / group["baseline"]["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
        baseline_rms = rms(before)
        gains, sounds = {}, {}
        for variant in VARIANTS:
            audio = np.fromfile(root / group[variant]["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
            gain = baseline_rms / rms(audio)
            gains[variant] = gain
            sounds[variant] = audio * gain
        # A single shared gain protects audition playback from any overs;
        # it never alters relative levels or compresses the transient.
        peak = max(float(np.max(np.abs(audio))) for audio in sounds.values())
        common = min(1.0, 0.97 / peak)
        main_case = any(token in case for token in (
            "01-repeated-notes-finger", "02-melody-accompaniment-finger",
            "03-alternating-strums-pick", "04-release-control-finger"))
        if main_case:
            comparison = np.concatenate((sounds["baseline"], gap, sounds["combined"])) * common
            info = write_wave(output / f"{case}-before-after.wav", rate, comparison)
            info.update(case=case, kind="whole-passage-RMS-matched-before-after",
                        segments=[{"variant": "baseline", "start_seconds": 0},
                                  {"variant": "combined", "start_seconds": len(before) / rate + 1}],
                        matching_gains={key: gains[key] for key in ("baseline", "combined")},
                        common_gain=common)
            derivatives.append(info)
        if "01-repeated-notes-finger" in case or "04-release-control-finger" in case:
            parts = []
            for index, variant in enumerate(VARIANTS):
                if index:
                    parts.append(gap)
                parts.append(sounds[variant])
            info = write_wave(output / f"{case}-six-ablations.wav", rate, np.concatenate(parts) * common)
            info.update(case=case, kind="six-independent-ablations", matching_gains=gains,
                        common_gain=common,
                        segments=[{"variant": variant, "start_seconds": index * (len(before) / rate + 1)}
                                  for index, variant in enumerate(VARIANTS)])
            derivatives.append(info)
        if "02-melody-accompaniment-finger" in case:
            bed = synthetic_bed(len(before), rate)
            bed *= baseline_rms * 10.0 ** (-11.0 / 20.0) / rms(bed)
            mix_before, mix_after = sounds["baseline"] + bed, sounds["combined"] + bed
            mix_gain = min(1.0, 0.97 / max(float(np.max(np.abs(mix_before))), float(np.max(np.abs(mix_after)))))
            info = write_wave(output / f"{case}-synthetic-test-mix-before-after.wav", rate,
                              np.concatenate((mix_before, gap, mix_after)) * mix_gain)
            info.update(case=case, kind="synthetic-bass-drums-test-mix", bed_level_db_relative_to_guitar_rms=-11,
                        matching_gains={key: gains[key] for key in ("baseline", "combined")},
                        common_gain=mix_gain,
                        segments=[{"variant": "baseline", "start_seconds": 0},
                                  {"variant": "combined", "start_seconds": len(before) / rate + 1}])
            derivatives.append(info)
            info = write_wave(output / f"{case}-synthetic-bed-only.wav", rate, bed * mix_gain)
            info.update(case=case, kind="synthetic-bed-only", common_gain=mix_gain)
            derivatives.append(info)
    return derivatives


def measurements(root: Path, groups: dict) -> list[dict]:
    rows = []
    for case, group in sorted(groups.items()):
        before = np.fromfile(root / group["baseline"]["raw"], dtype="<f4").astype(np.float64)
        before_rms = rms(before)
        for variant, render in group.items():
            audio = np.fromfile(root / render["raw"], dtype="<f4").astype(np.float64)
            audio_rms = rms(audio)
            rows.append({"case": case, "variant": variant,
                         "native_rms_change_db": 20 * math.log10(audio_rms / before_rms),
                         "native_peak": render["peak"],
                         "same_float_bits_as_baseline": render["raw_sha256"] == group["baseline"]["raw_sha256"],
                         "difference_rms_relative_to_baseline": rms(audio - before) / before_rms})
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renders", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-tree", type=Path, required=True, help="frozen source used for this renderer")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--renderer-binary", type=Path, required=True)
    parser.add_argument("--previous-renders", type=Path, help="old-API frozen 9bf1cff render directory")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output exists; preserve existing listening evidence")
    manifest, groups, renders = inspect_renders(args.renders)
    previous = None
    if args.previous_renders:
        old_manifest = json.loads((args.previous_renders / "manifest.json").read_text())
        for key in ("sample_rate", "channels", "block_size", "tempo_bpm", "gather_chords", "latency_samples"):
            if old_manifest[key] != manifest[key]:
                raise ValueError(f"prior renderer configuration differs: {key}")
        previous = compare_previous(args.renders, groups, args.previous_renders)
    args.output.mkdir(parents=True)
    provenance = collect_sources(args.source_tree, args.build_dir, args.renderer_binary, args.output)
    listening = args.output / "listening"
    listening.mkdir()
    derivatives = make_listening(args.renders, manifest, groups, listening)
    report = {"schema": 1, "native_renders": str(args.renders.resolve()),
              "manifest_sha256": sha256(args.renders / "manifest.json"), "provenance": provenance,
              "previous_byte_parity": previous, "renders": renders,
              "measurements": measurements(args.renders, groups), "listening": derivatives,
              "interpretation": "Difference measurements and validated renders do not establish listener preference. "
              "All variants use identical MIDI and controls. RMS matching is for listening derivatives only; "
              "native levels are retained. The synthetic test bed is not a real recording reference."}
    (args.output / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    rows = ["Acustra natural-performance audition", "", report["interpretation"], "",
            "Before/after: baseline first, one second of silence, combined second.",
            "Six ablations: baseline, contact, hand, damping, body, combined; one second between each.",
            "Release passages: short/long holds, then repeat with release velocities 16, 64 and 120.",
            "All native input scores, float audio, levels and derivative gains are in validation.json.", ""]
    for artifact in derivatives:
        if "segments" in artifact:
            rows.append(f"{artifact['file']}: " + ", ".join(
                f"{segment['variant']} {segment['start_seconds']:.2f}s" for segment in artifact["segments"]))
    (args.output / "LISTENING.txt").write_text("\n".join(rows) + "\n")
    print(f"Validated {len(renders)} native renders and wrote {len(derivatives)} listening derivatives.")
    if previous:
        print(f"All-disabled output matches frozen preceding DSP bytes in {len(previous['cases'])} cases.")


if __name__ == "__main__":
    main()
