# Independent microphone-variant comparison

These are scratch experiments. The report retains measured tradeoffs rather than treating a reduced target-band ratio as acceptance. Each variant has 600 paired native clips; all 136 Piezo clips are byte-identical. The 192 extension clips are microphone-only. No per-note gain normalization was used.

## Target fundamental balance by time window

Changes are dB of H1 relative to mean power of H2 and H3; negative means less fundamental emphasis. Ranges span the reported target notes, both rendered woods, both microphone captures and available string routes. They do not measure same-note loudness or modal damping.

| Variant | Shape | Attack | Early | Held | Late |
|---|---|---:|---:|---:|---:|
| measured_bridge | parlor | -20.21 to -12.06 | -20.40 to -10.73 | -15.65 to -11.06 | -18.54 to -5.36 |
| measured_bridge | auditorium | -14.43 to -11.66 | -14.06 to -12.22 | -17.28 to -10.96 | -20.76 to -13.11 |
| measured_bridge | jumbo | -24.59 to -11.50 | -16.58 to -13.48 | -16.03 to -11.44 | -20.25 to -6.06 |
| measured_blend | parlor | -10.92 to -0.51 | -11.83 to -3.64 | -7.55 to -2.97 | -8.06 to -1.77 |
| measured_blend | auditorium | -9.21 to -4.06 | -6.71 to -4.49 | -5.83 to -1.81 | -5.64 to -0.83 |
| measured_blend | jumbo | -8.73 to +0.61 | -6.80 to -0.36 | -6.32 to -2.06 | -8.34 to -1.22 |
| neck30_literal264 | parlor | -3.72 to +0.00 | -3.97 to -1.74 | -3.41 to -2.49 | -3.03 to -0.89 |
| neck30_literal264 | auditorium | -2.74 to -0.98 | -2.67 to -1.41 | -2.66 to -1.71 | -1.82 to -1.33 |
| neck30_literal264 | jumbo | -4.06 to -1.65 | -3.66 to -1.85 | -3.57 to -2.32 | -3.21 to -1.51 |
| offhole45_literal264 | parlor | -3.97 to -1.70 | -5.44 to -2.51 | -4.78 to -3.12 | -3.94 to -0.71 |
| offhole45_literal264 | auditorium | -3.98 to -2.87 | -4.15 to -2.75 | -3.83 to -2.33 | -2.59 to -1.14 |
| offhole45_literal264 | jumbo | -5.64 to +0.64 | -5.30 to -3.79 | -5.32 to -3.46 | -4.44 to -2.34 |
| free_neck30 | parlor | -0.65 to +2.84 | -1.18 to +0.99 | -0.90 to +0.41 | -0.21 to +2.35 |
| free_neck30 | auditorium | -0.33 to +1.13 | -0.59 to +0.65 | -0.52 to +0.23 | +0.07 to +0.85 |
| free_neck30 | jumbo | -1.73 to +0.99 | -1.16 to -0.06 | -1.41 to -0.45 | -1.49 to +0.43 |

Window bounds in seconds: {"attack": [0.02, 0.2], "early": [0.15, 0.65], "held": [0.2, 2.8], "late": [1.9, 2.8], "release": [3.05, 3.9]}.

## Interpreting resonance and cancellation

Parlor B1 has an attack H1 neighbor excess of about +5.6 to +5.8 dB in the baseline, followed by a much weaker held fundamental. A held-only metric cannot describe the initial boom. The windowed per-note JSON retains both facts.

The table below gives the worst distance-matched held H1 change among lower notes and the largest held spectral-balance increase among upper notes. A harmonic-ratio increase can be caused by higher-harmonic cancellation; it is not necessarily an equal increase in H1 level.

