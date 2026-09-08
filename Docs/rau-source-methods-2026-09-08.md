# Measured body candidates: source and spatial evidence

These reports describe the candidate-fitting stage. The qualified g35 and
three Rau banks were subsequently integrated into the local instrument; see
[the runtime review](body-models-2026-09-08.md). No release has been published
from them. Numerical fit reports establish conditional transfer approximation,
not playing realism or perceptual quality.

## Rau archive and capture identity

[Mark Rau's primary measurement page](https://rau.mit.edu/projects/GuitarMeasurements/)
identifies the guitars and says that all measurements use a calibrated impact
hammer and laser Doppler vibrometer, with calibrated microphone measurements
for some instruments. Its [public ZIP](https://rau.mit.edu/projectFiles/GuitarMeasurements/GuitarMeasurements.zip)
was accessible on 2026-09-08; HTTP size was 304,989,831 bytes and Last-Modified
was 2025-09-19. The webpage copyright year, server timestamp, instrument build
year, and measurement date must not be treated as interchangeable.

The inspected individual MAT files contain numbered `meas_N` records, each with
one `admittance`, `radiation`, `force`, and `velocity` array of 48,000 real
samples. They contain no microphone coordinates, sample-rate field, calibration
table, impact-position identifiers, or capture-group key. D18, OM3, Washburn and
D28 have five records; Nishihara has ten; 1929-00 and OM2 have fifteen; Ramirez
has two. The containing ZIP has no supplied README or explicit reuse license.

[Rau, Smith and Abel's 2023 methods paper](https://rau.mit.edu/papers/FA2023_MarkRau_JuliusSmith_JonathanAbel.pdf)
describes five measurements per object and sensor configuration. It includes
laboratory and lower-cost sensors, with ten guitars spanning several body
families. Section 3 states one-second records at 48 kHz. This corroborates the
48 kHz interpretation but does **not** identify which configuration, location,
or acquisition date corresponds to each numbered record in the later archive.
The paper's CC BY 3.0 license does not establish a license for the separate ZIP.

The author's [November 2024 vibration-measurement demo](https://rau.mit.edu/projects/VibrationMeasurementDemo/)
and its downloaded `VibrationMeasurements.m` provide scalar force/LDV examples;
line 14 explicitly specifies 48 kHz for the demo recordings. Neither the page
nor that code maps the guitar archive's 10/15 captures to microphone positions.

**No supported stereo assignment was found.** Five, ten, or fifteen scalar
records cannot be reinterpreted as left/right or three microphones from record
counts alone. They are called numbered captures, without a claim of statistical
independence or identical measurement configuration. Their disagreement is
reported, and the final numbered capture is excluded from all fitting and
model selection. No alignment or gain fit is applied between captures.

## Conditional fit results

The [candidate report](rau-guitar-candidate-report.json) records exact raw SHA256
hashes, executable/helper hashes captured at startup, package versions, selected
IDs, and a completion marker. The generated
[header](../Source/DSP/RauQualifiedGuitarData.h) contains only these passing banks:

| Archive instrument | Radiation modes | Passive bridge modes | Held-out pressure complex error | Held-out bridge complex error |
| --- | ---: | ---: | ---: | ---: |
| Martin D18V, 2007 | 131 | 45 | 0.2124 | 0.1705 |
| Santa Cruz OM3, 2022 | 122 | 56 | 0.2187 | 0.1835 |
| Washburn, 1897, parlor | 124 | 50 | 0.1902 | 0.2108 |

All also pass the existing applicable magnitude and radiation-band gates. The
1929-00, Nishihara ReydenSJ, Ramirez, OM2 and D28 candidates fail at least one
unchanged training or held-out gate and are excluded from the header. The
Nishihara model name alone is insufficient to establish exact jumbo dimensions.

Radiation retains the supplied complex phase and explicit fitted integer delay.
The causal 62.5 ms window may contain room reflections; no room geometry is
provided to resolve that uncertainty. Bridge fitting infers approximately
1.22 ms of instrumentation alignment for the three retained banks and fits a
positive-real scalar mobility after that alignment. This fitted advance is
not an independently measured latency and is not placed in the feedback loop.
The radiation response remains mono; the bridge describes one normal component.

## Additional spatial sources

[Robert Mores' 65-guitar dataset](https://zenodo.org/records/4604577), licensed
CC BY 4.0, has documented bass/center/treble bridge impacts, two accelerometers,
and three actual microphones. Its raw MAT checksum matches the repository's
existing pinned MD5. Records g35 (1978 Manuel Lopez Bellido, cedar/Rio
palisander) and g36 (2001 Jose Lopez Bellido, spruce/Indian palisander) were
measured in the Hamburg class-1 anechoic laboratory. Separate candidate fitting
uses the existing phase-preserving force-pair method and reports the unused
center impact as a spatial interpolation diagnostic. This supplies real
microphone diversity without inventing channels from Rau's numbered captures.

The [nylon pressure report](nylon-body-candidate-report.json) retains passing
134-mode g35 and 138-mode g36 phase-preserving banks. Both use 24,000-sample
causal windows selected by the existing low-mode Q convergence check. Every
endpoint, heave/rock and microphone-balance fit gate passes. A compiled float
recurrence also passes the heave/rock pressure gates; the largest relative
difference from the analytic model is 0.0000114. These are three measured
microphone positions, not a measured binaural listener or arbitrary sound field.

The initial fixed-pole passive bridge fits fail. The separate
[bounded refinement report](nylon-bridge-refinement-report.json) preserves those
limits while optimizing measured peak frequencies/Q and positive-semidefinite
residues. The 92-pole g35 result passes dense float32 checks: complex error
0.14037 and worst-string median magnitude error 0.82950 dB. Native float
recurrence checks also pass, with 0.00414 relative difference from the analytic
model. The run reaches its 800-iteration bound; no global convergence is claimed.
The g36 refined bridge still fails its magnitude gate at 2.05559 dB and is not
exported as a qualified matched bridge.

This qualification is relative to the existing rigid two-point target. The
unused center impact exposes appreciable spatial interpolation discrepancies,
reported separately rather than fitted away. Residue positivity, frequency
approximation, and center interpolation are different checks. A separate
50-pole g35 refinement passes the same gates and fits the current bridge
capacity: native complex error 0.18967 and worst median 0.86205 dB. The
[g35 candidate header](../Source/DSP/NylonG35CandidateData.h) combines that
50-mode bridge with the 134-mode pressure bank; the 92-mode result is retained
in the report. The compact bridge's native/analytic difference is 0.00337,
with positive real mobility throughout the checked 60–10,000 Hz band. Full
coupled-string stability and listening validation remain integration work.

The [NBody primary methods](https://www.cs.princeton.edu/~prc/ism98fin.pdf) and
[download index](https://www.cs.princeton.edu/~prc/NBody/) offer multiple actual
microphone positions for Dunlap and Abreu classical guitars and a Fender
d'Aquisto archtop. They do not supply a matched LDV bridge mobility or the
requested set of steel flat-top body families. They remain a possible nylon
spatial-radiation source, not a measured jumbo steel-body solution.
