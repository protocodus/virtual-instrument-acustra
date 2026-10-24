#!/usr/bin/env python3
"""Summarise a Tools/SteadyCallbackBenchmark.cpp CSV.

Per workload: callback count, baseline/candidate thread-CPU medians, the
median of per-callback candidate/baseline ratios, the reduction of the mean
callback CPU (all callbacks, and with pairs above either side's 98th
percentile dropped), and the 95th/99th percentiles. The aggregate is the
geometric mean over workloads of the candidate/baseline mean ratio, also
reported per repeat so its spread can be compared with a self-versus-self
run (the same tree on both sides), which is the noise floor.

Usage: python3 Tools/AnalyzeSteadyCallbackBenchmark.py RUN.csv
"""
import collections
import csv
import math
import statistics
import sys


def quantile(values, fraction):
    ordered = sorted(values)
    index = max(0, math.ceil(fraction * len(ordered)) - 1)
    return ordered[min(index, len(ordered) - 1)]


def geometric_mean(values):
    return math.exp(sum(math.log(value) for value in values) / len(values))


def trimmed_ratio(pairs):
    base_limit = quantile([b for b, _ in pairs], 0.98)
    candidate_limit = quantile([c for _, c in pairs], 0.98)
    kept = [(b, c) for b, c in pairs if b <= base_limit and c <= candidate_limit]
    return sum(c for _, c in kept) / sum(b for b, _ in kept)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    workloads = collections.defaultdict(list)
    with open(sys.argv[1], newline='') as handle:
        for row in csv.DictReader(handle):
            workloads[row['workload']].append(
                (int(row['rep']), int(row['baseline_ns']) / 1000.0,
                 int(row['current_ns']) / 1000.0))
    print(f"{'workload':18} {'n':>6} {'base p50':>8} {'cand p50':>8} "
          f"{'ratio p50':>9} {'mean red%':>9} {'trim red%':>9} "
          f"{'base p95':>8} {'cand p95':>8} {'base p99':>8} {'cand p99':>8}")
    mean_ratios, trimmed_ratios = [], []
    for name, rows in workloads.items():
        base = [b for _, b, _ in rows]
        candidate = [c for _, _, c in rows]
        mean_ratio = sum(candidate) / sum(base)
        trimmed = trimmed_ratio(list(zip(base, candidate)))
        mean_ratios.append(mean_ratio)
        trimmed_ratios.append(trimmed)
        # A callback can read zero thread CPU at the clock's granularity.
        paired = statistics.median(c / b for b, c in zip(base, candidate) if b > 0)
        print(f"{name:18} {len(rows):6} {quantile(base, .5):8.2f} "
              f"{quantile(candidate, .5):8.2f} {paired:9.4f} "
              f"{100 * (1 - mean_ratio):9.2f} {100 * (1 - trimmed):9.2f} "
              f"{quantile(base, .95):8.2f} {quantile(candidate, .95):8.2f} "
              f"{quantile(base, .99):8.2f} {quantile(candidate, .99):8.2f}")
    print(f"aggregate mean reduction {100 * (1 - geometric_mean(mean_ratios)):.2f}% "
          f"(trimmed {100 * (1 - geometric_mean(trimmed_ratios)):.2f}%)")
    repeats = sorted({rep for rows in workloads.values() for rep, _, _ in rows})
    for repeat in repeats:
        ratios = []
        for rows in workloads.values():
            chosen = [(b, c) for rep, b, c in rows if rep == repeat]
            ratios.append(sum(c for _, c in chosen) / sum(b for b, _ in chosen))
        print(f"  repeat {repeat}: mean reduction "
              f"{100 * (1 - geometric_mean(ratios)):.2f}%")


if __name__ == '__main__':
    main()
