#!/usr/bin/env python3
"""Fit Acustra's bounded physical parameters to the reference recordings.

The C++ renderer owns synthesis and the Python scorer owns descriptors.  This
driver exports targets once, asks the renderer to replace model files for each
candidate, and runs a bounded pattern search for the shared body, nylon
strings, steel strings, then the shared body once more.

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
import shutil
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
from typing import Any

import numpy as np

from FitPhysicalModel import PreparedManifest


NAMES = (
    "bodyFrequencyScale",
    "bodyQScale",
    "bridgeMobilityScale",
    "residueTiltDbPerOctave",
    "directGain",
    "nylon.fundamentalT60Scale",
    "nylon.frequencyLossScale",
    "nylon.apertureScale",
    "nylon.transientScale",
    "nylon.pluckDistanceScale",
    "nylon.velocityBrightnessDepth",
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
    "longitudinalGain",
    "longitudinalQ",
    "polarisationEndCorrectionMetres",
    "pickReleaseVelocityShare",
    "pickReleaseVelocityExponent",
    "pickTransientGain",
)
LOWER = np.asarray((
    0.96, 0.05, 0.25, -6.0, 0.0,
    0.4, 0.35, 0.35, 0.0, 0.7, 0.0,
    0.25, 0.4, 0.35, 0.35, 0.0, 0.7, 0.0,
    -1.0, 0.25, 0.0, -0.06, 0.5, 0.0, 100.0, 0.00325, 0.0, 10.0, 0.0,
    0.0, 0.0, 0.0,
))
UPPER = np.asarray((
    1.04, 1.8, 4.0, 6.0, 0.12,
    2.0, 3.0, 2.5, 3.0, 1.3, 1.2,
    4.0, 2.0, 3.0, 2.5, 3.0, 1.3, 1.2,
    1.0, 32.0, 0.04, 0.05, 4.0, 0.02, 8000.0, 0.060, 0.5, 400.0, 0.82e-3,
    2.0, 4.0, 8.0,
))
INITIAL = np.asarray((
    1.0, 1.0, 1.0, 0.0, 0.0,
    1.0, 1.0, 1.0, 1.0, 1.0, 0.0,
    1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 0.0,
    1.0, 1.0, 0.0061, -0.030, 1.30, 0.0, 1000.0, 0.020, 0.0, 80.0, 0.0008,
    0.0, 2.0, 0.0,
))
# The shipping vector, mirroring fittedPhysicalCalibration in
# Source/DSP/FittedPhysicalData.h, for --start shipping: a stage that fits a
# new mechanism around the calibration that ships rather than around the
# neutral baseline.
SHIPPING = np.asarray((
    1.0, 1.0, 0.754677154, 0.0, 0.0,
    1.037816616, 1.40369766, 2.12267268, 0.0, 1.12667139, 0.0375,
    0.749355465, 1.53, 0.52, 0.643124355, 0.494086432, 0.88819512, 1.1859375,
    -0.0706290118, 1.0, 0.00773577847, -0.0597851562, 2.28586032, 0.011,
    2187.76023, 0.00325, 0.0, 35.0, 0.0008,
    0.3125, 0.9296875, 0.125,
))
# The bridge-local direct path is deliberately fixed off. Its score direction
# was flat (and slightly worse on validation), so fitting it only lets a
# numerical solver choose an arbitrary raw-string mixture.
# Values a listening verdict chose are not refit. The corpus disagrees with
# them by construction - that disagreement is why they went to a listener - so
# leaving them free would simply undo the verdict on the next run.
BY_EAR = (
    "residueTiltDbPerOctave",
    "lowBodyModeGain",
    "steel.fundamentalT60Scale",
    "steel.frequencyLossScale",
    "bridgeConductanceFloor",
    # The axial resonators were switched off by ear on 2026-09-01 - their
    # narrow high-Q onset reads as a pitched water drop at every pluck - and
    # the corpus disagrees: it scored 6.7685 with them on against 6.8353 off.
    # That is exactly the disagreement freezing exists for.
    "longitudinalGain",
)
# With longitudinalGain frozen at zero the axial resonators are not summed at
# all, so their Q multiplies nothing and any value renders the same audio.
INERT = (
    "longitudinalQ",
)
# Values that are a published measurement rather than a fit. The corpus does
# see the end correction, but only weakly and only on steel: with the
# bridge-local direct path off, the polarisation it lengthens still reaches
# the output through the shared slope energy that drives the steel attack
# pitch. Zeroing it changes 40 of the 79 renders (all 32 steel and all 8 flat
# top; all 39 nylon byte-identical) and moves the score from
# 6.319236 / 6.327235 / 7.948337 to 6.320649 / 6.328354 / 7.953299 - a real
# preference for the published value, and far too small a lever to fit it on.
MEASURED = (
    "polarisationEndCorrectionMetres",
)
FROZEN = frozenset(NAMES.index(name) for name in BY_EAR + MEASURED + INERT)


def _free(indices: np.ndarray) -> np.ndarray:
    return np.asarray([index for index in indices if index not in FROZEN],
                      dtype=int)


GLOBAL = _free(np.asarray((0, 1, 2, 3, 18, 19, 22, 23, 24, 25, 26, 27, 28)))
NYLON = _free(np.arange(5, 11))
STEEL = _free(np.append(np.arange(11, 18), (20, 21)))
# The plectrum's three values are read by the Pick technique only, so they
# are fitted on the picked archtop rows rendered with it (--archtop-picking
# pick) and by nothing else; the finger-plucked flat-top and classical rows
# render with Finger whatever this stage does.
PICK = np.asarray((29, 30, 31))
# The steel excitation and the plectrum together, on the same picked rows:
# the four steel values that shape the pluck's contact, level law and
# brightness were fitted with Finger on recordings that were picked.
PICK_EXCITATION = np.asarray((14, 15, 16, 17, 29, 30, 31))

STAGES = {
    "shared-body": (None, GLOBAL),
    "nylon-string": ("nylon", NYLON),
    "steel-string": ("steel", STEEL),
    "shared-body-refine": (None, GLOBAL),
    "pick-release": ("steel", PICK),
    "pick-excitation": ("steel", PICK_EXCITATION),
}
DEFAULT_STAGES = ("shared-body", "nylon-string", "steel-string",
                  "shared-body-refine")

# Renderer options every evaluation carries, e.g. --archtop-picking pick.
RENDER_OPTIONS: list[str] = []


def _command(renderer: Path, directory: Path, values: np.ndarray,
             models_only: bool) -> list[str]:
    command = [str(renderer)]
    if models_only:
        command.append("--models-only")
    command.extend(RENDER_OPTIONS)
    command.append(str(directory))
    command.extend(format(float(value), ".9g") for value in values)
    return command


def _run_renderer(renderer: Path, directory: Path, values: np.ndarray,
                  models_only: bool) -> None:
    completed = subprocess.run(
        _command(renderer, directory, values, models_only),
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
_WORKER: dict[str, Any] = {}


def _worker_setup(renderer: Path, directories: Any,
                  render_options: list[str]) -> None:
    directory = Path(directories.get())
    _WORKER["renderer"] = renderer
    _WORKER["directory"] = directory
    _WORKER["train"] = PreparedManifest(directory / "train.json")
    RENDER_OPTIONS[:] = render_options


def _worker_evaluate(job: tuple[list[float], str | None]) -> dict[str, Any]:
    values, material = job
    _run_renderer(_WORKER["renderer"], _WORKER["directory"],
                  np.asarray(values, dtype=float), True)
    return _small_report(_WORKER["train"].score(material=material))


def _worker_directories(output: Path, jobs: int) -> list[Path]:
    """Give every worker but the first its own copy of targets and manifests.

    Model renders are not copied: the renderer writes every model the manifests
    reference on the first evaluation.
    """
    directories = [output]
    for index in range(1, jobs):
        directory = output.parent / f"{output.name}-worker{index}"
        directory.mkdir(parents=True, exist_ok=True)
        for source in sorted(output.iterdir()):
            if not source.is_file() or source.name.startswith("model-"):
                continue
            destination = directory / source.name
            if not destination.is_file():
                shutil.copy2(source, destination)
        directories.append(directory)
    return directories


class Objective:
    def __init__(self, executor: ProcessPoolExecutor, base: np.ndarray,
                 active: np.ndarray, material: str | None,
                 evaluations: list[dict[str, Any]],
                 checkpoint: Path | None = None,
                 provenance: dict[str, Any] | None = None):
        self.executor = executor
        self.base = base.copy()
        self.active = active
        self.material = material
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
                        "stage_material": self.material,
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
                [(values.tolist(), self.material) for values in pending.values()],
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
                    "stage_material": self.material,
                    "values": values.tolist(),
                    **report,
                })
                if improved:
                    self._save_best()
                print(
                    f"eval {len(self.evaluations):04d} "
                    f"{self.material or 'both':>5} score={score:.6f}",
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


def _fit_stage(name: str, material: str | None, active: np.ndarray,
               values: np.ndarray, executor: ProcessPoolExecutor,
               budget: int, evaluations: list[dict[str, Any]],
               checkpoint: Path | None = None,
               provenance: dict[str, Any] | None = None,
               ) -> tuple[np.ndarray, dict[str, Any]]:
    unit = (values[active] - LOWER[active]) / (UPPER[active] - LOWER[active])
    objective = Objective(executor, values, active, material, evaluations,
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
        "material": material,
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
        "--resume", action="store_true",
        help="reuse an existing renderer corpus and its current calibration",
    )
    parser.add_argument(
        "--start", choices=("neutral", "shipping"), default="neutral",
        help="calibration the search starts from: the neutral baseline "
             "(default) or the vector that ships",
    )
    parser.add_argument(
        "--stages", default=",".join(DEFAULT_STAGES),
        help="comma-separated stages to run, in order, from: "
             + ", ".join(STAGES) + f" (default: {','.join(DEFAULT_STAGES)})",
    )
    parser.add_argument(
        "--archtop-picking", choices=("finger", "pick", "thumb"),
        help="render the picked archtop rows with this tool (the renderer's "
             "own default otherwise); stages over the plectrum's values "
             "(pick-release, pick-excitation) need pick",
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
    # The plectrum's values are read by Pick only, so a stage over them
    # rendered with any other tool would search inert coordinates.
    needs_pick = [name for name in stage_names
                  if np.intersect1d(STAGES[name][1], PICK).size > 0]
    renderer = arguments.renderer.resolve()
    output = arguments.output.resolve()
    if not renderer.is_file():
        parser.error(f"renderer does not exist: {renderer}")

    values = (SHIPPING if arguments.start == "shipping" else INITIAL).copy()
    start = arguments.start
    archtop_picking = arguments.archtop_picking
    if arguments.resume:
        manifest_path = output / "train.json"
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
        if archtop_picking is None:
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
        if isinstance(order, list) and len(order) == candidate.size:
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
        elif candidate.size == 24:
            # The temporary all-material KC layout stored nylon at 21,
            # steel at 22 and the fret slope at 23.
            candidate = np.append(np.delete(candidate, 21), INITIAL[-1])
        elif 19 <= candidate.size < INITIAL.size:
            candidate = np.append(candidate, INITIAL[candidate.size:])
        if candidate.shape != INITIAL.shape or not np.all(np.isfinite(candidate)):
            parser.error(
                f"{result_path.name} has no valid 19- through "
                f"{INITIAL.size}-value calibration"
            )
        values = np.clip(candidate, LOWER, UPPER)
        values[4] = 0.0
    elif output.exists():
        parser.error("output already exists; use a new path or --resume")
    if needs_pick and archtop_picking != "pick":
        parser.error(f"{', '.join(needs_pick)}: a stage over the plectrum's "
                     "values needs --archtop-picking pick")
    if archtop_picking is not None:
        RENDER_OPTIONS[:] = ["--archtop-picking", archtop_picking]
    _run_renderer(renderer, output, values, arguments.resume)

    train = PreparedManifest(output / "train.json")
    baseline = train.score()
    print(f"baseline train score={baseline['score']:.6f}", flush=True)
    evaluations: list[dict[str, Any]] = []
    stages: list[dict[str, Any]] = []
    directories = _worker_directories(output, arguments.jobs)
    queue: Any = multiprocessing.Queue()
    for directory in directories:
        queue.put(str(directory))
    with ProcessPoolExecutor(
        max_workers=arguments.jobs,
        initializer=_worker_setup,
        initargs=(renderer, queue, list(RENDER_OPTIONS)),
    ) as executor:
        for name in stage_names:
            material, active = STAGES[name]
            values, stage = _fit_stage(
                name, material, active, values, executor,
                arguments.evaluations, evaluations,
                output / "fit-best.json",
                {"start": start, "render_options": list(RENDER_OPTIONS)},
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
    for directory in directories[1:]:
        shutil.rmtree(directory, ignore_errors=True)

    _run_renderer(renderer, output, values, True)
    final_train = train.score()
    validation = PreparedManifest(output / "validation.json").score()
    result = {
        "parameter_order": NAMES,
        "values": values.tolist(),
        "start": start,
        "resumed": arguments.resume,
        "render_options": list(RENDER_OPTIONS),
        "baseline_train": _small_report(baseline),
        "final_train": _small_report(final_train),
        "validation": _small_report(validation),
        "stages": stages,
        "evaluations": evaluations,
    }
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
