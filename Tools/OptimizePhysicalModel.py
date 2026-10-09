#!/usr/bin/env python3
"""Fit Acustra's bounded physical parameters to the reference recordings.

The C++ renderer owns synthesis and the Python scorer owns descriptors.  This
driver exports targets once, asks the renderer to replace model files for each
candidate, and runs a bounded pattern search for the body, the steel
strings, then the body once more.

The search is derivative-free because the objective is not differentiable.
Each partial is read as the largest peak inside a fixed +/-65-cent window, so
when two peaks compete inside one window the descriptor steps as the winner
changes.  Measured on the shipping calibration by resampling a render through a
+/-30-cent sweep in 0.5-cent steps: the harmonics term of a low steel E moves a
median 0.00024 per step, but a high steel note (E6, m84) moves 0.565 across one
0.5-cent step at the operating point itself - 6.6% of that note's term, 470
times its own median step - with no partial entering or leaving the search.
A one-sided finite difference reads that step as a slope, so the stages poll
the bounded box directly instead (Kolda, Lewis and Torczon, "Optimization by
Direct Search", SIAM Review 45 (2003) 385-482).
"""

from __future__ import annotations

import argparse
import json
import multiprocessing
import os
import queue as threadqueue
import shutil
import subprocess
import sys
import threading
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from pathlib import Path
from typing import Any

import numpy as np

from FitPhysicalModel import PreparedManifest


NAMES = (
    "bodyFrequencyScale",
    "bodyQScale",
    "bridgeMobilityScale",
    "residueTiltDbPerOctave",
    "steel.stiffnessScale",
    "steel.fundamentalT60Scale",
    "steel.frequencyLossScale",
    "steel.apertureScale",
    "steel.transientScale",
    "steel.pluckDistanceScale",
    "steel.velocityBrightnessDepth",
    "apertureRegisterExponent",
    "lowBodyModeGain",
    "steelDisplacementScaleMetres",
    "steelFretT60Slope",
    "highLossCutoffScale",
    "bridgeConductanceFloor",
    "bridgeConductanceCornerHz",
    "bridgeTailLengthMetres",
    "pickReleaseVelocityShare",
    "pickReleaseVelocityExponent",
    "pickEdgeRadiusMetres",
    "steelWoundBendingLoss",
    "steelPlainBendingLoss",
)
LOWER = np.asarray((
    0.96, 0.05, 0.25, -6.0, 0.25, 0.4,
    0.35, 0.35, 0.0, 0.7, 0.0, -1.0,
    0.25, 0.0, -0.06, 0.5, 0.0, 100.0,
    0.00325, 0.0, 0.0, 0.0, 0.0, 0.0,
))
UPPER = np.asarray((
    1.04, 1.8, 4.0, 6.0, 4.0, 2.0,
    3.0, 2.5, 3.0, 3.0, 1.2, 1.0,
    32.0, 0.04, 0.05, 4.0, 0.02, 8000.0,
    0.06, 2.0, 4.0, 0.0005, 0.25, 0.05,
))
INITIAL = np.asarray((
    1.0, 1.0, 1.0, 0.0, 1.0, 1.0,
    1.0, 1.0, 1.0, 1.0, 0.0, 1.0,
    1.0, 0.0061, -0.03, 1.3, 0.0, 1000.0,
    0.02, 0.0, 2.0, 0.0, 0.0, 0.0,
))
# The shipping vector, mirroring fittedPhysicalCalibration in
# Source/DSP/FittedPhysicalData.h, for --start shipping: a stage that fits a
# new mechanism around the calibration that ships rather than around the
# neutral baseline.
SHIPPING = np.asarray((
    1.0, 1.0, 0.754677154, 0.0, 0.749355465, 1.53,
    0.52, 0.4883279315, 2.2130696796, 1.8, 1.10625, -0.0706290118,
    4.0, 0.00773577847, -0.0597851562, 2.28586032, 0.011, 2187.76023,
    0.00325, 0.58203125, 0.85859375, 0.0001162109375, 0.035, 0.002334375,
))
# Values selected by listening remain fixed during every fitting stage.
BY_EAR = (
    "residueTiltDbPerOctave",
    "lowBodyModeGain",
    "steel.fundamentalT60Scale",
    "steel.frequencyLossScale",
    "bridgeConductanceFloor",
    "steel.pluckDistanceScale",
    "steel.apertureScale",
    "steel.transientScale",
    "steel.velocityBrightnessDepth",
    "pickReleaseVelocityShare",
    "pickReleaseVelocityExponent",
    "pickEdgeRadiusMetres",
    "steelWoundBendingLoss",
    "steelPlainBendingLoss",
)
FROZEN = frozenset(NAMES.index(name) for name in BY_EAR)


