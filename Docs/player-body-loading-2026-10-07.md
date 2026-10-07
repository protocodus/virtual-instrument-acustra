# Authored player contact on Classical 78

Classical 78 now adds a small, fixed held-player structural loss to the
supported/free-back Bellido measurement. It requires no extra performance
control. This is a bounded authored approximation, not a fitted measurement
of a player's posture, nor a listening preference result.

The archive's g35 measurement was made with neck/end-block supports and a
free back; see the [capture investigation](classical78-capture-2026-10-07.md).
A player's torso/forearm can dissipate structural vibration. Original
already contains recording-derived plate-Q and low-air decay adjustments,
with unknown player loading, so this mechanism leaves Original unchanged.
It does not add string palm muting, which is a separate existing mechanism.

For each measured Bellido mode, with frequency `f` and unloaded `Q`, the
additional inverse-Q is:

```
loss = 0.006 * f²/(f² + 180²) * 900²/(f² + 900²)
Q_loaded = 1/(1/Q + loss)
```

The two frequency scales and magnitude are authored. Reduced participation
below the plate band avoids assigning a full structural contact loss to the
mainly-air resonance. The high-band rolloff avoids replacing the existing
fitted broadband drain. The measured bank receives the loss once. No extra
loss is applied to synthetic radiation continuation (its existing density
estimator observes the configured bank) or the conductance-floor prototype.
No noise, mass/stiffness changes, room feedback,
moving resonance centers or independent random pole motion are added.

The same pure helper supplies radiation poles, deployed bridge coefficients
and the cached analytic bridge mobility used by string tuning. Radiation
residues retain their continuous input coupling through the zero-order-hold
ratio between the loaded pole and the unloaded 48 kHz reference. Bridge
force/moment residue matrices are unchanged. A construction configures one
fixed posture; attacks do not rebuild the body. The existing modal
recurrences are unchanged, and the new work occurs during configuration or
an analytic-cache rebuild. No callback timing claim is made here.

For the fitted modal approximation, each mobility mode is
`R * s/(s² + (omega/Q_loaded)*s + omega²)`, with positive damping and the
existing positive-semidefinite matrix `R`. Its real part on the imaginary
axis is `R * (omega/Q_loaded)*w² / ((omega²-w²)² +
(omega*w/Q_loaded)²)`, which remains positive semidefinite. The prewarped
bilinear transform preserves positive-realness. This establishes passivity
of the added fixed modal loss within the fitted model; it does not establish
collocation of the original measured response matrix. Construction/model
changes keep their existing finite bank fades and retained histories.

The offline `PerformanceRealism.playerBodyLoading` switch removes this
mechanism exactly. It is a setup-only comparison option that clears sounding
state, not a saved product parameter or an automation control.

Both 0.003 and 0.006 inverse-loss magnitudes were evaluated analytically. The
selected 0.006 gives the following free-mode brackets before construction
morphs. Peak changes refer to an isolated mobility resonance at its center;
they are not promises of equivalent emitted-note attenuation.

| Mode | Free T60 | 0.003 T60 | Selected T60 | Selected peak change |
| --- | ---: | ---: | ---: | ---: |
| 102.17 Hz | 0.5677 s | 0.5571 s | 0.5469 s | −0.325 dB |
| 336.93 Hz | 0.2187 s | 0.2046 s | 0.1923 s | −1.117 dB |
| 410.83 Hz | 0.1580 s | 0.1489 s | 0.1407 s | −1.008 dB |
| 555.50 Hz | 0.1392 s | 0.1302 s | 0.1223 s | −1.125 dB |
| 751.69 Hz | 0.2180 s | 0.1939 s | 0.1745 s | −1.932 dB |

The focused native held/released chord changes Main RMS level by
−0.3255/−0.3267/−0.3205 dB at 44.1/48/96 kHz; paired waveform RMS differences
are 11.37/11.39/10.52%. Held pitch for MIDI 40, 52 and 67 differs from the
unloaded version by at most 0.1 cent in a 0.35–0.80 s spectral scan. Native
levels and existing loudness reference tables are retained. Signal change
does not by itself establish better realism or listener preference.

`Acustra.PlayerBodyLoading` verifies independent loss math, actual radiation
centers and residue conversion, analytic/deployed mobility agreement,
positive-real matrix response across four shapes and three rates, Original
audio identity, pickup independence of microphone capture, held tuning,
chord/release stability, reprepare, setup-switch invalidation, interrupted
construction/model fades and block partition invariance. Existing supported/
free-measurement reconstruction fixtures explicitly disable player loading
so their original independent math and tolerances continue to verify the raw
model; they do not present the loaded production response as a free-body fit.

The new suite and the six affected existing suites (Engine, Capture,
GuitarModels, BodyShape, StringPitchRealism and StringDecayRealism) pass in the
isolated GCC 14.2 Release build. DSP/Performer and the new test also compile
as C++17. Full integrated and host validation belongs to the combined change;
it was not duplicated for this isolated mechanism.

Reproduce the focused validation from the repository root:

```sh
cmake -S . -B build-body -DCMAKE_BUILD_TYPE=Release -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON -DBUILD_TESTING=ON
cmake --build build-body --parallel 2 --target AcustraPlayerBodyLoadingTests AcustraDSPCxx17 AcustraEngineTests AcustraBodyShapeTests AcustraGuitarModelTests AcustraCaptureTests AcustraStringPitchRealismTests AcustraStringDecayRealismTests
ctest --test-dir build-body -R '^Acustra\.(PlayerBodyLoading|Engine|BodyShape|GuitarModels|Capture|StringPitchRealism|StringDecayRealism)$' --output-on-failure
```
