# The Dreadnought re-pluck at 0:05.48

The upper line in the Dreadnought Steel demo is the twelfth transverse partial of D4 on the B string, fret 3. The string's measured fundamental is 294.07 Hz. Its stiffness puts the partial at 3,551 Hz. The same line is present in released 1.3.3. Isolating the four chord tones in 1.9.55d1 identifies D4. Removing its excitation reduces the sustained line by 13.42 dB. Removing any other chord tone changes it by at most 0.22 dB.

## How the excess developed

* The first verified jump from the 1.3.3 sound occurs with per-contact pitch variation, introduced in `4e9f754` on October 20 and subsequently carried in 1.6.1. Disabling that feature alone in its introductory build restores the previous passage. It raised the sustained line by about 8 dB relative to whole-song loudness. Its purpose was to reproduce the small pitch differences caused by different fretting pressures. Even a few cents changes the phase of the existing vibration before the next stroke. In that introductory build, disabling the offsets on earlier D4 strokes recovered 6.68 dB. Disabling the offset only on the 5.48 s stroke recovered 0.75 dB. The pitch spread was based on guitar recordings. Those measurements set no target for the loudness of this overtone.
* In 1.9.44, increasing the simulated Finger release edge from 0.2 to 0.7 mm added another 4.4 dB to the same relative reading. Reverting only the radius recovers the earlier reading. This change was fitted to the broad Finger/Pick tonal difference in recorded playing. Interactions between strokes mean a softer first pluck can yield a louder individual harmonic on a later re-pluck. The historical deltas are local comparisons and should not be summed to predict the current sound.
* The excess in 1.9.55 comes primarily from the continuing-string re-pluck model. Introduced in `3d37a38` on October 5, this model already existed in the source corresponding to the 1.3.3f1 controls. It lets a second stroke act on one moving string, preserving its phase and energy history. Before applying the next pluck, it also mathematically arrests the previous wave's displacement and local velocity at a single contact point. That's a rigid-hold approximation. Even though it removes total sampled slope energy, the correction can concentrate energy in particular partials. In the same 1.9.55 performance, removing that constraint on the target D4 stroke alone reduced the sustained ring by 9.7 dB. Suppressing the fresh pluck shape, contact noise or soundboard springback individually on that stroke changed it by less than 0.1 dB.

Finger contact with a moving string and changes in fingering pressure have a physical basis. Instrument studies describe damping at repeated plucks and model fingertips with finite stiffness. An instantaneous perfectly rigid grasp on every re-pluck, and the resulting strength of this particular twelfth partial, have no such calibration. [Erkut et al., expressive parameters and repeated plucks](https://aes.org/publications/elibrary/elibrary-page/?id=9224) · [Cuzzucoli and Lombardo, physical model of guitar plucking](https://www.cglib.org/wp-content/uploads/cglib.org/Musicology/Physical%20model%20of%20the%20plucking%20process%20in%20the%20classical%20guitar.pdf)

## The 1.9.56 audition

In a multi-finger pinch, the pad now only partly arrests a ringing plain treble string. The string's transverse stiffness at the same contact point is `T / (L p (1 − p))`, with tension `T`, speaking length `L` and relative position `p`. The model weights the rigid correction by the pad stiffness divided by the sum of the pad and string stiffnesses. Applying this fraction to both displacement and velocity is a reduced model of the grasp. The effective 100 N/m pad stiffness is an audition setting fitted against the released phrase.

The same MIDI events at 104 BPM recall Output 0 dB, Pluck Position 0.28 and 20% piezo blend. The table gives raw stereo power integrated from 3,529 to 3,573 Hz, with each version playing at its saved level. All eight fresh factory Output settings and the soundhole position mapping are identical to 1.9.55.

| Saved Dreadnought measure | 1.3.3 | 1.9.55d1 | 1.9.56d1 |
|---|---:|---:|---:|
| Whole performance (LUFS) | −17.77 | −18.50 | −18.53 |
| 5.00–6.00 s ring (dBFS) | −63.83 | −48.53 | −61.91 |
| First 49 ms of the 5.48 s chord (dBFS) | −62.56 | −47.66 | −63.87 |
| 5.53–5.70 s sustain (dBFS) | −60.98 | −43.81 | −57.86 |

The sustained ring is 14.04 dB lower than 1.9.55. It remains 3.13 dB above released 1.3.3 at saved levels and 3.88 dB above it after matching whole-song loudness. The full performance changes by −0.024 LU from 1.9.55. With both engines running at 2× quality, the reduction is 13.31 dB and the full performance changes by −0.018 LU. The opening G passage retains its previous 2.55 kHz balance. On a later D repeat around 6.64 s the single partial is weaker than in the release, while both broader 2–4 and 4–8 kHz bands are closer to it.
