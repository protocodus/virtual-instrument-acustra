# Repeated chords by picking technique

The 1.9.57 audition changes how the hand catches an already-vibrating string when a chord is struck again. The engine applies the contact at its existing soundhole position before merging the returning wave with the new pluck. The effective contact stiffnesses below were selected by audition measurements against the official 1.3.3 Rack binary and the accepted 1.9.56d1. Their numerical values describe the audition tuning.

| Tool | Returning-string contact |
|---|---|
| Finger | The pinched B and high E retain the 100 N/m fingertip. Other chord contacts fade toward a 300 N/m pad as the string shortens by 16–46% from its open length. |
| Thumb | The effective pad stiffness follows 330 + 1,900 v⁴ N/m, with normalized MIDI velocity v. |
| Pick | A light stroke uses a 300 N/m edge. The soft response fades to the firm catch between velocities 0.80 and 0.88, with increasing palm pressure, and on the plain strings between the sounding lengths of frets 12 and 15. |

The main listening guard is the fourth stroke of an Auditorium/Spruce Pick B-major chord, MIDI 71/75/78 at velocity 113. The repeated notes and all their earlier contacts were played into the native Rack device before measuring. At 48 kHz and common 2× processing, the 3.13 kHz sustain in successive windows 42–127 and 140–225 ms after onset was as follows, in dBFS.

| Version | Early | Late |
|---|---:|---:|
| Released 1.3.3 | −37.70 | −41.91 |
| Accepted 1.9.56d1 | −39.47 | −46.47 |
| Uniform 300 N/m experiment | −32.60 | −36.85 |
| Per-technique 1.9.57d1 | −39.47 | −46.47 |

The test on the 1.9.56 Dreadnought Steel performance at 5.48 s and the Palm Muted Steel Pick passages at 0–2 and 4–6 s uses archived controls and identical event times. The local comparison includes full-history A/B recordings, alternative velocities, timing and sample rates, and a chromatic sweep across the four body shapes and three woods.

Across 7,296 repeated strokes on four guitar bodies and three woods, 1,525 of the 1,644 previously flagged partials moved closer to 1.3.3. The median reductions were 6.16 dB on Finger, 4.26 dB on Thumb and 4.80 dB on Pick. New excesses above both 1.3.3 and 1.9.56 appeared on 40 strokes, compared with 89 for the shared soft-contact trial. One residual is a 6.69 kHz partial on the third Jumbo Pick F♯-major stroke at velocity 102, which rose 4.1–5.6 dB versus 1.9.56.

All 2,880 isolated strikes matched accepted output sample for sample, as did the original Dreadnought and palm-muted comparison passages. Across the eight factory performances, both sample rates and three recall conditions, the maximum whole-song shift from 1.9.56 was 0.1204 LU. All eight sets of factory control values matched the 1.9.56 patches.