def _free(*names: str) -> np.ndarray:
    return np.asarray([NAMES.index(name) for name in names
                       if NAMES.index(name) not in FROZEN], dtype=int)


GLOBAL = _free("bodyFrequencyScale", "bodyQScale", "bridgeMobilityScale",
               "residueTiltDbPerOctave", "apertureRegisterExponent",
               "lowBodyModeGain", "highLossCutoffScale", "bridgeConductanceFloor",
               "bridgeConductanceCornerHz", "bridgeTailLengthMetres")
STEEL = _free(*(name for name in NAMES if name.startswith("steel.")),
              "steelDisplacementScaleMetres", "steelFretT60Slope")
# Pick-only stages had no searchable coordinates after the rejected impact
# path was removed. Its surviving release parameters remain fixed by ear.
STAGES = {
    "shared-body": GLOBAL,
    "steel-string": STEEL,
    "shared-body-refine": GLOBAL,
}
DEFAULT_STAGES = ("shared-body", "steel-string", "shared-body-refine")

# Renderer options every evaluation carries, e.g. --archtop-picking pick.
RENDER_OPTIONS: list[str] = []


def _command(renderer: Path, directory: Path, values: np.ndarray,
             models_only: bool, scope: str | None = None,
             options: list[str] | None = None) -> list[str]:
    command = [str(renderer)]
    if models_only:
        command.append("--models-only")
        if scope is not None:
            command.extend(("--scope", scope))
    command.extend(RENDER_OPTIONS if options is None else options)
    command.append(str(directory))
    command.extend(format(float(value), ".9g") for value in values)
    return command


def _run_renderer(renderer: Path, directory: Path, values: np.ndarray,
                  models_only: bool, scope: str | None = None,
                  options: list[str] | None = None) -> None:
    completed = subprocess.run(
        _command(renderer, directory, values, models_only, scope, options),
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"renderer failed ({completed.returncode}):\n{completed.stdout}"
        )


def _small_report(report: dict[str, Any]) -> dict[str, Any]:
    return {
        "score": report["score"],
        "example_count": report["example_count"],
        "unique_model_count": report["unique_model_count"],
        "terms": report["terms"],
    }


# One evaluation is a 79-render corpus and a scored split - about 30 seconds -
# and a stage of a few hundred of them only fits in a working day if several
# run at once. Each candidate is independent and the renderer is deterministic,
# so a worker owns its own copy of the corpus directory and the result does not
# depend on how many workers there are.
# A worker's state is its own whether the workers are processes or, with
# --threads, threads of this one (where a sandbox forbids the semaphores a
# process pool needs; the renders are subprocesses either way).
_LOCAL = threading.local()


class _WorkerState:
    def __getitem__(self, key: str) -> Any:
        return _LOCAL.__dict__[key]

    def __setitem__(self, key: str, value: Any) -> None:
        _LOCAL.__dict__[key] = value


_WORKER = _WorkerState()


def _worker_setup(renderer: Path, directories: Any,
                  protocols: list[list[str]]) -> None:
    # One corpus per protocol (the renderer options it is rendered with);
    # a single protocol is the ordinary search.
    corpora = [Path(directory) for directory in directories.get()]
    _WORKER["renderer"] = renderer
    _WORKER["corpora"] = [
        (options, directory, PreparedManifest(directory / "train.json"))
        for options, directory in zip(protocols, corpora)]
    RENDER_OPTIONS[:] = protocols[0]


def _worker_evaluate(values: list[float]) -> dict[str, Any]:
    reports = []
    for options, directory, train in _WORKER["corpora"]:
        # A candidate is scored on the training rows, so the renderer
        # replaces only their models (the final render after the search
        # replaces every one).
        _run_renderer(_WORKER["renderer"], directory,
                      np.asarray(values, dtype=float), True, "train", options)
        reports.append(_small_report(train.score()))
    if len(reports) == 1:
        return reports[0]
    # Several protocols score one calibration: the objective is their mean,
    # so it stays on the scale of one training score.
    return {
        "score": float(np.mean([report["score"] for report in reports])),
        "protocols": {" ".join(options) or "default": report
                      for (options, _, _), report
                      in zip(_WORKER["corpora"], reports)},
    }


