#!/usr/bin/env python3
"""Package paired recording descriptors and explicit regression results.

This reads existing BenchmarkOpenCorpora outputs; it does not render, fit,
rescore, or decide listener approval. Regression JSON files record exact exit
statuses, and remain separate evidence from the recording comparisons.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


INTERPRETATION = (
    "Lower descriptor loss means closer measured features on the same recordings. "
    "It is not a perceptual realism score, listening preference, or approval."
)
LIMITATIONS = [
    "These are transfers between different guitars and capture chains, not exact recordings of either modeled guitar.",
    "Eastman and Martin velocities are assumed to be 91; Martin's picking tool is unknown and rendered as Finger.",
    "These recordings have informed previous development and are not an untouched final test set.",
    "Reference pre-roll, source tuning offsets, and unequal clean durations contribute to descriptor losses.",
    "The isolated-note renderer starts a fresh engine per unique tool/pitch/velocity and shares it across recorded repeats.",
    "Static note descriptors do not evaluate consecutive plucks, strum timing, legato, or explicit release gestures.",
    "Physical and event regressions establish their declared behaviors; they do not establish audible realism or recording-calibrated gesture laws.",
    "Final aggregate comparisons cannot attribute a loss change to any one of the ten commits.",
]
SOURCE_FILES = (
    "Source/DSP/AcustraEngine.cpp", "Source/DSP/AcustraEngine.h",
    "Source/DSP/AcustraPerformer.cpp", "Source/DSP/AcustraPerformer.h",
    "Tools/ExternalCorpusRenderer.cpp", "Tools/BenchmarkOpenCorpora.py",
    "Tools/FitPhysicalModel.py", "Tools/OptimizePhysicalModel.py",
    "CMakeLists.txt",
)


class Evidence:
    def __init__(self) -> None:
        self.files: dict[str, dict] = {}

    def artifact(self, path: Path) -> dict:
        path = path.resolve()
        key = str(path)
        if key not in self.files:
            digest = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(block)
            self.files[key] = {
                "path": key, "bytes": path.stat().st_size,
                "sha256": digest.hexdigest(),
            }
        return self.files[key]

    def read(self, path: Path) -> dict:
        self.artifact(path)
        def invalid(value: str):
            raise ValueError(f"{path}: nonfinite JSON number {value}")
        document = json.loads(path.read_text(encoding="utf-8"), parse_constant=invalid)
        if not isinstance(document, dict):
            raise ValueError(f"{path}: expected a JSON object")
        return document

    @staticmethod
    def resolve_spec(spec: dict | str, directory: Path) -> Path:
        path = Path(spec if isinstance(spec, str) else spec["path"])
        return path if path.is_absolute() else directory / path

    def run(self, directory: Path) -> dict:
        directory = directory.resolve()
        summary = self.read(directory / "summary.json")
        renderer = self.artifact(Path(summary["renderer"]["path"]))
        if renderer["sha256"] != summary["renderer"]["sha256"]:
            raise ValueError(f"{directory}: renderer changed since measurement")
        sources = {}
        cache = Path(renderer["path"]).parent / "CMakeCache.txt"
        if cache.is_file():
            self.artifact(cache)
            for line in cache.read_text(encoding="utf-8").splitlines():
                if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL="):
                    source = Path(line.split("=", 1)[1])
                    sources = {name: self.artifact(source / name)
                               for name in SOURCE_FILES if (source / name).is_file()}
                    break
        source_documents = {}
        for corpus in summary["corpora"]:
            rows = Path(corpus["rows_json"])
            if self.artifact(rows)["sha256"] != corpus["rows_json_sha256"]:
                raise ValueError(f"{rows}: corpus rows changed since measurement")
            for name in ("SOURCE.md", "analysis.json", "regions.json"):
                if (rows.parent / name).is_file():
                    self.artifact(rows.parent / name)
            if (rows.parent / "SOURCE.md").is_file():
                source_documents[corpus["corpus"]] = {
                    "artifact": self.artifact(rows.parent / "SOURCE.md"),
                    "recorded_provenance": (rows.parent / "SOURCE.md").read_text(encoding="utf-8"),
                }
        manifests = {}
        for split, score in summary["splits"].items():
            manifest_path = directory / f"{split}.json"
            manifest = self.read(manifest_path)
            if len(manifest["examples"]) != score["example_count"]:
                raise ValueError(f"{manifest_path}: scored row count differs")
            examples = {}
            for row in manifest["examples"]:
                if row["id"] in examples:
                    raise ValueError(f"{manifest_path}: duplicate example ID")
                examples[row["id"]] = {
                    "metadata": {key: value for key, value in row.items()
                                 if key not in ("target", "model")},
                    "target_format": {key: value for key, value in row["target"].items()
                                      if key != "path"},
                    "target": self.artifact(self.resolve_spec(row["target"], directory)),
                    "model": self.artifact(self.resolve_spec(row["model"], directory)),
                }
            manifests[split] = {
                "artifact": self.artifact(manifest_path),
                "analysis_sample_rate": manifest["analysis_sample_rate"],
                "recorded_metadata": {key: value for key, value in manifest.items()
                                      if key != "examples"},
                "examples": examples,
            }
        return {"directory": str(directory), "summary": summary,
                "build_source_files_at_report_time": sources,
                "reference_source_documents": source_documents, "manifests": manifests}

    def pair(self, baseline_directory: Path, candidate_directory: Path) -> dict:
        baseline, candidate = self.run(baseline_directory), self.run(candidate_directory)
        summaries = [run["summary"] for run in (baseline, candidate)]
        if summaries[0]["splits"].keys() != summaries[1]["splits"].keys():
            raise ValueError("paired runs have different scored splits")
        if summaries[0]["renderer"]["model_controls"] != summaries[1]["renderer"]["model_controls"]:
            raise ValueError("paired runs have different rendering controls")
        if summaries[0]["calibration_values"] != summaries[1]["calibration_values"]:
            raise ValueError("paired runs have different calibration values")
        for field in ("calibration_order", "picking"):
            if summaries[0].get(field) != summaries[1].get(field):
                raise ValueError(f"paired runs have different {field}")
        corpus_hashes = [{item["corpus"]: item["rows_json_sha256"]
                          for item in summary["corpora"]} for summary in summaries]
        if corpus_hashes[0] != corpus_hashes[1]:
            raise ValueError("paired runs have different corpus row hashes")
        comparisons = {}
        for split in summaries[0]["splits"]:
            rows = [run["manifests"][split] for run in (baseline, candidate)]
            if rows[0]["analysis_sample_rate"] != rows[1]["analysis_sample_rate"]:
                raise ValueError(f"{split}: analysis rates differ")
            if rows[0]["examples"].keys() != rows[1]["examples"].keys():
                raise ValueError(f"{split}: scored example IDs differ")
            audio_pairs = {}
            for identifier, first in rows[0]["examples"].items():
                second = rows[1]["examples"][identifier]
                if (first["metadata"] != second["metadata"]
                        or first["target_format"] != second["target_format"]
                        or first["target"]["sha256"] != second["target"]["sha256"]):
                    raise ValueError(f"{split}/{identifier}: paired reference differs")
                audio_pairs[first["model"]["path"], second["model"]["path"]] = (
                    first["model"]["sha256"] == second["model"]["sha256"])
            comparison_path = candidate_directory / f"{split}-compare.json"
            comparison = self.read(comparison_path)
            if "error" in comparison:
                raise ValueError(f"{comparison_path}: paired scorer reported an error")
            aggregate = comparison["aggregate"]
            if aggregate["example_count"] != len(rows[0]["examples"]):
                raise ValueError(f"{split}: paired scorer row count differs")
            for side, summary in zip(("baseline", "candidate"), summaries):
                if not math.isclose(aggregate["score"][side], summary["splits"][split]["score"],
                                    rel_tol=1e-10, abs_tol=1e-10):
                    raise ValueError(f"{split}: paired loss does not match {side} summary")
            counts = {name: term["baseline_count"] == term["candidate_count"]
                      for name, term in aggregate["terms"].items()}
            for name, term in aggregate["terms"].items():
                for side, summary in zip(("baseline", "candidate"), summaries):
                    if term[f"{side}_count"] != summary["splits"][split]["term_counts"][name]:
                        raise ValueError(f"{split}/{name}: paired term count differs from {side} summary")
            comparisons[split] = {
                "paired_scorer_report": comparison,
                "paired_report_artifact": self.artifact(comparison_path),
                "term_coverage_unchanged": counts,
                "unique_static_audio_pairs": len(audio_pairs),
                "byte_identical_static_audio_pairs": sum(audio_pairs.values()),
                "static_audio_changed": not all(audio_pairs.values()),
            }
        return {"baseline": baseline, "candidate": candidate,
                "paired_comparisons": comparisons}

    def regressions(self, directory: Path) -> dict:
        records, improvements = [], {}
        if not directory.is_dir():
            raise ValueError(f"{directory}: regression log directory is missing")
        for path in sorted(directory.glob("*.json")):
            result = self.read(path)
            improvement = result.get("improvement")
            if improvement is not None:
                if type(improvement) is not int or improvement not in range(1, 11):
                    raise ValueError(f"{path}: improvement must be an integer from 1 to 10")
                if improvement in improvements:
                    raise ValueError(f"{path}: duplicate improvement {improvement}")
                improvements[improvement] = result.get("returncode")
            returncode = result.get("returncode")
            if returncode is not None and type(returncode) is not int:
                raise ValueError(f"{path}: returncode must be an integer")
            artifacts = {}
            references = {"stdout_file": result.get("stdout_file")}
            prior = result.get("baseline_result")
            if isinstance(prior, dict):
                references["baseline_log_file"] = prior.get("log_file")
            for key, value in references.items():
                if value:
                    artifacts[key] = self.artifact(self.resolve_spec(str(value), path.parent))
            records.append({
                "artifact": self.artifact(path), "recorded_result": result,
                "referenced_logs": artifacts,
                "candidate_exit_status": ("not_recorded" if returncode is None else
                                          "passed" if returncode == 0 else "failed"),
            })
        return {
            "interpretation": "Explicit exit statuses and physical/event behavior checks; separate from recording descriptor and listening evidence.",
            "directory": str(directory.resolve()), "records": records,
            "expected_improvement_count": 10,
            "recorded_improvements": sorted(improvements),
            "missing_improvements": sorted(set(range(1, 11)) - improvements.keys()),
            "all_ten_candidate_regressions_passed": (
                len(improvements) == 10 and all(code == 0 for code in improvements.values())),
        }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("baseline-original", "candidate-original", "baseline-bellido",
                 "candidate-bellido", "regression-log-dir", "out"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    parser.add_argument("--repeat-report", type=Path,
                        help="optional measured Eastman repeat-level-summary.json")
    parser.add_argument("--ctest-run", type=Path,
                        help="optional exact CTest command/exit-status sidecar with --output-junit")
    parser.add_argument("--browser-report", type=Path,
                        help="optional listening-review browser test report; never human approval")
    parser.add_argument("--listening-manifest", type=Path,
                        help="optional final listening manifest; verify its packaged audio hashes")
    parser.add_argument("--reference-provenance", type=Path,
                        help="optional frozen prepared-reference hashes and master verification")
    args = parser.parse_args()
    if args.out.exists():
        parser.error("--out must be a new file")
    try:
        evidence = Evidence()
        models = {
            "original": evidence.pair(args.baseline_original, args.candidate_original),
            "bellido1978": evidence.pair(args.baseline_bellido, args.candidate_bellido),
        }
        regressions = evidence.regressions(args.regression_log_dir)
        repeat_path = args.repeat_report
        if repeat_path is None:
            eastman = next((corpus for corpus in models["original"]["baseline"]["summary"]["corpora"]
                            if corpus["corpus"] == "eastman"), None)
            if eastman is not None:
                repeat_path = Path(eastman["rows_json"]).parent / "repeat-level-summary.json"
        repeats = {
            "interpretation": "Observed variation, not a controlled-force or contact-noise calibration. Actual velocities and string/fret assignments are unknown.",
            "reported_20_to_250_ms_median_within_group_sd_db": {"finger": 1.09, "pick": 1.47},
            "reported_repeated_pitch_group_count": {"finger": 9, "pick": 9},
            "reported_source_rows_sha256": "78b03c69d3c2de85201446fe65e291093f5d35e6e5eff43406af484a29e7ba2f",
            "measurement_status": "Previously measured during this task; source report unavailable here.",
        }
        if repeat_path is not None and repeat_path.is_file():
            repeats.update({"measurement_status": "Existing measured report retained; not recomputed.",
                            "artifact": evidence.artifact(repeat_path),
                            "recorded_measurement": evidence.read(repeat_path)})
        completion = {}
        if args.ctest_run:
            result = evidence.read(args.ctest_run)
            command = result["command"]
            junit = Path(command[command.index("--output-junit") + 1])
            cases = ET.parse(junit).getroot().findall(".//testcase")
            counts = {"executed": len(cases),
                      "failed": sum(case.find("failure") is not None or case.find("error") is not None for case in cases),
                      "skipped": sum(case.find("skipped") is not None for case in cases)}
            if not counts["executed"]:
                raise ValueError("CTest report contains zero test cases")
            completion["ctest"] = {"recorded_result": result, "counts": counts,
                "junit_artifact": evidence.artifact(junit),
                "run_artifact": evidence.artifact(args.ctest_run),
                "passed": result["returncode"] == 0 and counts["failed"] == 0 and counts["skipped"] == 0}
        if args.browser_report:
            result = evidence.read(args.browser_report)
            completion["listening_browser"] = {"artifact": evidence.artifact(args.browser_report),
                "recorded_result": result, "automated_votes_are_human_approval": False}
        if args.listening_manifest:
            manifest = evidence.read(args.listening_manifest)
            if args.browser_report:
                browser = completion["listening_browser"]["recorded_result"]
                if (browser.get("review_id") != manifest["review_id"]
                        or browser.get("manifest_sha256") != evidence.artifact(args.listening_manifest)["sha256"]):
                    raise ValueError("Browser report belongs to a different listening package")
            audio = {}
            for track in manifest["tracks"]:
                versions = [version for mode in track["modes"].values() for version in mode["versions"]]
                if track.get("optional_reference"):
                    versions.append(track["optional_reference"])
                for version in versions:
                    artifact = evidence.artifact(args.listening_manifest.parent / version["file"])
                    if artifact["sha256"] != version["sha256"]:
                        raise ValueError(f"Listening audio differs from manifest: {version['file']}")
                    audio[version["file"]] = artifact
            completion["listening_package"] = {"manifest_artifact": evidence.artifact(args.listening_manifest),
                "recorded_manifest": manifest, "verified_audio": audio,
                "approval": "pending_listener_review"}
            for name in ("index.html", "Acustra-AB-listening-test.zip"):
                path = args.listening_manifest.parent / name
                if path.is_file():
                    completion["listening_package"][name] = evidence.artifact(path)
        if args.reference_provenance:
            provenance = evidence.read(args.reference_provenance)
            for master in provenance["master_files"]:
                if evidence.artifact(Path(master["path"]))["sha256"] != master["expected_sha256"]:
                    raise ValueError(f"Reference master differs from its frozen pin: {master['file']}")
            for target in provenance["targets"]:
                artifact = evidence.artifact(args.reference_provenance.parent / target["file"])
                if artifact["sha256"] != target["sha256"]:
                    raise ValueError(f"Prepared target differs from frozen reference: {target['id']}")
            completion["reference_provenance"] = {"artifact": evidence.artifact(args.reference_provenance),
                "recorded_provenance": provenance}
        report = {
            "schema_version": 1,
            "created_utc": datetime.now(timezone.utc).isoformat(),
            "interpretation": INTERPRETATION, "limitations": LIMITATIONS,
            "models": models, "regressions": regressions, "reference_repeat_variation": repeats,
            "completion_checks": completion,
            "report_generator": evidence.artifact(Path(__file__)),
            "artifact_inventory": list(evidence.files.values()),
            "approval": {"status": "pending_listener_review", "inferred_from_measurements": False},
        }
        args.out.parent.mkdir(parents=True, exist_ok=True)
        with args.out.open("x", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2, allow_nan=False)
            stream.write("\n")
    except (OSError, ValueError, KeyError, StopIteration, ET.ParseError) as error:
        print(f"evidence generation failed: {error}", file=sys.stderr)
        return 1
    print(f"Evidence written to {args.out}; listener approval remains pending.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
