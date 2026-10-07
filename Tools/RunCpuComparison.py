#!/usr/bin/env python3
"""Run the exact-audio CPU matrix on a frozen dual-build benchmark.

First use --untimed-check. Run timings only after coordinating an idle machine;
the runner cannot detect other builds or render jobs. Results describe hot native
callbacks, not host overhead or a hard real-time guarantee.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time


def positive_integer(value):
    result = int(value)
    if not 1 <= result <= 100000:
        raise argparse.ArgumentTypeError("must be between 1 and 100000")
    return result


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--pairs", type=positive_integer, default=128)
    parser.add_argument("--warmups", type=positive_integer, default=16)
    parser.add_argument("--cpu", type=int)
    parser.add_argument("--untimed-check", action="store_true")
    parser.add_argument("--flush-denormals", action="store_true",
        help="use the x86 FTZ/DAZ callback mode used by JUCE for both builds")
    args = parser.parse_args()
    if args.output.exists():
        parser.error("output already exists; preserve earlier evidence")
    if not hasattr(os, "sched_getaffinity"):
        parser.error("this coordinated timing runner requires Linux CPU affinity")
    allowed = os.sched_getaffinity(0)
    cpu = min(allowed) if args.cpu is None else args.cpu
    if cpu not in allowed:
        parser.error("selected CPU is outside the process affinity")
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True)
    os.sched_setaffinity(0, {cpu})

    jobs = []
    for technique in ("finger", "thumb", "pick"):
        for capture in ("stereo", "mono", "piezo"):
            jobs.append((f"{technique}-{capture}", ["--technique", technique,
                "--capture", capture, "--natural-performance"]))
        jobs.append((f"{technique}-controls", ["--technique", technique,
            "--include-44100", "--controls-only"]))
        jobs.append((f"{technique}-transitions", ["--technique", technique,
            "--include-44100", "--transitions-only"]))
    jobs.extend([
        ("finger-hard", ["--technique", "finger", "--velocity", "127",
            "--touch", "1", "--natural-performance"]),
        ("finger-soft", ["--technique", "finger", "--velocity", "32",
            "--touch", "0", "--natural-performance"]),
        ("legacy-default", []),
    ])
    manifest = {
        "affinity_cpu": cpu,
        "pairs_per_case": 0 if args.untimed_check else args.pairs,
        "warmup_pairs": 0 if args.untimed_check else args.warmups,
        "untimed_validation_only": args.untimed_check,
        "denormal_mode": "x86_ftz_daz" if args.flush_denormals else "inherited",
        "require_equal_output_hashes": True,
        "runner_sha256": digest(Path(__file__)),
        "started_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "binary": str(binary), "binary_sha256": digest(binary),
        "timing_scope": "Alternating paired hot native callbacks. Preparation, "
            "preroll, snapshot restoration, validation and reporting are excluded. "
            "Both source versions must be frozen and compiled with matching flags. "
            "Other builds, tests and renders must be idle during timing.",
        "jobs": [], "complete": False,
    }
    manifest_path = args.output / "cpu-run-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
    cases = 0
    for name, options in jobs:
        output = args.output / f"cpu-{name}.json"
        command = [str(binary), str(output), str(args.pairs), str(args.warmups),
            *options]
        if args.untimed_check:
            command.append("--untimed-check")
        if args.flush_denormals:
            command.append("--flush-denormals")
        with (args.output / f"cpu-{name}.log").open("w") as log:
            subprocess.run(command, check=True, stdout=log, stderr=subprocess.STDOUT)
        result = json.loads(output.read_text())
        if not result.get("complete") or not result.get("results"):
            raise RuntimeError(f"incomplete benchmark: {name}")
        if result["untimed_validation_only"] != args.untimed_check:
            raise RuntimeError(f"wrong timing mode: {name}")
        if result["denormal_mode"] != manifest["denormal_mode"]:
            raise RuntimeError(f"wrong denormal mode: {name}")
        for row in result["results"]:
            if args.untimed_check:
                equal = row["baseline_output_fnv64"] == row["current_output_fnv64"]
                if not row["untimed_checks_passed"] or result["measured_pairs"] != 0:
                    raise RuntimeError(f"untimed state check failed: {name}")
            else:
                equal = row["baseline"]["output_fnv64"] == row["current"]["output_fnv64"]
                if not row["bit_identical"]:
                    equal = False
                for version in ("baseline", "current"):
                    if len(row[version]["samples_us"]) != args.pairs:
                        raise RuntimeError(f"incomplete trial count: {name}")
            if not equal:
                raise RuntimeError(f"CPU optimization changed output: {name}, "
                    f"{row['rate']}/{row['frames']}/{row['guitar_model']}/{row['scenario']}")
            if name.endswith("-transitions"):
                proof = row["intrinsic_ramp_proof"]
                for version in ("baseline", "current"):
                    active = proof[f"{version}_active_sections"]
                    if active < 0 or (active > 0) != proof["expected_active"]:
                        raise RuntimeError(f"transition state was not exercised: {name}")
        cases += len(result["results"])
        manifest["jobs"].append({"name": name, "command": command,
            "cases": len(result["results"]), "file": output.name,
            "sha256": digest(output), "equal_output_hashes": True})
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        print(f"Finished {name}: {len(result['results'])} exact-output cases", flush=True)
    if cases != 1030:
        raise RuntimeError(f"unexpected matrix size: {cases}")
    if digest(binary) != manifest["binary_sha256"]:
        raise RuntimeError("benchmark binary changed during execution")
    manifest.update({"cases": cases, "complete": True,
        "ended_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())})
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
