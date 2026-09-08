# Nylon wound-bass loss audit — 8 September 2026

**The bass-only broad-loss candidate was rejected on training evidence.** It
slows the directly played bass upper partials in the intended direction, but
also prolongs coupled energy during treble notes that already decay too
slowly. This outweighs the bass improvement. No candidate development scores
were opened, no softer-coefficient search followed, and the engine change was
reverted. The [retained numerical report](nylon-bass-loss-2026-09-08.json)
contains native coefficients, per-note signed diagnostics, coverage and hashes.

## Primary coefficients and the proposed narrow change

The author's [corrected Table I, page 947](https://euphonics.org/wp-content/uploads/2022/03/Guitar_II.pdf)
in Woodhouse, *Plucked Guitar Transients: Comparison of Measurements and
Synthesis*, Acta Acustica 90 (2004), 945–965, gives these **dimensionless
internal-friction** coefficients. They belong to D'Addario Pro Arte
“Composites, hard tension” strings, not the EJ45 construction used elsewhere
in Acustra.

| Engine order | E2 | A2 | D3 | G3 | B3 | E4 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Internal friction `eta_F` | 0.00002 | 0.00007 | 0.00005 | 0.00014 | 0.00040 | 0.00040 |
| Bending loss `eta_B`, dimensionless | 0.020 | 0.025 | 0.020 | 0.020 | 0.020 | 0.024 |
| Air coefficient `eta_A`, s⁻¹ | 1.2 | 0.9 | 1.2 | 1.7 | 1.2 | 1.5 |

The corrected table and equation 8 were visually checked from the author's
PDF. Equation 8's small-loss model is

```text
eta_n = [T*(eta_F + eta_A/omega_n) + EI*eta_B*(n*pi/L)^2]
        / [T + EI*(n*pi/L)^2]
amplitude decay sigma_n = omega_n*eta_n/2
decay in dB/s = (20/ln(10))*sigma_n
```

The coefficients are fitted intrinsic-loss estimates; they are not measurements
of the complete currently modeled guitar. Woodhouse explicitly describes
the air term and constant coefficients as pragmatic approximations.

