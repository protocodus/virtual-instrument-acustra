# Third-party notices

Acustra's original source code is covered by the project's `LICENSE` file.
The separately licensed framework, recordings and adapted measurement data
below are not relicensed by Acustra.

## Tárrega scores for the repertoire demonstrations

Works: *Recuerdos de la Alhambra* and *Lágrima* by Francisco Tárrega
(1852–1909)

Licence: public domain. Tárrega died in 1909, so every copyright term
measured from the author's death expired in 1979 at the latest.

`Tools/GenerateRepertoireScores.py` reads two Mutopia Project MIDI files —
[`recuerdos.mid`](https://www.mutopiaproject.org/ftp/TarregaF/recuerdos/recuerdos.mid)
(md5 `b91ef372bc2f64e383be1a539f033f62`) and
[`lagrima-duo.mid`](https://www.mutopiaproject.org/ftp/TarregaF/lagrima-duo/lagrima-duo.mid)
(md5 `0b20164983fe93c99a0afc689d55f86b`) — and takes from them only Tárrega's
composition: which pitch sounds when, and for how long. Mutopia declares the
Lágrima file public domain; the Recuerdos typesetting carries CC BY-SA 3.0,
which covers that edition's own engraving. No engraving, fingering, barre
indication or editorial marking is read or reproduced, and none appears in
`Tools/RepertoireScores.h`, which holds pitch, onset and length alone. The
velocities in that header are an authored performance written by this project,
not data from either file.

## Shinyguitar microphone recordings (steel bank)

Work: *Shinyguitar* archtop-guitar sample library

Performer and mapping: D. Smolken / Karoryfer Lecolds

The upstream documentation does not name the string alloy. Acustra classifies
the corpus as its steel bank because the library includes simultaneous magnetic-
pickup captures, which require ferromagnetic strings; this is an inference, not
an upstream material claim.

Source: <https://github.com/sfzinstruments/karoryfer.shinyguitar>

Pinned source commit: `57243cca85277dbcc120ce17c6178032f93c80f3`

Licence: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)

The offline reference target embeds all 272 acoustic-microphone sustain WAVs
referenced by `Programs/acoustic.sfz` over MIDI 38–84: 17 roots, four velocity
layers and four round robins. Full native durations and captured relative
levels are kept; generation converts the 24-bit mono WAVs to PCM16 and adds a
60 ms terminal half-cosine de-click fade. The deterministic aggregate SHA-256 is
`4526e15b6b242dd68481c48eacae9409c09bc3bebec1da39cf66070f36a4ef7c`;
its path-and-file-byte method and inclusion rule are documented in
`ThirdParty/Shinyguitar-README.txt` and `Assets/SampleBank/manifest.json`.

## Eastman E1D steel-string recordings

Work: first-party Eastman E1D dreadnought picked and finger-plucked recordings,
recorded 2026-07-23

Creator/performer: Arthur, owner of the `ferrosintesis` source repository

Source introduction commit:
<https://github.com/0x4D44/ferrosintesis/tree/810318c92e33e31b36638b0ffa7ffc834a2ae6a2/samples/acoustic-guitar-eastman-e1d>

Generation was checked from repository revision
`94edbcfef226986d6ac28330020bc301fa5207d9`; both Opus-copy hashes match the
introduction commit.

Picked Opus source copy (`picked.opus`, 5,957,761 bytes) SHA-256:
`35e9e45b42a70f2fada2c9d93bf809d9046562fa3fa40cbac415ef75b92d926d`

Finger-plucked Opus source copy (`plucked.opus`, 5,945,086 bytes) SHA-256:
`16f5a8c7cde555441143243f264a03eff142cb8aafb6924236a82755a0396ce0`

Licence: [CC0 1.0 Universal](https://creativecommons.org/publicdomain/zero/1.0/)

The offline reference target embeds eight pitch anchors from the finger-plucked
performance as a reproducible evaluation corpus. They are not linked or
selected by the public engine:
switching to this sparse, unmatched recording session at the midpoint of Touch
caused an unacceptable level and spectral discontinuity. The stereo Opus source
copy is decoded at 48 kHz, sliced to three seconds beginning 20 ms before its
pinned onset, and given a 60 ms terminal half-cosine fade. Acustra uses
late-sustain fundamental fits for playback tuning; that metadata change does
not alter the packed PCM. The local upstream source note is distributed as
`ThirdParty/Eastman-E1D-README.md`.

`Assets/SampleBank/manifest.json` records every reference region's mapping,
decoded hash, packed offset, onset, fade and source provenance. The separate
reference payload is transformed audio from these CC0 works; it is not
Acustra-authored source audio. The VST3, Audio Unit and standalone binaries
contain no part of this payload.

CC0 imposes no attribution or share-alike condition; this notice is retained
for provenance and change disclosure. CC0 covers only rights each affirmer had
authority to waive, supplies the material without warranty, and does not grant
trademark or patent rights. “Eastman E1D” identifies the recorded instrument
and implies no manufacturer affiliation or endorsement.

## Robert Mores guitar-measurement archive

Creator: Robert Mores, Hamburg University of Applied Sciences

Title: *Archive for the acoustical documentation of classical Spanish
guitars, flamenco guitars and romantic guitars from private and public
collections – bridge mobility* (2021)

Source: <https://doi.org/10.5281/zenodo.4604577>

Licence: [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/)

`Source/DSP/MeasuredBodyData.h` is adapted from the archive's
`qualified_selected_impulses.mat` (MD5
`733cb10baf5ce36d8bf333610ffbb260`). Acustra selects g21, the archive's 2018
Lester DeVoe flamenca blanca with spruce top and cypress back and sides. It
uses the guitar's first and third one-second segments,
containing bass-side and treble-side normal bridge impacts. After applying the
archive's full-record half-cosine taper and SI calibration scales, it forms
complex H1 force-to-pressure responses to the upper, treble-side and bass-side
microphones, each 10 cm above the top plate. Their common time origin and
relative phase are retained.

The archive's physical-measures table identifies g21's strings as Savarez
Tomatito, with nylon/KF trebles and wound multifilament basses. Acustra adapts
this flamenco measurement for its steel strings; it is not a measured
steel-strung body. Both `MeasuredBodyData.h` and
`MeasuredBridgeData.h` derive from these records; the bridge adaptation fits
positive-semidefinite heave/rocking residues to the calibrated bass/treble
acceleration-to-force measurements.

For each g21 microphone/impact path, Acustra inverse-transforms the H1 response,
retains 3000 samples, leaves the first 2700 unchanged and applies an authored
300-sample raised-cosine fade. These common causal windows preserve inter-path phase; no independent
minimum-phase reconstruction or delay alignment is applied.

Acustra jointly selects shared frequencies and Q values over 80 Hz–10 kHz
from all six paths and fits regularised complex endpoint residues.
The smallest passing prominence-ordered prefix has 127 poles for g21, with frequencies, Q values and residue components rounded to float32.
For bass/treble forces `Fb`/`Ft`, the transformed inputs are `F = Fb + Ft` and
`T = Ft - Fb = M/a`, where `a` is the assumed impact half-spacing. The pressure
paths are `(Ht + Hb)/2` and `(Ht - Hb)/2`, respectively. Separate force/moment
states drive all three microphone outputs. This basis reconstructs the two
measured inputs; extending it to arbitrary strings assumes a rigid saddle and
does not identify a horizontal-force response.

Apart from the plate-mode Q below, no corpus averaging, FIR remainder, per-path
peak/RMS normalisation or authored
left/right gain spread is used. The causal fades, modal reduction, force-pair
basis conversion, global gain and shape/material transformations are Acustra
changes; the selected body calibration has neutral frequency/Q scales, residue
tilt and low-mode gain. Runtime output has no RMS matching and is not calibrated
to absolute sound pressure. No DAFx-26 coefficient is used in this body model.

Since 2026-09-27 the g21 bank's plate modes carry the damping of five flamenca
blancas the same archive measured anechoically, g37, g38, g39, g42 and g43,
each fitted by the same generator and gates at its own converged window (g41
fails them): each g21 mode's Q is scaled by the population's median Q over
g21's own median in the octave round it, and never raised
(`Tools/GenerateBodyForcePair.py --plate-q median`). Until 2026-09-27 the rule
covered 300 Hz-10 kHz; since 2026-09-28 it reaches down to 150 Hz, so T1 (178.5
Hz), the 208.7 Hz rocking mode, the 229.0 Hz mode and the 287 Hz broad mode are
damped by it too. The air group below 150 Hz and every frequency and residue
are as fitted from g21. This Q scaling is an Acustra change to the archive's
data.

Since 2026-09-28 steel's own bridge, the g21 bridge bank, plays on its
radiation bank's poles (since 2026-09-29 it is the Original model's only
bridge):
`MeasuredBridgeData.h` records, for each of its 47 modes, the radiation mode
that is the same resonance (18 of them: the nearest, inside that mode's
as-fitted half-power band, the bridge mode narrower than the radiation bank's
local spacing), and the engine gives such a mode that radiation mode's
frequency and Q after the anchor, Shape and Wood transforms and the plate-Q
rule above; the other modes keep their measured frequency under the same
transforms and take the plate-Q rule's octave factor on their Q, inside the
same 150 Hz-10 kHz band. Every bridge residue is scaled by 0.274
(`steelTopMobilityRatio`), the Fylde Falstaff's measured mobility over g21's
(below), so the flamenca's bridge
drains at a steel-string guitar's level. These are Acustra changes to the
archive's data (`Tools/GenerateMeasuredBridge.py`).

In the steel blend (`Source/DSP/SteelBodyBlend.h`) two further
transformations of the same archive ship. (1) The four g21 modes between 150
and 300 Hz take their Q 0.85 of the way, in log Q, from the as-fitted value
to the plate-Q rule's (`GenerateBodyForcePair.py --plate-q-t1-weight`). (2) A
jointly fitted body, `MeasuredJointBodyData.h` (`Tools/GenerateJointBody.py`):
131 frequency/Q pairs fitted jointly to g21's six microphone paths and its
four accelerometer paths (velocity/force over the whole record, the hammer
differentiated, a 2-sample alignment), refined by variable projection below
the bridge's 2245 Hz cross-side corner, 56 modes carrying
positive-semidefinite heave/rocking mobility residues, the plate-Q rule from
300 Hz on the common Q, and the mobility scaled by 0.32275
(`steelJointTopMobilityRatio`), the RMS ratio of
the Fylde Falstaff's measured mobility (Carcagno et al., below) to g21's at
the bass impact over 80 Hz-4 kHz. The header carries all 131 modes; the
engine plays the 9 below 550 Hz, radiation and bridge, at 0.075 in parallel
with g21's modes, which play at 0.925 below 550 Hz and at 0.985 (radiation)
and 1 (bridge) above it. The weights, the band, these fits and the level
ratio are Acustra changes to the archive's data.

Acustra ships only those transformed numerical coefficients. It does not ship
the source MAT file, recorded impulses, photographs or documentation from the
archive. The attribution, source link, licence link and description of changes
above must accompany distributions containing the coefficient table.

### Additional Bellido body

The Bellido 1978 Model uses Mores g35, a 1978 Manuel Lopez Bellido classical
guitar (cedar/Rio palisander), under the same CC BY 4.0 archive license above;
Acustra plays it with steel strings. `Source/DSP/BellidoData.h`
(`Tools/GenerateBellidoBody.py`) contains 134 fitted force/moment radiation
modes and 50 positive-semidefinite bridge modes; the modifications include
calibrated H1 extraction, causal tapering and bounded frequency/Q/residue
fitting. See [`Docs/body-models-2026-09-08.md`](Docs/body-models-2026-09-08.md)
and the linked fit reports.

The archive's other records are no longer shipped: g34 (a 1971 Manuel
Contreras classical, which the nylon strings played until 2026-09-29) and the
unused g36 candidate were removed with the nylon model. g37, g38, g39, g42 and
g43 contribute only the plate-Q statistic above; none of their coefficients
ship.

