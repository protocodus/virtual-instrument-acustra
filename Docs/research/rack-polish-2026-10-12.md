# Acustra Rack polish, 2026-10-12

Historical report for the initial 1.3.0 polish. The later
[chorus repair](chorus-repair-2026-10-12.md) supersedes its independent-plane
tuning interpretation and changes the maximum Detune to ±20 cents. The original
±50-cent measurements below describe the earlier revision.

The user requested exact equal-tempered string tuning, optional per-note Detune,
and a smaller open/fretted difference while retaining guitar behaviour. This
report covers canonical engine changes. The Rack adapter owns the new rear
control, panel relayout, Sustain keyswitch/indicator and preset Room defaults;
this work does not expose a new desktop plug-in control.

## Per-note Detune

`EngineParameters.detune` is normalized 0–1; the Rack's 0–100 control maps to
this value. At 1 a new played contact draws uniformly within ±50 cents; at 0
its offset is zero. A separate deterministic per-string random stream leaves
all picking, excitation-noise, repeat-force and strum streams unchanged.
Queued notes draw at actual contact, using the current amount. A held note
keeps its offset through live amount changes, bends, master tuning and sustain.
A reattack draws again, after retained old waves/arrivals have been captured;
a hammer/pull also draws at its new contact. The offset changes fixed-fret
pitch/tension rather than moving the finger. Returning to an idle open string
clears the note offset. Panic clears sounding offsets but retains performance
random-stream progression; prepare restarts deterministic streams.

The exact pitch-geometry cache key includes the note offset. Tests cover ±50
bounds, both signs, zero/nonfinite amounts, held-note stability, scheduled
amount changes, panic/prepare, forced/cached parity with nonzero Detune, and a
scheduled same-note Pick repeat at 96 kHz retaining waves and refreshing its
reference-rate delay. No allocation, lock or I/O was introduced into render.

## Open/fretted difference

Held-string excitation, speaking length, stiffness, loss and pluck-point laws
already vary continuously with the physical fret; there is no separate open
string timbre switch. The large explicit discontinuity was damping after key
release: nominal fretted T60 0.16 s against open 1.25 s. Fretted damping now
uses a gentler 0.32 s endpoint, already within the previous slow-release
velocity range. Open remains 1.25 s, so the nominal loss-rate ratio falls from
7.8:1 to 3.9:1. The finite pad-contact blend still combines loss rates, and
release velocity still scales them. Vacated fretted strings use the same
0.32 s endpoint. This is an authored performance choice requested by the user,
not a new measured material parameter. Release joins, sustain and retained
repluck tails keep their separate rules.

The tests' exact expected release times were updated to the requested endpoint;
lifecycle witnesses now wait the physical return deadline rather than the old
fixed short window. Signal/energy/convergence tolerances were preserved.

## Tuning measurements

Baseline canonical main: `8ea51f9`. Probe: C++17 Clang `-O3`, compiling
`AcustraEngine.cpp` directly with the matching header; 48 kHz, independent
fresh engine for each note, Finger velocity 0.6, default controls, Room and
Release Noise zero, explicit string-per-channel assignment. All six strings
were measured at frets 0, 1, 5, 12 and 20. Three-second mono-left renders were
measured with a Hann-window Fourier peak, FFT coarse search followed by a
bounded complex sinusoidal amplitude maximum. Early window 0.35–1.1 s;
sustained window 1.6–2.7 s. These are acoustic/model measurements, not listening
preferences. Scratch probes and raw floats are under `/tmp/acustra-*`.

The former parallel wave borrowed the normal plane's bridge phase correction,
although the two experience different bridge loads. It could take over the
sustain more than ten cents away from the equal-tempered MIDI request. A
smooth correction now preserves the shared compensation for plane end-phase
mismatches under one cent, reaches independent compensation at four cents,
and has no note/register boundary. This preserves authored treble articulation
under the historical observer while correcting excessive low-register splits.
Physical speaking length, material stiffness and the body/bridge remain intact.

Verified first-stage default Dreadnought results:

| String/note | Baseline late cents | Corrected late cents |
|---|---:|---:|
| Low E, fret 12, E3 | +10.780 | +0.150 |
| A, fret 5, D3 | -9.745 | -0.504 |
| Open G, G3 | +4.566 | -0.732 |
| Largest absolute error, 30-point 48 kHz sweep | 10.780 | 1.198 |

The 90-point signal regression at 44.1, 48 and 96 kHz measured a worst sustained
error of 1.21721 cents. MIDI-derived reference pitch itself is exact equal
temperament (A4=440 Hz); attack tension and physical modal interactions are
measured separately rather than being described as mathematically zero cents.

On the selected smooth blend, open G versus first fret's attack H2–H10/H1
ratio difference moves from +1.997 to -0.858 dB (0.04–0.15 s window), and its
fundamental decay-rate difference moves from +3.201 to -0.246 dB/s (matched
0.35–0.6 and 1.0–1.25 s windows). The sign now changes, while the absolute gap
shrinks. This held-tone change comes from tuning; the gentler key-up damping
is a separate authored change. High E E4/F4 still differs by about 9.56 dB in
this harmonic ratio because of the frequency-dependent body response; no
flattening EQ was added to erase that guitar resonance.