| Variant | Worst low H1 change | Case | Largest upper H1/H2-H3 change | Case |
|---|---:|---|---:|---|
| measured_bridge | -16.99 | dreadnought-spruce-mono_mic-m45-s1 | +17.86 | jumbo-spruce-mono_mic-m53-s3 |
| measured_blend | -8.76 | auditorium-mahogany-mono_mic-m40-s1 | +20.74 | jumbo-mahogany-mono_mic-m58-s4 |
| neck30_literal264 | -16.23 | parlor-mahogany-mono_mic-m40-s1 | +3.39 | parlor-mahogany-stereo_mic-m57-s4 |
| offhole45_literal264 | -16.47 | parlor-spruce-stereo_mic-m41-s1 | +7.07 | auditorium-spruce-stereo_mic-m56-s4 |
| free_neck30 | -2.70 | parlor-spruce-mono_mic-m40-s1 | +3.71 | auditorium-spruce-stereo_mic-m56-s4 |

## Independent measured bound

NBody mic 4 changes the empirical low-air/plate-band power ratio relative to front mic 1 by about −2.1 to −6.7 dB across four classical-guitar strikes, recorder-reference choices and 100–200 ms windows. Mic 3 instead raises that ratio about +3.5 to +6.2 dB. This supports direction-dependent balance changes of several dB, but not a universal reduction, an identified neck-direction index, or a transfer function for the present g21 guitar. Source geometry is internally approximate, the records are short, and a license for shipping the raw NBody data was not found. No NBody coefficients are used in these variants.

## Assessment

- Measured bridge is a real pressure-tap control, not a new studio position. It greatly lowers target fundamental balance but creates a Parlor F# low-note dip and large changes elsewhere.
- Measured blend is a frequency-independent average of real pressure taps, not a new measured location. It lowers target balance but adds Jumbo upper-bass mono holes; its large harmonic-ratio increases sometimes reflect suppressed higher harmonics rather than equivalent fundamental gain.
- Coupled neck30 gives moderate target-balance reduction but severely weakens Parlor open-bass fundamentals after distance matching. Its constrained A0 spatial relation does not reproduce the held-out measured tap well.
- Coupled offhole45 has similar low-note cancellation plus stronger upper-bass emphasis. It is not a demonstrated solution.
- Free neck30 fits the three measured taps more closely and has the smallest played low-note disruption. Yet its Parlor and Auditorium target balance is nearly unchanged in the held window and can increase on attack. It is the most defensible listening candidate of these projections, but not a demonstrated fix for the reported boom. A Parlor/Maple extreme force/moment FRF combination also develops a deep notch near 257 Hz; Maple was not part of the played grid.

The free fit uses a rank-two source matrix with condition about 5.2–5.8. Training complex errors are about 2.1% for A0 heave and 6.0–6.5% for T1 heave; moment errors remain 15–23%. These are training residuals, not independent validation at 30 cm. The earlier constrained A0 held-out errors are roughly 60–92%. Neither model establishes the complete broadband studio field. All variants preserve existing source poles and mechanics by construction, separately checked by the parent runtime invariant harness.

Recommendation: retain all five as documented listening experiments. None demonstrates both a consistent reduction of the reported attack/held boom and absence of relevant tradeoffs. A low flag count is not acceptance. The source model, missing studio measurements and user listening judgement remain separate questions.

## Reproducibility and limits

comparison-summary.json includes exact score/report/override hashes, every target/window range, the most adverse low/upper-note cases and FRF extrema. The five *-extended.json files retain all individual native and force/moment cases. scorer-controls.json checks the independent rational transfer formula against the complex-residue form, an identity override and in-phase/anti-phase stereo controls.

- Research-only coefficient injection; no new production model or EQ section.
- 600 cases per variant: original 408 plus 192 upper-note microphone cases. Piezo has 136 cases, not 200.
- Two native woods only: Spruce and Mahogany. The force-transfer sweep separately covers all three woods.
- Attack, early, held and late power are separate observations; held averages can conceal attack peaks.
- Spectral H1/H2-H3 reduction differs from reducing same-string note-neighbor excess.
- Only a declared single common distance gain is compensated in numerical reporting. Audio is untouched.
- No output spectral power is called internal modal energy or free decay; this is one pluck/control/rate grid.
- FRF M/F=-1,0,+1 brackets the normalized input axes; it is not a claim that every point occurs during every pluck.
- No scalar score establishes studio-position accuracy or listening preference.
