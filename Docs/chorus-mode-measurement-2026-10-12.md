# Acustra physical mode measurements, 2026-10-12

The chorus repair preserves one speaking delay for both string polarisations
and calibrates their sustained pitch together. This report distinguishes real
modal detuning from the displaced Fourier maximum of an unresolved mode pair.
It supports the sound repair; it makes no CPU speedup claim.

[ModalTuningTests](../Tests/ModalTuningTests.cpp) independently reconstructs the
configured digital bridge and all twelve live string-loop transfers, including
the Thiran read, intrinsic loss and dispersion. It solves the coupled system's
damped poles without calling the production tuning helper. Twelve bounded
complex-root starts per fixture, root residuals below `1e-8`, and expected mode
coverage guard against missed or spurious roots. Radiation weights use the
physical pluck direction, force/moment microphone residues, stereo width and
the actual reference-period force derivative at each sample rate. Both the
three-second energy centre and dominant radiating mode must remain within two
cents. Both planes must also retain equal target and current speaking delays.

All fifteen checks passed at 44.1, 48 and 96 kHz. The maximum centre error was
1.53401 cents; the bounds were unchanged when 44.1 kHz coverage was added.

| Fixture | 44.1 kHz centre | 48 kHz centre | 96 kHz centre |
| --- | ---: | ---: | ---: |
| Dreadnought open G | +0.481 cents | +0.480 cents | +0.474 cents |
| Dreadnought low-E fret 12 | +1.534 cents | +1.532 cents | +1.506 cents |
| Dreadnought A-string fret 5 | -1.160 cents | -1.161 cents | -1.161 cents |
| Jumbo open low E | +0.222 cents | +0.224 cents | +0.231 cents |
| Dreadnought D-string fret 2, velocity 0.72 | -0.008 cents | -0.008 cents | -0.006 cents |

The other fixtures use Finger at velocity 0.6. Room and release noise are zero;
sympathetic strings remain connected. The five cases cover two selected body
shapes, not every construction or fret. Radiation weights approximate source
projection using the sustained pluck direction; they do not project every
initial travelling-wave state. Pole coordinates describe the configured
linearized dynamics. Faster body-induced transient modes remain present.

The frozen pre-polish `8ea51f9` control fails this physical guard: low-E fret-12
E3 has a true sustained centre near +10.829 cents, Jumbo open low E near +9.471
cents, and open G near -2.588 cents. The current candidate passes using the
same limits. The separate chorus waveform guard rejects the initial
independent-delay `1d1f432` change and verifies coherent upper partials, so pitch
accuracy and audible beating are checked independently.

Short-window negative controls establish why selected Fourier-maximum guards
were replaced. Old open G has poles at -3.334 and +0.199 cents, only 0.40 Hz
apart; its 1.1-second Hann maximum reads +4.566 cents, outside both. A fit of
those damped modes reconstructs +4.56397 cents with 0.0462% waveform residual.
Old A-string D3 similarly reads -9.74526 cents outside its physical pole family;
the modal reconstruction reads -9.74505 cents with 0.0222% residual.

The exact mono-average observer used by the Engine test supplies another
negative control. D-string E3 at velocity 0.72 reads +10.924547 cents over
0.82–1.32 seconds. Its poles are -9.523, -0.120, +0.553 and +1.234 cents;
their reconstruction reads +10.922089 cents with 0.04048% residual. The fast
lower member decays at 3.623/s, and the three-second centre is about -0.008
cents. Unresolved modal cancellation explains the displaced maximum without
requiring a pole at +10.925 cents. Other established pitch bounds remain in
place; this proof does not widen a physical tuning limit.

Frozen matching engine/header snapshots, dependencies, compiler commands,
recordings, positive and historical negative logs, reconstruction tools and
SHA-256 receipts are retained in the Rack project's
`Output/validation/chorus-investigation-20261012/math-oracle/receipt-final.json`.
The original contact-direction weighting receipt is preserved as historical
evidence; the final receipt uses the sustained pluck split and actual observer.
The final cache review found no remaining reset, preparation, copying or
retuning blocker. Complete authoritative open-string inventory and exact
nominal cache inputs prevent dependence on mutable sounding-voice history.
