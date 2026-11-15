# Acustra 1.9.51 song compatibility audition

The reference is the released Reason Rack 1.3.3 ARM64 module, SHA-256
`917197eaa395615841f51f38bdec294713ba5de27eeb81138a14fe4c9e6c7821`.
The other native modules were the 1.9.50d1 engine and the recalibrated 1.9.51d1 candidate, SHA-256
`24972b69a897ab5faa944ad676e16ebf09631b6c5a45c394d8fad689805e3e7d`. The song captures are from that candidate.
This is a candidate for listening. There is no listening verdict yet.

## Performance and capture

The test host played all eight Rack factory MIDI files through the native modules
at 48 kHz in 64-frame batches, using each song's tempo, note events, pitch bend,
palm controller and two seconds of tail. The same historical patch values
(Rack source `cf0b9860f`) were set on each module, including Output. That source
provides a saved-settings fixture. The released binary provides the reference
sound. No personal Reason song was used.

For the controlled comparison, both generations' quality properties were set
to 2×. A second pass loaded only the properties in the historical patches, so
later builds used their 1× Quality default. The figures below use the original
render levels. Each band's stereo power is relative to 60 Hz–10 kHz. Six
bands cover 80–200, 200–500, 500–1000, 1000–2000, 2000–4000 and 4000–8000 Hz.
Each band and each song has equal weight in the mean absolute difference.
Loudness is integrated BS.1770 over each complete render.

| Historical patch | 1.9.50 level change (LU) | Candidate level change (LU) | 1.9.50 band difference (dB) | Candidate band difference (dB) |
| --- | ---: | ---: | ---: | ---: |
| DADGAD Mono Mic | +2.2 | +0.8 | 3.71 | 0.88 |
| Dreadnought Steel | +1.4 | +0.1 | 1.61 | 0.85 |
| Jumbo Steel Pick | +0.6 | -1.0 | 3.05 | 1.21 |
| Mahogany Steel Thumb | +3.0 | +1.5 | 1.18 | 0.91 |
| Open G Slide-ready | -0.8 | -1.5 | 2.88 | 1.32 |
| Palm Muted Steel Pick | +3.9 | +0.7 | 3.81 | 1.95 |
| Parlor Steel Fingerstyle | +3.0 | +1.6 | 3.31 | 0.48 |
| Piezo Stage Steel | +4.3 | +0.8 | 3.61 | 1.63 |

Across all eight songs, the mean band difference fell from 2.89 to 1.15 dB.
The largest loudness shift fell from 4.3 to 1.6 LU. Loading the older settings
without a Quality override gave 2.82 and 1.16 dB respectively, with a maximum
candidate loudness shift of 1.6 LU.

A third pass used the saved values from the 1.9.50d1 Rack bank at
`a78ae2873`, including that bank’s 1× Quality and Output. Against 1.9.50,
the eight candidate renders were 0.7–2.6 LU quieter. The mean absolute
six-band difference was 2.24 dB. These results apply to the supplied
performances.

The local audition pack is in the Rack workspace under
`diagnostics/acustra-1.9.51-voicing-2026-11-15/listening/`. Open
`index.html` for aligned switching between all eight songs at their stored
levels. The optional RMS match uses one constant gain per take. The
25.15-second WAV plays Dreadnought and Jumbo excerpts from each version.

## Calibration choices

- Four Finger patches, ten pitches from E2 to A5 and five velocities from 40 to 120
  informed the fingertip radius sweep. At 0.25 mm, the median 3–8 kHz share in
  the first 30 ms was about 4.6 dB below the release, compared with 15.3 dB below
  at 0.7 mm. From 30–200 ms it was within 0.5 dB of the release. This leaves
  the corrected bridge corner, gradual saddle unloading and current string decay in place.
- The Pick distance sweep covered all three Pick patches at 14 pitches and three
  velocities. A distance scale of 0.55 restored much of the third harmonic on
  upper Jumbo notes. Median H3 relative to H1 there was 2.2 dB below the release,
  compared with 8.2 dB below at 0.95. All positions continue to follow the
  panel and the length of the vibrating string.
- The existing smooth velocity curve now reaches full gain at velocity 127.
  An ordinary 72-to-100 accent is about 5–6 dB across Finger, Pick and Thumb.
  Strums retain the 1.9 model's bounded force variation. At very low velocities
  Pick still speaks more strongly than the release, especially below MIDI 24.
- After construction calibration, a 1.4 dB cut at the Main and separate Piezo
  output stages reduced the largest level shift on the eight historical patches.
  Fresh factory programs have separately metered Output settings. The
  Dreadnought factory patch loads +3 dB. Rack Init and partial states without
  Output use the historical 0 dB. Fresh Palm Muted Steel Pick uses the most
  bridgeward position (0%), 89% Body and +6 dB Output. Its bridgeward hand
  brings the factory phrase's midrange closer to the older tone. Loading a
  saved position, Body or Output retains its numeric value.

All eight fresh factory patches measured between −9.69 and −8.94 VU over
27 note and chord cases per patch (0 VU = −12 dBFS). The maximum protected
sample in that stress test was −1.5 dBFS. The hard Pick calibration now
renders the same performance at two lower output gains, checks that both
waveforms scale linearly and recovers the peak before output protection.
The hardest stereo mic, mono mic and pickup peaks moved from +0.36, −0.29
and +4.54 dBFS on 1.9.50 to +0.18, −0.91 and +1.23 dBFS respectively.
These internal peaks can exceed full scale before the output limiter.

The Rack all-patch test includes fixed 1.3.3 harmonic and velocity references.
Compare the same passage at stored level to judge a mix, and at matched level
to judge the contact sound and decay.

## Recall limits inherited from earlier Rack versions

A 1.3.3 song using stored tuning index 4 meant half-step down. Since before
1.9.19 this retired slot has played Open G. A song that saves only the earlier
`oversampling` property can also open at 1× in builds using `quality`.
The development changes keep the existing stored property definitions.
