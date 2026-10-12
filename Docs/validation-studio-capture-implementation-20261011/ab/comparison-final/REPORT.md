# Targeted native capture A/B

296 cases per version, identical scores and controls. Before is frozen d54; after build/binary hashes are recorded in normalized-after-manifest.json and normalized-after-renders/report.json. The production microphone references include the fixed calibration-phrase level match; approved mixture weights are unchanged. No mixture weights were changed by this test.

The scratch fixture sets the host Master Tune before the note. It preserves open/stopped geometry and retensions all strings while body/microphone poles remain fixed. This also retunes sympathetic strings; it is not an external force-only frequency sweep. Zero-cent scratch output was proven byte-identical to the original d54 renderer.

| Target | Window | H1 balance change | Raw H1 change | H1 after global level match |
|---|---|---:|---:|---:|
| parlor MIDI47 | attack | -4.20 to -1.91dB | -1.44 to -0.38dB | -0.23 to +0.89dB |
| parlor MIDI47 | early | -3.87 to -2.14dB | -1.52 to -0.40dB | -0.32 to +0.86dB |
| parlor MIDI47 | held | -4.27 to -1.92dB | -1.55 to -0.41dB | -0.37 to +0.83dB |
| parlor MIDI47 | late | -4.27 to -2.12dB | -1.74 to -0.29dB | -0.83 to +1.08dB |
| auditorium MIDI45 | attack | -4.23 to -1.31dB | -2.32 to -1.02dB | -0.95 to -0.10dB |
| auditorium MIDI45 | early | -3.91 to -2.09dB | -2.40 to -1.02dB | -1.03 to -0.11dB |
| auditorium MIDI45 | held | -3.92 to -2.16dB | -2.46 to -1.02dB | -1.09 to -0.11dB |
| auditorium MIDI45 | late | -5.24 to -2.06dB | -3.72 to -0.79dB | -2.35 to +0.12dB |
| jumbo MIDI40 | attack | -4.33 to -1.32dB | -2.44 to -1.44dB | -1.07 to -0.53dB |
| jumbo MIDI40 | early | -3.63 to -2.59dB | -2.56 to -1.39dB | -1.19 to -0.47dB |
| jumbo MIDI40 | held | -3.83 to -2.66dB | -2.65 to -1.25dB | -1.28 to -0.34dB |
| jumbo MIDI40 | late | -5.68 to -2.53dB | -3.78 to -0.94dB | -2.87 to -0.03dB |
| jumbo MIDI41 | attack | -4.44 to -1.23dB | -2.23 to -1.22dB | -0.86 to -0.26dB |
| jumbo MIDI41 | early | -3.54 to -2.28dB | -2.31 to -1.25dB | -0.94 to -0.29dB |
| jumbo MIDI41 | held | -3.84 to -2.36dB | -2.35 to -1.24dB | -0.98 to -0.30dB |
| jumbo MIDI41 | late | -5.52 to -1.91dB | -3.65 to -0.94dB | -2.74 to -0.03dB |

Ranges include both woods, both microphone captures and the full−100…+100c primary sweep. H1 balance is H1 relative to mean H2/H3 power. Native raw powers, early/held/release windows, folded stereo and estimated settled H1 frequencies remain in the machine-readable reports.

## Exact centre: spectral balance versus local pitch prominence

These are different quantities. Spectral balance compares H1 with H2/H3 within one note. Local pitch prominence compares H1 power at0c with the arithmetic mean of H1 power at−100c and+100c on the same string/fret. The latter is a bounded tuning-neighbor proxy, not an ordinary chromatic-note or modal-Q measurement. Values below use the early0.15–0.65s window; all windows are in comparison.json.

