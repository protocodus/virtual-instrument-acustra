#!/usr/bin/env python3
"""Validate and package RenderNaturalPerformance's deterministic ablations.

Native floats and source/build evidence are retained. Listening derivatives
use explicitly recorded whole-passage RMS matching, without EQ or limiting.
The optional mix audition uses a separately retained synthetic bass/drum bed;
it is neither a recorded performance nor an independent realism reference.

When all four --production-* arguments are supplied, --renders is the
analysis-only ablation set with the previous construction trims. Primary
before/after clips instead compare --previous-renders with production's
combined variant. Only ConstructionLoudnessData.h may differ between the
analysis and production DSP snapshots; both are preserved with their builds.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import importlib.util
import json
import math
import shutil
from pathlib import Path

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly


LEGACY_VARIANTS = ("baseline", "contact", "hand", "damping", "body", "combined")
VARIANTS = ("baseline", "contact", "hand", "damping", "body", "continuity", "combined")
METER_SPEC = importlib.util.spec_from_file_location(
    "acustra_natural_loudness_meter", Path(__file__).with_name("MeasureMaterialLoudness.py"))
assert METER_SPEC is not None and METER_SPEC.loader is not None
METER = importlib.util.module_from_spec(METER_SPEC)
METER_SPEC.loader.exec_module(METER)


def variant_order(manifest: dict) -> tuple[str, ...]:
    return VARIANTS if manifest["schema"] == 2 else LEGACY_VARIANTS


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def rms(audio: np.ndarray) -> float:
    return float(np.sqrt(np.mean(np.square(audio, dtype=np.float64))))


def native_lufs(audio: np.ndarray, rate: int) -> float:
    if rate != METER.RATE:
        divisor = math.gcd(rate, METER.RATE)
        audio = resample_poly(audio, METER.RATE // divisor, rate // divisor, axis=0)
    loudness = METER.integrated_loudness(audio)
    if not math.isfinite(loudness):
        raise ValueError("native passage is too short or quiet for integrated loudness")
    return loudness


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
    tool_origins = {}
    for tool_name in ("RenderNaturalPerformance.cpp", "PackageNaturalPerformance.py", "MeasureMaterialLoudness.py"):
        # The packager and meter run now, and can postdate a frozen DSP
        # revision. Record their actual executing source, not an older copy
        # that happens to exist inside that revision's Tools directory.
        path = (source / "Tools" / tool_name if tool_name == "RenderNaturalPerformance.cpp"
                else Path(__file__).resolve().with_name(tool_name))
        if not path.is_file():
            # The old DSP snapshot cannot contain a newly authored renderer.
            # Explicitly retain the actual current harness in this case.
            path = Path(__file__).resolve().with_name(tool_name)
        target = destination / "Tools" / tool_name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
        hashes[f"Tools/{tool_name}"] = sha256(path)
        tool_origins[tool_name] = str(path.resolve())
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
            "actual_tool_source_paths": tool_origins,
            "build_root": str(build.resolve()), "build_sha256": build_hashes,
            "renderer_binary": str(binary.resolve()), "renderer_sha256": sha256(binary)}


def inspect_renders(root: Path, require_baseline: bool = True) -> tuple[dict, dict[str, dict[str, dict]], list[dict]]:
    manifest = json.loads((root / "manifest.json").read_text())
    if manifest["schema"] not in (1, 2) or manifest["channels"] != 2 or manifest["post_gain"] != 1:
        raise ValueError("unsupported native render schema")
    if manifest["schema"] == 2 and manifest.get("explicit_string_mode_cc126_value") != 6:
        raise ValueError("physical-string fixture must use CC126 value 6")
    groups: dict[str, dict[str, dict]] = {}
    reports = []
    for render in manifest["renders"]:
        switch_names = ("contactRelease", "coherentHand", "gestureDamping", "playerBodyLoading")
        single = {"contact": 0, "hand": 1, "damping": 2, "body": 3}
        if manifest["schema"] == 2:
            switch_names += ("retuneContinuity",)
            single["continuity"] = 4
        if render["variant"] not in variant_order(manifest):
            raise ValueError("unknown render variant")
        expected = {name: render["variant"] == "combined" or single.get(render["variant"]) == index
                    for index, name in enumerate(switch_names)}
        if render["switches"] != expected:
            raise ValueError(f"variant switches do not match label: {render['case']}: {render['variant']}")
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
        if manifest["schema"] == 2:
            validate_allocations(root, render)
            record["allocation_sha256"] = sha256(root / render["allocations"])
        records[render["variant"]] = record
        reports.append(record)
    for name, group in groups.items():
        reference = group["baseline"] if require_baseline else next(iter(group.values()))
        for record in group.values():
            for key in ("parameters", "frames", "score_sha256"):
                if record[key] != reference[key]:
                    raise ValueError(f"variant input confound: {name}: {key}")
    return manifest, groups, reports


def validate_allocations(root: Path, render: dict) -> None:
    """Cross-check actual attacks against the MIDI score, including mode changes."""
    def rows(path):
        with path.open() as stream:
            return [{key: int(value) for key, value in row.items()}
                    for row in csv.DictReader(stream, delimiter="\t")]
    explicit = False
    expected = []
    for event in rows(root / render["score"]):
        if event["status"] == 0xb0 and event["channel"] == 1:
            if event["data1"] == 126:
                if event["data2"] != 6:
                    raise ValueError("physical-string fixture must use CC126 value 6")
                explicit = True
            elif event["data1"] == 127:
                explicit = False
        if event["status"] == 0x90 and event["data2"] > 0:
            expected.append({"frame": event["frame"], "channel": event["channel"],
                             "midi_note": event["data1"], "explicit_strings": int(explicit),
                             "expected_string": event["channel"] - 1 if explicit else -1})
    allocations = rows(root / render["allocations"])
    if len(expected) != len(allocations):
        raise ValueError(f"score and physical attack counts differ: {render['case']}")
    explicit_count = 0
    for intent, actual in zip(expected, allocations):
        if any(actual[key] != value for key, value in intent.items()):
            raise ValueError(f"score and physical attack evidence differ: {render['case']}")
        if intent["explicit_strings"]:
            explicit_count += 1
            if (actual["actual_string"] != intent["expected_string"]
                    or not 0 <= actual["actual_string"] < 6
                    or actual["fret"] != actual["midi_note"] - actual["open_midi"]):
                raise ValueError(f"explicit physical-string mismatch in evidence: {render['case']}")
    if explicit_count != render["verified_explicit_attacks"]:
        raise ValueError(f"physical allocation evidence count mismatch: {render['case']}")


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


def make_listening(root: Path, manifest: dict, groups: dict, output: Path,
                   production_root: Path | None = None, production_groups: dict | None = None,
                   previous_root: Path | None = None, previous_groups: dict | None = None) -> list[dict]:
    rate = manifest["sample_rate"]
    order = variant_order(manifest)
    gap = np.zeros((rate, 2))
    derivatives = []
    for case, group in sorted(groups.items()):
        if set(group) != set(order):
            continue
        before = np.fromfile(root / group["baseline"]["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
        baseline_rms = rms(before)
        gains, sounds = {}, {}
        for variant in order:
            audio = np.fromfile(root / group[variant]["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
            gain = baseline_rms / rms(audio)
            gains[variant] = gain
            sounds[variant] = audio * gain
        primary_before, primary_after = before, sounds["combined"]
        primary_gains = {key: gains[key] for key in ("baseline", "combined")}
        primary_sources = {"before": str((root / group["baseline"]["raw"]).resolve()),
                           "after": str((root / group["combined"]["raw"]).resolve())}
        primary_native_delta = 20 * math.log10(group["combined"]["rms"] / group["baseline"]["rms"])
        if production_root is not None:
            assert production_groups is not None and previous_root is not None and previous_groups is not None
            old, new = previous_groups[case]["baseline"], production_groups[case]["combined"]
            primary_before = np.fromfile(previous_root / old["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
            production_audio = np.fromfile(production_root / new["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
            primary_gains = {"baseline": 1.0, "combined": rms(primary_before) / rms(production_audio)}
            primary_after = production_audio * primary_gains["combined"]
            primary_sources = {"before": str((previous_root / old["raw"]).resolve()),
                               "after": str((production_root / new["raw"]).resolve())}
            primary_native_delta = 20 * math.log10(new["rms"] / old["rms"])
        # A single shared gain protects audition playback from any overs;
        # it never alters relative levels or compresses the transient.
        peak = max(float(np.max(np.abs(audio))) for audio in sounds.values())
        common = min(1.0, 0.97 / peak)
        primary_common = min(1.0, 0.97 / max(float(np.max(np.abs(primary_before))),
                                             float(np.max(np.abs(primary_after)))))
        main_case = any(token in case for token in (
            "01-repeated-notes-finger", "02-melody-accompaniment-finger",
            "03-alternating-strums-pick", "04-release-control-finger"))
        if main_case:
            comparison = np.concatenate((primary_before, gap, primary_after)) * primary_common
            info = write_wave(output / f"{case}-before-after.wav", rate, comparison)
            info.update(case=case, kind="whole-passage-RMS-matched-before-after",
                        segments=[{"variant": "baseline", "start_seconds": 0},
                                  {"variant": "combined", "start_seconds": len(before) / rate + 1}],
                        matching_gains=primary_gains, common_gain=primary_common,
                        native_after_minus_before_rms_db=primary_native_delta,
                        native_sources=primary_sources,
                        comparison="previous-production-versus-current-production" if production_root else "shared-calibration-ablation")
            derivatives.append(info)
        if "01-repeated-notes-finger" in case or "04-release-control-finger" in case:
            parts = []
            for index, variant in enumerate(order):
                if index:
                    parts.append(gap)
                parts.append(sounds[variant])
            count_name = "seven" if len(order) == 7 else "six"
            info = write_wave(output / f"{case}-{count_name}-ablations.wav", rate, np.concatenate(parts) * common)
            info.update(case=case, kind=f"{count_name}-independent-ablations", matching_gains=gains,
                        comparison="isolated-mechanisms-with-one-shared-calibration",
                        common_gain=common,
                        segments=[{"variant": variant, "start_seconds": index * (len(before) / rate + 1)}
                                  for index, variant in enumerate(order)])
            derivatives.append(info)
        if "02-melody-accompaniment-finger" in case:
            bed = synthetic_bed(len(before), rate)
            bed *= rms(primary_before) * 10.0 ** (-11.0 / 20.0) / rms(bed)
            mix_before, mix_after = primary_before + bed, primary_after + bed
            mix_gain = min(1.0, 0.97 / max(float(np.max(np.abs(mix_before))), float(np.max(np.abs(mix_after)))))
            info = write_wave(output / f"{case}-synthetic-test-mix-before-after.wav", rate,
                              np.concatenate((mix_before, gap, mix_after)) * mix_gain)
            info.update(case=case, kind="synthetic-bass-drums-test-mix", bed_level_db_relative_to_guitar_rms=-11,
                        matching_gains=primary_gains, native_sources=primary_sources,
                        native_after_minus_before_rms_db=primary_native_delta,
                        comparison="previous-production-versus-current-production" if production_root else "shared-calibration-ablation",
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


def validate_production(analysis_manifest: dict, analysis_groups: dict, production_manifest: dict,
                        production_groups: dict, previous_manifest: dict, previous_groups: dict,
                        analysis_source: Path, production_source: Path) -> dict:
    # Reject incomplete overlaps rather than silently selecting the convenient
    # subset. A combined-only production render is enough; no production
    # all-disabled render substitutes for the real preceding instrument.
    cases = set(analysis_groups)
    if cases != set(production_groups) or cases != set(previous_groups):
        raise ValueError("analysis, production and previous render case sets differ")
    for name, candidate in (("production", production_manifest), ("previous", previous_manifest)):
        for key in ("schema", "sample_rate", "channels", "block_size", "tempo_bpm", "gather_chords", "latency_samples", "post_gain"):
            if candidate[key] != analysis_manifest[key]:
                raise ValueError(f"{name} renderer configuration differs: {key}")
    for case in cases:
        if set(analysis_groups[case]) != set(variant_order(analysis_manifest)):
            raise ValueError(f"isolated analysis is missing an ablation: {case}")
        if "combined" not in production_groups[case] or "baseline" not in previous_groups[case]:
            raise ValueError(f"required production or preceding variant missing: {case}")
        reference = analysis_groups[case]["baseline"]
        for candidate in (production_groups[case]["combined"], previous_groups[case]["baseline"]):
            for key in ("parameters", "frames", "score_sha256"):
                if candidate[key] != reference[key]:
                    raise ValueError(f"production comparison input confound: {case}: {key}")
        if not all(production_groups[case]["combined"]["switches"].values()):
            raise ValueError(f"production combined render has a disabled mechanism: {case}")
    def source_hashes(source):
        return {str(path.relative_to(source)): sha256(path)
                for path in (source / "Source" / "DSP").rglob("*") if path.is_file()}
    analysis_hashes, production_hashes = source_hashes(analysis_source), source_hashes(production_source)
    if set(analysis_hashes) != set(production_hashes):
        raise ValueError("production and analysis DSP file inventories differ")
    differences = sorted(name for name in analysis_hashes if analysis_hashes[name] != production_hashes[name])
    allowed = "Source/DSP/ConstructionLoudnessData.h"
    if any(name != allowed for name in differences):
        raise ValueError(f"production and analysis DSP differ beyond output calibration: {differences}")
    for tool in ("Tools/RenderNaturalPerformance.cpp", "CMakeLists.txt"):
        if sha256(analysis_source / tool) != sha256(production_source / tool):
            raise ValueError(f"production and analysis build/renderer differ: {tool}")
    return {"case_count": len(cases), "shared_scores_and_parameters": True,
            "allowed_DSP_difference": allowed, "actual_DSP_differences": differences,
            "analysis_calibration_sha256": analysis_hashes[allowed],
            "production_calibration_sha256": production_hashes[allowed]}


def production_measurements(previous_root: Path, previous_groups: dict,
                            production_root: Path, production_groups: dict, rate: int) -> list[dict]:
    rows = []
    for case in sorted(production_groups):
        old, new = previous_groups[case]["baseline"], production_groups[case]["combined"]
        before = np.fromfile(previous_root / old["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
        after = np.fromfile(production_root / new["raw"], dtype="<f4").reshape(-1, 2).astype(np.float64)
        before_lufs, after_lufs = native_lufs(before, rate), native_lufs(after, rate)
        rows.append({"case": case, "baseline_native_peak": old["peak"], "production_native_peak": new["peak"],
                     "baseline_native_lufs": before_lufs, "production_native_lufs": after_lufs,
                     "native_loudness_change_lu": after_lufs - before_lufs,
                     "native_rms_change_db": 20 * math.log10(new["rms"] / old["rms"]),
                     "difference_rms_relative_to_baseline": rms(after - before) / rms(before)})
    return rows


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renders", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--source-tree", type=Path, required=True, help="frozen source used for this renderer")
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--renderer-binary", type=Path, required=True)
    parser.add_argument("--previous-renders", type=Path, help="old-API frozen 9bf1cff render directory")
    parser.add_argument("--production-renders", type=Path, help="combined renders with the delivered calibration")
    parser.add_argument("--production-source-tree", type=Path)
    parser.add_argument("--production-build-dir", type=Path)
    parser.add_argument("--production-renderer-binary", type=Path)
    args = parser.parse_args()
    production_arguments = (args.production_renders, args.production_source_tree,
                            args.production_build_dir, args.production_renderer_binary)
    if any(production_arguments) and (not all(production_arguments) or not args.previous_renders):
        parser.error("all four --production-* arguments and --previous-renders are required together")
    if args.output.exists():
        parser.error("output exists; preserve existing listening evidence")
    manifest, groups, renders = inspect_renders(args.renders)
    previous = None
    previous_manifest, previous_groups = None, None
    if args.previous_renders:
        old_manifest = json.loads((args.previous_renders / "manifest.json").read_text())
        for key in ("sample_rate", "channels", "block_size", "tempo_bpm", "gather_chords", "latency_samples"):
            if old_manifest[key] != manifest[key]:
                raise ValueError(f"prior renderer configuration differs: {key}")
        previous = compare_previous(args.renders, groups, args.previous_renders)
        previous_manifest, previous_groups, _ = inspect_renders(args.previous_renders)
    production_manifest, production_groups, production_renders = None, None, None
    production_validation = None
    if args.production_renders:
        production_manifest, production_groups, production_renders = inspect_renders(args.production_renders, require_baseline=False)
        production_validation = validate_production(manifest, groups, production_manifest, production_groups,
            previous_manifest, previous_groups, args.source_tree, args.production_source_tree)
    args.output.mkdir(parents=True)
    provenance = collect_sources(args.source_tree, args.build_dir, args.renderer_binary, args.output)
    production_provenance = None
    if args.production_renders:
        (args.output / "production").mkdir()
        production_provenance = collect_sources(args.production_source_tree, args.production_build_dir,
                                                args.production_renderer_binary, args.output / "production")
    listening = args.output / "listening"
    listening.mkdir()
    derivatives = make_listening(args.renders, manifest, groups, listening, args.production_renders,
                                 production_groups, args.previous_renders, previous_groups)
    report = {"schema": 1, "native_renders": str(args.renders.resolve()),
              "manifest_sha256": sha256(args.renders / "manifest.json"), "provenance": provenance,
              "previous_byte_parity": previous, "renders": renders,
              "measurements": measurements(args.renders, groups), "listening": derivatives,
              "production_render_root": str(args.production_renders.resolve()) if args.production_renders else None,
              "production_provenance": production_provenance, "production_validation": production_validation,
              "production_renders": production_renders,
              "production_measurements": production_measurements(args.previous_renders, previous_groups,
                  args.production_renders, production_groups, manifest["sample_rate"]) if args.production_renders else None,
              "native_loudness_meter": "BS.1770-4 via MeasureMaterialLoudness.py; non-48k audio polyphase-resampled to48k for metering only",
              "interpretation": "Difference measurements and validated renders do not establish listener preference. "
              "All variants use identical MIDI and controls. RMS matching is for listening derivatives only; "
              "native levels are retained. The synthetic test bed is not a real recording reference."}
    (args.output / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    rows = ["Acustra natural-performance audition", "", report["interpretation"], "",
            "Before/after: baseline first, one second of silence, combined second.",
            "Ablations: " + ", ".join(variant_order(manifest)) + "; one second between each.",
            "Release passages: short/long holds, then repeat with release velocities 16, 64 and 120.",
            "All native input scores, float audio, levels and derivative gains are in validation.json.", ""]
    if args.production_renders:
        rows[3:3] = ["Primary before/after and test mixes: actual previous production versus current production calibration.",
                     "Ablation reels: isolated mechanisms, all using the previous construction trims.",
                     "Production-native peaks, RMS and LUFS shifts are retained separately in validation.json.", ""]
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
