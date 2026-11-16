# Steel-string soundhole contact geometry

Acustra models a 648 mm open string and a 101.6 mm soundhole on all four
bodies. Its acoustic body model describes the outline, depth and opening
diameter. The distance between the saddle and opening was estimated from
the front product photograph for each representative body.

| Acustra body | Photograph | 12th fret | Near lip | Saddle | Centerline distance | Minimum strike |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Parlor | [Martin 0-18](https://www.martinguitar.com/guitars/retired-models/0-18.html) | 487 | 715 | 819 | 102 mm | 110 mm |
| Auditorium | [Martin 000-18](https://www.martinguitar.com/guitars/standard-series/000-18.html) | 471 | 700 | 794 | 94 mm | 105 mm |
| Dreadnought | [Martin D-28](https://www.martinguitar.com/guitars/standard-series/D-28.html) | 465 | 698 | 787 | 90 mm | 100 mm |
| Jumbo | [Gibson SJ-200 Standard](https://www.gibson.com/products/gibson-sj-200-standard-antique-natural) | 473 | 690 | 779 | 94 mm | 105 mm |

The three location columns are vertical pixel coordinates after cropping the
full guitar and scaling its height to 1,000 pixels. Measurements follow the
middle of the strings to the bridge-side inner lip of the opening and the
middle of the compensated saddle. Multiplying the lip-to-saddle pixel
distance by 324 mm and dividing by the 12th-fret-to-saddle pixel distance
transfers the photographed proportions to the modeled scale. Allow roughly
3 mm for perspective and reading the edges. The manufacturers publish the
photographs and specifications. The coordinates and the working minima
are estimates for Acustra.

The aperture curves away from the saddle under the outside strings. At
lateral offset `y`, a circular opening of radius `r` moves its bridge-side
edge neckward by `r − √(r² − y²)`. The 54.8 mm bridge string spacing on the
000-18 and D-28 puts the outside strings at most 27.4 mm off center, or
8.0 mm farther from the saddle at the opening. String spacing narrows toward
the nut. The rounded minima allow a straight stroke across all six strings
to reach the opening. They apply to the struck string in both transverse
planes, including the normal plane's bridgeward offset.

The contact law starts bending toward the opening one quarter of its diameter
(25.4 mm) before the unconstrained hand reaches the rim. This transition is a
voicing choice. Points farther neckward retain their original travel.
A conventional upward slide keeps shortening the physical string to fret 20
(204.1 mm). Beyond it the virtual pitch keeps rising while the hand stays
on the playable string.

## Strike verification

`Acustra.PhysicalHandGeometry` checks 864 fresh strikes at 48 kHz across
four bodies, three techniques, six strings, three frets and both ends of
panel Position and MPE CC74. It recovers the two transverse-plane locations
from the release shapes written to the delay lines. The closest observed
normal-plane strikes were 115.1 mm (Parlor), 110.2 mm (Auditorium),
105.3 mm (Dreadnought) and 110.2 mm (Jumbo). The preceding build reached
12.5 mm on each body in the same cases.

Additional fixtures change the hand and body for a queued return stroke
on a ringing string, strike a natural harmonic and exercise virtual slides
at 8, 48 and 192 kHz. Strikes at extremely short digital periods check the
physical center, requested pitch and audible output. The written wave
plane is measured at periods long enough to resolve its location.

For a Pick at the most bridgeward panel setting, a 0.001-semitone slide
across open pitch changed release displacement by at most 0.0014% in the
paired test. A downward virtual slide as far as eight octaves remained
within 1.214 times the matched open stroke.

## Saved performances

Eight archived MIDI and patch pairs were played through the released 1.3.3
Rack module and the candidate at their saved quality settings. The change
in integrated loudness ranged from −1.5 to +1.9 LU. At matched 2× quality
the mean absolute difference in relative band power was 1.61 dB across
the eight pieces and six frequency bands spanning 80 Hz to 8 kHz. On the two Steel Pick parts, the
difference from the listener-preferred 1.9.50 was 0.93 dB on Jumbo and
0.62 dB on Palm Muted, down from 1.85 and 2.00 dB with 1.9.51. These
measurements describe the captured passages. Actual recall depends on the
notes, controls and mix.