| Centre / wood / capture | Spectral balance before → after | Local H1 prominence before → after |
|---|---:|---:|
| parlor B1 / spruce / stereo_mic | +21.32 → +19.15dB | -0.45 → -0.46dB |
| parlor B1 / spruce / mono_mic | +23.55 → +20.55dB | -0.49 → -0.49dB |
| parlor B1 / mahogany / stereo_mic | +22.41 → +19.66dB | -0.30 → -0.32dB |
| parlor B1 / mahogany / mono_mic | +21.54 → +18.96dB | -0.34 → -0.35dB |
| auditorium A1 / spruce / stereo_mic | +14.13 → +11.90dB | +1.94 → +1.94dB |
| auditorium A1 / spruce / mono_mic | +15.00 → +12.54dB | +1.89 → +1.89dB |
| auditorium A1 / mahogany / stereo_mic | +12.40 → +9.83dB | +1.90 → +1.91dB |
| auditorium A1 / mahogany / mono_mic | +12.45 → +9.92dB | +1.87 → +1.88dB |
| jumbo E1 / spruce / stereo_mic | +11.82 → +8.75dB | +0.23 → +0.03dB |
| jumbo E1 / spruce / mono_mic | +13.71 → +10.17dB | -0.23 → -0.39dB |
| jumbo E1 / mahogany / stereo_mic | +9.63 → +6.68dB | +0.69 → +0.50dB |
| jumbo E1 / mahogany / mono_mic | +11.80 → +8.33dB | +0.23 → +0.08dB |
| jumbo F1 / spruce / stereo_mic | +13.05 → +10.21dB | +1.66 → +1.67dB |
| jumbo F1 / spruce / mono_mic | +14.78 → +11.64dB | +1.65 → +1.65dB |
| jumbo F1 / mahogany / stereo_mic | +11.55 → +8.95dB | +1.52 → +1.54dB |
| jumbo F1 / mahogany / mono_mic | +11.66 → +9.10dB | +1.57 → +1.57dB |

## Partial-cancellation checks

- primary, H2: worst power change -2.11dB, primary-auditorium-mahogany-mono_mic-finger-v96-m45-s2-c-100.000000, attack; baseline -51.69dBFS → after -53.81dBFS.
- primary, H3: worst power change +0.44dB, primary-auditorium-mahogany-stereo_mic-finger-v96-m45-s2-c+100.000000, attack; baseline -46.27dBFS → after -45.83dBFS.
- excitation, H2: worst power change -0.82dB, excitation-jumbo-mahogany-stereo_mic-thumb-v56-m41-s1-c-50.000000, attack; baseline -55.50dBFS → after -56.31dBFS.
- excitation, H3: worst power change +0.53dB, excitation-auditorium-mahogany-stereo_mic-thumb-v56-m45-s2-c+50.000000, attack; baseline -72.10dBFS → after -71.57dBFS.
- partial, H2: worst power change +0.71dB, partial-parlor-spruce-mono_mic-finger-v96-m40-s1-c-15.621130, attack; baseline -41.10dBFS → after -40.39dBFS.
- partial, H3: worst power change -1.94dB, partial-parlor-mahogany-mono_mic-finger-v96-m40-s1-c-15.621130, attack; baseline -63.19dBFS → after -65.13dBFS.

## Listening files

Each pitch A/B WAV alternates before then after at−100c,0c,+100c. The partial witness WAVs alternate versions at lowE tunings whose nominal third harmonics are245/254Hz, for Finger96, Pick120 and Thumb56. Exact segment offsets and hashes are in audition/manifest.json. No listening preference is claimed.

AFTER matching gain uses one scalar per capture across the whole primary Finger96 grid, applied unchanged to all models, woods, notes and techniques. Raw numerical measurements are unnormalized.

- stereo_mic: after gain +1.371dB; pair headroom gain +0.000dB.
- mono_mic: after gain +0.912dB; pair headroom gain +0.000dB.

## Limits

- Master tuning shifts all string references and sympathetic pitches; it is a physically consistent excitation sweep, not a pure external force-frequency sweep.
- Body/microphone poles are fixed for each construction. Existing attack pitch and body pulling can shift measured peaks slightly from nominal excitation.
- Two visible woods at48kHz. No Maple, sample-rate matrix, new model tuning or listening acceptance.
- Early-window harmonic integration cannot resolve arbitrarily narrow cancellation. H1–H4 powers and partial witness cases remain separate from target balance.

All extrema and individual cases are retained; a reduced target ratio alone is not acceptance. Fine pitch spacing does not overcome the spectral resolution of a short attack window.