## Mark Rau guitar measurements: not distributed

Creator: Mark Rau. Source:
<https://rau.mit.edu/projects/GuitarMeasurements/>.

No explicit reuse license was supplied for the separately downloaded raw ZIP;
the paper's license is not represented as a license for that dataset. Neither
the raw records nor anything fitted from them is included in this repository:
the 1897 Washburn parlor, 2022 Santa Cruz OM3 and 2007 Martin D18V models once
built from them are retired. `Tools/GenerateRauGuitarCandidates.py` fits them
locally from the source ZIP, and `.gitignore` keeps its header and report out
of commits.

## Measured Fylde steel-string bridge: derived constants only

Samuele Carcagno, Roger Bucknall, Jim Woodhouse, Claudia Fritz and Christopher
J. Plack, *Effect of back wood choice on the perceived quality of steel-string
acoustic guitars*, JASA 144(6), 3533–3547 (2018),
<https://doi.org/10.1121/1.5084735>.

Source data: <https://osf.io/f4pqa/>,
`guitar_back_wood_code_data_v1.0.1.zip`,
SHA-256 `1d35dd28ece660eadc165be34995255fae6f56c9cb3d8d6d96bd00fe2902e582`.

Licence: [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
The dataset's licence is supplied in its `LICENCE.txt` and OSF record.

Acustra uses the first `specSet` column in `bridge_admittance_all.mat`: the
commissioned Fylde Falstaff with Sitka spruce top, Brazilian rosewood back
and sides, ebony bridge and Elixir Nanoweb Light 80/20 Bronze steel strings.
The manufacture year is unspecified. The measurement is normal bridge
velocity/force between the fifth and sixth strings, with strings damped.

No bank of this measurement ships. Until 2026-09-29 Acustra shipped a 44-mode
modal fit of it (`MeasuredSteelBridgeData.h`) as an optional bridge; that
choice and its coefficients were removed. The measurement still underlies
three derived constants, fitted by `Tools/FyldeBridgeReference.py` (which
selects measured peaks, pins the three lowest body frequencies and Q values
to Table I, infers a polarity and phase alignment from a constrained fit, and
fits nonnegative scalar residues while retaining measured SI magnitude):
`steelTopMobilityRatio` in `MeasuredBridgeData.h`, the geometric mean over
80 Hz–4 kHz of the fit's |Y| over the Mores g21 bridge bank's at the matching
(bass-side) position, 0.274, which scales the flamenca's bridge to a
steel-string guitar's level; `steelJointTopMobilityRatio` in
`MeasuredJointBodyData.h`, the corresponding RMS ratio for the jointly fitted
body, 0.32275; and the piezo's bridge conductance (`PiezoDesign` in
`AcustraEngine.h`), below. The source MAT, recorded/synthesized audio and
experimental participant data are not distributed. This attribution, licence
link, source link and description of changes must accompany distributions of
these derived values.