def _worker_directories(output: Path, jobs: int) -> list[Path]:
    """Give every worker but the first its own copy of targets and manifests.

    Model renders are not copied: the renderer writes every model the manifests
    reference on the first evaluation. Nor are the sample player's control
    renders, which no training score reads.
    """
    directories = [output]
    for index in range(1, jobs):
        directory = output.parent / f"{output.name}-worker{index}"
        directory.mkdir(parents=True, exist_ok=True)
        for source in sorted(output.iterdir()):
            if (not source.is_file() or source.name.startswith("model-")
                    or (source.name.startswith("sample-")
                        and source.suffix == ".f32")):
                continue
            destination = directory / source.name
            if not destination.is_file():
                shutil.copy2(source, destination)
        directories.append(directory)
    return directories


class Objective:
    def __init__(self, executor: Any, base: np.ndarray,
                 active: np.ndarray, stage: str,
                 evaluations: list[dict[str, Any]],
                 checkpoint: Path | None = None,
                 provenance: dict[str, Any] | None = None):
        self.executor = executor
        self.base = base.copy()
        self.active = active
        self.stage = stage
        self.evaluations = evaluations
        self.checkpoint = checkpoint
        self.provenance = provenance or {}
        self.cache: dict[tuple[float, ...], float] = {}
        self.best_values = base.copy()
        self.best_score = float("inf")
        self.count = 0

    def _save_best(self) -> None:
        # A stage is hours of renders and the search keeps its best only in
        # memory, so every improvement goes to disk; --resume reads it back
        # when no finished fit-result.json exists.
        if self.checkpoint is None:
            return
        # Through a temporary sibling, so an interruption during the write -
        # the case the checkpoint exists for - leaves the previous one whole.
        partial = self.checkpoint.with_suffix(".json.partial")
        partial.write_text(
            json.dumps({"parameter_order": NAMES,
                        "values": self.best_values.tolist(),
                        "score": self.best_score,
                        "stage": self.stage,
                        "evaluations": len(self.evaluations),
                        **self.provenance}, indent=2) + "\n",
            encoding="utf-8",
        )
        os.replace(partial, self.checkpoint)

    def values(self, unit: np.ndarray) -> np.ndarray:
        values = self.base.copy()
        values[self.active] = LOWER[self.active] + unit * (
            UPPER[self.active] - LOWER[self.active]
        )
        return values

    def batch(self, units: list[np.ndarray]) -> np.ndarray:
        candidates = [self.values(unit) for unit in units]
        # The C++ boundary is float, so parameters that serialize identically
        # are the same physical candidate and need only one render.
        keys = [tuple(float(np.float32(value)) for value in candidate)
                for candidate in candidates]
        pending: dict[tuple[float, ...], np.ndarray] = {}
        for key, candidate in zip(keys, candidates):
            if key not in self.cache:
                pending.setdefault(key, candidate)
        if pending:
            reports = self.executor.map(
                _worker_evaluate,
                [values.tolist() for values in pending.values()],
            )
            for (key, values), report in zip(pending.items(), reports):
                score = float(report["score"])
                self.cache[key] = score
                self.count += 1
                improved = score < self.best_score
                if improved:
                    self.best_score = score
                    self.best_values = values.copy()
                self.evaluations.append({
                    "stage": self.stage,
                    "values": values.tolist(),
                    **report,
                })
                if improved:
                    self._save_best()
                print(
                    f"eval {len(self.evaluations):04d} "
                    f"{self.stage} score={score:.6f}",
                    flush=True,
                )
        return np.asarray([self.cache[key] for key in keys])


