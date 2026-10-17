# Wood material response

The user reported that Wood sounded too similar between settings. The control
was reaching both front ends, the radiation bank and the reciprocal bridge.
The problem was the model's limited material response, rather than a missing
parameter connection. Parameters, enum order, saved values and layout stay
unchanged.

## Audit of the preceding model

At `26b5cef`, Spruce/Mahogany/Maple selected four authored factors for
frequency, Q, brightness and radiation. Mahogany's frequency factor was
0.991 and Maple's 1.025. Those factors did not come from a wood-property
dataset. A uniform frequency shift also moved the air and top resonances
together. Neither the bridge's modal residues nor its broadband plate
conductance accounted for material density. Overall construction loudness
compensation could then remove much of the remaining amplitude difference.

A fixed-control probe at that revision played MIDI 40, 55 and 64 separately,
Finger velocity 0.72, at every Shape. After matching RMS, Spruce/Mahogany
spectral-distance medians were 0.80 dB for stereo microphones and 0.55 dB
for Piezo. These are engineering descriptors, not perceptual thresholds.
Rack's Body 0.70/Piezo Mix 0.20 blend did not systematically erase the
difference. The source, library hashes and probe are retained in the local
`build-material-gestures-20261011/wood-audit` evidence directory.

## Material assumptions

The replacement treats Wood as a substitution of the effective vibrating
plate material at the same thickness and geometry. The reference is Spruce;
its existing measured and authored body response is retained. The other
settings use representative quarter-cut wood properties near 12% moisture:

| Setting | Representative wood | Density, kg/m³ | Bending E_L, GPa | E_R/E_L | G_LR/E_L | ν_LR |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Spruce | Sitka spruce | 448 | 10.8 | 0.078 | 0.064 | 0.372 |
| Mahogany | True/Honduras mahogany | 530 | 10.3 | 0.107 | 0.066 | 0.314 |
| Maple | Sugar/hard maple | 705.6 | 12.6 | 0.132 | 0.111 | 0.424 |

