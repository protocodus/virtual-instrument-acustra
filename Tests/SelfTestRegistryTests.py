#!/usr/bin/env python3
"""Check that every Python tool with a self-test runs under ctest.

A tool that defines --self-test or --smoke but is not named in CMakeLists.txt
is a check nobody runs: CI only runs ctest.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    registered = set(re.findall(r"CMAKE_CURRENT_SOURCE_DIR}/Tools/([A-Za-z0-9_]+\.py)\"", cmake))
    defined = sorted(path.name for path in (ROOT / "Tools").glob("*.py")
                     if re.search(r"add_argument\(\s*[\"']--(self-test|smoke)[\"']",
                                  path.read_text(encoding="utf-8")))
    missing = [name for name in defined if name not in registered]
    for name in missing:
        print(f"FAIL: Tools/{name} has a self-test that ctest never runs")
    if missing:
        return 1
    print(f"All {len(defined)} Python tools with a self-test are registered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