def _pattern_search(objective: Objective, unit: np.ndarray, budget: int,
                    step: float = 0.25,
                    smallest: float = 1.0 / 512.0) -> np.ndarray:
    """Compass search in the unit box: poll +/-step on each free coordinate.

    The poll is a full one so the step accepted is the best of the box, which
    makes the walk independent of the order the workers finish in. A poll that
    beats the incumbent moves it and keeps the step; a poll that does not halves
    the step. The floor of 1/512 of a bound range is where the objective stops
    resolving a coordinate: a nine-point sweep of highLossCutoffScale in steps
    of 1/500 of its range, around the shipping value, reads 6.324086, 6.323484,
    6.328220, 6.320992, 6.319236, 6.322703, 6.322967, 6.329617, 6.328153 - a
    0.007 wiggle on steps that small, the size of the gain a whole stage is
    looking for.
    """
    best = float(objective.batch([unit])[0])
    while objective.count < budget and step >= smallest:
        poll: list[np.ndarray] = []
        for index in range(unit.size):
            for direction in (step, -step):
                candidate = unit.copy()
                candidate[index] = min(1.0, max(0.0, unit[index] + direction))
                if candidate[index] != unit[index]:
                    poll.append(candidate)
        if not poll:
            break
        scores = objective.batch(poll)
        chosen = int(np.argmin(scores))
        if scores[chosen] < best:
            unit, best = poll[chosen], float(scores[chosen])
        else:
            step *= 0.5
    return unit


