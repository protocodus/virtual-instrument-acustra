#!/usr/bin/env python3
"""Check that Tools/OptimizePhysicalModel.py cannot refit a listening verdict.

Values a listener chose are frozen (BY_EAR) so that no stage undoes them. The
contact noise and click levels were rejected by ear (Docs/decisions.md, Set
16: "the pick is TOO LOUD") and ship at zero, so every stage must leave them
there, and every stage must still have something left to search.
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Tools"))
import OptimizePhysicalModel as optimiser  # noqa: E402

REJECTED_BY_EAR = (
    "contactNoiseFinger", "contactNoisePick",
    "contactClickFinger", "contactClickPick",
)


def main() -> int:
    failures = []
    names = optimiser.NAMES
    if len(set(optimiser.BY_EAR + optimiser.MEASURED + optimiser.INERT)) != \
            len(optimiser.BY_EAR + optimiser.MEASURED + optimiser.INERT):
        failures.append("a name is listed in more than one frozen set")
    for name in optimiser.BY_EAR + optimiser.MEASURED + optimiser.INERT:
        if name not in names:
            failures.append(f"frozen name {name} is not a calibration value")
    for name in REJECTED_BY_EAR:
        index = names.index(name)
        if index not in optimiser.FROZEN:
            failures.append(f"{name} (index {index}) is not frozen")
        if optimiser.SHIPPING[index] != 0.0:
            failures.append(f"{name} ships at {optimiser.SHIPPING[index]}, not 0")
    for stage, free in optimiser.STAGES.items():
        refit = sorted(names[index] for index in free
                       if index in optimiser.FROZEN)
        if refit:
            failures.append(f"stage {stage} refits frozen {', '.join(refit)}")
        if len(free) == 0:
            failures.append(f"stage {stage} has nothing left to search")
    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print(f"OptimizePhysicalModel freezes {len(optimiser.FROZEN)} of "
          f"{len(names)} values across {len(optimiser.STAGES)} stages")
    return 0


if __name__ == "__main__":
    sys.exit(main())
