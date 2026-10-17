# Released saddle load at the piezo — 2026-10-11

The piezo now observes the existing released static load as well as the
travelling-wave force and saddle motion. The force model, its listener-chosen
0.45 release share, string sensitivities, pickup circuit, antialiasing kernel
and gains are unchanged. Final combined-model validation is recorded
separately from the focused evidence below.

## Physical path and units

The existing spring-back source represents the load removed from the saddle
crown when the picking hand releases a displaced string. For tension `T`,
wave speed `c`, sounding length `L`, pluck fraction `p` from the bridge and
transverse displacement `y`, the small-slope load is `T*y/(p*L)`. In the
waveguide's units, `Z=T/c`, the round trip is `D=2*L*fs/c` samples, and the
existing source kick is `2*Z*share*h/(p*D)`. Multiplying by the displacement
unit and `fs` gives the same load, including the existing release share.
The fixed-reference derivative and newtons-per-unit conversion already used
for ordinary string force supply that conversion at the sensor.

The load enters the bridge's heave and rocking equations at that string's
existing lever arm. A load on the saddle crown also passes through the
under-saddle element. Its piezo contribution is therefore the same string's
existing sensitivity times `releaseStepLevel`; it needs no new bleed amount
or geometry. The sensor still subtracts the force caused by saddle motion.
The body and string feedback equations are unchanged.

The [2026-10-01 decision](../decisions.md) excluded this direct force from the
piezo because the then-current preamp headroom and overload fixture failed.
That was an explicit observation exception, not a mechanical explanation.
The current body termination and 0.45 release share differ from that state.
The present correction does not establish that this reduced spring-back
envelope is a measured under-saddle transient; it makes the two observers
consistent with the force the existing instrument already applies.

## Focused feasibility evidence

An isolated copy of the current source was frozen before the piezo edit;
source hashes, commands and results are in
[the evidence directory](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/piezo-release-load-2026-10-11/manifest.json).
One acceptance run retained the original headroom and overload gates:

- Twelve injected loads, ±0.25 N at each of six strings, followed the
  independent analytic high-passed-step force envelope and each string's
  sensitivity: maximum error relative to the injected force `1.56927e-5`.
- Unit piezo weights reproduced the complete bridge reaction bit for bit
  over 288,000 samples, with and without bridge coupling. The previous test
  disabled the released load to obtain this identity.
- The weighted sum of six individual sensors agreed within `3.23136e-7`.
- The reference and twelve-stroke player headroom fixtures passed: minimum
  0.851039 dB; hottest reference open-circuit voltage 1.93404 V, player
  sequence peak 2.8404 V. Construction loudness matching occurs after the
  circuit and does not create this headroom.
- All twenty isolated output-stage clipping cases retained zero input-stage
  limiting and full-band approximation error below −60 dB; the worst was
  −65.5905 dB. Combined-stage overload remained finite and within the
  physical follower range. Ordinary maximum-velocity strokes at the bridge
  can still briefly saturate the modeled preamp.

**The original acceptance run failed one gate.** Its exact diagnostic was:

```text
FAIL: isolated piezo clip aliasing is not 1.5 dB under a bare clip in the upper half, at 44100
```

At 44.1 kHz and Touch 0.58, the upper-half full-band error mean was
−66.1405 dB, versus −65.0238 dB for a bare clip: improvement 1.1167 dB,
below the old 1.5 dB requirement. This result is retained in
[acceptance.log](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/piezo-release-load-2026-10-11/acceptance.log), not
reported as an original-gate pass.

## Why the relative comparison uses the established passband

The new force narrows the input-linear output-clipping interval to
0.965691–0.991083 times the shipped sensitivity. Its upper-half cases clip
one host sample by only 0.113, 0.158 and 0.203 dB. Those are near-onset
cases even though they occupy the upper half of the available interval.

The three waveforms were exported and compared with the same eight-times
band-limited clip reference. Their error improvement over bare clipping is:

| Isolated interval position | Through 0.25 fs | Through 0.375 fs | Through 0.45 fs |
|---|---:|---:|---:|
| 50% | 4.20 dB | 1.71 dB | −0.86 dB |
| 70% | 7.26 dB | 4.13 dB | 1.27 dB |
| 90% | 10.77 dB | 6.25 dB | 2.94 dB |

The excess error is concentrated above 0.375 fs. The existing twelve-tap
BLAMP rolls off there; `PiezoCircuitTests` already reports harmonics above
that boundary without imposing its in-band fidelity gate. Difference from
an ideal clipped waveform in this band includes intentionally attenuated
harmonics as well as aliasing. A fixed relative improvement across that
entire band is an empirical property of the old stimulus, not a universal
property of the antialiasing method. The kernel is unchanged.

`CaptureTests` now keeps all three requirements for the standard-Touch
upper-half comparison:

1. The original −60 dB absolute error ceiling through 0.45 fs, on every
   isolated case.
2. The unchanged 1.5 dB relative improvement within the already established
   passband, through 0.375 fs.
3. Positive mean relative improvement through 0.45 fs as well.

The independent continuous-circuit harmonic, aliasing, overload and recovery
tests retain their existing gates. The corrected focused fixture and its
bypass-BLAMP negative control are recorded alongside the original failed
run. The [focused positive check](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/piezo-release-load-2026-10-11/clip-guard-positive.log)
passes with 4.0312 dB passband and 1.1167 dB full-band mean improvement.
The [bypass check](https://github.com/protocodus/virtual-instrument-acustra/blob/1023937601cc7973785dd912addec3f5481273d9/Docs/validation/piezo-release-load-2026-10-11/clip-guard-negative.log)
fails both relative guards: its error equals the bare clip's in each band.
The final combined instrument still requires its complete validation.