Elasticity and specific-gravity data come from the [USDA Wood Handbook,
chapter 5, Tables 5–1, 5–2, 5–3 and 5–5](https://research.fs.usda.gov/download/treesearch/62244.pdf).
Domestic specific gravity uses oven-dry mass, so density at 12% moisture is
`SG_12 × 1120`, not `SG_12 × 1000`. The [Queensland government American
mahogany guide](https://www.business.qld.gov.au/industries/farms-fishing-forestry/forests-wood/properties-timbers/american-mahogany)
supplies its representative 530 kg/m³ air-dry density. These are separate
population sources, not measurements of one board. The common approximate
bending-to-axial modulus correction cancels in the relative stiffness ratios.

Wood is orthotropic: along-grain, across-grain and shear stiffness matter.
The equivalent bending stiffness is computed from the angular average of
inverse square-root directional stiffness. This preserves the modal-density
scaling of an orthotropic plate; it is not the shape of every individual
guitar mode. See the [orthotropic plate experiment and equations](https://pmc.ncbi.nlm.nih.gov/articles/PMC5554064/)
and [plate modal-density derivation](https://euphonics.org/3-2-4-the-modal-density-of-a-vibrating-plate/).

For relative stiffness `k` and mass `m`, plate frequency scales with
`sqrt(k/m)`. The cavity-spring frequency acting on the plate scales with `1/sqrt(m)`;
the rigid-box Helmholtz frequency remains fixed. Solving the existing
coupled air/top system gives each low resonance its own frequency and
participation. The same poles drive radiation and reciprocal bridge motion.
Higher plate bridge residues scale with `1/m`, and the dense-mode bridge
conductance with `1/sqrt(k*m)`. This lets the material affect how the strings
lose energy through the bridge as well as the microphone spectrum.

No species-specific damping measurement is available for these bodies, so
the measured/reference Q is retained. The old species brightness and
radiation multipliers are removed. Existing construction calibration adjusts
overall level only; it does not flatten material-specific resonances.

## Interpretation and limits

Similar frequencies do not imply similar materials: density and stiffness
can rise together. These representative properties predict higher plate
frequency factors close to one, while mass differs much more. The intended
contrast is in air/top balance, resonance strength and string decay, rather
than an imposed bright/dark EQ.

The low top mode uses the effective stiffness as a reduced-model
approximation. Individual modal grain directions, separate bracing and
bridge mass, back/side participation, moisture history and board-specific
damping are not identified by the available recordings. A real instrument
may also use different plate thickness or bracing for a different wood.
Consequently this is a physically constrained material substitution, not a
claim to identify an exact species from sound. [Wood properties and their
variability](https://euphonics.org/10-3-wood-properties-and-measurement/) explain
why large universal tone claims would be misleading.

## Review of the user-supplied articles

The [Arundel article](https://arundelguitars.com/insights/how-wood-affects-sound-the-science-of-guitar-tone)
describes traditional tone associations but provides no experimental
citations or material dataset. Density alone does not establish how much
high-frequency energy a material dissipates; stiffness, loss and the
coupled structure must be considered separately.

The [Guitar Earo article](https://guitarearo.substack.com/p/maple-vs-mahogany-the-study-that)
describes a purported 2021 solid-body Maple/Mahogany comparison without
identifying authors, title or a primary source. Its specific presence-band,
sustain and blind-test numbers could not be verified and are not used to
fit Acustra. The actual [Jasiński et al. 2021 experiment](https://journals.pan.pl/Content/121810/PDF/aoa.2021.138150.pdf)
used simplified single-string structures of sapele, rosewood, pine and
plywood. It found pitch-dependent spectral differences and successful listening discrimination, while warning
that its simplified structure could exaggerate the effect. It was not a
two-guitar Maple/Mahogany test. [Phillip Self's Maple/Mahogany thesis](https://faculty.tamuc.edu/cbertulani/cab/papers/SelfThesis.pdf)
found that brightness differences depended on the string; it did not
measure sustain. Neither supplies the missing article's claimed result.

More directly relevant to this implementation, [Merchel, Altinsoy and
Olson, 2019](https://tu-dresden.de/ing/elektrotechnik/ias/aha/ressourcen/dateien/professur/publikationen/Merchel2019jasa-Guitar_Quality.pdf)
studied nine otherwise closely controlled steel-string guitars with varying
Sitka-spruce soundboard/brace density and stiffness. Listeners distinguished them, and
preferences depended on material properties within that particular design.
This supports modelling both mass and stiffness; it does not provide a
universal Maple EQ or a damping coefficient for these three settings.

By comparison, [Carcagno et al., 2018](https://pubmed.ncbi.nlm.nih.gov/30599660/)
found a marginal effect from changing the back and sides of six otherwise
similar acoustic guitars. That result concerns a different component.
Acustra's effective vibrating-plate substitution must not be described as a
simulation of that back/side-only experiment.

## Validation

`Acustra.WoodMaterial` passes on the final source as part of **80/80 passing
DSP suites**. Independent 8192-interval Simpson integration checks the
orthotropic scaling; symmetric spectral projectors independently check the
coupled pole and residue changes. The largest relative error across these
identities is 4.23e-7. Actual bridge conductance remains positive and body
poles remain inside the unit circle across the tested constructions and
rates. Removing only material scaling from the dense bridge conductance
in an off-tree negative control fails the intended assertion (relative
error 0.601), while the other modal identities still pass.

Before construction-level recalibration, all twelve Spruce fixed-note
renders across four Shapes were sample-exact against `26b5cef`, as were
their modal/transfer rows. This proves preservation of the Spruce physical
reference with equal gains. Final delivered Spruce levels also include
recalibration: stereo gains move 0 to +0.446 dB, mono -0.064 to +0.493 dB,
and Piezo +0.182 to +0.649 dB across Shape/technique cells. The default
microphone reference remains exact; default Piezo moves +0.320 dB.

The fixed-control probe retains twelve same-Shape/note comparisons for each
wood pair. Median power-weighted spectral distances after matching RMS are:

| Pair / capture | Previous model | Final model |
| --- | ---: | ---: |
| Spruce / Mahogany, stereo | 0.803 dB | 1.625 dB |
| Spruce / Mahogany, Piezo | 0.547 dB | 1.166 dB |
| Spruce / Maple, stereo | 1.651 dB | 1.783 dB |
| Spruce / Maple, Piezo | 0.933 dB | 1.728 dB |

These are descriptive spectral differences, not audibility thresholds or
preference scores. The coherent Rack microphone/piezo blend also retains
material contrast; its final medians are 2.173 dB for Mahogany and 2.267 dB
for Maple. Its small change under recalibration reflects the altered balance
of the two capture paths. The [source and descriptor evidence](../validation/material-gestures-2026-10-11/wood-descriptors.json)
and [before/after comparison](../validation/material-gestures-2026-10-11/wood-comparison.json)
record the full protocol.

The [Wood listening page](http://127.0.0.1:8771/wood/) compares the same
performance through three woods and three captures, with native and
RMS-matched playback. The before/after includes construction-level
recalibration; it is not a pure mechanism ablation. Fixed-gain Spruce parity
is documented separately. [The full candidate report](material-gestures-realism-2026-10-11.md)
records Rack validation and the stronger physical piezo saturation at
maximum-force bridge extremes. Those cases are included in the listening
review rather than inferred inaudible from finite output.