## DAFx-26 nonlinear modal synthesis paper: no licensed material

Michele Ducceschi, Riccardo Russo and Craig J. Webb, *Measurement-Informed
Nonlinear Modal Synthesis of 65 Classical Guitars*, Proceedings of the 29th
International Conference on Digital Audio Effects (DAFx-26), 2026,
<https://dafx26.mit.edu/assets/papers/DAFx26_paper_40.pdf>, CC BY 4.0.

Until 2026-09-29 `Source/DSP/AcustraEngine.cpp` reproduced the paper's Table 1
(the EJ45 nylon strings' diameters, effective densities and Young's moduli)
for Acustra's nylon strings. That table was removed with the nylon strings,
and no coefficient table, figure, text or audio from the paper is included
now. The engine still cites the paper for methods and single published
values: its fixed-end attachment (the 3.25 mm stub that its xi_b = 0.995
leaves on a 650 mm scale) and its longitudinal tension-increase expression.
The paper's measurement-to-modal procedure also informs the Mores-data
adaptation described above.

## Published figures behind the piezo chain: no licensed material

The under-saddle piezo chain (`AcustraEngine::PiezoDesign` in
`Source/DSP/AcustraEngine.h`, `Tools/PiezoReference.py`, Docs/decisions.md
2026-09-29) takes individual published values: part values, datasheet
limits and single measured figures. None comes from a dataset, table or file
under a licence, and no figure, schematic, table, text or recording from any
of these sources is reproduced or distributed; each value is cited beside it
in the source.

