#!/usr/bin/env python3
"""Preserved, sequential native timing of two immutable DSP snapshots.

Prepare without timing while waiting for a coordinated quiet run:
  python3 Tools/RunRealismQuickWinsBenchmark.py prepare BASELINE_DSP CANDIDATE_DSP NEW_OUTPUT
  python3 Tools/RunRealismQuickWinsBenchmark.py run NEW_OUTPUT

Or compile both and immediately execute each once:
  python3 Tools/RunRealismQuickWinsBenchmark.py compare BASELINE_DSP CANDIDATE_DSP NEW_OUTPUT

DSP inputs are directories containing AcustraEngine.cpp and AcustraPerformer.cpp.
An existing output directory is never reused for prepare/compare, and a timing
run is never repeated. A failed attempt keeps its logs and partial evidence.
This is native callback timing, not a Reason/Recon scheduling guarantee.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time


FLAGS = ["-std=c++17", "-O3", "-DNDEBUG", "-Wall", "-Wextra", "-Wpedantic"]
VERSIONS = ("baseline", "candidate")


def utc() -> str:
    return datetime.now(timezone.utc).isoformat()


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inventory(root: Path) -> dict[str, str]:
    result = {}
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"snapshot must contain ordinary files, not symlink: {path}")
        if path.is_file():
            result[str(path.relative_to(root))] = digest(path)
    return result


def write_json(path: Path, data) -> None:
    path.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n")


def record(output: Path, event: str, **fields) -> None:
    with (output / "history.jsonl").open("a") as stream:
        stream.write(json.dumps({"utc": utc(), "event": event, **fields}) + "\n")


def prepare(baseline: Path, candidate: Path, output: Path, compiler_name: str) -> None:
    if output.exists():
        raise ValueError(f"preserve prior evidence: output already exists: {output}")
    originals = {"baseline": baseline.resolve(), "candidate": candidate.resolve()}
    for source in originals.values():
        if source == output or source in output.parents:
            raise ValueError("output must be outside both DSP source directories")
        for filename in ("AcustraEngine.cpp", "AcustraPerformer.cpp", "AcustraPerformer.h"):
            if not (source / filename).is_file():
                raise ValueError(f"DSP directory is missing {filename}: {source}")
    compiler = shutil.which(compiler_name)
    if compiler is None:
        raise ValueError(f"compiler is unavailable: {compiler_name}")
    # Preserve the driver's basename: on some systems clang++ is a symlink
    # to clang, whose argv[0] selects different C++ link defaults.
    compiler = os.path.abspath(compiler)
    output.mkdir(parents=True)
    record(output, "prepare_started")
    script = Path(__file__).resolve()
    harness = script.with_name("BenchmarkRealismQuickWins.cpp")
    shutil.copy2(harness, output / harness.name)
    shutil.copy2(script, output / script.name)
    manifest = {
        "schema": 1, "created_utc": utc(), "compiler_path": compiler,
        "compiler_real_path": str(Path(compiler).resolve()),
        "compiler_version": subprocess.check_output([compiler, "--version"], text=True),
        "flags": FLAGS, "platform": platform.platform(), "machine": platform.machine(),
        "processor": platform.processor(), "logical_cpus": os.cpu_count(),
        "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
        "harness_sha256": digest(output / harness.name),
        "runner_sha256": digest(output / script.name),
        "original_sources": {key: str(value) for key, value in originals.items()},
        "source_files_sha256": {}, "compile_commands": {}, "binary_sha256": {},
        "compiled": False, "run_order": list(VERSIONS), "runs_per_revision": 1,
        "method_limit": "One sequential native run per revision; fixed baseline-then-candidate order. No isolated-core or Reason host deadline guarantee; wall timing can include preemption. Thread CPU reports charged execution. Timer overhead is retained, not subtracted.",
    }
    write_json(output / "manifest.json", manifest)
    for version, original in originals.items():
        before = inventory(original)
        snapshot = output / "sources" / version / "DSP"
        snapshot.parent.mkdir(parents=True)
        shutil.copytree(original, snapshot)
        if inventory(snapshot) != before or inventory(original) != before:
            raise RuntimeError(f"{version} source changed while copying; preserved incomplete attempt")
        manifest["source_files_sha256"][version] = before
    write_json(output / "manifest.json", manifest)
    for version in VERSIONS:
        source = output / "sources" / version / "DSP"
        command = [compiler, *FLAGS, "-I", str(source.parent), str(output / harness.name),
                   str(source / "AcustraEngine.cpp"), str(source / "AcustraPerformer.cpp"),
                   "-o", str(output / f"benchmark-{version}")]
        manifest["compile_commands"][version] = command
        write_json(output / "manifest.json", manifest)
        record(output, "compile_started", version=version, command=command)
        with (output / f"compile-{version}.log").open("x") as log:
            status = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=False).returncode
        record(output, "compile_finished", version=version, exit_code=status)
        if status != 0:
            raise RuntimeError(f"{version} compile failed; see compile-{version}.log")
        manifest["binary_sha256"][version] = digest(output / f"benchmark-{version}")
    manifest["compiled"] = True
    write_json(output / "manifest.json", manifest)
    record(output, "prepare_finished")
    print(f"Prepared identical C++17/O3 binaries without timing: {output}", flush=True)


def compare_summaries(output: Path) -> None:
    summaries = {version: json.loads((output / version / "summary.json").read_text())
                 for version in VERSIONS}
    if not all(summary["complete"] for summary in summaries.values()):
        raise RuntimeError("incomplete timing attempt; partial files retained without comparison")
    first = {(row["workload"], row["sample_rate"]): row for row in summaries["baseline"]["results"]}
    second = {(row["workload"], row["sample_rate"]): row for row in summaries["candidate"]["results"]}
    if first.keys() != second.keys() or len(first) != 8:
        raise RuntimeError("baseline and candidate workload sets differ")
    if digest(output / "baseline" / "events.csv") != digest(output / "candidate" / "events.csv"):
        raise RuntimeError("baseline and candidate public scores differ")
    comparison = {"schema": 1, "complete": True, "run_order": list(VERSIONS),
                  "reruns": 0, "identical_event_score": True, "results": []}
    lines = ["Native 64-frame callback timing; one sequential run per revision.",
             "Eight seconds per workload; setup excluded; timer overhead retained.",
             "No deadline-based pass/fail or automatic rerun. Wall includes possible preemption.",
             "These results do not establish Reason/Recon scheduling or unclipped outputs.", "",
             "workload | rate | clock | before p50/p95/p99/max us | after p50/p95/p99/max us | deadline exceedances before/after"]
    for key, before in first.items():
        after = second[key]
        if before["blocks"] != after["blocks"] or before["events"] != after["events"]:
            raise RuntimeError("callback/event accounting differs between revisions")
        row = {"workload": key[0], "sample_rate": key[1], "blocks": before["blocks"],
               "baseline": before, "candidate": after, "candidate_over_baseline": {}}
        for clock in ("wall", "thread_cpu"):
            row["candidate_over_baseline"][clock] = {
                stat: after[clock][stat] / before[clock][stat]
                for stat in ("p50_us", "p95_us", "p99_us", "max_us", "total_ns")}
            fmt = lambda item: "/".join(f"{item[clock][stat]:.3f}" for stat in
                                        ("p50_us", "p95_us", "p99_us", "max_us"))
            lines.append(f"{key[0]} | {key[1]} | {clock} | {fmt(before)} | {fmt(after)} | "
                         f"{before[clock]['deadline_exceedances']}/{after[clock]['deadline_exceedances']}")
        comparison["results"].append(row)
    write_json(output / "comparison.json", comparison)
    (output / "comparison.txt").write_text("\n".join(lines) + "\n")


def run_prepared(output: Path) -> None:
    manifest = json.loads((output / "manifest.json").read_text())
    if not manifest["compiled"]:
        raise ValueError("both binaries must have compiled successfully first")
    for version in VERSIONS:
        if inventory(output / "sources" / version / "DSP") != manifest["source_files_sha256"][version]:
            raise ValueError(f"preserved {version} DSP snapshot changed")
        if digest(output / f"benchmark-{version}") != manifest["binary_sha256"][version]:
            raise ValueError(f"preserved {version} executable changed")
    if digest(output / "BenchmarkRealismQuickWins.cpp") != manifest["harness_sha256"]:
        raise ValueError("preserved harness changed")
    # Atomic exclusive marker prevents any second attempt, including after a
    # killed or failed run. A later investigation must use a new output path.
    with (output / "run-started.json").open("x") as stream:
        json.dump({"utc": utc(), "run_order": list(VERSIONS)}, stream)
        stream.write("\n")
    statuses = {}
    for version in VERSIONS:
        record(output, "timing_started", version=version)
        start = time.monotonic()
        with (output / f"run-{version}.log").open("x") as log:
            try:
                status = subprocess.run([str(output / f"benchmark-{version}"), str(output / version)],
                                        stdout=log, stderr=subprocess.STDOUT, timeout=600,
                                        check=False).returncode
            except subprocess.TimeoutExpired:
                status = "timeout"
        statuses[version] = status
        record(output, "timing_finished", version=version, exit_code=status,
               elapsed_process_seconds=time.monotonic() - start)
        print(f"{version}: {status}; preserved {output / version}", flush=True)
    write_json(output / "run-status.json", {"utc": utc(), "exit_codes": statuses})
    if any(status != 0 for status in statuses.values()):
        raise RuntimeError("a benchmark failed; all attempt logs/results preserved; no rerun")
    compare_summaries(output)
    record(output, "comparison_finished")
    print(f"Preserved timing comparison: {output / 'comparison.txt'}", flush=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="mode", required=True)
    for mode in ("prepare", "compare"):
        sub = subparsers.add_parser(mode)
        sub.add_argument("baseline_dsp", type=Path)
        sub.add_argument("candidate_dsp", type=Path)
        sub.add_argument("output", type=Path)
        sub.add_argument("--compiler", default="clang++")
    subparsers.add_parser("run").add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    try:
        if args.mode in ("prepare", "compare"):
            prepare(args.baseline_dsp, args.candidate_dsp, output, args.compiler)
        if args.mode in ("run", "compare"):
            run_prepared(output)
    except Exception as error:
        if output.is_dir():
            record(output, "command_failed", mode=args.mode, error=str(error))
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