def _fit_stage(name: str, active: np.ndarray,
               values: np.ndarray, executor: Any,
               budget: int, evaluations: list[dict[str, Any]],
               checkpoint: Path | None = None,
               provenance: dict[str, Any] | None = None,
               ) -> tuple[np.ndarray, dict[str, Any]]:
    unit = (values[active] - LOWER[active]) / (UPPER[active] - LOWER[active])
    objective = Objective(executor, values, active, name, evaluations,
                          checkpoint, provenance)
    _pattern_search(objective, np.clip(unit, 0.0, 1.0), budget)
    fitted = objective.best_values
    print(
        f"stage {name}: {objective.best_score:.6f}; "
        f"evaluations={objective.count}",
        flush=True,
    )
    return fitted, {
        "name": name,
        "active": [NAMES[index] for index in active],
        "best_score": objective.best_score,
        "evaluations": int(objective.count),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("renderer", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--evaluations", type=int, default=400,
        help="renders the pattern search may spend per stage (default: 400)",
    )
    parser.add_argument(
        "--jobs", type=int, default=1,
        help="candidates rendered at once; each needs its own corpus copy "
             "(default: 1)",
    )
    parser.add_argument(
        "--threads", action="store_true",
        help="run the --jobs workers as threads of this process rather than "
             "as processes (the same renders and scores)",
    )
    parser.add_argument(
        "--resume", action="store_true",
        help="reuse an existing renderer corpus and its current calibration",
    )
    parser.add_argument(
        "--start", choices=("neutral", "shipping"), default="neutral",
        help="calibration the search starts from: the neutral baseline "
             "(default) or the vector that ships",
    )
    parser.add_argument(
        "--set", nargs="*", default=[], metavar="NAME=VALUE",
        help="start the search with these values in place of --start's "
             "(e.g. a measured value the stages are to be fitted around)",
    )
    parser.add_argument(
        "--stages", default=",".join(DEFAULT_STAGES),
        help="comma-separated stages to run, in order, from: "
             + ", ".join(STAGES) + f" (default: {','.join(DEFAULT_STAGES)})",
    )
    parser.add_argument(
        "--archtop-picking", choices=("finger", "pick", "thumb"),
        help="render the picked archtop rows with this tool (the renderer's "
             "own default otherwise)",
    )
    parser.add_argument(
        "--joint-picking",
        help="comma-separated tools (finger, pick, thumb) to render the "
             "picked archtop rows with, one corpus each under OUTPUT; every "
             "candidate is rendered with all of them and scored by the mean "
             "of their training scores, so values the tools share (a "
             "string's loss) are fitted once for all of them",
    )
    arguments = parser.parse_args()
    if arguments.evaluations < 1:
        parser.error("--evaluations must be positive")
    if arguments.jobs < 1:
        parser.error("--jobs must be positive")
    stage_names = [name.strip() for name in arguments.stages.split(",") if name.strip()]
    unknown = [name for name in stage_names if name not in STAGES]
    if unknown or not stage_names:
        parser.error(f"unknown stages: {', '.join(unknown) or 'none given'}")
    renderer = arguments.renderer.resolve()
    output = arguments.output.resolve()
    if not renderer.is_file():
        parser.error(f"renderer does not exist: {renderer}")

    values = (SHIPPING if arguments.start == "shipping" else INITIAL).copy()
    start = arguments.start
    if arguments.set and arguments.resume:
        parser.error("--set starts a search; a resumed one continues from its "
                     "checkpoint")
    for item in arguments.set:
        name, separator, text = item.partition("=")
        if not separator or name not in NAMES:
            parser.error(f"--set expects NAME=VALUE with a calibration name, "
                         f"not {item!r}")
        index = NAMES.index(name)
        value = float(text)
        if not LOWER[index] <= value <= UPPER[index]:
            parser.error(f"--set {name}={value} is outside "
                         f"[{LOWER[index]}, {UPPER[index]}]")
        values[index] = value
    if arguments.set:
        start = f"{start} with {' '.join(arguments.set)}"
    archtop_picking = arguments.archtop_picking
    joint = ([name.strip() for name in arguments.joint_picking.split(",")
              if name.strip()] if arguments.joint_picking else [])
    if joint and archtop_picking is not None:
        parser.error("--joint-picking and --archtop-picking are exclusive")
    if any(name not in ("finger", "pick", "thumb") for name in joint) \
            or len(set(joint)) != len(joint):
        parser.error("--joint-picking takes distinct tools from finger, "
                     "pick, thumb")
    # Each protocol is one corpus: OUTPUT itself, or OUTPUT/TOOL per tool.
    corpora = ([(["--archtop-picking", name], output / name)
                for name in joint] if joint else None)
    if arguments.resume:
        manifest_path = (corpora[0][1] if joint else output) / "train.json"
        if not manifest_path.is_file():
            parser.error("--resume output has no train.json")
        manifest_data = json.loads(manifest_path.read_text(encoding="utf-8"))
        # A resumed run continues the experiment the corpus recorded. The
        # manifest itself says which tool its archtop rows were rendered
        # with, whether a fit has finished on it or the renderer alone wrote
        # it; the models-only render below would otherwise replace those
        # rows with the renderer's own default.
        controls = manifest_data.get("model_controls")
        stored_picking = (controls.get("archtop_picking")
                          if isinstance(controls, dict) else None)
        if joint:
            for options, directory in corpora:
                stored = json.loads((directory / "train.json").read_text(
                    encoding="utf-8")).get("model_controls") or {}
                if stored.get("archtop_picking") not in (None, options[-1]):
                    parser.error(f"{directory} was rendered with "
                                 f"--archtop-picking "
                                 f"{stored.get('archtop_picking')}")
        elif archtop_picking is None:
            archtop_picking = stored_picking
        elif stored_picking not in (None, archtop_picking):
            parser.error(f"the corpus was rendered with --archtop-picking "
                         f"{stored_picking}; pass the same, or a new output")
        # The checkpoint exists only while a search is unfinished (a finished
        # run removes it below), so when it is present it belongs to the run
        # to continue, even beside an older run's finished result.
        result_path = output / "fit-best.json"
        if not result_path.is_file():
            result_path = output / "fit-result.json"
        if result_path.is_file():
            result_data = json.loads(result_path.read_text(encoding="utf-8"))
            # The start its values came from, not this command line's.
            start = result_data.get("start", start)
        else:
            # Neither a checkpoint nor a finished result: the calibration the
            # corpus was last rendered with, as its manifest records it, is
            # the current one, whatever the command line's start.
            result_path = manifest_path
            result_data = {
                "values": manifest_data.get("calibration_values", []),
                "parameter_order": manifest_data.get("calibration_order"),
            }
        candidate = np.asarray(result_data.get("values", []), dtype=float)
        order = result_data.get("parameter_order")
        if not isinstance(order, list) or len(order) != candidate.size:
            order = manifest_data.get("calibration_order")
        # Values are carried over by name, so a calibration from an older
        # vector (the nylon strings' values among them, before 2026-09-29)
        # resumes with what still exists; a vector without names does not.
        if not isinstance(order, list) or len(order) != candidate.size:
            parser.error(f"{result_path.name} has no named calibration")
        migrated = INITIAL.copy()
        destination = {name: index for index, name in enumerate(NAMES)}
        aliases = {
            "steel.displacementScaleMetres":
                "steelDisplacementScaleMetres",
        }
        for value, name in zip(candidate, order):
            target = destination.get(aliases.get(name, name))
            if target is not None:
                migrated[target] = value
        candidate = migrated
        if not np.all(np.isfinite(candidate)):
            parser.error(f"{result_path.name} has a non-finite calibration")
        values = np.clip(candidate, LOWER, UPPER)
    elif output.exists():
        parser.error("output already exists; use a new path or --resume")
    if not joint:
        RENDER_OPTIONS[:] = (["--archtop-picking", archtop_picking]
                             if archtop_picking is not None else [])
        corpora = [(list(RENDER_OPTIONS), output)]
    else:
        RENDER_OPTIONS[:] = corpora[0][0]
        output.mkdir(parents=True, exist_ok=arguments.resume)
    for options, directory in corpora:
        _run_renderer(renderer, directory, values, arguments.resume,
                      None, options)

    trains = [PreparedManifest(directory / "train.json")
              for _, directory in corpora]
    baselines = [train.score() for train in trains]
    for (options, _), report in zip(corpora, baselines):
        print(f"baseline train score={report['score']:.6f} "
              f"{' '.join(options)}", flush=True)
    evaluations: list[dict[str, Any]] = []
    stages: list[dict[str, Any]] = []
    workers = [_worker_directories(directory, arguments.jobs)
               for _, directory in corpora]
    queue: Any = (threadqueue.Queue() if arguments.threads
                  else multiprocessing.Queue())
    for index in range(arguments.jobs):
        queue.put([str(directories[index]) for directories in workers])
    with (ThreadPoolExecutor if arguments.threads else ProcessPoolExecutor)(
        max_workers=arguments.jobs,
        initializer=_worker_setup,
        initargs=(renderer, queue, [options for options, _ in corpora]),
    ) as executor:
        for name in stage_names:
            values, stage = _fit_stage(
                name, STAGES[name], values, executor,
                arguments.evaluations, evaluations,
                output / "fit-best.json",
                {"start": start, "render_options": list(RENDER_OPTIONS),
                 "protocols": [options for options, _ in corpora]},
            )
            stages.append(stage)
            # A full run is hours long; leave each stage's answer on disk so a
            # crash costs one stage rather than the run.
            (output / "fit-progress.json").write_text(
                json.dumps({"parameter_order": NAMES,
                            "values": values.tolist(),
                            "stages": stages}, indent=2) + "\n",
                encoding="utf-8",
            )
    for directories in workers:
        for directory in directories[1:]:
            shutil.rmtree(directory, ignore_errors=True)

    finals = []
    validations = []
    for (options, directory), train in zip(corpora, trains):
        _run_renderer(renderer, directory, values, True, None, options)
        finals.append(train.score())
        validations.append(
            PreparedManifest(directory / "validation.json").score())
    final_train = {"score": float(np.mean([r["score"] for r in finals]))}
    validation = {"score": float(np.mean([r["score"] for r in validations]))}
    result = {
        "parameter_order": NAMES,
        "values": values.tolist(),
        "start": start,
        "resumed": arguments.resume,
        "render_options": list(RENDER_OPTIONS),
        "baseline_train": _small_report(baselines[0]),
        "final_train": _small_report(finals[0]),
        "validation": _small_report(validations[0]),
        "stages": stages,
        "evaluations": evaluations,
    }
    if joint:
        # The mean the search minimised, and each protocol's own reading.
        result["baseline_train"] = {
            "score": float(np.mean([r["score"] for r in baselines]))}
        result["final_train"] = final_train
        result["validation"] = validation
        result["protocols"] = {
            " ".join(options): {"baseline_train": _small_report(b),
                                "final_train": _small_report(f),
                                "validation": _small_report(v)}
            for (options, _), b, f, v
            in zip(corpora, baselines, finals, validations)}
    (output / "fit-result.json").write_text(
        json.dumps(result, indent=2) + "\n", encoding="utf-8"
    )
    # The finished result supersedes the search's checkpoint.
    (output / "fit-best.json").unlink(missing_ok=True)
    print("fitted values:", " ".join(format(value, ".9g") for value in values))
    print(f"final train score={final_train['score']:.6f}")
    print(f"validation score={validation['score']:.6f}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, ValueError) as error:
        print(f"OptimizePhysicalModel: {error}", file=sys.stderr)
        raise SystemExit(1)
