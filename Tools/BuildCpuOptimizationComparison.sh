#!/usr/bin/env bash
set -euo pipefail
# Build an untimed old/new exact-output and public-state comparison. Both source
# trees must be frozen; each namespace compiles against that tree's own headers.
# Usage: bash Tools/BuildCpuOptimizationComparison.sh OLD_SOURCE NEW_SOURCE NEW_BUILD
# Optional ACUSTRA_PARITY_COMPILER selects the SAME compiler for both revisions.
script_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
baseline_root=${1:?Pass frozen baseline source}
current_root=${2:?Pass frozen candidate source}
build_root=${3:?Pass a fresh build directory}
compiler=$(command -v -- "${ACUSTRA_PARITY_COMPILER:-c++}")
flags=(-std=c++17 -O3 -DNDEBUG -fPIC -Wall -Wextra -Wpedantic -Werror)
if [[ -e "$build_root" ]]; then echo 'Preserve existing evidence: output already exists' >&2; exit 1; fi
for source_root in "$baseline_root" "$current_root"; do
    test -f "$source_root/Source/DSP/AcustraEngine.cpp"
    test -f "$source_root/Source/DSP/AcustraPerformer.cpp"
done
mkdir -p -- "$build_root"
cp -- "$script_directory/CompareCpuOptimization.cpp" "$build_root/CompareCpuOptimization.cpp"
cp -- "$script_directory/../Tests/PerformanceBattery.h" "$build_root/PerformanceBattery.h"
cp -- "${BASH_SOURCE[0]}" "$build_root/BuildCpuOptimizationComparison.sh"
python3 - "$baseline_root" "$current_root" "$build_root" "$compiler" <<'PY'
from pathlib import Path
import hashlib, json, platform, subprocess, sys
baseline,current,build,compiler=map(Path,sys.argv[1:])
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def tree(root):return {str(p.relative_to(root)):digest(p) for p in sorted((root/'Source'/'DSP').rglob('*')) if p.is_file()}
m={'complete':False,'baseline_source':str(baseline.resolve()),'current_source':str(current.resolve()),
   'baseline_files_sha256':tree(baseline),'current_files_sha256':tree(current),
   'tool_files_sha256':{p.name:digest(p) for p in build.iterdir() if p.is_file()},
   'compiler':subprocess.check_output([str(compiler),'--version'],text=True).splitlines()[0],
   'compiler_path':str(compiler.resolve()),'platform':platform.platform(),
   'flags':['-std=c++17','-O3','-DNDEBUG','-fPIC','-Wall','-Wextra','-Wpedantic','-Werror'],
   'definitions':['acustra=acustra_baseline','acustra=acustra_current'],
   'comparison':'memcmp each Main L/R and requested Piezo sample; exact serialized public state words; no timing'}
(build/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
PY
"$compiler" "${flags[@]}" -c "$build_root/CompareCpuOptimization.cpp" -o "$build_root/main.o"
for version in baseline current; do
    if [[ $version == baseline ]]; then source_root=$baseline_root; else source_root=$current_root; fi
    definitions=(-Dacustra=acustra_$version -DACUSTRA_PARITY_ADAPTER=$version)
    "$compiler" "${flags[@]}" "${definitions[@]}" -I "$source_root/Source" -c "$build_root/CompareCpuOptimization.cpp" -o "$build_root/$version-adapter.o"
    "$compiler" "${flags[@]}" "${definitions[@]}" -I "$source_root/Source" -c "$source_root/Source/DSP/AcustraEngine.cpp" -o "$build_root/$version-engine.o"
    "$compiler" "${flags[@]}" "${definitions[@]}" -I "$source_root/Source" -c "$source_root/Source/DSP/AcustraPerformer.cpp" -o "$build_root/$version-performer.o"
done
"$compiler" "${flags[@]}" "$build_root/main.o" "$build_root/baseline-adapter.o" "$build_root/baseline-engine.o" "$build_root/baseline-performer.o" "$build_root/current-adapter.o" "$build_root/current-engine.o" "$build_root/current-performer.o" -o "$build_root/AcustraCpuOptimizationComparison"
python3 - "$build_root" <<'PY'
from pathlib import Path
import hashlib,json,sys
build=Path(sys.argv[1]); manifest=build/'manifest.json'; m=json.loads(manifest.read_text())
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
for version in ('baseline','current'):
    root=Path(m[f'{version}_source'])
    actual={str(p.relative_to(root)):digest(p) for p in sorted((root/'Source'/'DSP').rglob('*')) if p.is_file()}
    if actual!=m[f'{version}_files_sha256']:raise SystemExit(version+' source changed while compiling')
for name,value in m['tool_files_sha256'].items():
    if digest(build/name)!=value:raise SystemExit('harness changed while compiling')
m['executable_sha256']=digest(build/'AcustraCpuOptimizationComparison'); m['complete']=True
manifest.write_text(json.dumps(m,indent=2)+'\n')
print(build/'AcustraCpuOptimizationComparison')
PY
