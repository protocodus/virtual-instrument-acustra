#!/usr/bin/env python3
"""Reproduce the wet-side and deferred room anti-alias experiments.

Requires NumPy/SciPy and a freshly rebuilt JUCE-free AcustraDSP library.
Writes the native impulse responses, probe source/binary, measurements and
source/library hashes into a new output directory. Never changes model data.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

import numpy as np
import scipy
from scipy import signal


PROBE = r'''
#include "DSP/AcustraEngine.h"
#include <array>
#include <cmath>
#include <complex>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include "antialias.h"
namespace acustra { struct AcustraEngineTestAccess {
static auto room(double rate) { auto p=std::make_unique<AcustraEngine::RoomAmbience>();
p->prepare(rate); return p; }
}; }
int main(int argc,char** argv) {
if(argc!=2)return 1;
constexpr double pi=3.14159265358979323846;
const std::string dir=argv[1];
auto room=acustra::AcustraEngineTestAccess::room(48000.0);
std::ofstream raw(dir+"/room-baseline.f32",std::ios::binary);
std::ofstream candidate(dir+"/room-side-150.f32",std::ios::binary);
for(int i=0;i<48000;++i) {
 float l=0,r=0;room->process(i==0?1.f:0.f,l,r);
 raw.write(reinterpret_cast<const char*>(&l),4);raw.write(reinterpret_cast<const char*>(&r),4);
 const float mid=.5f*(l+r),side=room->coherentSide(.5f*(l-r));
 const float cl=mid+side,cr=mid-side;
 candidate.write(reinterpret_cast<const char*>(&cl),4);candidate.write(reinterpret_cast<const char*>(&cr),4);
}
std::cout<<"rate,source_hz,observed_hz,baseline_db,candidate_db,reduction_db\n";
for(double rate:{64001.,96000.,192000.})for(double hz:{8000.,18000.,22000.,30000.,42000.,52000.}) {
 if(hz>=rate/2)continue;
 auto reference=acustra::AcustraEngineTestAccess::room(rate);
 const double internal=rate/reference->decimation;
 const double observed=std::abs(hz-std::round(hz/internal)*internal);
 double levels[2]{};
 for(int trial=0;trial<2;++trial) {
  auto q=acustra::AcustraEngineTestAccess::room(rate);
  const double* taps=rate==64001?fir64001:rate==96000?fir96000:fir192000;
  std::array<double,63> history{};int position=0;std::complex<double> projected{};
  for(int i=0;i<int(rate*2);++i) {
   double input=std::sin(2*pi*hz*i/rate);
   if(trial) {history[position]=input;input=0;
    for(int j=0;j<63;++j)input+=taps[j]*history[(position+63-j)%63];
    position=(position+1)%63;}
   float l=0,r=0;q->process(float(input),l,r);
   if(i>=int(rate))projected+=double(l)*std::exp(std::complex<double>(0,-2*pi*observed*i/rate));
  }
  levels[trial]=20*std::log10(2*std::abs(projected)/rate+1e-30);
 }
 std::cout<<std::setprecision(10)<<rate<<','<<hz<<','<<observed<<','
  <<levels[0]<<','<<levels[1]<<','<<levels[1]-levels[0]<<'\n';
}
}
'''


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build-dsp"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--compiler", default="c++")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    library = build / "libAcustraDSP.a"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    probe = output / "room-probe.cpp"
    probe.write_text(PROBE)
    with (output / "antialias.h").open("w") as header:
        for rate in (64001, 96000, 192000):
            internal = rate / int(np.ceil(rate / 64000))
            taps = signal.firwin(63, .44 * internal, window=("kaiser", 8), fs=rate)
            header.write(f"static const double fir{rate}[63]={{"
                         + ",".join(format(t, ".17g") for t in taps) + "};\n")
    binary = output / "room-probe"
    command = [args.compiler, "-std=c++17", "-O2", "-I" + str(root / "Source"),
               str(probe), str(library), "-o", str(binary)]
    subprocess.run(command, check=True)
    alias_csv = subprocess.check_output([str(binary), str(output)], text=True)
    (output / "alias-comparison.csv").write_text(alias_csv)
    before = np.fromfile(output / "room-baseline.f32", np.float32).reshape(-1, 2)
    after = np.fromfile(output / "room-side-150.f32", np.float32).reshape(-1, 2)
    rows = []
    late_rows = []
    for low, high in ((40, 120), (120, 250), (250, 500), (500, 1000), (1000, 4000)):
        sos = signal.butter(4, [low, high], btype="bandpass", fs=48000, output="sos")
        a = signal.sosfilt(sos, before, axis=0)
        b = signal.sosfilt(sos, after, axis=0)
        rows.append(dict(low_hz=low, high_hz=high,
            energy_change_db=float(10 * np.log10(np.sum(b*b) / np.sum(a*a))),
            baseline_correlation=float(np.corrcoef(a.T)[0, 1]),
            candidate_correlation=float(np.corrcoef(b.T)[0, 1])))
        late = signal.sosfilt(sos, before[1200:], axis=0)
        late_rows.append(dict(low_hz=low, high_hz=high,
            baseline_correlation=float(np.corrcoef(late.T)[0, 1])))
    corner_screen = []
    mid = before.astype(float).mean(1)
    side = .5 * (before[:, 0].astype(float) - before[:, 1])
    for corner in (100, 150, 200):
        filtered = signal.sosfilt(signal.butter(2, corner, btype="highpass",
                                               fs=48000, output="sos"), side)
        stereo = np.column_stack((mid + filtered, mid - filtered))
        values = []
        for low, high in ((40, 120), (120, 250)):
            sos = signal.butter(4, [low, high], btype="bandpass", fs=48000, output="sos")
            a = signal.sosfilt(sos, before, axis=0)
            b = signal.sosfilt(sos, stereo, axis=0)
            values.append(dict(low_hz=low, high_hz=high,
                energy_change_db=float(10 * np.log10(np.sum(b*b) / np.sum(a*a)))))
        corner_screen.append(dict(corner_hz=corner, bands=values))
    report = dict(rate=48000, duration_seconds=1, bands=rows,
        late_after_25ms_bands=late_rows, corner_screen=corner_screen,
        mono_sum_max_rounding_error=float(np.max(np.abs(
            before.astype(float).sum(1) - after.astype(float).sum(1)))),
        alias_trials=list(csv.DictReader(alias_csv.splitlines())),
        decision="150 Hz wet-side filter selected for audition; anti-alias FIR deferred",
        listening="No listening preference established by these measurements.")
    (output / "measurements.json").write_text(json.dumps(report, indent=2) + "\n")
    sources = [root / "Source/DSP/AcustraEngine.cpp", root / "Source/DSP/AcustraEngine.h",
               root / "Source/DSP/PerformanceRealism.h", Path(__file__).resolve(), library,
               probe, output / "antialias.h", binary]
    manifest = dict(command=command, compiler=subprocess.check_output(
        [args.compiler, "--version"], text=True).splitlines()[0],
        git_head=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        git_diff_sha256=hashlib.sha256(subprocess.check_output(["git", "diff", "--binary"], cwd=root)).hexdigest(),
        files={str(p): digest(p) for p in sources}, numpy=np.__version__, scipy=scipy.__version__)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
