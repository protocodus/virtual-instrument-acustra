#!/usr/bin/env python3
"""Check that Tools/OptimizePhysicalModel.py cannot refit a listening verdict.

Values a listener chose are frozen (BY_EAR) so that no stage undoes them.
Every fitting stage must preserve those values and still have something left
to search, and none may search a value the engine no longer reads (RETIRED).
"""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Tools"))
import OptimizePhysicalModel as optimiser  # noqa: E402


def main() -> int:
    failures = []
    names = optimiser.NAMES
    if len(set(optimiser.BY_EAR)) != len(optimiser.BY_EAR):
        failures.append("a name is listed more than once in BY_EAR")
    for name in optimiser.BY_EAR:
        if name not in names:
            failures.append(f"frozen name {name} is not a calibration value")
    for name in optimiser.RETIRED:
        if name not in names:
            failures.append(f"retired name {name} is not a calibration value")
    retired = {names.index(name) for name in optimiser.RETIRED
               if name in names}
    for stage, free in optimiser.STAGES.items():
        refit = sorted(names[index] for index in free
                       if index in optimiser.FROZEN)
        if refit:
            failures.append(f"stage {stage} refits frozen {', '.join(refit)}")
        # A value the engine no longer reads gives a stage nothing to find.
        searched = sorted(names[index] for index in free if index in retired)
        if searched:
            failures.append(f"stage {stage} searches retired {', '.join(searched)}")
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
