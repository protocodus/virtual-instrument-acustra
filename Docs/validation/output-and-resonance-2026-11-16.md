# Rack 1.9.55 audition against released 1.3.3

The official Apple Silicon Rack 1.3.3 module (SHA-256 `917197eaa395615841f51f38bdec294713ba5de27eeb81138a14fe4c9e6c7821`) and both development revisions ran in the same offline native event host, at 48 kHz with 64-frame blocks. Stored songs replay their original MIDI at the original tempo and saved Output. A missing modern Quality setting selects 1× on the new instrument while the old factory patches selected 2×. The same controls were also replayed at common 2×. Loudness uses the K-weighted gated stereo signal.

## Output level

Each factory Output trim was fitted to its own released performance. The independent nine-second score includes a bass line, dyads, an arpeggio, treble notes and strums. Values in the table are candidate minus released integrated loudness in LU, except for the indicated older build column and Output gain. The original MIDI and archived 1.3.3 module are common to all comparisons.

| Preset | 1.9.54 fresh Δ LU | 1.9.55 fresh Δ LU | Independent fresh Δ LU | Archived song Δ LU | New factory Output |
|---|---:|---:|---:|---:|---:|
| DADGAD Mono Mic | +10.44 | -0.02 | -1.03 | +0.18 | -7.2 dB |
| Dreadnought Steel | +1.79 | +0.00 | +0.78 | -0.73 | +1.1 dB |
| Jumbo Steel Pick | +4.28 | -0.01 | -0.64 | +0.07 | -5.1 dB |
| Mahogany Steel Thumb | +7.83 | -0.05 | -1.70 | +1.33 | -5.9 dB |
| Open G Slide-ready | +4.19 | +0.05 | +1.18 | -2.14 | -1.5 dB |
| Palm Muted Steel Pick | +10.29 | -0.02 | -0.80 | +1.09 | -8.2 dB |
| Parlor Steel Fingerstyle | +9.29 | -0.01 | -0.69 | +0.99 | -6.5 dB |
| Piezo Stage Steel | +10.13 | -0.05 | -1.14 | +1.45 | -8.0 dB |

Saved user Output and automation retain their stored values. A single factory gain cannot equalize every pitch or velocity under a different physical model. For the controlled phrases and individual keys, both versions used the same controls, including Output. Each individual note is compared by stereo RMS over its first 400 ms.

| Independent check | Size | 1.9.54 mean absolute difference | 1.9.55 mean absolute difference | 1.9.55 90th percentile |
|---|---:|---:|---:|---:|
| Phrases across all bodies, woods and gestures | 48 | 1.36 LU | 1.04 LU | 2.07 LU |
| Individual keys, four velocities and four gestures | 2,880 | 2.58 dB | 1.49 dB | 3.06 dB |


## The Dreadnought and Jumbo resonance

The earlier change to wound-string damping brightened the ordinary transverse string partials. It also reduced damping of the separate lengthwise vibration in the wound G. The body then radiated a whistle around 2.55 kHz on open G and 2.86 kHz when the string was fretted to A. In controlled scratch builds, removing the longitudinal force from the bridge or increasing its damping reduced these lines. Changing room, release noise or processing quality preserved the narrow peaks. The new model gives the wound strings independent longitudinal damping. Finger releases also use a wider pad on firm chord strokes. The soundhole position and the transverse string calibration are retained.


The following readings integrate a 44 Hz window around each line after applying one loudness offset per whole performance. Large differences can occur where the release had little energy at that exact frequency.

| Saved passage | Frequency | 1.9.54 above 1.3.3 | 1.9.55 above 1.3.3 | Change |
|---|---:|---:|---:|---:|
| Dreadnought Steel 0.75–1.3 s | 2549 Hz | +33.1 dB | +17.4 dB | -15.8 dB |
| Dreadnought Steel 5–6 s | 2221 Hz | +7.5 dB | +7.4 dB | -0.1 dB |
| Dreadnought Steel 5–6 s | 2862 Hz | +10.7 dB | +2.7 dB | -8.0 dB |
| Dreadnought Steel 5–6 s | 3551 Hz | +15.6 dB | +16.0 dB | +0.5 dB |
| Dreadnought Steel 5–6 s | 3706 Hz | +6.9 dB | +5.4 dB | -1.5 dB |
| Jumbo Steel Pick 0–2.14 s | 2865 Hz | +17.0 dB | +4.1 dB | -12.9 dB |
| Jumbo Steel Pick 4.29–6.42 s | 2862 Hz | +18.6 dB | +6.0 dB | -12.6 dB |
| Jumbo Steel Pick 6.45–7.7 s | 2865 Hz | +19.7 dB | +4.9 dB | -14.7 dB |

Dreadnought’s second passage contains several harmonics. The improvement is largest at 2.86 kHz. The 3.55 kHz partial after the quieter repeated chord remains stronger than the release. It is the twelfth partial of D4 on the B string, fret 3. Suppressing that one note at the original timing removes 13.42 dB from this narrow band during the 5.53–5.70 s sustain window. The sustained Jumbo line is reduced on the opening D, middle A and closing D.

## Piezo Stage Steel

Moving the hand toward the soundhole changed which string partials reach the saddle. Restoring the two earlier placement settings in an isolated scratch build recovered 5.69 dB of the missing 1–2 kHz. Reverting the picked release alone recovered 2.12 dB. Those changes interact. Restoring the earlier saddle loading or springback made less than 0.1 dB difference in that band.

The candidate applies a broad presence correction around 1.35 kHz to the picked saddle signal before it reaches the dedicated pickup output, capture selector and mic blend. Finger and Thumb bypass that contour. Here are the normalized shares of the bands, expressed as differences from release on the archived score.

| Saved Piezo band | 1.9.54 vs release | 1.9.55 vs release |
|---|---:|---:|
| 500–1000 Hz | -2.49 dB | -0.86 dB |
| 1000–2000 Hz | -7.37 dB | -2.71 dB |
| 2000–4000 Hz | -1.59 dB | -1.53 dB |
| 4000–8000 Hz | +0.44 dB | -0.20 dB |


## Palm Muted Steel Pick

The two passages use the same archived dyads and bridge-hand settings as the approved 1.9.54 audition. The 6.84–7.29 kHz band is reported as a fraction of full-window stereo power.

| Passage | 1.3.3 | Approved 1.9.54 | Candidate |
|---|---:|---:|---:|
| 0–2 s | -26.51 dB | -26.80 dB | -26.65 dB |
| 4–6 s | -24.39 dB | -24.03 dB | -24.15 dB |