The [DAFx26 65-guitar paper](https://dafx26.mit.edu/assets/papers/DAFx26_paper_40.pdf)
instead gives `c_m = sigma_0 + eta_f*omega_m/2`,
`sigma_0 = 6 ln(10)/T60` in equation 25, and lists
`eta_f=(2.0–2.5)*10^-4 s` in Table 2. The printed units are inconsistent with
adding that product to an inverse-time damping coefficient. The coefficient
range also is not the corrected per-string `eta_F` table above. For an ODE
`q_ddot + c_m*q_dot + omega_m^2*q = 0`, the weak-loss amplitude exponent is
`c_m/2`; confusing these conventions creates another factor of two. Thus the
DAFx summary should not be copied as an exact measured decay law.

The one bounded candidate changed only the common nylon broad-loss coefficient
for the three wound basses:

```cpp
constexpr std::array<float, 3> nylonBassFrictionLoss {
    2.0e-5f, 7.0e-5f, 5.0e-5f // E2, A2, D3; dimensionless eta_F
};
const float viscousLoss = (steel ? 1.65e-4f
    : (stringIndex < 3 ? nylonBassFrictionLoss[stringIndex] : 2.25e-4f))
    * (1.0f + 1.35f * age);
```

Everything after that expression remained unchanged, including
`broadLoss = 72*viscousLoss*frequencyLossScale`, its pole at approximately
`14.3*f0`, H1 filter-gain compensation, all 29 calibration values, and the
other string construction's own coefficients. **The factor 72, the pole
mapping and the age multiplier remain authored approximations.** This is a
physically motivated coefficient experiment, not realization of equation 8,
and it does not substitute `eta_B` for `eta_F`.

## Native loop decomposition

A temporary native probe configured the actual engine at 48 kHz, with zero
attack pitch, age 0.15, no hand damping and bridge coupling disabled. It read
the configured scalar gain, both mixed one-poles, dispersion allpass and
fractional-delay coefficients. Complex roots of the complete isolated
discrete-loop characteristic equation supply modal frequencies and decay.
This isolates string-loop loss; it is **not a measurement of loaded microphone
decay**.

The columns below are mean amplitude decay contributions in dB/s, for the
open string's H5–H12. Unit-circle log magnitudes divided by total group delay
attribute the contributions; their sum differs from the exact pole decay by
less than `2.1e-6 dB/s` across the 36 configurations checked (six strings,
frets 0/5/12, ages 0/0.15).

| String | Scalar gain | Broad mixed pole | High mixed pole | Current exact total | Candidate exact total |
| --- | ---: | ---: | ---: | ---: | ---: |
| E2 | 14.660 | 4.726 | 0.173 | 19.559 | 15.335 |
| A2 | 14.739 | 6.145 | 0.382 | 21.266 | 17.117 |
| D3 | 14.865 | 7.942 | 0.837 | 23.643 | 17.587 |

The allpass dispersion and fractional delay have zero direct attenuation.
Intrinsic bending stiffness changes mode frequencies and group delays, so it
still affects the realized decay indirectly.

| Open string | Current H1 | Candidate H1 change | H2–H4 change | H5–H12 change |
| --- | ---: | ---: | ---: | ---: |
| E2 | 14.718 | <0.001 | −0.650 | −4.225 |
| A2 | 14.837 | <0.001 | −0.638 | −4.149 |
| D3 | 14.997 | <0.001 | −0.932 | −6.057 |

H1 stays fixed because the existing scalar compensation replaces the removed
filter loss at the fundamental. The native fundamental T60 is approximately
4.18 seconds before the additional per-cycle attenuation and bridge losses.
This candidate cannot resolve a roughly 6 dB/s fundamental over-decay error.
At fret 12, H5–H12 loss falls by 7.94/7.58/10.76 dB/s for E/A/D, so a one-second
upper-partial tail can become substantially louder even when its initial
excitation is unchanged.

For context only, evaluating Woodhouse's complete fresh-string equation 8 at
the **current engine's different tensions and existing measured EI** gives
open E/A/D H1 rates of about 5.26/4.12/5.41 dB/s, and H5–H12 means of
6.22/6.36/7.80 dB/s. These are neither current-guitar ground truth nor a reason
to apply the whole earlier rejected loss law. The source string set, age and
loaded-body energy leakage differ.

## Loaded training evidence explains the rejection

The frozen experiment at `/tmp/acustra-nylon-bass-loss-20260908` renders the
existing dry corpus and Yamaha training pitches with all calibration fixed.
Its predeclared first gate requires both nylon training total and decay scores
to improve. Both instead worsen:

| Training corpus | Total, baseline → candidate | Decay term, baseline → candidate |
| --- | ---: | ---: |
| Dry nylon | 7.597779 → 8.317316 | 7.013181 → 8.795738 |
| Yamaha nylon | 7.932492 → 8.127128 | 5.511634 → 5.976038 |

An independent diagnostic uses only the 29 dry training notes and nine Yamaha
training pitches with two reference takes each. Candidate features use the
same scorer as the authenticated baseline cache. All decay comparisons below
use **common finite target/baseline/candidate partials**. Positive signed error
means the model decays too quickly; negative means too slowly.

| Training register, H5–H12 | Common partial/take count | Mean signed error, baseline → candidate | Mean absolute error, baseline → candidate |
| --- | ---: | ---: | ---: |
| Dry bass, MIDI <55 | 48 | +7.00 → +1.55 | 8.59 → 7.67 |
| Dry middle, MIDI 55–63 | 56 | −6.96 → −7.97 | 12.12 → 12.91 |
| Dry treble, MIDI ≥64 | 109 | −7.99 → −23.94 | 27.86 → 35.98 |
| Yamaha bass | 41 | +11.63 → +6.79 | 13.36 → 9.99 |
| Yamaha middle | 31 | +6.39 → +6.39 | 8.61 → 8.61 |
| Yamaha treble | 36 | −19.84 → −26.73 | 24.29 → 29.80 |

Dry bass H1 mean signed error stays at +5.95 dB/s; H2–H4 improves only from
+14.17 to +13.32. Dry treble H2–H4 worsens from −2.38 to −7.70. These are
means over matched individual partials, not the medians in the broader
[initial diagnosis](sound-quality-diagnosis-2026-09-08.md).

Per-note examples show this is not merely a pooled-score artifact:

| Corpus / MIDI | H5–H12 mean signed error, baseline → candidate | Change in H5–H12 absolute error |
| --- | ---: | ---: |
| Dry / 40 | +15.93 → +11.82 | −4.11 |
| Dry / 50 | +7.46 → +1.47 | −3.58 |
| Dry / 54 | +4.82 → −2.60 | +1.84 |
| Dry / 72 | +1.54 → −14.89 | +13.05 |
| Dry / 80 | −47.82 → −80.78 | +23.37 |
| Dry / 84 | −41.36 → −73.54 | +22.60 |
| Yamaha / 50 | +7.67 → +1.61 | −3.29 |
| Yamaha / 74 | −13.99 → −25.56 | +10.42 |
| Yamaha / 79 | −25.26 → −50.21 | +18.16 |

Three extra dry-treble H5–H12 observations become available in the candidate;
the table excludes them and still shows the regression. The existing slope
estimator follows measurable partial energy, which can include coupled idle
strings and beating. These results do not uniquely identify which normal mode
owns a tail, but they do rule out missing-value coverage as the whole cause.

The experiment's audio hash audit finds all 32 dry steel and eight flat-top
steel models identical, but **none of the 39 dry nylon models identical**,
including treble. Preserving a treble string's own coefficients does not
preserve its radiation when the idle bass strings share the bridge. The
independent native probe verifies that the treble's own isolated coefficients
are unchanged. That local/global distinction is essential to the diagnosis.

There is also an attack cost: median bass late-minus-early high-band error
worsens from +10.51 to +11.38 dB on dry training and +14.28 to +14.75 dB on
Yamaha training. The comparison uses the same scorer onset and the same
0–12 ms and 40–100 ms windows as the initial diagnosis; a static gain or
FFT-bin-width offset cancels in each model-minus-target difference.

## Separate scalar-loss trial also rejected

An independent one-point trial increased intrinsic nylon bass T60 by 1.5,
with the original broad-loss coefficient restored and all other calibration
fixed. It was not combined with the friction-coefficient candidate. Its
protocol calls this a calibration hypothesis, not a measured universal
winding law. Both first-stage training gates again failed:

| Training corpus | Total, baseline → scalar candidate | Decay term, baseline → scalar candidate |
| --- | ---: | ---: |
| Dry nylon | 7.597779 → 7.698723 | 7.013181 → 7.257603 |
| Yamaha nylon | 7.932492 → 7.941337 | 5.511634 → 5.590897 |

The frozen protocol, source, exact patch and reports are retained at
`/tmp/acustra-nylon-bass-t60-20260908`. This candidate was also reverted
without development scoring or a parameter search. Its aggregate failure
does not by itself establish the same per-note mechanism; only the broad-loss
trial received the detailed signed attribution above.

Together these results support measuring or decomposing the shared idle-string
tail before another bass-loss reduction. A longer intrinsic bass T60 can
improve played-bass H1 while prolonging shared energy. Changing only
played-string losses would hide that interaction and would require separate
physical justification. Further damping-coefficient searches are not selected
by this audit.

The native-analysis scratch directory is
`/tmp/acustra-nylon-loss-audit-20260908`; its original engine SHA-256 is
`8cfe7c31e935a575391a3dce2b3c239d9612bd2fa93605796fd8adfea22a9275`.
No repository engine files were edited for the independent decomposition or
signed diagnostic. The report preserves coefficient and renderer provenance;
source/reference audio remains in its existing authorized storage.