The user explicitly approved retaining the natural acoustic interaction after
being told about a remaining roughly six-cent sympathetic shift. Extra
48 kHz sweeps of the same 30 points measured maximum sustained offsets of
2.56 cents on Parlor, 6.37 on Auditorium and 3.64 on Jumbo. These are audible
coupled-mode measurements, not a detuned MIDI scale: Auditorium G3 played on
the D string coincides with the idle open G string and they form bright/dark
modes through the shared body. A prototype isolated damped-pole solver and
loaded-junction solver did not robustly resolve the bright branch; neither
prototype, its caches, nor any broad numerical rewrite was retained. The
chosen scope is exact equal-tempered reference tuning with physical attack,
body and sympathetic interactions retained, not mathematically zero acoustic
frequency deviation at every instant.

## Floating-velocity convergence witness

The new plane compensation changed a quiet Thumb re-pluck's numerical wave
phase. Its fine-velocity audio guard exposed a float-resolution floor despite
force and contact convergence. At 48 kHz, MIDI 40, first velocity 0.5, second
velocity 0.5+2^-20 after 512 frames: reference RMS 0.0111641; difference RMS
1.11964e-6 (-119.02 dBFS), maximum difference 8.50484e-6 (-101.41 dBFS), relative
difference 1.00289e-4. The old waveform passed its fixed relative allowance,
but the changed waveform's body/force differencing propagated float-wave
rounding beyond that allowance.

This was diagnosed separately from the physical force law: at the smallest
positive representable velocity interval (2^-24), the retained-wave L2 change
was 3.2344 times a one-ULP wave budget, with maximum absolute change 4.47035e-8
(12 times the largest retained-wave ULP, 3.72529e-9). Larger intervals 2^-22
and 2^-20 changed the wave by 6.72 and 24.30 one-ULP budgets while the emitted
fine audio approached a floor. A deliberate alternating one-ULP perturbation
of the retained waves, with no control/ownership change, propagated to RMS
3.50298e-7. Clearing repeat memory as a negative control changed the output by
relative 1.81169e-2, over 180 times the observed fine floor.

Independent wave-only and applied-delay-only perturbations each reproduced
part of the numerical floor, but did not bound the complete response. The
fixture therefore measures the whole instrument at the two adjacent
representable velocity inputs (`std::nextafter` on either side of 0.5). Force,
release, energy and filter inputs are quantized together by a real contact;
this measures their combined float-input resolution without an arbitrary
multiplier or a DSP format change. The adjacent inputs must themselves pass
tight force and contact guards, so a genuine force/contact ownership switch
cannot be admitted as numerical resolution.

The adjacent-input resolution witness measured relative floors of
3.73195e-5 (Finger), 4.09825e-5 (Pick) and 1.06663e-4 (Thumb). Thumb's
positive one-ULP input difference was RMS 1.1908e-6; its negative step was
4.41221e-7. The repeat-memory negative control was at least 169 times the
resolution floor (Thumb), and the fixture explicitly requires at least a
20-fold separation. All three techniques passed those discrimination guards.

The audio admission metric changed: it retains the original 1% of coarse-error
allowance and admits the larger of its original 2e-5 floor and the directly
measured adjacent-input resolution. Force and contact corner guards are
unchanged. A repeat-memory negative control must exceed the resulting limit;
the original exact-velocity force/residual switch would also fail the adjacent
force/contact guards. This is a precision-aware test-observer change; the
DSP's numeric formats and operations were preserved. Unsuccessful double-delay,
double-Thiran, combined-slip and double-junction experiments were removed
entirely. The fixture does not claim that its old fixed audio bound remained
unchanged, or that a waveform-resolution-only diagnosis explained every bit.

## Validation status

The frozen requested DSP passes Engine, StringPitchRealism, PitchGeometryCache,
Release, ReleaseJoin, ReleaseRealism and PhysicalStopping. The new 90-point
signal pitch guard measured a worst sustained error of 1.21721 cents; the
unchanged 3 dB Finger articulation guard passed. VelocityRealism passes with
the precision-aware input-resolution witness and repeat-memory negative
control described above. `AcustraDSPCxx17` builds. The ownership lifecycle
witnesses now wait the requested 0.32 s damping plus the existing 80 ms return
allowance; none of their ownership requirements was removed.

Focused logs: `/tmp/acustra-polish-focused-final-20261012.log` records seven
passing suites and the superseded wave-only Velocity witness failure;
`/tmp/acustra-velocity-final-20261012.log` records the final VelocityRealism run.
The compact final resolution metrics are in
`/tmp/acustra-resolution-final-diagnostic.log`. Root coordinates the remaining
canonical suite, Rack wrapper/SDK builds, performance measurements, archive
inspection and host evidence; those outcomes belong to the Rack delivery
report and are not inferred from engine tests.
