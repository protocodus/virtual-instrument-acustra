#!/bin/sh
# Builds Tools/CpuRegressionStream.cpp against a DSP tree, with the Rack-like
# flags (default) or the host flags. See the protocol at the head of that file.
#
#   Tools/BuildCpuRegressionStream.sh <out-binary> [rack|host] [source-root=.]
#
# <source-root> lets a frozen baseline tree (a git worktree) be built with the
# same tool and flags as the candidate.
set -eu
out=$1
mode=${2:-rack}
root=${3:-$(dirname "$0")/..}
tools=$(cd "$(dirname "$0")" && pwd)
cxx=${CXX:-clang++}
flags="-std=c++17 -O3 -DNDEBUG -DACUSTRA_EXTENDED_SAMPLE_RATES=1 -g"
if [ "$mode" = rack ]; then flags="$flags -fno-builtin"; fi
$cxx $flags -I"$root/Source" -I"$tools" \
    "$root/Source/DSP/AcustraEngine.cpp" "$root/Source/DSP/AcustraPerformer.cpp" \
    "$tools/CpuRegressionStream.cpp" -o "$out"
