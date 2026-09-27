#!/usr/bin/env python3
"""Score Acustra against open corpora of isolated guitar notes.

Protocol. A corpus is a rows.json in the shared corpus row schema, built by a
separate extraction step from recordings licensed CC0, CC BY or with an
explicit no-restriction statement (the source URL, licence text and SHA-256 of
every source file live in the corpus's SOURCE.md; audio is never committed):

    {"corpus": NAME, "license": SPDX-or-text, "source": URL, "rows": [
      {"id": "NAME-...", "split": S, "material": "steel"|"nylon",
       "picking": "finger"|"pick"|"thumb"|null, "midi": M, "velocity": 1-127,
       "round_robin": R, "dynamic_group": G|null, "dynamic_marking": D|null,
       "target": {"path": "targets/ID.f32", "sample_rate": HZ, "channels": 1|2},
       "onset_seconds_in_source": T, "isolation_db": I, "clean_seconds": C,
       "measured_f0_hz": F, "cents_from_midi": X, "peak_dbfs": P, "notes": ""}]}

Each target is float32 little-endian, interleaved, at the source's own sample
rate and channel count, starting 20 ms before the note onset and ending at
min(next onset - 20 ms, onset + 4.2 s) with a 60 ms terminal half-cosine fade.
The extraction rejects notes with clean_seconds < 1.25 s or isolation_db <
30 dB (note peak level minus the RMS level of the 100 ms before the onset), and
takes midi as the nearest MIDI note to the settled f0 after removing the take's
global tuning offset. Target paths resolve relative to their rows.json.

This tool re-checks what it can before rendering anything: the fields and
their types, material steel|nylon, integer midi 0-127 and velocity 1-127,
unique ids across every corpus given, clean_seconds >= 1.25 and isolation_db
>= 30, a readable finite .f32 whose byte count divides into whole frames, a
duration from 1.27 s (20 ms pre-roll plus the 1.25 s floor) to 4.222 s, and a
faded end (the last 2 ms at least 30 dB below the file's peak; --allow-unfaded
waives only this, for the embedded bank's exports, which are cropped rather
than faded). A row that fails stops the run; --lenient drops it instead and
lists it in summary.json.
Nothing is trimmed, aligned, equalised, denoised or level-matched here.

Model side: Tools/ExternalCorpusRenderer.cpp (CMake target
AcustraExternalCorpusRenderer), which is PhysicalFitRenderer's renderModel and
calibration mapping with the schedule read from a job file: a fresh
AcustraEngine per unique (material, picking, midi, velocity), 48 kHz,
127-sample blocks, 4.2 s of stereo from a note-on at frame zero, default public
controls, the named bridge and guitar model, nylon on the Auditorium slot (the
measured classical) unless --shape is given. Round robins and dynamic groups
share one render, as in the bank benchmark. The calibration is the shipping
vector (OptimizePhysicalModel.SHIPPING, which mirrors fittedPhysicalCalibration)
unless --values or --set say otherwise. A row whose picking is null is played
with --picking-default (finger unless given); --picking-override plays every
row with one tool. A render that comes back silent (the engine cannot place
that note on a string) is dropped, reported, and never scored as silence.

Scoring: one FitPhysicalModel manifest per (corpus, split), named
CORPUS.SPLIT.json, with absolute target paths and the rows' material,
round_robin and dynamic_group, scored by the unchanged
FitPhysicalModel.PreparedManifest: onset detection; attack 0-100 ms;
harmonics 80-250 and 400-900 ms; settled tuning 400-1200 ms; decay
120 ms-4.0 s; body bands 80-900 ms; dynamics only inside a dynamic group that
holds at least two velocities. Lower means closer descriptors on exactly these
recordings; it is not a listening verdict. The table reports the total, the
seven terms and each material. This tool neither fits nor selects: a split
used to choose a calibration stops being a held-out reading and should be
named as such.

Three things the scores carry that are not the model alone, reported per
split in summary.json under "diagnostics" rather than corrected. The target's
20 ms pre-roll puts its detected onset about 20 ms after the render's, and the
attack term's latency residual (1.5 ms tolerance) carries that difference: on
the eight flat-top bank notes re-cut this way it is 3.5% of the attack term.
The tuning term compares partials with nominal MIDI pitch at A440, so a corpus
whose instrument sat off A440 reads its tuning offset as tuning error. And the
decay term reads slopes from 120 ms to the end of a short target, so it
measures early decay on a short row: the same eight notes cut to 4.2, 2.0 and
1.3 s score decay 2.46, 5.35 and 7.28, and trimming the render to the target's
length does not bring it back (4.31 and 7.53), so it is the window, not a
length mismatch. Compare decay, and totals, between splits of similar clean
length only.

--compare BASEDIR runs FitPhysicalModel's paired comparison
(FitPhysicalModel.py NEWDIR/KEY.json --compare BASEDIR/KEY.json) for every
split both directories hold. BASEDIR must be an earlier output of this tool,
over the same rows, run with --keep so its model renders still exist; this
run's own renders are compared before they are removed. Without --keep the
model .f32 files are deleted after scoring (the disk this runs on is nearly
full); manifests, job files, per-split score reports and summary.json remain.

Usage:
  python3 Tools/BenchmarkOpenCorpora.py CORPUS/rows.json [MORE/rows.json ...] \
      --renderer BUILD/AcustraExternalCorpusRenderer --output NEWDIR \
      [--bridge-model original|fylde] [--shape parlor|auditorium|dreadnought|jumbo] \
      [--guitar-model original|bellido1978] \
      [--picking-default finger|pick|thumb] [--picking-override finger|pick|thumb] \
      [--values V1 ... V32 | --set INDEX_OR_NAME=VALUE ...] \
      [--splits SPLIT_OR_CORPUS.SPLIT,...] [--keep] [--compare BASEDIR] \
      [--jobs N] [--lenient] [--allow-unfaded]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

import numpy as np

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import FitPhysicalModel as fit  # noqa: E402
from OptimizePhysicalModel import LOWER, NAMES, SHIPPING, UPPER  # noqa: E402

MODEL_RATE = 48_000
MODEL_CHANNELS = 2
MODEL_FRAMES = round(4.2 * MODEL_RATE)
MODEL_BYTES = MODEL_FRAMES * MODEL_CHANNELS * 4
PRE_ROLL = 0.020
MIN_CLEAN = 1.25
MIN_ISOLATION = 30.0
MAX_TAIL = 4.2
FADE_CHECK_SECONDS = 0.002
FADE_CHECK_DB = 30.0
SILENT_PEAK = 1.0e-6
MATERIALS = ("steel", "nylon")
PICKINGS = ("finger", "pick", "thumb")
TERMS = ("attack", "harmonics", "tuning", "pitch_trajectory", "decay",
         "body", "dynamics")
KEY_PATTERN = re.compile(r"[^A-Za-z0-9._-]")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def _is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def _is_number(value: Any) -> bool:
    return (isinstance(value, (int, float)) and not isinstance(value, bool)
            and math.isfinite(value))


def _row_problems(row: Any, base: Path,
                  check_fade: bool) -> tuple[list[str], dict[str, Any]]:
    """Return the protocol violations of one row and its checked target."""
    if not isinstance(row, dict):
        return ["row is not an object"], {}
    problems: list[str] = []
    if not isinstance(row.get("id"), str) or not row["id"]:
        problems.append("id must be a non-empty string")
    if not isinstance(row.get("split"), str) or not row["split"]:
        problems.append("split must be a non-empty string")
    if row.get("material") not in MATERIALS:
        problems.append(f"material must be steel or nylon, not {row.get('material')!r}")
    if row.get("picking") not in PICKINGS + (None,):
        problems.append(f"picking must be finger, pick, thumb or null, not {row.get('picking')!r}")
    if not _is_int(row.get("midi")) or not 0 <= row["midi"] <= 127:
        problems.append("midi must be an integer 0-127")
    if not _is_int(row.get("velocity")) or not 1 <= row["velocity"] <= 127:
        problems.append("velocity must be an integer 1-127")
    if not _is_int(row.get("round_robin")) or row["round_robin"] < 0:
        problems.append("round_robin must be a non-negative integer")
    if row.get("dynamic_group") is not None and not isinstance(row["dynamic_group"], str):
        problems.append("dynamic_group must be a string or null")
    if not _is_number(row.get("clean_seconds")) or row["clean_seconds"] < MIN_CLEAN:
        problems.append(f"clean_seconds must be >= {MIN_CLEAN}: {row.get('clean_seconds')!r}")
    if not _is_number(row.get("isolation_db")) or row["isolation_db"] < MIN_ISOLATION:
        problems.append(f"isolation_db must be >= {MIN_ISOLATION}: {row.get('isolation_db')!r}")

    target = row.get("target")
    if not isinstance(target, dict) or not isinstance(target.get("path"), str):
        problems.append("target must be an object with a path")
        return problems, {}
    rate, channels = target.get("sample_rate"), target.get("channels")
    if not _is_int(rate) or rate < 8_000:
        problems.append("target.sample_rate must be an integer >= 8000")
    if channels not in (1, 2) or isinstance(channels, bool):
        problems.append("target.channels must be 1 or 2")
    path = Path(target["path"])
    if not path.is_absolute():
        path = base / path
    path = path.resolve()
    if path.suffix.lower() != ".f32":
        problems.append("target must be a .f32 file")
    if not path.is_file():
        problems.append(f"target file is missing: {path}")
    if problems:
        return problems, {}

    size = path.stat().st_size
    if size == 0 or size % (4 * channels):
        return [f"target byte count {size} is not whole {channels}-channel float32 frames"], {}
    audio = np.fromfile(path, dtype="<f4").reshape(-1, channels)
    seconds = audio.shape[0] / rate
    if not np.all(np.isfinite(audio)):
        problems.append("target has non-finite samples")
    if seconds < PRE_ROLL + MIN_CLEAN - 1.0 / rate:
        problems.append(f"target is {seconds:.3f} s, shorter than {PRE_ROLL + MIN_CLEAN:.2f} s")
    if seconds > PRE_ROLL + MAX_TAIL + 0.002:
        problems.append(f"target is {seconds:.3f} s, longer than {PRE_ROLL + MAX_TAIL:.2f} s")
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if not peak > 0.0:
        problems.append("target is silent")
    elif check_fade:
        tail = audio[-max(1, round(FADE_CHECK_SECONDS * rate)):]
        tail_db = 20.0 * math.log10(max(float(np.sqrt(np.mean(tail * tail))), 1e-20) / peak)
        if tail_db > -FADE_CHECK_DB:
            problems.append(f"target end is {tail_db:.1f} dB re peak; expected the 60 ms fade")
    return problems, {"path": str(path), "sample_rate": rate, "channels": channels,
                      "seconds": seconds}


def load_corpora(paths: list[Path], lenient: bool, check_fade: bool,
                 wanted: set[str] | None) -> tuple[list[dict[str, Any]],
                                                   list[dict[str, Any]],
                                                   list[dict[str, Any]]]:
    corpora, rows, rejected = [], [], []
    identifiers: set[str] = set()
    failures: list[str] = []
    for path in paths:
        path = path.resolve()
        document = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(document, dict) or not isinstance(document.get("rows"), list):
            raise SystemExit(f"{path}: expected an object with a rows array")
        name = document.get("corpus")
        if not isinstance(name, str) or not name:
            raise SystemExit(f"{path}: corpus must be a non-empty string")
        if any(corpus["corpus"] == name for corpus in corpora):
            raise SystemExit(f"{path}: corpus {name} was given twice")
        used = 0
        prefix_warnings = 0
        for index, row in enumerate(document["rows"], 1):
            if (wanted is not None and isinstance(row, dict)
                    and row.get("split") not in wanted
                    and f"{name}.{row.get('split')}" not in wanted):
                continue
            problems, target = _row_problems(row, path.parent, check_fade)
            identifier = row.get("id") if isinstance(row, dict) else None
            label = identifier if isinstance(identifier, str) else f"{name} row {index}"
            if isinstance(identifier, str):
                if identifier in identifiers:
                    problems.append("duplicate id")
                identifiers.add(identifier)
                if not identifier.startswith(name + "-"):
                    prefix_warnings += 1
            if problems:
                rejected.append({"corpus": name, "id": label, "problems": problems})
                failures.append(f"{label}: " + "; ".join(problems))
                continue
            rows.append({**row, "corpus": name, "target_checked": target})
            used += 1
        if prefix_warnings:
            print(f"warning: {name}: {prefix_warnings} ids do not start with "
                  f"'{name}-'", file=sys.stderr)
        corpora.append({"corpus": name, "rows_json": str(path),
                        "rows_json_sha256": sha256(path),
                        "license": document.get("license"),
                        "source": document.get("source"),
                        "rows": len(document["rows"]), "used": used})
    if failures and not lenient:
        shown = "\n  ".join(failures[:40])
        more = f"\n  ... and {len(failures) - 40} more" if len(failures) > 40 else ""
        raise SystemExit(f"{len(failures)} rows violate the protocol "
                         f"(--lenient drops them):\n  {shown}{more}")
    for failure in failures:
        print(f"dropped: {failure}", file=sys.stderr)
    return corpora, rows, rejected


def calibration_values(arguments: argparse.Namespace) -> list[float]:
    values = [float(value) for value in (
        arguments.values if arguments.values is not None else SHIPPING)]
    for item in arguments.set or []:
        index_text, separator, value_text = item.partition("=")
        if not separator:
            raise SystemExit(f"--set expects INDEX=VALUE, not {item!r}")
        if index_text in NAMES:
            index = NAMES.index(index_text)
        else:
            try:
                index = int(index_text)
            except ValueError:
                raise SystemExit(f"--set: unknown calibration {index_text!r}") from None
        if not 0 <= index < len(values):
            raise SystemExit(f"--set: index {index} is outside 0-{len(values) - 1}")
        values[index] = float(value_text)
    for index, value in enumerate(values):
        if not math.isfinite(value) or not LOWER[index] <= value <= UPPER[index]:
            raise SystemExit(f"calibration {index} ({NAMES[index]}) = {value} is "
                             f"outside [{LOWER[index]}, {UPPER[index]}]")
    return values


def render_options(arguments: argparse.Namespace) -> list[str]:
    # The renderer parses these in this order, as PhysicalFitRenderer does.
    options: list[str] = []
    if arguments.bridge_model:
        options += ["--bridge-model", arguments.bridge_model]
    if arguments.shape:
        options += ["--shape", arguments.shape]
    if arguments.guitar_model:
        options += ["--guitar-model", arguments.guitar_model]
    return options


def model_key(material: str, picking: str, midi: int, velocity: int) -> str:
    return f"{material}-{picking}-m{midi}-v{velocity}"


def render_models(jobs: list[tuple[str, str, str, int, int]], renderer: Path,
                  output: Path, options: list[str], values: list[float],
                  processes: int) -> tuple[dict[str, Any], list[str]]:
    job_directory = output / "jobs"
    job_directory.mkdir()
    formatted = [format(value, ".9g") for value in values]
    running = []
    for index in range(processes):
        chunk = jobs[index::processes]
        if not chunk:
            continue
        job_file = job_directory / f"jobs-{index:02d}.txt"
        job_file.write_text("".join(f"{key} {material} {picking} {midi} {velocity}\n"
                                    for key, material, picking, midi, velocity in chunk),
                            encoding="utf-8")
        command = [str(renderer), *options, str(job_file), str(output), *formatted]
        running.append((command, subprocess.Popen(
            command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)))
    echoes = []
    failed = []
    for command, process in running:
        stdout, stderr = process.communicate()
        if process.returncode != 0:
            failed.append(f"{' '.join(command[:len(options) + 3])} ...: "
                          f"exit {process.returncode}\n{stdout}{stderr}")
            continue
        lines = [line for line in stdout.splitlines() if line.strip()]
        echoes.append(json.loads(lines[-1]))
    if failed:
        raise SystemExit("renderer failed:\n" + "\n".join(failed))
    for echo in echoes[1:]:
        if (echo["calibration_values"] != echoes[0]["calibration_values"]
                or echo["model_controls"] != echoes[0]["model_controls"]):
            raise SystemExit("renderer processes disagree about their controls")
    if sum(echo["rendered"] for echo in echoes) != len(jobs):
        raise SystemExit("renderer did not render every job")
    return echoes[0], running[0][0]


def check_models(jobs: list[tuple[str, str, str, int, int]],
                 output: Path) -> set[str]:
    silent = set()
    for key, *_ in jobs:
        path = output / f"model-{key}.f32"
        if not path.is_file() or path.stat().st_size != MODEL_BYTES:
            raise SystemExit(f"{path} is missing or has the wrong byte count")
        if float(np.max(np.abs(np.fromfile(path, dtype="<f4")))) < SILENT_PEAK:
            silent.add(key)
    return silent


def _huber(values: np.ndarray) -> np.ndarray:
    return fit._huber_squared(np.asarray(values, dtype=np.float64))


def score_split(key: str, manifest: Path, output: Path,
                members: list[dict[str, Any]]) -> dict[str, Any]:
    prepared = fit.PreparedManifest(manifest)
    report = prepared.score()
    (output / f"{key}-score.json").write_text(
        json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
    materials = sorted({example["material"] for example in prepared.examples
                        if example["material"]})
    by_material = {material: prepared.score(material=material)["score"]
                   for material in materials}

    # Diagnostics the total already contains; see the protocol above.
    model_features: dict[str, dict[str, Any]] = {}
    latency_target, latency_model, f0_target, f0_model = [], [], [], []
    seconds = [row["target_checked"]["seconds"] for row in members]
    for example in prepared.examples:
        spec = example["model_spec"]
        cache = f"{spec['path']}@{example['midi']}"
        if cache not in model_features:
            audio = np.fromfile(manifest.parent / spec["path"], dtype="<f4")
            model_features[cache] = fit.extract_features(
                audio.reshape(-1, MODEL_CHANNELS).mean(axis=1).astype(np.float64),
                MODEL_RATE, example["midi"])
        target = example["target_features"]
        model = model_features[cache]
        latency_target.append(target["latency_seconds"])
        latency_model.append(model["latency_seconds"])
        f0_target.append(float(target["tuning"][0]))
        f0_model.append(float(model["tuning"][0]))
    latency_residuals = (np.asarray(latency_model) - np.asarray(latency_target)) / 0.0015
    attack = report["terms"]["attack"]
    attack_total = attack["score"] * attack["count"] if attack["score"] is not None else 0.0

    def median(values: list[float], scale: float = 1.0) -> float | None:
        finite = [value for value in values if math.isfinite(value)]
        return float(np.median(finite)) * scale if finite else None

    return {
        "manifest": str(manifest),
        "example_count": report["example_count"],
        "unique_model_count": report["unique_model_count"],
        "score": report["score"],
        "terms": {name: report["terms"][name]["score"] for name in TERMS},
        "term_counts": {name: report["terms"][name]["count"] for name in TERMS},
        "by_material": by_material,
        "diagnostics": {
            "target_seconds_median": median(seconds),
            "target_onset_ms_median": median(latency_target, 1000.0),
            "model_onset_ms_median": median(latency_model, 1000.0),
            "latency_share_of_attack_term": (
                float(np.sum(_huber(latency_residuals)) / attack_total)
                if attack_total > 0 else None),
            "target_f0_cents_from_nominal_median": median(f0_target),
            "model_f0_cents_from_nominal_median": median(f0_model),
        },
    }


def compare_split(key: str, candidate: Path, baseline: Path) -> dict[str, Any]:
    result = subprocess.run(
        [sys.executable, str(HERE / "FitPhysicalModel.py"), str(candidate),
         "--compare", str(baseline)], capture_output=True, text=True)
    if result.returncode != 0:
        return {"error": (result.stderr or result.stdout).strip().splitlines()[-1:]}
    return json.loads(result.stdout)


def _cell(value: float | None, width: int = 8, digits: int = 3) -> str:
    return f"{value if value is not None else float('nan'):{width}.{digits}f}"


def _change(entry: dict[str, Any]) -> str:
    percent = entry.get("change_percent")
    return f"{percent:+.2f}%" if percent is not None else "n/a"


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rows", nargs="+", type=Path, metavar="CORPUS_ROWS_JSON")
    parser.add_argument("--renderer", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, metavar="NEWDIR")
    parser.add_argument("--bridge-model", choices=("original", "fylde"))
    parser.add_argument("--shape", choices=("parlor", "auditorium", "dreadnought", "jumbo"))
    parser.add_argument("--guitar-model", choices=("original", "bellido1978"))
    parser.add_argument("--picking-default", choices=PICKINGS, default="finger",
                        help="picking for rows whose picking is null (default finger)")
    parser.add_argument("--picking-override", choices=PICKINGS,
                        help="play every row with this picking")
    values = parser.add_mutually_exclusive_group()
    values.add_argument("--values", nargs=len(NAMES), type=float, metavar="V",
                        help="the full calibration vector (default: shipping)")
    values.add_argument("--set", nargs="+", action="extend", metavar="INDEX=VALUE",
                        help="override shipping values by index or name")
    parser.add_argument("--splits", help="comma list of SPLIT or CORPUS.SPLIT to score")
    parser.add_argument("--keep", action="store_true",
                        help="keep the model renders (needed to be a --compare baseline)")
    parser.add_argument("--compare", type=Path, metavar="BASEDIR",
                        help="paired comparison with an earlier --keep output")
    parser.add_argument("--jobs", type=int, default=os.cpu_count() or 4,
                        help="renderer processes and scoring threads")
    parser.add_argument("--lenient", action="store_true",
                        help="drop rows that violate the protocol instead of stopping")
    parser.add_argument("--allow-unfaded", action="store_true",
                        help="accept targets without the terminal fade (bank exports)")
    arguments = parser.parse_args()

    renderer = arguments.renderer.resolve()
    if not renderer.is_file() or not os.access(renderer, os.X_OK):
        raise SystemExit(f"renderer is not an executable file: {renderer}")
    output = arguments.output.resolve()
    if output.exists():
        raise SystemExit(f"output already exists: {output}")
    if not output.parent.is_dir():
        raise SystemExit(f"output parent does not exist: {output.parent}")
    if arguments.compare is not None and not arguments.compare.is_dir():
        raise SystemExit(f"--compare directory does not exist: {arguments.compare}")
    if arguments.jobs < 1:
        raise SystemExit("--jobs must be at least 1")
    values = calibration_values(arguments)
    options = render_options(arguments)

    wanted = ({name.strip() for name in arguments.splits.split(",") if name.strip()}
              if arguments.splits else None)
    corpora, rows, rejected = load_corpora(arguments.rows, arguments.lenient,
                                           not arguments.allow_unfaded, wanted)
    if not rows:
        raise SystemExit("no rows to score")
    for row in rows:
        row["picking_played"] = (arguments.picking_override or row.get("picking")
                                 or arguments.picking_default)
        row["model_key"] = model_key(row["material"], row["picking_played"],
                                     row["midi"], row["velocity"])
    jobs = sorted({(row["model_key"], row["material"], row["picking_played"],
                    row["midi"], row["velocity"]) for row in rows})

    output.mkdir()
    try:
        return run(arguments, output, renderer, options, values, corpora, rows,
                   rejected, jobs)
    finally:
        # The disk this runs on is nearly full: drop the renders unless asked,
        # also when a run stops part way.
        if not arguments.keep:
            for path in [*output.glob("model-*.f32"), *output.glob("model-*.f32.partial")]:
                path.unlink()


def run(arguments: argparse.Namespace, output: Path, renderer: Path,
        options: list[str], values: list[float], corpora: list[dict[str, Any]],
        rows: list[dict[str, Any]], rejected: list[dict[str, Any]],
        jobs: list[tuple[str, str, str, int, int]]) -> int:
    started = time.monotonic()
    echo, command = render_models(jobs, renderer, output, options, values,
                                  min(arguments.jobs, len(jobs)))
    render_seconds = time.monotonic() - started
    silent = check_models(jobs, output)
    unplayable = [{"id": row["id"], "model_key": row["model_key"]}
                  for row in rows if row["model_key"] in silent]
    for item in unplayable:
        print(f"dropped: {item['id']}: the engine rendered {item['model_key']} silent",
              file=sys.stderr)
    rows = [row for row in rows if row["model_key"] not in silent]
    if not rows:
        raise SystemExit("every render was silent")

    splits: dict[str, list[dict[str, Any]]] = {}
    for row in rows:
        key = KEY_PATTERN.sub("_", f"{row['corpus']}.{row['split']}")
        members = splits.setdefault(key, [])
        if members and (members[0]["corpus"], members[0]["split"]) != (
                row["corpus"], row["split"]):
            raise SystemExit(f"splits {members[0]['corpus']}.{members[0]['split']} and "
                             f"{row['corpus']}.{row['split']} share the file name {key}")
        members.append(row)
    corpus_by_name = {corpus["corpus"]: corpus for corpus in corpora}
    renderer_sha = sha256(renderer)
    manifests: dict[str, Path] = {}
    for key, members in splits.items():
        corpus = corpus_by_name[members[0]["corpus"]]
        manifest = {
            "analysis_sample_rate": MODEL_RATE,
            "model_render_complete": True,
            "calibration_order": list(NAMES),
            "calibration_values": echo["calibration_values"],
            "model_controls": {**echo["model_controls"],
                               "picking_default": arguments.picking_default,
                               "picking_override": arguments.picking_override},
            "provenance": {
                "corpus": corpus["corpus"],
                "split": members[0]["split"],
                "license": corpus["license"],
                "source": corpus["source"],
                "rows_json": corpus["rows_json"],
                "rows_json_sha256": corpus["rows_json_sha256"],
                "target_timing": "20 ms before the note onset to min(next onset - 20 ms, "
                                 "onset + 4.2 s), 60 ms terminal half-cosine fade",
                "model_render": "fresh AcustraEngine per material/picking/MIDI/velocity; "
                                "48000 Hz; 127-sample blocks; 4.2 s; selected bridge, "
                                "otherwise default controls",
                "renderer": str(renderer),
                "renderer_sha256": renderer_sha,
            },
            "examples": [{
                "id": row["id"],
                "material": row["material"],
                "picking": row["picking_played"],
                "midi": row["midi"],
                "velocity": row["velocity"],
                "round_robin": row["round_robin"],
                "dynamic_group": row.get("dynamic_group"),
                "target": {key_: row["target_checked"][key_]
                           for key_ in ("path", "sample_rate", "channels")},
                "model": {"path": f"model-{row['model_key']}.f32",
                          "sample_rate": MODEL_RATE, "channels": MODEL_CHANNELS},
            } for row in members],
        }
        path = output / f"{key}.json"
        path.write_text(json.dumps(manifest, indent=2), encoding="utf-8")
        manifests[key] = path

    with ThreadPoolExecutor(min(arguments.jobs, len(manifests))) as executor:
        scored = dict(zip(manifests, executor.map(
            lambda item: score_split(item[0], item[1], output, splits[item[0]]),
            manifests.items())))

    title = output.name
    print(f"{title:28s} " + " ".join(f"{name[:6]:>8s}" for name in ("total",) + TERMS))
    for key, result in scored.items():
        print(f"  {key:26s} {result['score']:8.4f} "
              + " ".join(_cell(result["terms"][name]) for name in TERMS))
    for key, result in scored.items():
        print(f"  {key:26s} " + "  ".join(f"{material} {score:.4f}"
                                           for material, score in result["by_material"].items())
              + f"  ({result['example_count']} rows, {result['unique_model_count']} renders)")

    comparisons: dict[str, Any] = {}
    if arguments.compare is not None:
        base = arguments.compare.resolve()
        pairs = {key: base / f"{key}.json" for key in manifests
                 if (base / f"{key}.json").is_file()}
        for key in manifests:
            if key not in pairs:
                print(f"  {key}: no baseline manifest in {base}")
        with ThreadPoolExecutor(max(1, min(arguments.jobs, len(pairs)))) as executor:
            reports = dict(zip(pairs, executor.map(
                lambda item: compare_split(item[0], manifests[item[0]], item[1]),
                pairs.items())))
        for key, report in reports.items():
            (output / f"{key}-compare.json").write_text(
                json.dumps(report, indent=2, sort_keys=True), encoding="utf-8")
            if "error" in report:
                print(f"  {key}: comparison failed: {' '.join(report['error'])} "
                      "(was the baseline run with --keep over the same rows?)")
                comparisons[key] = report
                continue
            aggregate = report["aggregate"]
            line = [f"ALL {aggregate['score']['baseline']:.4f}->"
                    f"{aggregate['score']['candidate']:.4f} ({_change(aggregate['score'])})"]
            line += [f"{material}: {entry['score']['baseline']:.4f}->"
                     f"{entry['score']['candidate']:.4f} ({_change(entry['score'])})"
                     for material, entry in report["by_material"].items()]
            print(f"  {key} vs {base.name}: " + " | ".join(line))
            print(f"  {'':26s} " + " ".join(
                f"{name[:6]} {_change(aggregate['terms'][name])}" for name in TERMS))
            comparisons[key] = {
                "baseline": str(pairs[key]),
                "aggregate": {name: aggregate["terms"][name]["change_percent"]
                              for name in TERMS} | {
                    "score": aggregate["score"]["change_percent"]},
                "by_material": {material: entry["score"]["change_percent"]
                                for material, entry in report["by_material"].items()},
            }

    summary = {
        "tool": "Tools/BenchmarkOpenCorpora.py",
        "renderer": {"path": str(renderer), "sha256": renderer_sha,
                     "options": options, "command": command[:len(options) + 1]
                     + ["JOBFILE", str(output)] + command[len(options) + 3:],
                     "model_controls": echo["model_controls"]},
        "calibration_order": list(NAMES),
        "calibration_values": echo["calibration_values"],
        "picking": {"default": arguments.picking_default,
                    "override": arguments.picking_override},
        "corpora": corpora,
        "rejected_rows": rejected,
        "unplayable_rows": unplayable,
        "unique_model_renders": len(jobs),
        "render_seconds": round(render_seconds, 2),
        "splits": scored,
        "compare": comparisons,
        "models_kept": arguments.keep,
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
