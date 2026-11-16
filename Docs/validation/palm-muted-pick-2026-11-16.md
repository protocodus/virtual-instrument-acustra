# Palm-muted steel pick

Use the released Rack 1.3.3 ARM64 module as the reference for every Acustra
sound change. Its SHA-256 is
`917197eaa395615841f51f38bdec294713ba5de27eeb81138a14fe4c9e6c7821`.
Play the same MIDI, patch and controller moves through each version. Compare
tone at matched loudness, and measure recall at the saved output level.

The two reported passages on Palm Muted Steel Pick repeat an E2/B2
interval. Near 6.98 and 7.11 kHz the B2 string sustains two high partials
well past its initial attack. Isolating the pickup, switching off room
and removing the axial-force and pick-noise components traced the ring to
the pitched string release. The microphone response also contributed to
the loss of body between 500 Hz and 1 kHz.

The 1.9.53 voicing increases the frequency-dependent round-trip loss of a
picked string under palm pressure. It uses the existing string filter and
follows the attack technique when the hand presses or lifts on a ringing
note. The microphone's open-playing 500 Hz cut moves toward a broad 770 Hz
lift as the heel settles, with pressure-dependent level compensation. These
are listening adjustments against the released instrument. The soundhole
geometry and the pickup circuit keep their preceding designs.

| Measurement on archived Palm Muted Steel Pick | Released 1.3.3 | 1.9.52d1 | 1.9.53d1 |
| --- | ---: | ---: | ---: |
| Full phrase, 500 Hz–1 kHz share (dB) | −8.74 | −16.98 | −12.30 |
| 0–2 s, 500 Hz–1 kHz share (dB) | −10.91 | −18.44 | −14.06 |
| 4–6 s, 500 Hz–1 kHz share (dB) | −7.97 | −17.27 | −12.78 |
| 0–2 s, upper-partial decay by 75–150 ms (dB) | −60.1 | −20.8 | −33.6 |
| 4–6 s, upper-partial decay by 75–150 ms (dB) | −60.2 | −21.3 | −33.4 |
| Full phrase, saved level relative to release (LU) | 0.00 | +1.80 | +0.51 |

The three Rack modules rendered the frozen performance at 48 kHz and
matched 2× quality. Band shares use summed stereo power relative to
60 Hz–10 kHz for each performance or excerpt. Decay compares power at
6.84–7.29 kHz in repeated E2/B2 strokes to their own first 24 ms.
The full-phrase 2–4 kHz band is 4.9 dB below released 1.3.3, compared
with 2.9 dB below in 1.9.52. The other seven archived performances
reproduced 1.9.52 byte for byte at both matched and stored qualities.
These results cover the archived parts and controls.
