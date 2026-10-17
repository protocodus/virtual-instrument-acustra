# Moving-stop transport limit — 2026-10-11

The existing conventional pitch slide is a smoothed change of the folded
string's round-trip delay. It is not a causal moving-fret solution preserving
the full displacement and velocity field. An isolated witness confirms that
its returning bridge wave changes before motion at the fret could reach the
bridge. This remains an explicit reduced-model limit; no partial transport
patch was adopted in this completion pass.

## Independent timing target

Consider an ideal nondispersive transverse string, a stationary bridge at
`x = 0`, constant wave speed `c`, and a prescribed rigid stop at `x = L(t)`.
The stop moves subsonically; there is no simultaneous change of tension.
For a pulse launched at bridge time `s`, reflected at time `τ`, and received
back at bridge time `t`, characteristics give

```text
s = τ − L(τ)/c
t = τ + L(τ)/c
D(t) = t − s = 2 L(t − D(t)/2)/c
```

The required delay depends on the stop's position at reflection time. Using
`D(t) = 2 L(t)/c` instead changes waves that already reflected before the
motion began. For a constant stop speed `v`, the exact returned waveform's
time-coordinate derivative is `(c − v)/(c + v)`; the instantaneous-length
reader gives `1 − 2v/c`. These agree only to first order in `v/c`.

The [independent oracle](../../Tools/research/project-completion-2026-10-11/moving-stop/MovingStopOracle.py)
solves those characteristics by bisection. A separate constant-speed closed
form checks the solver to a maximum absolute error of `3.47e−18 s`. Its
examples use the existing 6 ms exponential time scale and legal fret
intervals on the low E string. This is an ideal boundary calculation, not a
dispersive string/contact solver or a measured guitar dataset.

## Current-code witness

[`StringLoop::advance`](../../Source/DSP/AcustraEngine.cpp) glides `currentDelay`
then reads that delay immediately. The
[C++ witness](../../Tools/research/project-completion-2026-10-11/moving-stop/CurrentLoopCausality.cpp)
calls that production method from the frozen `70eeafc` engine object with
matching headers. Its DSP code is preprocessor-identical to the completion
starting source `56d4678`. The witness was then relinked against the frozen
final `2decfdf` object and matching headers: all twelve raw cases are exactly
identical. The final compile command and hashes are retained in the manifest.

Two identical, settled loops receive the same outgoing sinusoid. Only one
target delay changes. Loss is disabled, and the default first dispersion
section contributes a known two-sample delay. The incoming waves first
differ at zero-based sample 2 in all twelve fixtures (44.1, 48 and 96 kHz).
The physical earliest return of a boundary change is initially `L(0)/c`.

| Low E slide | Earliest physical return | Relative incoming-wave RMS difference before that return at 48 kHz | Largest pulse-arrival discrepancy in the oracle's seven launches |
| --- | ---: | ---: | ---: |
| Fret 2 → 0 | 5.405 ms | 0.3451 | 0.7942 ms |
| Fret 2 → 4 | 5.405 ms | 0.3035 | 0.6075 ms |
| Fret 5 → 17 | 4.545 ms | 0.8569 | 1.5629 ms |
| Fret 17 → 5 | 2.273 ms | 0.7158 | 1.4343 ms |

These normalized read-path differences establish the causality discrepancy;
they do not measure complete-instrument loudness, listener preference or
rendered fundamental-pitch error. The oracle's echo-frequency diagnostics
likewise describe the returned time coordinate, not cents of musical tuning.
The continuous oracle and discrete float delay glide share a time scale;
they are separate fixtures, not a sample-identical trajectory comparison.

## Why a read-offset patch was not adopted

The current representation does not retain a physical stop-length history.
`Voice::fretMotion` stores only its present length and exists for the wound
string friction source. `currentDelay` also contains tension, attack and
dispersion effects, so it cannot serve as a stop-position history.

A retarded-length offset could correct this isolated fixed-speed return
path, but `StringLoop::displacementAt` and local contacts would still unfold
the string using instantaneous geometry. Reset, repeated contact, refretting
and captured tails would need the same physical trajectory and field
mapping. Freezing a new offset when a tail is captured would not preserve
the boundary history already in flight. Simultaneous tension changes require
wave-speed history, beyond the fixed-`c` equation above. An offset alone
would therefore introduce another internally inconsistent model.

A complete replacement needs coherent two-way transport, local observation,
contact and retained-wave rules, followed by independent pulse/Doppler,
stationary-parity, reset/copy/repluck, coupled stability and final audio/CPU
evidence. The earlier
[moving-endpoint experiment](../../Tools/MovingEndpointPowerPortCorrections/README.md)
addresses a transverse compliant **bridge** coordinate in a small modal
fixture; it does not qualify a longitudinal moving fret. Published
[slide-guitar work](https://liu.diva-portal.org/smash/get/diva2%3A535809/FULLTEXT01.pdf)
also uses an explicit moving contact and traveling-wave junctions, rather
than establishing an instantaneous full-round-trip read as exact transport.

The retained slide has bounded delay smoothing, pitch/contact geometry and
an authored wound-friction source. It must not be described as a full
moving-boundary field solution or as having exact causal return timing.
No new playing technique, parameter, DSP version or acoustic validation is
implied by this evidence note.

## Reproduction and provenance

The [manifest](../validation/project-completion-2026-10-11/moving-stop/provenance.json)
records compiler and original commands, source/object hashes, and archived
witness/output hashes. The engine compile command is also retained in the
preceding [build receipt](../validation/dsp-finalization-2026-10-11/benchmark-build-commands.json).
Recover `Source/` from commit `70eeafcf7a36a34c208f3244322b92ab0c81767d`
into the manifest's frozen-source directory, then run the two recorded
compile commands and the resulting witness to reproduce
[the twelve raw cases](../validation/project-completion-2026-10-11/moving-stop/current-loop-causality.jsonl).
Running the archived Python script reproduces
[the independent pulse results](../validation/project-completion-2026-10-11/moving-stop/MovingStopOracle.json).
Only small source, JSON and provenance files are archived; no alternate
engine or binary is added to the project.
