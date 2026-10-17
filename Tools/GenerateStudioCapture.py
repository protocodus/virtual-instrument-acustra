#!/usr/bin/env python3
"""Generate the approved studio-observer blend's rate-independent data.

Requires NumPy. Inputs are the repository's CC-BY-4.0 Mores pressure residues;
no prepared runtime coefficient dump or sample rate enters this generation.
Geometry is the declared research prior, not an identified studio sound field.
See Docs/research/capture-combinations-2026-10-11.md for the listening decision/limits.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
C = 343.0
ORDER = 112
POLE_SCALE = 1000.0
XYZ = np.array([[.10, 0., .10], [-.10, 0., .10], [.10, .20, .10]])
REFERENCE = dict(width=.264, length=.490, depth=.091, hole_radius=.042,
                 hole_y=.14, plate_ry=.164, outline=.72)
# Width, length, depth, hole diameter, outline factor; engine BodyGeometry.
BODIES = ((.343, .467, .108, .1016, .72),
          (.381, .492, .105, .1016, .72),
          (.397, .508, .111, .1016, .75),
          (.432, .533, .124, .1016, .72))
SELECTION = (("MeasuredBodyData.h", 0), ("MeasuredBodyData.h", 1),
             ("MeasuredJointBodyData.h", 1), ("MeasuredJointBodyData.h", 2))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_modes(path):
    rows = []
    for match in re.finditer(r'^\s*\{ ([^{}]+) \},$', path.read_text(), re.M):
        fields = match[1].replace('f', '').split(',')
        if len(fields) in (14, 17):
            rows.append([float(x) for x in fields[:14]])
    table = np.array(rows)
    pressure = np.empty((len(rows), 3, 2), complex)
    for axis in range(2):
        for mic in range(3):
            k = 2 + 6 * axis + 2 * mic
            pressure[:, mic, axis] = table[:, k] + 1j * table[:, k + 1]
    return table[:, :2], pressure


def quadrature(g):
    """Unit volume-flow columns: lower-bout plate and uniform circular hole.

    Area-uniform 24-point radial Gauss/96 midpoint angular quadrature; plate
    velocity profile (1-r^2)^2, excluding the hole. These are explicit priors.
    """
    u, w = np.polynomial.legendre.leggauss(24)
    u, w = (u + 1) / 2, w / 2
    theta = (np.arange(96) + .5) * 2 * np.pi / 96
    radius, angle = np.meshgrid(np.sqrt(u), theta, indexing='ij')
    weights = np.repeat(w, 96) / 96
    plate = np.column_stack((g['width'] / 2 * radius.ravel() * np.cos(angle.ravel()),
                             g['plate_ry'] * radius.ravel() * np.sin(angle.ravel()),
                             np.zeros(24 * 96)))
    pw = weights * np.repeat((1 - u) ** 2, 96)
    pw[plate[:, 0] ** 2 + (plate[:, 1] - g['hole_y']) ** 2
       < g['hole_radius'] ** 2] = 0.
    hole = np.column_stack((g['hole_radius'] * radius.ravel() * np.cos(angle.ravel()),
                            g['hole_y'] + g['hole_radius'] * radius.ravel() * np.sin(angle.ravel()),
                            np.zeros(24 * 96)))
    return ((plate, pw / pw.sum()), (hole, weights / weights.sum()))


def green(points, pole, geometry, reference_distance):
    """Rayleigh kernel, with common normal-distance arrival reference removed.

    G=sum(w/r * exp(-s*(r-d)/c)); common rho*s/(2*pi) is absorbed in the
    fitted source strengths. Only relative source-path phase is retained.
    This local modal projection is not a full broadband propagation delay.
    """
    result = np.empty((len(points), 2), complex)
    for source, (nodes, weights) in enumerate(quadrature(geometry)):
        for mic, point in enumerate(points):
            r = np.linalg.norm(nodes - point, axis=1)
            result[mic, source] = np.sum(weights / r * np.exp(-pole * (r - reference_distance) / C))
    return result


def helmholtz(g):
    area = np.pi * g['hole_radius'] ** 2
    neck = .003 + 1.7 * g['hole_radius']
    volume = g['outline'] * g['width'] * g['length'] * g['depth']
    return C / (2 * np.pi) * np.sqrt(area / (volume * neck))


def target_geometry(shape):
    width, length, depth, hole, outline = BODIES[shape]
    return dict(width=width, length=length, depth=depth, hole_radius=hole / 2,
                outline=outline, plate_ry=.164 / .490 * length,
                hole_y=.14 / .490 * length)


def target_point(g, placement):
    if placement == 0:
        return np.array([.10, (.490 - .164) / .490 * g['length'], .30])
    return np.array([.15, g['hole_y'] / 2, .45])


def fit_sources():
    constrained, free, diagnostics = [], [], []
    for filename, index in SELECTION:
        fq, residues = read_modes(ROOT / 'Source/DSP' / filename)
        f, q = fq[index]
        pole = -np.pi * f / q + 2j * np.pi * f
        G = green(XYZ, pole, REFERENCE, .10)
        a = (f / helmholtz(REFERENCE)) ** 2 - 1
        d = (G @ np.array([a, 1.])) / (a + 1)
        amplitude = (np.conj(d) @ residues[index]) / np.vdot(d, d)
        strengths = np.linalg.lstsq(G, residues[index], rcond=1e-10)[0]
        constrained.append(amplitude)
        free.append(strengths)
        diagnostics.append(dict(source=filename, index=index, frequency_hz=float(f), q=float(q),
            free_condition=float(np.linalg.cond(G)),
            constrained_relative_error_by_input=(np.linalg.norm(d[:, None] * amplitude - residues[index], axis=0)
                                                 / np.linalg.norm(residues[index], axis=0)).tolist(),
            free_relative_error_by_input=(np.linalg.norm(G @ strengths - residues[index], axis=0)
                                          / np.linalg.norm(residues[index], axis=0)).tolist()))
    return np.array(constrained), np.array(free), diagnostics


def series():
    coefficients = np.empty((4, 2, 2, ORDER + 1))
    bounds = []
    max_error = 0.
    max_parity_error = 0.
    # Includes the full positive radiation Q floor, a much broader envelope
    # than these four poles can reach through sanitized calibration.
    max_pole = abs(complex(-np.pi * 400 / .05, 2 * np.pi * 400))
    for shape in range(4):
        g = target_geometry(shape)
        for placement in range(2):
            point = target_point(g, placement)
            distance = point[2]
            for source, (nodes, weights) in enumerate(quadrature(g)):
                r = np.linalg.norm(nodes - point, axis=1)
                delta = -(r - distance) / C
                term = weights / r
                for k in range(ORDER + 1):
                    coefficients[shape, placement, source, k] = np.sum(term)
                    term = term * (delta * POLE_SCALE) / (k + 1)
                u = float(max_pole * np.max(abs(delta)))
                bound = float(np.sum(weights / r) * math.exp(u + (ORDER + 1) * math.log(u)
                                                           - math.lgamma(ORDER + 2)))
                bounds.append(dict(shape=shape, placement=placement, source=source,
                                   max_argument=u, absolute_remainder_bound=bound))
            # Independent direct exponential checks include the conservative
            # domain edges and low-Q interior. The real geometry is even in x;
            # the same kernel therefore supplies both virtual microphones.
            for f in (45., 90., 180., 250., 400.):
                for q in (.05, .1, .5, 1., 15., 150.):
                    pole = -np.pi * f / q + 2j * np.pi * f
                    G = green(np.array([point, point * [-1, 1, 1]]), pole, g, distance)
                    approx = np.array([np.polynomial.polynomial.polyval(pole / POLE_SCALE, c)
                                       for c in coefficients[shape, placement]])
                    max_error = max(max_error, float(np.max(abs(approx - G[0]) / abs(G[0]))))
                    max_parity_error = max(max_parity_error, float(np.max(abs(G[0] - G[1]) / abs(G[0]))))
    assert max_error < 1e-12
    assert max_parity_error < 1e-12
    assert max(x['absolute_remainder_bound'] for x in bounds) < 1e-15
    return coefficients, dict(order=ORDER, pole_scale=POLE_SCALE,
        frequency_domain_hz=[45., 400.], q_domain=[.05, 150.],
        max_relative_direct_error=max_error, max_relative_lr_symmetry_error=max_parity_error,
        kernels=bounds)


def cpp_array(values, depth=0):
    if np.ndim(values) == 0:
        if np.iscomplexobj(values):
            return '{ ' + format(float(np.real(values)), '.17e') + ', ' + format(float(np.imag(values)), '.17e') + ' }'
        return format(float(values), '.17e')
    indent = '    ' * (depth + 1)
    return '{\n' + indent + (',\n' + indent).join(cpp_array(v, depth + 1) for v in values) + '\n' + '    ' * depth + '}'


def generate():
    # Do not silently retain a stale virtual geometry if an engine body changes.
    engine = (ROOT / 'Source/DSP/AcustraEngine.cpp').read_text()
    geometry_block = re.search(r'constexpr std::array<BodyGeometry, 4> steelStringBodies \{\{(.*?)\}\};',
                               engine, re.S)
    if geometry_block is None:
        raise RuntimeError('Cannot verify the engine body geometries')
    geometry = [[float(x.strip().removesuffix('f')) for x in row.split(',')]
                for row in re.findall(r'\{([^{}]+)\}', geometry_block[1])]
    if not np.array_equal(np.array(geometry), np.array(BODIES)):
        raise RuntimeError('Engine geometry changed: update and revalidate the observer geometry')
    constrained, free, fits = fit_sources()
    coefficients, validation = series()
    inputs = {str(p.relative_to(ROOT)): sha(p) for p in [
        ROOT / 'Source/DSP/MeasuredBodyData.h', ROOT / 'Source/DSP/MeasuredJointBodyData.h', Path(__file__)]}
    text = '''// Generated by Tools/GenerateStudioCapture.py; do not edit.
// Rate-independent observation data for the approved fixed studio blend.
// Mores pressure residues: CC BY 4.0, https://doi.org/10.5281/zenodo.4604577
// The finite plate/hole geometry is a declared prior, not a measured new position.
// No body poles, bridge mobility, pickup model, or equalizer is encoded here.
// G(s) = sum_k coefficients[k] * (s/1000)^k, s=-pi*f/Q + i*2*pi*f.
// Validated f=45..400 Hz, Q=.05..150 (wider than all four calibrated low modes).
// Common normal-distance arrival is removed; this remains a modal projection,
// not a complete causal broadband model of microphone propagation.
#pragma once
#include <complex>

namespace acustra::detail
{
'''
    for name, digest in inputs.items():
        text += '// SHA256 ' + name + ': ' + digest + '\n'
    text += f'inline constexpr int studioGreenOrder = {ORDER};\n'
    text += f'inline constexpr double studioPoleScale = {POLE_SCALE:.1f};\n'
    text += '// Mode order: own A0/T1, joint A0/T1; columns are force and normalized moment.\n'
    text += 'inline constexpr std::complex<double> studioConstrainedSource[4][2] = ' + cpp_array(constrained) + ';\n'
    text += '// Free fit: mode, source (plate/hole), input (force/moment).\n'
    text += 'inline constexpr std::complex<double> studioFreeSource[4][2][2] = ' + cpp_array(free) + ';\n'
    text += 'inline constexpr double studioHelmholtzHz[4] = ' + cpp_array(np.array([helmholtz(target_geometry(i)) for i in range(4)])) + ';\n'
    text += '// Shape, placement (neck .30 m/off-hole .45 m), source (plate/hole), power.\n'
    text += f'inline constexpr double studioGreen[4][2][2][{ORDER + 1}] = ' + cpp_array(coefficients) + ';\n'
    text += '} // namespace acustra::detail\n'
    report = dict(inputs=inputs, geometry_reference=REFERENCE, body_geometry=BODIES,
                  numpy_version=np.__version__,
                  engine_geometry_sha256=hashlib.sha256(geometry_block[0].encode()).hexdigest(),
                  source_selection=SELECTION, fit_diagnostics=fits, series_validation=validation,
                  weights=dict(current=.4, unboosted_treble=.2, constrained_neck_compensated=.1,
                               constrained_offhole_compensated=.1, free_neck_compensated=.1, measured_blend=.1),
                  nominal_distance_compensation=dict(neck=3., offhole=4.5),
                  output_sha256=hashlib.sha256(text.encode()).hexdigest())
    return text, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'Source/DSP/StudioCaptureData.h')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--check', action='store_true', help='verify checked-in header without changing it')
    args = parser.parse_args()
    text, report = generate()
    if args.check:
        if not args.output.exists() or args.output.read_text() != text:
            raise SystemExit('Studio capture data differs; regenerate the header.')
    else:
        args.output.write_text(text)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    print(json.dumps(dict(output=str(args.output), sha256=report['output_sha256'],
                          max_kernel_error=report['series_validation']['max_relative_direct_error'],
                          maximum_absolute_series_remainder=max(x['absolute_remainder_bound'] for x in
                              report['series_validation']['kernels'])), indent=2))


if __name__ == '__main__':
    main()
