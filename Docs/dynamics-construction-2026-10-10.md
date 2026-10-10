# Velocity dynamics and construction history, 2026-10-10

The user requested a wider dynamic range in the shared DSP and Reason port,
with very quiet, delicate low-velocity strokes. They also asked whether
changing Wood and Shape back and forth changes a final combination's sound.

## Playing force

The previous pluck displacement used `v^(1.32 - 0.5 * brightnessDepth)`.
The current fitted depth, 1.10625, gave an exponent of 0.766875. This
sublinear response raised low velocities and compressed the dynamics.

The new response uses `v * v`. The existing Touch response and physical
contact law remain in place, but the smaller excitation actually reaches
the contact, string displacement and attack/release components. The nominal
contact reference divides out the same force curve. MIDI 127 retains its
force reference; MIDI 64 has approximately one quarter of its force.
No control, calibration table, per-note normalization or new latency is added.
This is an authored keyboard response, not a measurement-derived velocity law.

Finger's release gesture has an independent authored response: effective
edge radius is `0.2 mm * nominalForce^0.6165625` before the existing Touch
and hand-compliance factors. With force `v²`, its release time retains the
established `v^-0.766875` response. The exponent is fixed, never recomputed
from brightness calibration. The full-force reference remains unchanged.
This preserves approved soft-to-hard articulation while reducing actual
string force; it is not a measured skin/contact-area law. Pick and Thumb
retain their contact laws.

A constant Finger radius at the new tiny forces made release times exceed
a string period and failed existing articulation gates, despite darker
initial-loop spectra. Removing burst noise, springback, attack pitch or
reciprocal feedback did not repair those failures. Radius exponents 1/3 and
1/2 also failed. The accepted response passes all eight unchanged per-note
requirements: over 3 dB fixed-observer harmonic-band rise and increasing
native harmonic centroid, on 16 paired noise seeds. No measured numerical
algorithm defect or universal biomechanical law is claimed from that diagnosis.

The frozen comparison starts at canonical `ce285e37dc4bf73bd1c309be80c2f449c5550bf8`.
Both source snapshots and the renderer use the same native arm64 clang++
toolchain, C++17, `-O2 -Wall -Wextra -Werror`, and extended-rate definition.
The 600 cases cover four constructions, three techniques, three captures,
E2/E4, eight velocities and default-construction strums at 48 kHz/64 frames.
Each fresh engine holds for one second and releases for 250 ms, dry, unity
Output, Body 70%, Piezo Mix 0% to isolate the selected sensor. Sources,
renderer and results are hashed; audition ladders share gain 1.0, with no
per-layer normalization.

Median RMS relative to the matching hardest stroke across 24 single-note
construction/capture/register cases per technique:

| MIDI velocity | Finger before / after | Pick before / after | Thumb before / after |
| --- | ---: | ---: | ---: |
| 8 | -19.0 / -48.6 dB | -21.8 / -68.5 dB | -19.1 / -48.9 dB |
| 16 | -14.2 / -36.4 dB | -15.7 / -49.8 dB | -14.4 / -36.8 dB |
| 32 | -9.3 / -24.1 dB | -10.1 / -29.3 dB | -9.6 / -24.8 dB |
| 64 | -4.6 / -11.9 dB | -4.8 / -12.9 dB | -4.8 / -12.5 dB |
| 96 | -1.9 / -4.9 dB | -2.0 / -5.1 dB | -1.9 / -5.1 dB |

All 600 final-source renders are finite and nonzero; all 525
adjacent velocity comparisons rise strictly in RMS. All 75 hardest-stroke
renders, including release and strums, are byte-identical in this isolated
velocity/contact comparison. The independent follower endpoint corrections
change final waveform phase: maximum full-velocity pitch difference is
0.0387 cents, RMS difference 0.0090 dB, peak difference 0.0206 dB and early
brightness difference 0.0292 dB. They are not bit-identical to the baseline;
the maximum complete-wave relative norm difference is 3.53%.