- Rod Elliott, Elliott Sound Products, "Project 202 - Piezo Preamps", Fig. 1,
  <https://sound-au.com/project202.htm>: the preamp circuit's topology and
  its resistor and capacitor values, read off the published figure. The
  circuit is modelled from those values; the figure itself is not copied.
- Texas Instruments, OPAx134 datasheet (SBOS058B),
  <https://www.ti.com/lit/ds/symlink/opa2134.pdf>: the OPA2134's open-loop
  gain, gain-bandwidth, slew rate, input capacitance, bias current, input
  common-mode range and output swing.
- The 1N4148's saturation current and emission coefficient (2.52 nA, 1.752)
  and 4 pF junction capacitance, from the widely reposted SPICE model.
- Manfred Zollner, *Physics of the Electric Guitar* (2005), chapter 6,
  <https://www.gitec-forum-eng.de/wp-content/uploads/2019/03/poteg-6-piezo-pickups.pdf>:
  a bridge piezo's sensitivity of about 0.2 V/N at 1.45 nF (the Ovation
  EA-68), its 1-2 V output when played loudly (section 6.7), which bounds
  `Tools/CalibratePiezo.py`'s check, and the measurement rig's Q of 18, taken
  as the bound on the element's loss. Figs 6.24/6.25 are cited as the reason
  loudness, not RMS, is matched; nothing is read off them.
