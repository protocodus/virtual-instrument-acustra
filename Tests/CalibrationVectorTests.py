#!/usr/bin/env python3
"""Check that the calibration vector's documented size is the one in use.

OptimizePhysicalModel.NAMES is the calibration vector the renderer takes.
Its usage must list one argument per value, README must give the same
count, and the renderer must accept the full vector and no other length
(previous schemas took 37 or 48 values).

    CalibrationVectorTests.py EXTERNAL_CORPUS_RENDERER
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
    renderer = Path(sys.argv[1]).resolve()
    count = len(optimiser.NAMES)
    failures = []
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
    # previous schemas' 37 and 48.
    shipping = [format(float(value), ".9g") for value in optimiser.SHIPPING]
    # One note keeps the accepted run short; the renderer checks the vector's
    # length before it reads the job file, so the rejected runs render nothing.
    with tempfile.TemporaryDirectory(prefix="acustra-calibration-") as temporary:
        jobs = Path(temporary) / "jobs.txt"
        jobs.write_text("c4 finger 60 100\n")
        for values in (shipping, shipping[:-1], shipping + ["0"] * (37 - count),
                       shipping + ["0"] * (48 - count)):
            length = len(values)
            output = Path(temporary) / f"out-{length}"
            result = subprocess.run([str(renderer), str(jobs), str(output), *values],
                                    capture_output=True, text=True)
            if length == count and result.returncode != 0:
                failures.append(f"{renderer.name} with {length} values: "
                                f"{result.stderr.strip()}")
            # 4.2 s of 48 kHz stereo float32.
            if length == count and result.returncode == 0:
                render = output / "model-c4.f32"
                if not render.is_file() or render.stat().st_size != 4.2 * 48000 * 2 * 4:
                    failures.append(f"{renderer.name} did not write {render.name} "
                                    "as 4.2 s of 48 kHz stereo float32")
            if length != count and result.returncode == 0:
                failures.append(f"{renderer.name} accepted {length} values, "
                                f"the vector has {count}")
    for failure in failures:
        print(f"FAIL: {failure}")
    if failures:
        return 1
    print(f"The {count}-value calibration vector is documented and accepted")
    return 0


if __name__ == "__main__":
    sys.exit(main())
