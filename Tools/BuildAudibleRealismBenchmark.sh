#!/usr/bin/env bash
set -euo pipefail

# Compile the same paired harness against each revision's OWN headers and DSP.
# Usage: bash Tools/BuildAudibleRealismBenchmark.sh OLD_SOURCE CURRENT_SOURCE NEW_BUILD [HARNESS.cpp]
# Source roots must be stable snapshots containing Source/DSP. For example:
#   mkdir old-source
#   git archive 9bf1cff | tar -x -C old-source
#   bash Tools/BuildAudibleRealismBenchmark.sh old-source frozen-current cpu-build
#   cpu-build/AcustraAudibleRealismBenchmark checks.json --natural-performance --untimed-check
# Coordinate an idle machine before timed runs. The script builds; it never times.
# Override ACUSTRA_CPU_COMPILER to select a compiler; both revisions use it.

script_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
baseline_root=${1:?Pass the frozen baseline root containing Source/DSP}
current_root=${2:?Pass the frozen current root containing Source/DSP}
benchmark_build=${3:?Pass a fresh output directory}
harness_source=${4:-$script_directory/AudibleRealismBenchmark.cpp}
compiler=${ACUSTRA_CPU_COMPILER:-c++}
compiler=$(command -v -- "$compiler")
flags=(-std=c++20 -O3 -DNDEBUG -fPIC -Wall -Wextra -Wpedantic)

if [[ -e "$benchmark_build" ]]; then
    echo "Preserve previous evidence: build output already exists" >&2
    exit 1
fi
for source_root in "$baseline_root" "$current_root"; do
    if [[ ! -f "$source_root/Source/DSP/AcustraEngine.cpp" || ! -f "$source_root/Source/DSP/AcustraPerformer.cpp" ]]; then
        echo "Both source snapshots must contain the canonical engine and Performer" >&2
        exit 1
    fi
done
mkdir -p -- "$benchmark_build"
cp -- "$harness_source" "$benchmark_build/AudibleRealismBenchmark.cpp"
cp -- "${BASH_SOURCE[0]}" "$benchmark_build/BuildAudibleRealismBenchmark.sh"
harness_source=$benchmark_build/AudibleRealismBenchmark.cpp

python3 - "$baseline_root" "$current_root" "$benchmark_build" "$compiler" <<'PY'
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys

baseline, current, build, compiler = map(Path, sys.argv[1:])
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def tree(root):
    return {str(path.relative_to(root)): digest(path)
            for path in sorted((root / "Source" / "DSP").rglob("*")) if path.is_file()}
manifest = {
    "compiler": subprocess.check_output([str(compiler), "--version"], text=True).splitlines()[0],
    "compiler_path": str(compiler.resolve()),
    "flags": ["-std=c++20", "-O3", "-DNDEBUG", "-fPIC", "-Wall", "-Wextra", "-Wpedantic"],
    "namespace_definitions": ["acustra=acustra_baseline", "acustra=acustra_current"],
    "platform": platform.platform(),
    "cpu_affinity": sorted(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else None,
    "harness_sha256": digest(build / "AudibleRealismBenchmark.cpp"),
    "build_script_sha256": digest(build / "BuildAudibleRealismBenchmark.sh"),
    "baseline_source": str(baseline.resolve()), "current_source": str(current.resolve()),
    "baseline_files_sha256": tree(baseline), "current_files_sha256": tree(current),
    "complete": False,
}
cpuinfo = Path("/proc/cpuinfo")
if cpuinfo.exists():
    manifest["cpu_model"] = next((line.split(":", 1)[1].strip()
        for line in cpuinfo.read_text().splitlines() if line.startswith("model name")), "unknown")
(build / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
PY

"$compiler" "${flags[@]}" -c "$harness_source" -o "$benchmark_build/main.o"
for version in baseline current; do
    if [[ $version == baseline ]]; then source_root=$baseline_root; else source_root=$current_root; fi
    definitions=(-Dacustra=acustra_$version -DACUSTRA_REALISM_ADAPTER=$version)
    "$compiler" "${flags[@]}" "${definitions[@]}" -I "$source_root/Source" -c "$harness_source" -o "$benchmark_build/$version-adapter.o"
    "$compiler" "${flags[@]}" "${definitions[@]}" -I "$source_root/Source" -c "$source_root/Source/DSP/AcustraEngine.cpp" -o "$benchmark_build/$version-engine.o"
    "$compiler" "${flags[@]}" "${definitions[@]}" -I "$source_root/Source" -c "$source_root/Source/DSP/AcustraPerformer.cpp" -o "$benchmark_build/$version-performer.o"
done
"$compiler" "${flags[@]}" "$benchmark_build/main.o" "$benchmark_build/baseline-adapter.o" \
    "$benchmark_build/baseline-engine.o" "$benchmark_build/baseline-performer.o" \
    "$benchmark_build/current-adapter.o" "$benchmark_build/current-engine.o" \
    "$benchmark_build/current-performer.o" -o "$benchmark_build/AcustraAudibleRealismBenchmark"

python3 - "$benchmark_build" <<'PY'
import hashlib
import json
from pathlib import Path
import sys

build = Path(sys.argv[1])
manifest_path = build / "manifest.json"
manifest = json.loads(manifest_path.read_text())
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
for version in ("baseline", "current"):
    root = Path(manifest[f"{version}_source"])
    actual = {str(path.relative_to(root)): digest(path)
              for path in sorted((root / "Source" / "DSP").rglob("*")) if path.is_file()}
    if actual != manifest[f"{version}_files_sha256"]:
        raise SystemExit(f"{version} source changed during compilation; freeze again and rebuild")
if digest(build / "AudibleRealismBenchmark.cpp") != manifest["harness_sha256"]:
    raise SystemExit("frozen harness changed during compilation")
manifest["executable_sha256"] = digest(build / "AcustraAudibleRealismBenchmark")
manifest["complete"] = True
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
print(build / "AcustraAudibleRealismBenchmark")
PY
