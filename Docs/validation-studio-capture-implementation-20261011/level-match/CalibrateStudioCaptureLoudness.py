#!/usr/bin/env python3
"""Restore each microphone route's prior phrase loudness after its spatial blend.

Compare frozen pre-blend and raw-blend renderers using the unchanged seeded
phrase and hardest-case score from CalibrateConstructionLoudness. The 72 cells
are 4 shapes x 3 woods x 3 techniques x 2 microphone routes. Each generated
float32 factor is 10**((old LUFS - new LUFS)/20). There is no per-note fit, EQ,
headroom-driven gain adjustment, or special default-cell constraint. Legacy
construction references and the Piezo chain must stay untouched.

The report includes both versions' hardest-case peaks for every cell and the
predicted compensated peak before the safety limiter. Prediction inverts the
existing limiter before applying the flat gain; it is not a final-engine test.
Final gains must be rebuilt and checked by the independent construction tests.

  python3 Tools/CalibrateStudioCaptureLoudness.py \
    --baseline-renderer BEFORE --blend-renderer RAW_BLEND \
    --json build-audit/studio-level-match/report.json --write-header --jobs 2
  python3 Tools/CalibrateStudioCaptureLoudness.py \
    --measurements build-audit/studio-level-match/report.json --write-header
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import scipy

import CalibrateConstructionLoudness as protocol

ROOT = Path(__file__).resolve().parent.parent
HEADER = ROOT / "Source/DSP/StudioCaptureLoudnessData.h"
CAPTURES = ("stereo_mic", "mono_mic")
SCHEMA = "AcustraStudioCaptureLevelMatchV1"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def gain(old, new):
    return float(np.float32(10.0 ** ((old - new) / 20.0)))


def measure(baseline, blend, jobs):
    start = time.monotonic()
    binaries = {"baseline": Path(baseline).resolve(), "raw_blend": Path(blend).resolve()}
    binary_hashes = {name: sha(path) for name, path in binaries.items()}
    env = {k: v for k, v in os.environ.items() if not k.startswith("ACUSTRA_STUDIO_")}
    with tempfile.TemporaryDirectory(prefix="acustra-studio-level-match-") as temp:
        work = Path(temp)
        protocol.meter.write_performance(*protocol.phrase_set(), work / "phrase.txt")
        protocol.meter.write_performance(*protocol.hardest_case(), work / "hardest.txt")
        scores = {name: {"text": (work / (name + ".txt")).read_text(),
                         "sha256": sha(work / (name + ".txt"))}
                  for name in ("phrase", "hardest")}

        def one(job):
            shape, wood, picking, capture = job
            key = "-".join(job)
            versions = {}
            for version, binary in binaries.items():
                result = {}
                for name in ("phrase", "hardest"):
                    output = work / (key + "-" + version + "-" + name + ".f32")
                    cmd = [str(binary), str(work / (name + ".txt")), str(output),
                           capture, picking, "--body-shape", shape,
                           "--body-material", wood, "--room", "0"]
                    if name == "hardest":
                        cmd += ["--touch", "1", "--pluck-position", "0"]
                    subprocess.run(cmd, env=env, check=True, capture_output=True)
                    audio = np.fromfile(output, dtype="<f4").reshape(-1, 2)
                    if not np.isfinite(audio).all() or not np.any(audio):
                        raise RuntimeError("invalid audio: " + key + " " + version)
                    absolute = protocol.unlimited(audio)
                    item = {"audio_sha256": sha(output), "frames": len(audio),
                            "peak_dbfs": float(20 * np.log10(np.max(np.abs(audio)))),
                            "prelimiter_peak_dbfs": float(20 * np.log10(np.max(absolute))),
                            "limiter_active_samples": int(np.sum(np.abs(audio) > protocol.LIMIT_KNEE))}
                    if name == "phrase":
                        item["lufs"] = protocol.meter.integrated_loudness(audio)
                    result[name] = item
                    output.unlink()
                versions[version] = result
            old = versions["baseline"]["phrase"]["lufs"]
            new = versions["raw_blend"]["phrase"]["lufs"]
            factor = gain(old, new)
            db = float(20 * np.log10(factor))
            return {"shape": shape, "wood": wood, "picking": picking, "capture": capture,
                    "cell": protocol.index(shape, wood, picking), "versions": versions,
                    "level_match_gain": factor, "level_match_db": db,
                    "predicted_phrase_error_lu": new + db - old,
                    "predicted_hardest_prelimiter_peak_dbfs":
                        versions["raw_blend"]["hardest"]["prelimiter_peak_dbfs"] + db}

        grid = list(itertools.product(protocol.SHAPES, protocol.WOODS, protocol.PICKINGS, CAPTURES))
        rows = []
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            for row in pool.map(one, grid):
                rows.append(row)
                if len(rows) % 12 == 0:
                    print(f"Measured {len(rows)}/{len(grid)} paired cells in {time.monotonic()-start:.1f}s", flush=True)
        assert binary_hashes == {name: sha(path) for name, path in binaries.items()}, "renderer changed"
    worst = max(rows, key=lambda row: row["predicted_hardest_prelimiter_peak_dbfs"])
    return {"schema": SCHEMA, "argv": sys.argv, "seed": protocol.SEED,
            "calibration_policy": "Independent prior seeded phrase; flat prior/raw-blend LUFS ratio per construction, technique and microphone route. No fit to ConstructionLoudnessTests, notes or resonance metrics.",
            "controls": {"sample_rate": protocol.RATE, "room": 0, "other_controls": "renderer defaults; hardest score Touch=1, Pluck Position=0, all three techniques"},
            "binary_paths": {k: str(v) for k, v in binaries.items()}, "binary_sha256": binary_hashes,
            "source_sha256": {str(path.relative_to(ROOT)): sha(path) for path in
                              (Path(__file__).resolve(), Path(protocol.__file__).resolve(),
                               Path(protocol.meter.__file__).resolve())},
            "runtime": {"python": sys.version, "platform": platform.platform(),
                        "numpy": np.__version__, "scipy": scipy.__version__},
            "scores": scores, "paired_cells": len(rows), "native_renders": len(rows) * 4,
            "wall_seconds": time.monotonic() - start, "jobs": jobs, "rows": rows,
            "summary": {"gain_db_range": [min(r["level_match_db"] for r in rows), max(r["level_match_db"] for r in rows)],
                        "max_abs_float32_phrase_error_lu": max(abs(r["predicted_phrase_error_lu"]) for r in rows),
                        "worst_compensated_hardest": {k: worst[k] for k in ("shape", "wood", "picking", "capture", "predicted_hardest_prelimiter_peak_dbfs")},
                        "compensated_hardest_above_minus2_dbfs": [{k: r[k] for k in ("shape", "wood", "picking", "capture", "predicted_hardest_prelimiter_peak_dbfs")} for r in rows if r["predicted_hardest_prelimiter_peak_dbfs"] > -2],
                        "phrase_limiter_active_samples": sum(r["versions"][v]["phrase"]["limiter_active_samples"] for r in rows for v in ("baseline", "raw_blend"))},
            "limitations": ["Headroom after compensation is predicted by exact inverse limiter and flat multiplication; rebuild and native final validation remain required.",
                            "BS.1770 gates may change after a gain. Float32 phrase-error arithmetic is a prediction, not an independent re-render.",
                            "No geometry weights, modal poles, mechanical state, EQ or Piezo level is adjusted."]}


def header_text(report):
    if report["schema"] != SCHEMA or len(report["rows"]) != 72:
        raise ValueError("wrong measurement schema or cell count")
    rows = report["rows"]
    lines = ["// Generated by Tools/CalibrateStudioCaptureLoudness.py; do not edit by hand.",
             "// Flat microphone-only factors restore each legacy construction/style/route's",
             "// integrated loudness on the existing fixed calibration phrase (seed 20260929).",
             "// Index: (shape * 3 + wood) * 3 + picking. Legacy Piezo references bypass these.",
             "// Baseline renderer SHA256: " + report["binary_sha256"]["baseline"],
             "// Raw-blend renderer SHA256: " + report["binary_sha256"]["raw_blend"],
             "#pragma once", "", "#include <array>", "", "namespace acustra::detail", "{"]
    for capture, symbol in (("stereo_mic", "studioStereoLevelMatch"), ("mono_mic", "studioMonoLevelMatch")):
        selected = sorted((r for r in rows if r["capture"] == capture), key=lambda r: r["cell"])
        assert [r["cell"] for r in selected] == list(range(36))
        values = [gain(r["versions"]["baseline"]["phrase"]["lufs"], r["versions"]["raw_blend"]["phrase"]["lufs"]) for r in selected]
        lines.append("inline constexpr std::array<float, 36> " + symbol + " {{")
        for i, shape in enumerate(protocol.SHAPES):
            lines.append("    // " + shape + ": spruce, mahogany, maple x finger, pick, thumb")
            tokens = [format(v, ".9g") for v in values[i*9:(i+1)*9]]
            tokens = [(v if "." in v or "e" in v else v + ".0") + "f" for v in tokens]
            lines.append("    " + ", ".join(tokens) + ("," if i < 3 else ""))
        lines.append("}};")
    lines += ["} // namespace acustra::detail", ""]
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-renderer", type=Path)
    parser.add_argument("--blend-renderer", type=Path)
    parser.add_argument("--measurements", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--jobs", type=int, choices=(1, 2), default=1)
    parser.add_argument("--write-header", action="store_true")
    args = parser.parse_args()
    if args.measurements:
        report = json.loads(args.measurements.read_text())
    else:
        if not args.baseline_renderer or not args.blend_renderer:
            parser.error("both frozen renderers or --measurements required")
        report = measure(args.baseline_renderer, args.blend_renderer, args.jobs)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    if args.write_header:
        HEADER.write_text(header_text(report))
        print("Wrote", HEADER)
    print(json.dumps(report["summary"], indent=2))


if __name__ == "__main__":
    main()
