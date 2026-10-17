# Shape now changes the bridge as well as microphone color

The user reported insufficient distinction between body shapes. The UI mapping
was correct: its radio buttons and host parameter directly select all four
shapes, and updating the displayed preset does not reapply its controls.
The missing signal path was mechanical. Shape changed radiation alone, while
the bridge and its tuning compensation ignored it. All 48 tested Piezo shape
pairs were consequently byte-identical.

The change applies the existing Shape frequency and Q directions to the
selected bridge modes. The radiation table is not widened. Frequencies in the
existing 85–145 Hz low-mode category follow the air-mode ratio; other modes
follow the plate-frequency ratio. Positive-semidefinite heave/rock residues
remain unchanged, so the modal stiffness and loss change without inventing a
new coupling or fitted gain. The conductance floor has no measured shape map
and remains unchanged. These are authored construction variations, not newly
measured guitars or a universal relationship between size and Q.

Original keeps **Dreadnought as its calibrated bridge reference**. Its
radiation still uses Auditorium as the unwarped reference: this legacy offset
is retained to preserve the default instrument. Named models retain their
native families: Bellido and Santa Cruz/Auditorium, Washburn/Parlor and
Martin/Dreadnought. The same bridge-mode transform is used by the runtime
junction and the isolated-port phase approximation that tunes the strings.

Shape has its own reconfiguration path, separate from string construction.
Active and idle delay targets are updated with the existing dependency passes
and slew; note ownership, stored string waves and retained re-pluck tails
survive. The existing measured-model bridge transition resets the mechanical
mode histories and rebases the acoustic derivative so the boundary step is
not rendered as an excitation. Microphone radiation retains its 40 ms
crossfade and last-request queue. This does not claim conservation under
arbitrary time-varying geometry; the stationary modes remain passive and
transition behavior is tested independently.

## Measured control distinction

The independent comparison starts a fresh engine for each MIDI 40, 55, 64 and
74 note at velocity 91, with Finger, fixed seed and other controls. Each
signal is normalized by its own 0–500 ms stereo RMS before comparing fixed
frequency bands. The table gives the median power-weighted absolute band
contrast over the four notes, during 100–500 ms sustain. These are spectral
distinctions, not a listening-panel result or realism score.

| Material / capture | Parlor–Auditorium, before → after | Auditorium–Dreadnought | Dreadnought–Jumbo |
|---|---:|---:|---:|
| Steel / Stereo mic | 1.735 → 2.543 dB | 0.976 → 2.423 dB | 3.231 → 3.595 dB |
| Nylon / Stereo mic | 1.722 → 1.825 dB | 2.181 → 3.109 dB | 1.592 → 2.231 dB |
| Steel / Piezo | 0 → 1.854 dB | 0 → 1.117 dB | 0 → 1.103 dB |
| Nylon / Piezo | 0 → 0.648 dB | 0 → 0.783 dB | 0 → 0.884 dB |

All 48 final Piezo shape pairs differ. This is not a blanket increase across
every pair and window: Parlor–Jumbo sustain contrast changes from 3.748 to
3.703 dB for steel and from 3.154 to 2.274 dB for nylon, while its attack
contrast increases. All pairs and windows are retained in
[the qualification JSON](body-shape-coupling-2026-09-09.json).

The comparison also verifies exact baseline/final audio for **16/16
Original/Dreadnought Finger cases** and **32/32 named native cases**, covering
both microphone and Piezo observations. The custom comparison renderer agrees
exactly with the frozen production PerformanceRenderer in its independent
24-case authentication.

## Fixed recording comparison and its tradeoff

Original/Auditorium now has a different mechanical response. The unchanged
Yamaha/Walden training comparison uses nine pitches and two recordings per
pitch for each material, the same calibration, Finger excitation, velocity,
feature extraction and weights. There is no fitting or development-set read.

| Training instrument | Before total | After total | Change |
|---|---:|---:|---:|
| Yamaha nylon | 7.932492354449021 | 7.806703174653165 | −1.59% |
| Walden steel | 9.149220342042632 | 9.616855484215874 | +5.11% |

Lower is better. Nylon's total improves through decay/body terms, but its
attack and harmonic terms worsen; steel's attack, harmonic and decay terms
worsen. This is a more distinct body variation with a material recording
tradeoff, not a universal realism improvement. The calibrated default
Dreadnought and named native responses remain unchanged. Full per-term scores,
selection counts and source-report hashes are preserved in the JSON.

## Native verification and negative controls

The focused target is **`Acustra.BodyShape`**, executable
`AcustraBodyShapeTests`, implemented in `Tests/BodyShapeTests.cpp`.

- Actual configured digital biquads are compared with the tuning phase using
  an independent direct 2×2 anchor solve: 2,880 comparisons over five models,
  both string materials, four shapes and 8/24/48/96 kHz. Worst discrepancy is
  0.492829 cents. The initial new 0.25-cent check failed; the frozen baseline
  itself measures 0.289307 cents from existing float-coefficient/analytic-formula
  differences. The final documented reconstruction budget is 1 cent. Existing
  end-to-end tuning gates are unchanged, and both initial failure logs remain.
- Exact stored main-wave, ownership and two-tail checks accompany active/idle
  retuning, first-sample derivative rebasing and future-note comparison with a
  fresh engine at the selected shape. Largest immediate target change is
  1.01393 samples; current delays slew rather than jump.
- Static chord work and rapid-switch/panic cases cover both materials,
  microphone/Piezo and 8/48/96 kHz. Minimum cumulative body work is zero;
  maximum output peak is 0.696149, below the limiter knee. Sampling the six
  physical string directions does not alone prove full matrix passivity:
  unchanged PSD residue matrices and positive modal damping provide that
  stationary-system argument.
- Deliberately omitting the phase transform fails with a 70.5572-cent mismatch
  and missing retune. Discarding tails on Shape causes four failures. Omitting
  the derivative rebase causes four failures. These controls retain their
  separate source, executable and log hashes.

The focused tests pass. Full engine/plugin checks and any callback measurements
belong to the root qualification report. Source hashes, immutable input-report
hashes, exact score values and native log provenance are in the companion JSON;
temporary native artifacts are under `/tmp/acustra-body-shape-20260908`.