Fundamental searches remain inside their search interval, with approximately
4.3 cents maximum error. Finger/Thumb velocity 8 and above retain a strong
fundamental; Pick velocity 1 is essentially silence (-149 to -130.5 dBFS RMS
in these cases) and its tiny transient can dominate its spectrum. Do not
describe that bottom layer as perceptually audible at an arbitrary playback
level. Velocity 32 and above show no measured midrange deadband or delayed
energy onset requiring a weaker force curve.

Finger's matched-velocity early treble/bass balance stays near its baseline
(approximately 0 dB median change through velocity 64), while velocity 16's
actual RMS falls by 22.2 dB. Pick/Thumb's lower-frequency energy can fall
faster than their treble, so their normalized balance does not uniformly
become darker. Their absolute treble/noise is quieter. These are measured
excitation/timbre changes, not a listener preference result.

`VelocityRealismTests` measures emitted soft/middle/hard audio on wound and
plain strings, all three techniques and captures, at 44.1 and 96 kHz. The
physical contact, full-force reference, monotonic rise and usable middle
velocity are checked independently of the initializer's algebra.
Its same-velocity random-force brightness witness observes the same moderate
held-force region at velocity .445 that the previous curve supplied at .12,
keeping the strict force/geometry and 4% spectral-moment margins. The old
Pick/Thumb line-peak ordering now checks independently reconstructed held
displacement: a weak Pick's slower release can redistribute its initial
line peak below Thumb without reversing held-force ordering. Audible style
separation remains covered independently.

The first full suite found the ContactRelease fixture still predicting a
constant Finger edge radius at nonfull velocities. Its independent expectation
now includes the authored velocity response above, using observed MIDI velocity
and independently recovered held force. The two-stage filter prediction and
its strict numeric tolerances are unchanged; the failed run is retained.

## Construction round trips

The body, bridge and string coefficients recover the requested construction.
Two exponential float followers did not reach exact endpoints: the output,
mono and piezo references, and the string loop's smoothed delay. Their stalled
values depended on approach direction. The existing control-glide rule now
also completes these followers when the next float cannot move or is within
`1e-4` of its target. Retunes and body/bridge fades keep their histories and
continue to glide; settled values become exact.

`ConstructionHistoryTests` covers all 12 stored Shape/Wood combinations
(eight visible choices plus four legacy Maple choices), both property orders,
three complete traversals, and same-tick, settled and interrupted transitions
at 44.1, 48 and 96 kHz. Coefficients and delay endpoints are inspected before
reset can rebuild them. With matched performance draws, all 216 unreset
silent cycles, 216 reset cases and 24 cleared natural-wave clones are
sample-exact.

The Reason wrapper revealed another case: while asleep it advances no audio
samples, so a construction fade could resume on the next strike instead of
settling during the silent selection. The shared engine now exposes explicit
`settleSuspendedParameters()` for a host that has suspended inaudible audio.
It completes construction fades/references and idle delays without advancing
clocks, restarting random draws, resetting controllers or clearing retained
waves. Active or planned performances reject that operation. Reason invokes
it for engine parameter changes before the sleeping device's first strike.
All 48 wrapper comparisons (12 stored combinations, both property orders,
sleeping and processing zero-input paths) match all three buses sample for
sample after three complete traversals.

An already ringing guitar cannot be expected to equal a newly cleared one:
its wave/contact histories carry energy, and ordinary repeated strokes also
vary naturally. The 24 unreset natural-tail cases separately check coefficients
and endpoints. Output tails fall below `1e-7` (-140 dBFS) after 6–8.2 seconds;
the following strikes differ by at most 3.205e-5 relative RMS and 1.685e-6
absolute sample amplitude. Microphone tail amplitude is not a bound on all
stored string energy. Clearing those same engines through public All Sound
Off makes their controlled strikes sample-exact. Production panic/reset does
not restart random performance draws.

Local frozen measurements and failed endpoint witnesses are retained in the
Reason workspace under `Release/validation/dynamics-construction-20261010/`.
Native engine tests do not establish a built VST/AU or interactive Reason
playback; the Reason build and host evidence are recorded by that adapter.
