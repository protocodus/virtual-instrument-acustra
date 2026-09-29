#!/usr/bin/env python3
"""Check that the calibration vector's documented size is the one in use.

OptimizePhysicalModel.NAMES is the calibration vector the renderers take.
Both renderers' usage must list one argument per value, README must give the
same count, and the renderers must accept the full vector and no other
length (the builds that had nylon strings took 48 values).

    CalibrationVectorTests.py PHYSICAL_FIT_RENDERER EXTERNAL_CORPUS_RENDERER
"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
import OptimizePhysicalModel as optimiser  # noqa: E402

NOT_VALUES = {"OUTPUT", "JOBFILE", "OUTDIR"}


def usage_values(renderer: Path) -> list[str]:
    result = subprocess.run([str(renderer), "--help"], capture_output=True,
                            text=True, check=True)
    usage = result.stdout.splitlines()[0]
    tokens = re.findall(r"(?<![\w|-])[A-Z][A-Z0-9_]+(?![\w|])", usage)
    return [token for token in tokens if token not in NOT_VALUES]


def main() -> int:
    fit, corpus = (Path(argument).resolve() for argument in sys.argv[1:3])
    count = len(optimiser.NAMES)
    failures = []
    for renderer in (fit, corpus):
        listed = usage_values(renderer)
        if len(listed) != count or len(set(listed)) != count:
            failures.append(f"{renderer.name} usage lists {len(listed)} "
                            f"calibration values, the vector has {count}")
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for pattern in (r"calibration stores (\d+) bounded", r"the (\d+) calibration arguments"):
        found = re.findall(pattern, readme.replace("\n", " "))
        if not found:
            failures.append(f"README no longer says {pattern!r}")
        failures.extend(f"README says {value} for {pattern!r}, the vector has {count}"
                        for value in found if int(value) != count)
    # The renderer takes the full shipping vector and refuses a short one or
    # the nylon builds' 48.
    shipping = [format(float(value), ".9g") for value in optimiser.SHIPPING]
    with tempfile.TemporaryDirectory(prefix="acustra-calibration-") as temporary:
        for values in (shipping, shipping[:-1], shipping + ["0"] * (48 - count)):
            length = len(values)
            output = Path(temporary) / f"smoke-{length}"
            result = subprocess.run([str(fit), "--smoke", str(output), *values],
                                    capture_output=True, text=True)
            if length == count and result.returncode != 0:
                failures.append(f"{fit.name} --smoke with {length} values: "
                                f"{result.stderr.strip()}")
            if length != count and result.returncode == 0:
                failures.append(f"{fit.name} --smoke accepted {length} values, "
                                f"the vector has {count}")
    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print(f"The {count}-value calibration vector is documented and accepted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