- Radial Engineering, PZ-DI product page: the DI's 1 MOhm input setting.
- Mogami 2524 cable's capacitance (130 pF/m), from the manufacturer's
  published specification.
- Martin Keith, "Troubleshooting imbalance between strings on common acoustic
  guitar pickup systems", *Acoustic Guitar*, and US patent 6,822,156 B1: the
  string-to-string imbalance of an installed under-saddle pickup, which bounds
  the per-string weights (their pattern is this project's own choice).
- F. Esqueda, S. Bilbao and V. Valimaki, "Aliasing reduction in clipped
  signals", IEEE Transactions on Signal Processing 64(20), 2016: the
  band-limited ramp (BLAMP) method the clip's corners are corrected with. The engine's residual is its
  own Kaiser-windowed sinc, tabulated by `Tools/PiezoReference.py`; no code
  or table from the paper is included.

The bridge conductance under the saddle (1.59e-3 s/kg) is the mean of Re(Y)
over 5-7 kHz of the measured Fylde mobility (see its entry above); no new
data is added for it.

## JUCE 8.0.14

JUCE is Copyright (c) Raw Material Software Limited.

Repository: <https://github.com/juce-framework/JUCE/tree/8.0.14>

A copy of the JUCE licence text is vendored at
[`ThirdParty/JUCE-LICENSE.md`](ThirdParty/JUCE-LICENSE.md) so it can accompany
every distributed plug-in bundle, application and installer package.

JUCE framework modules are dual-licensed under the GNU Affero General Public
License version 3 (AGPLv3) and the commercial JUCE licence. Building or
distributing Acustra with JUCE therefore requires either compliance with the
AGPLv3 for the complete combined work or an appropriate commercial JUCE
licence. Review the JUCE 8 licence terms before distribution:
<https://github.com/juce-framework/JUCE/blob/8.0.14/LICENSE.md>

JUCE includes or interfaces with additional third-party components. Their
copyright notices and licence terms are listed in JUCE's own `LICENSE.md` and
source tree. The VST3 SDK portions used through JUCE are identified there as
MIT-licensed; Apple's Audio Unit frameworks are supplied by the macOS SDK and
remain subject to Apple's terms.

Acustra's source tree and offline reference tools include the two CC0
recording families described above. The distributed plug-in and standalone
binaries include none of their recorded audio, and include no convolution
impulse response, neural model or added room/reverb capture.
