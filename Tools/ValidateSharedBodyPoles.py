#!/usr/bin/env python3
"""Revalidate retained joint-body residues under the common digital-pole basis.

This is a convention migration check, not a refit. Requires the MD5-verified
qualified_selected_impulses.mat from https://zenodo.org/records/4604577.
It compares both conventions to the original six-position bridge targets,
preserves every committed coefficient, and optionally updates only provenance.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import re

import numpy as np

import GenerateJointBody as joint


def legacy_mobility(rows, frequency, arm):
    s = 2j * joint.FS * np.tan(np.pi * frequency / joint.FS)
    total = np.zeros(len(frequency), complex)
    for f, q, h, c, r in rows:
        omega = 2 * joint.FS * np.tan(np.pi * f / joint.FS)
        total += (h + 2*arm*c + arm*arm*r) * s / (s*s + omega/q*s + omega*omega)
    return total


def validate(raw: Path, output: Path, update_provenance: bool = False):
    repo = Path(__file__).resolve().parents[1]
    header = repo / "Source/DSP/MeasuredJointBodyData.h"
    source = header.read_text()
    rows = np.array([[float(x.rstrip("f")) for x in
                      re.findall(r"[-+]?\d+(?:\.\d*)?(?:e[-+]?\d+)?f", line)]
                     for line in source.splitlines() if line.strip().startswith("{ ")])
    if rows.shape != (131, 17):
        raise ValueError("Expected the retained 131-mode joint bank; review the migration check")
    rows = np.column_stack((rows[:, :2], rows[:, -3:]))
    blend = (repo / "Source/DSP/SteelBodyBlend.h").read_text()
    cutoff = float(re.search(r"#define ACUSTRA_STEEL_BLEND_JOINT_BAND_HZ (\d+(?:\.\d+)?)f", blend)[1])
    frequency, treble, bass, cross = joint.bridge.extract_two_point(raw, 21)
    corner, _ = joint.bridge.coherence_corner(raw, 21)

    def stats(fn, subset, lo, hi):
        index = np.flatnonzero((frequency >= lo) & (frequency <= hi))[::3]
        grid = frequency[index]
        rotation = np.exp(2j*np.pi*grid*joint.ALIGNMENT/joint.FS)
        tt, bb, tb = [x[index]*rotation for x in (treble, bass, cross)]
        model, target = [], []
        for u in joint.bridge.STRING_LEVER_ARMS:
            model.append(fn(subset, grid, u))
            two = ((1+u)**2*tt+(1-u)**2*bb+2*(1-u*u)*tb)/4
            target.append(np.where(grid < corner, two, .5*(tt+bb)))
        model, target = np.array(model), np.array(target)
        return dict(relative_complex_error=float(np.linalg.norm(model-target)/np.linalg.norm(target)),
                    worst_median_magnitude_error_db=float(max(np.median(abs(20*np.log10(
                        np.maximum(abs(m), 1e-30)/np.maximum(abs(t), 1e-30))))
                        for m, t in zip(model, target))))

    report = dict(raw_url="https://zenodo.org/records/4604577",
                  raw_md5=joint.bridge.digest(raw), sample_rate=joint.FS,
                  corner_hz=float(corner), runtime_cutoff_hz=cutoff,
                  note="Retained post-plate-Q residues, no refit. Historical header errors precede that Q change.")

    # g21's separately fitted bridge also has shared modes at runtime. Apply
    # precisely its already-existing twin f/Q substitutions and unpaired Q
    # ratios, before the authored anchor/air extension, Shape, Wood or levels.
    own_text = (repo / "Source/DSP/MeasuredBridgeData.h").read_text()
    radiation_text = (repo / "Source/DSP/MeasuredBodyData.h").read_text()
    def array_block(text, name):
        return text.split(name + " {{", 1)[1].split("}};", 1)[0]
    def float_rows(text):
        return np.array([[float(x.rstrip("f")) for x in
                          re.findall(r"[-+]?\d+(?:\.\d*)?(?:e[-+]?\d+)?f", line)]
                         for line in text.splitlines() if line.strip().startswith("{ ")])
    own = float_rows(array_block(own_text, "measuredSteelBridgeModes"))
    radiation = float_rows(array_block(radiation_text, "measuredSteelBodyModes"))
    twins = np.array([int(x) for x in re.findall(r"-?\d+", array_block(own_text, "steelBridgeRadiationTwins"))])
    unpaired_q = np.array([float(x.rstrip("f")) for x in re.findall(
        r"[-+]?\d+(?:\.\d*)?(?:e[-+]?\d+)?f", array_block(own_text, "steelBridgeUnpairedQRatio"))])
    for index, twin in enumerate(twins):
        if twin >= 0:
            own[index, :2] = radiation[twin, :2]
        else:
            own[index, 1] *= unpaired_q[index]
    def corrected_own(subset, frequency, arm):
        return (joint.string_mobility(subset[twins >= 0], frequency, arm)
                + legacy_mobility(subset[twins < 0], frequency, arm))
    report["own_bank_convention"] = "Existing twin f/Q substitutions and unpaired Q ratios; before authored anchor, air extension, Shape, Wood and level calibration. Unpaired prototypes unchanged."
    for name, subset, corrected in (("full", rows, joint.string_mobility),
                                    ("kept_runtime_subset", rows[rows[:, 0] < cutoff], joint.string_mobility),
                                    ("own_aligned_bank", own, corrected_own)):
        report[name] = {}
        for lo, hi in ((60, 600), (60, 10000)):
            result = dict(old=stats(legacy_mobility, subset, lo, hi),
                          common_poles=stats(corrected, subset, lo, hi))
            grid = np.geomspace(lo, hi, 2000)
            before = np.array([legacy_mobility(subset, grid, u) for u in joint.bridge.STRING_LEVER_ARMS])
            after = np.array([corrected(subset, grid, u) for u in joint.bridge.STRING_LEVER_ARMS])
            result["relative_transfer_change"] = float(np.linalg.norm(after-before)/np.linalg.norm(before))
            result["minimum_real_mobility"] = float(after.real.min())
            if result["minimum_real_mobility"] < -1e-10:
                raise ValueError("Corrected bank has negative driving-point conductance")
            report[name][f"{lo}-{hi}"] = result
    full = report["full"]["60-10000"]["common_poles"]
    if (full["relative_complex_error"] > joint.bridge.MAX_COMPLEX_RELATIVE_ERROR
            or full["worst_median_magnitude_error_db"] > joint.bridge.MAX_MEDIAN_MAGNITUDE_ERROR_DB):
        raise ValueError("Corrected convention missed the unchanged measured bridge fit gates")
    output.write_text(json.dumps(report, indent=2) + "\n")
    if update_provenance:
        start, end = "// BEGIN SHARED-POLE REVALIDATION\n", "// END SHARED-POLE REVALIDATION\n"
        if start in source:
            source = source[:source.index(start)] + source[source.index(end)+len(end):]
        annotation = (start
            + "// Revalidated by Tools/ValidateSharedBodyPoles.py against the original\n"
            + "// MD5-verified g21 measurements; every frequency, Q and residue retained.\n"
            + "// The historical fit used damped f/Q for radiation but natural f/Q for\n"
            + "// mobility. Runtime now shares actual digital poles via a=2fs(p-1)/(p+1),\n"
            + "// Y=R*s/(s^2-2Re(a)*s+|a|^2), p=exp((-pi*f/Q+j*2pi*f)/fs).\n"
            + "// This is a basis correction with revalidation, not a new measured fit.\n"
            + f"// Retained post-plate-Q bank: complex error {full['relative_complex_error']:.6f},\n"
            + f"// worst median magnitude error {full['worst_median_magnitude_error_db']:.6f} dB;\n"
            + "// unchanged gates 0.24 and 1.6 dB. Original pre-Q fit metrics follow.\n" + end)
        # Keep the historical generated fit description, identified explicitly
        # by the migration provenance above it; numerical table bytes stay put.
        updated = source.replace("// Robert Mores,", annotation + "// Robert Mores,", 1)
        if updated[updated.index("#pragma once"):] != source[source.index("#pragma once"):]:
            raise AssertionError("Provenance update touched modal data")
        header.write_text(updated)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--raw-mat", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--update-provenance", action="store_true")
    args = parser.parse_args()
    print(json.dumps(validate(args.raw_mat, args.output, args.update_provenance), indent=2))
