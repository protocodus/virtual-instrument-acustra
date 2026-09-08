# Measured guitars and stronger body variations

The local instrument now offers four additional measured bridge/body pairs.
The **Guitar** presets choose their native family, wood reference and string
material together; **Model** independently selects the measured bank.
Original preserves the existing g21/g34 selection and optional Fylde bridge.

| Model | Native preset | Radiation / bridge modes | Microphone evidence |
| --- | --- | ---: | --- |
| Manuel Lopez Bellido, 1978 | Auditorium, Cedar, Nylon | 134 / 50 | Three actual microphones, force and normalized moment |
| Washburn, 1897 | Parlor, Spruce reference, Steel | 124 / 50 | One measured microphone, normal force only |
| Santa Cruz OM3, 2022 | Auditorium, Spruce reference, Steel | 122 / 56 | One measured microphone, normal force only |
| Martin D18V, 2007 | Dreadnought, Spruce reference, Steel | 131 / 45 | One measured microphone, normal force only |

The Bellido cedar top is documented. Spruce is the neutral wood-control
reference for the other three, not a recovered wood measurement. Detailed
dimensions, bracing, gauge and microphone electronics have not been identified.
Changing the strings or construction away from a native preset creates a
model variation, not another measured real instrument.

Sources, exact input hashes, fit conditions and rejected models are in
[the measurement review](rau-source-methods-2026-09-08.md),
[Rau fit report](rau-guitar-candidate-report.json) and
[Bellido refinement report](nylon-bridge-refinement-report.json).
The Bellido source is Robert Mores' CC BY 4.0 archive. Rau's separate raw ZIP
has no explicit redistribution license; raw data stays outside the repository
and this work has not published a release.

## Mechanical and microphone integration

Every new guitar uses its own positive-real bridge mobility in the existing
reciprocal six-string junction. The original corpus's conductance floor is
excluded from these measured banks, and the global mobility fitting control
becomes a relative multiplier with unit gain at the shipped calibration.
The identical rule is used for bridge feedback and string tuning. The
56-mode fixed capacity accommodates OM3 without truncating its fit.

Bellido preserves the measured force/moment microphone residues. The scalar
Rau data supplies no rocking response or second microphone: the unmeasured
rocking mobility is zero, and Stereo mic and Mono mic both give the same mono
observation. Their bridge-local direct path is also folded to mono. Neither
numbered repetitions nor artificial panning are presented as recorded stereo.

Rau's fitted microphone delays (40, 48 and 44 samples at 48 kHz) remain outside
mechanical feedback. A causal fifth-order Lagrange delay preserves the treble
at fractional host rates; a two-tap delay was rejected because it introduced
up to 2.35 dB of extra 10 kHz attenuation at 44.1 kHz. Current native tests
against ideal complex propagation have worst high-band error 0.194 dB.
Body changes retain the existing 40 ms crossfade and coalesce rapid updates;
delay history follows each fading body and is cleared by panic.

Fixed microphone sensitivity trims of 1.10, 3.15, 3.65 and 3.24 respectively
make the four models comparable to Original/Auditorium for their string
material. They were rounded from an equal-weight eight-note, velocity-100
RMS sweep. These trims do not establish absolute SPL, and never modify bridge
feedback or piezo output. There is no adaptive loudness processing.

## Shape differences

The Shape control now spans air-resonance anchors of 128, 107, 98 and 88 Hz,
plate-frequency factors 1.18, 1.00, 0.90 and 0.82, and broader bass/damping
directions from Parlor to Jumbo. These are authored soundboard-color
variations. In particular, increasing Q with size is a musical direction,
not a universal physical law. They transform radiation while leaving the
selected measured bridge intact. At a named guitar's native family and wood
reference, relative factors return its original fitted response.

[The reproducible comparison](body-control-comparison-2026-09-08.json) uses
identical notes and removes each note's overall spectral energy before
comparing frequency bands. All six shape pairs separate more strongly in
both materials. Mean pairwise band distances rise from 3.45–5.57 to
5.31–7.85 dB for steel and 3.12–7.46 to 6.19–8.20 dB for nylon. This measures
distinctness; it is not a perceptual realism score.

Run `Tools/CompareGuitarBodies.py --help` for frozen-renderer reproduction and
level-matched listening WAVs. Native tests independently reconstruct the
microphone impulse/complex frequency response, verify every default bridge
residue and the absent legacy floor, and check coupled chords, rapid model
switches and panic. The tested native chords have nonnegative cumulative
body-port work and peaks below the output limiter.

Deliberately restoring the old linear delay fails the new Martin/44.1 kHz
test (2.100 dB high-band error), and restoring the old conductance floor fails
the nominal bridge test. A nonzero bridge-local direct gain also preserves
exact mono output for all three scalar models. These negative controls ensure
the tests detect the reviewed integration failures.
