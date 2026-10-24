#!/usr/bin/env bash
set -euo pipefail
# Build Tools/SteadyCallbackBenchmark.cpp over two frozen source trees.
# Usage: bash Tools/BuildSteadyCallbackBenchmark.sh BASELINE_ROOT CANDIDATE_ROOT OUT_DIR
# Each root holds Source/DSP. CXX picks the compiler (default c++);
# BASELINE_FLAGS / CANDIDATE_FLAGS add flags to one side only, e.g.
# CANDIDATE_FLAGS=-fno-semantic-interposition to time AcustraDSP's GCC flag.
# Both sides otherwise use -std=c++20 -O3 -DNDEBUG -fPIC, as AcustraDSP does.
script_directory=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
baseline_root=${1:?Pass the frozen baseline root}
candidate_root=${2:?Pass the frozen candidate root}
out=${3:?Pass a fresh output directory}
compiler=${CXX:-c++}
flags=(-std=c++20 -O3 -DNDEBUG -fPIC -Wall -Wextra)
if [[ -e "$out" ]]; then echo 'Output already exists' >&2; exit 1; fi
mkdir -p -- "$out"
source_file="$script_directory/SteadyCallbackBenchmark.cpp"
"$compiler" "${flags[@]}" -c "$source_file" -o "$out/main.o"
for version in baseline current; do
    if [[ $version == baseline ]]; then
        root=$baseline_root; read -r -a extra <<< "${BASELINE_FLAGS:-}"
    else
        root=$candidate_root; read -r -a extra <<< "${CANDIDATE_FLAGS:-}"
    fi
    definitions=(-Dacustra=acustra_$version)
    "$compiler" "${flags[@]}" "${extra[@]}" "${definitions[@]}" -DADAPTER=$version \
        -I "$root/Source" -c "$source_file" -o "$out/$version-adapter.o"
    for unit in AcustraEngine AcustraPerformer; do
        "$compiler" "${flags[@]}" "${extra[@]}" "${definitions[@]}" -I "$root/Source" \
            -c "$root/Source/DSP/$unit.cpp" -o "$out/$version-$unit.o"
    done
done
"$compiler" "${flags[@]}" "$out"/main.o "$out"/baseline-*.o "$out"/current-*.o \
    -o "$out/AcustraSteadyCallbackBenchmark"
echo "$out/AcustraSteadyCallbackBenchmark"
