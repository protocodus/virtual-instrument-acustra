# Initial-state spatial limits of the nonlinear string prototype

Dated 2026-09-08. This independent static audit shows why the 12-mode
[numerical prototype](NonlinearStringPrototype.md) cannot yet stand in for a
full-band guitar string. For its unchanged 4 mm Gaussian-smoothed triangular
pluck, 12 modes omit 2.6344% of the linear energy but understate initial
endpoint force by 9.9147% relative to a 192-mode spatial reference. Time-step
convergence within the 12-mode model does not remove this spatial error.

The [protocol](NonlinearStringSpatialAuditProtocol.md) was frozen before
calculation. The [script](NonlinearStringSpatialAudit.py) and
[complete results](NonlinearStringSpatialAudit.json) retain all 15 cases,
quadrature checks and physical bandwidth counts. The separate moving-endpoint
audit keeps its own fixed 12-mode protocol; these results do not change or
rescue any of its gates.

## Fixed physical state

Use the original prototype's plain nylon G3: length 0.650 m, density
`1140*pi*0.001^2/4 kg/m`, tension derived from its 195.99771799 Hz ideal
fundamental, `EA=2.7e9*pi*0.001^2/4 N`, and `EI=310e-6 N m^2`. The displacement
coefficients remain

```
q_n = 2*A*sin(pi*n*p)/(pi^2*n^2*p*(1-p))
      * exp(-0.5*pi^2*n^2*sigma^2),
p = .173, sigma = .012, A = 1/2/4 mm.
```

The string is fixed at both ends and initially at rest. Each count
`N=12,24,48,96,192` evaluates this same analytic series truncated after N;
there is no renormalisation of peak displacement, energy or force. The
geometrically exact local nonlinear potential uses the original metre-valued
trapezoidal weights, first with 8N and then 16N intervals. This is a static
calculation, not an audio-rate integration of modes above Nyquist.

## Force and energy are different convergence tests

With `k_n=n*pi/L`, `K_n=L/2*(T*k_n^2+EI*k_n^4)` and `d_n=2/(L*k_n)`, the
linear endpoint force is `d'Kq=sum (T*k_n+EI*k_n^3)q_n`. The nonlinear stress
is the same exact potential derivative `tau(s)` used in the prototype.

The physical point-stress observer adds `tau(s(0))`. The conservative weak
reaction of the finite moving-endpoint lifting instead adds

```
d' grad_q(Phi) - grad_y(Phi)
  = integral tau(s(x)) * [1+2*sum(n=1..N) cos(k_n*x)] / L dx.
```

The script evaluates that Dirichlet kernel directly. Its difference from
point stress is one closure error *within a selected finite model*. Both
forces can agree closely while both differ substantially from the
higher-resolution string.

At 4 mm, relative to the 192-mode state:

| Modes | Highest physical mode, Hz | Excluded linear energy | Total energy error | Point-force error | Weak-reaction error |
|---|---:|---:|---:|---:|---:|
| 12 | 2,372.98 | 2.63442% | -2.61149% | -9.91467% | -9.80630% |
| 24 | 4,869.80 | 0.435394% | -0.429035% | -3.42228% | -3.42659% |
| 48 | 10,673.02 | 0.00898579% | -0.00861335% | -0.344028% | -0.364723% |
| 96 | 27,577.39 | 0.0000000807% | -0.0000000184% | +0.00151748% | +0.000285745% |
| 192 | 88,993.26 | Reference | Reference | Reference | Reference |

The 12-mode weak/point difference is only +0.120300%, despite their roughly
10% errors against the higher-resolution state. At 24 modes it is only
-0.00446943%, while both forces still have about 3.4% error. This is why a
small weak/point difference alone cannot qualify the radiated force.

All potential/energy/reaction comparisons between 8N and 16N quadrature pass
the frozen tolerance. All 15 result rows exactly reproduce after packaging
the script. Independent review confirmed the Dirichlet-kernel identity,
physical stiffness and energy, endpoint sign, Gaussian coefficients and
source/protocol hashes.

## Bandwidth implications

For this G3 string only, 24 physical modes lie strictly below 5 kHz, 45 below
10 kHz, 62 below 15 kHz and 77 below 20 kHz. The 77th mode is at 19,899.70 Hz;
the next is at 20,269.77 Hz. At 48 kHz, 87 modes lie strictly below Nyquist.
The prototype's minimum quadrature condition is `intervals >= 2N+1`; this
condition does not prove adequate nonlinear spatial resolution during motion.

The 96- and 192-mode states above exceed 48 kHz Nyquist and are **static spatial
references**, not valid 48 kHz candidates. A future full-band implementation
must independently qualify coupled stability, nonlinear spatial/time
resolution, input and output forces, measured losses, physical-reference
convergence, callback cost and real-recording behavior. Neither adding 77
modes nor preserving their linear poles establishes those properties.

These numbers are specific to a plain G3 string and one fixed position and
width. They do not transfer a plain-string axial modulus to wound bass
strings, determine a realistic finger profile, or demonstrate a sound-quality
gain. The unsmoothed triangular slope/force in the JSON is a labelled
comparison; its bending energy is singular, so it is not an energy reference.

Reproduce into a fresh directory with standard-library Python 3:

```sh
python3 Tools/NonlinearStringSpatialAudit.py /tmp/acustra-spatial-new-result
```

Existing output directories are rejected to preserve prior results. No
production file, calibration, recording selection or public control changes.
