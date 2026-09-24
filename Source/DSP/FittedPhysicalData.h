// Acustra: bounded offline-fit parameters for the physical model.

#pragma once

namespace acustra
{

struct MaterialCalibration
{
    // Nylon's bending stiffness comes from Woodhouse's measured per-string
    // EI table (nylonBendingEI in AcustraEngine.cpp), not a fitted scale on
    // a diameter-derived value, so nylon.stiffnessScale is inert: it stays
    // 1.0 and is not part of the calibration array. Steel still uses it.
    float stiffnessScale;
    float fundamentalT60Scale;
    float frequencyLossScale;
    float apertureScale;
    float transientScale;
    float pluckDistanceScale;
    float velocityBrightnessDepth;
};

struct PhysicalCalibration
{
    float bodyFrequencyScale;
    float bodyQScale;
    float bridgeMobilityScale;
    float residueTiltDbPerOctave;
    float directGain;
    MaterialCalibration nylon;
    MaterialCalibration steel;
    float apertureRegisterExponent { 1.0f };
    float lowBodyModeGain { 1.0f };
    float steelDisplacementScaleMetres { 0.0061f };
    float steelFretT60Slope { -0.030f };
    float highLossCutoffScale { 1.30f };
    // Above its modal-overlap frequency a plate's driving-point mobility tends
    // to a real constant (Cremer and Heckl, Structure-Borne Sound). A finite
    // positive-real modal fit cannot represent that dense overlap, so its
    // conductance collapses in the top of the band; this bounded real term
    // restores the floor. Being a positive real admittance it cannot make the
    // junction active.
    float bridgeConductanceFloor { 0.0f };
    float bridgeConductanceCornerHz { 1000.0f };
    // Length of string between the saddle and its anchor, which sets the
    // stiffness T/L of the spring each string presents to the bridge node.
    // DAFx-26 attaches the body at xi_b=0.995 and leaves whatever stub that
    // fraction implies; a real bridge anchors the string at a fixed distance
    // behind the saddle instead - roughly 12-16 mm at a steel-string's pins,
    // further at a classical's tie block - and the distance is not part of
    // the g21 measurement, so it is bounded and fitted rather than assumed.
    // Re-swept on the classical bridge rather than inherited from g21, over
    // E, G, Am, D and C chords. Worst separable note's pull early/late in
    // cents: 1.4/3.8 at the 3.25 mm stub, 2.1/2.4 at 8 mm, 2.9/3.2 at
    // 17.2 mm, 25.8/98.2 with no anchor at all. The coincident pairs of the
    // same sweep - a played fundamental landing on a lower played note's
    // partial, where the tracker reads the beat between them and not a pull -
    // run the other way: 58.1/12.4 at 3.25 mm, 28.2/10.2 at 8 mm, 14.1/26.7
    // at 17.2 mm. So the sweep settles that an anchor is needed and not which
    // length: 3.25 mm loses the late window to 8 mm and loses the coincident
    // column outright. It shipped at 3.25 mm - the bound's floor and DAFx-26's
    // own attachment, xi_b = 0.995 on a 650 mm scale, the only published
    // dimension among the candidates - until the 2026-09-04 refit, which took
    // it to 59.1 mm against a 60 mm ceiling and improved the tuning term on
    // all three splits (3.459 -> 2.869 training, 3.378 -> 3.304 development
    // validation, 2.429 -> 1.717 flat top). That is longer than the 12-16 mm
    // a steel string's pins give and longer than any candidate the chord
    // sweep covered, so the two measurements now disagree about this value:
    // the corpus wants a softer termination than the pull sweep does, and
    // nothing here has measured the pull at 59 mm.
    float bridgeTailLengthMetres { 0.020f };
    // Transverse motion stretches the string; DAFx-26's tension increase
    // EA/(2L) times the mean square slope is a force at the saddle, and it
    // resonates at the string's own longitudinal modes, n*c_long/(2L) with
    // c_long = sqrt(EA/mu). For this steel set that is 1.5 to 3.9 kHz and for
    // plain nylon 1.18 kHz, both from the construction data the transverse
    // model already uses. Because the drive is a squared slope it carries the
    // products of transverse partials, so what the resonators pass are the
    // sum and difference phantom partials rather than an added tone. Zero is
    // an exact no-op.
    float longitudinalGain { 0.0f };
    float longitudinalQ { 80.0f };
    // The two transverse polarisations of a real string are not in tune with
    // each other, and Woodhouse, "Plucked guitar transients: comparison of
    // measurements and synthesis", Acta Acustica 90 (2004) 945-965, Sec. 4.3
    // (https://euphonics.org/wp-content/uploads/2022/03/Guitar_II.pdf) shows
    // where the split comes from: not from the body, whose measured 2x2
    // admittance matrix splits the pair by only about 0.1 Hz, but from an end
    // correction at the terminations, the string rolling on the fret crown
    // and possibly the saddle. He measures the polarisation parallel to the
    // soundboard as the longer one - so the normal polarisation is the higher
    // member of the pair - by "about 0.8 mm in 650 mm" on the B string, and
    // calls that "more significant ... than that coming from the body
    // admittance matrix". This is that length, as a length: it is a total
    // over both terminations for an open string, not a per-termination
    // figure, and he publishes no law for how it varies from string to
    // string, so the same length goes to every string. It is bounded and
    // fittable rather than fixed because he calls the attribution tentative
    // and says the exact amount "would require detailed computation"; the
    // bound is one string diameter - his own remark that the correction is of
    // the order of the string diameter - taken as the 0.82 mm B string the
    // 0.8 mm was measured on (nylonDiameterMetres in AcustraEngine.cpp).
    float polarisationEndCorrectionMetres { 0.0008f };
    // The Pick technique only; Finger and Thumb never read these. A string
    // does not leave a plectrum's tip from rest: the contact region is
    // carried at the tip's speed until it slips, so the release carries a
    // velocity over the contact width beside the displacement. Its kinetic
    // energy, as a share of the pluck's stored energy, is
    //     share(v) = pickReleaseVelocityShare * v^pickReleaseVelocityExponent
    // for MIDI velocity v in 0-1, both fitted on the picked archtop rows
    // across their four velocity layers (FitPhysicalModel's harmonics term
    // reads the loud layer's H7-H12 4-6 dB under the recordings and its
    // H1-H3 5-9 dB over them, and the soft layer the other way; a velocity
    // component's partials fall 6 dB/octave slower than a displacement's,
    // which is the tilt that grows). Zero is the exact legacy pluck. The
    // plectrum's contact transient is an impact and grows with the pick's
    // speed squared, not with the note it starts; pickTransientGain scales
    // that broadband burst, and zero keeps the Finger burst law.
    float pickReleaseVelocityShare { 0.0f };
    float pickReleaseVelocityExponent { 2.0f };
    float pickTransientGain { 0.0f };
};

// Refit on 2026-09-04 around the two-way junction and the saddle anchor, by a
// bounded pattern search rather than the one-sided Gauss-Newton stages that
// produced the previous vector (Tools/OptimizePhysicalModel.py says why the
// derivative was not trustworthy). Training 6.319236 -> 5.662377, development
// validation 6.327235 -> 5.893771, the eight flat-top rows 7.948337 ->
// 6.913712: all three splits improve, which is what the 2026-08-31 refit
// could not do. Five of the twenty-six free values sit on a bound - bodyQScale
// 0.0534 over a 0.05 floor, bridgeMobilityScale on 0.25, nylon's transient
// scale on 0, the fret T60 slope on -0.06 and the saddle-to-anchor length at
// 59.1 mm under a 60 mm ceiling - so on those five the bound is choosing the
// value and not the data.
// The 2026-09-04 refit, with two values pinned back to their measurements.
// A bounded pattern search over the 115-note benchmark moved every free value
// and improved all four splits, but it drove two onto bounds that contradict
// what the repository measured, and both are restored here:
//   * bridgeTailLengthMetres. The fit ran it to 59.1 mm against a 60 mm
//     ceiling, four times longer than any length the chord-pull sweep covered.
//     At 59.1 mm nylon's late chord pull reaches 8.7 cents against the 7-cent
//     bound the tuning work was promoted on; at DAFx-26's published 3.25 mm
//     stub it is 0.8, and held-out validation is BETTER pinned (5.731) than
//     free (5.894), so the long tail was fitting the training rows.
//   * bridgeMobilityScale. The fit ran it to its 0.25 floor, scaling a
//     measured mobility four times down, and that rail is what made a nylon
//     hammer-on quieter as it got harder (-0.35 dB per velocity step at all
//     three rates). Restored to the measured 0.754677154, the engine suite is
//     green with every bound at its former value except the two below.
// What the fit did move, and what it bought: training 6.319236 -> 6.111540,
// development validation 6.327235 -> 6.072344, and the eight never-fitted
// flat-top rows 7.948337 -> 8.073304 (1.6% worse, the one split that loses).
// 2026-09-05: the listener selected the phase-preserving force/moment body's
// stereo presentation (Docs/decisions.md). Its measured-response fit uses
// neutral body frequency/Q scales, residue tilt and low-mode gain; carrying
// the old minimum-phase calibration over suppressed upper bands by 18-28 dB.
// Keep the remaining string/bridge calibration above. These four neutral
// factors are the auditioned model, not a new fit or an absolute-SPL claim.
// 2026-09-08: nylon T60 and contact width calibrated on fixed Yamaha CM-40
// pitches, then checked on disjoint pitches and the separate classical bank.
// Both datasets improve; steel is unchanged. See Docs/realism-work.md and
// Docs/realism-calibration-2026-09-08.json for assumptions and exact scores.
inline constexpr PhysicalCalibration fittedPhysicalCalibration {
    1.0f, 1.0f, 0.754677154f, 0.0f, 0.0f,
    // Nylon's fundamental T60 scale is 1.4, chosen by ear on 2026-09-24 over
    // the fitted 1.037816616 (Docs/decisions.md). The classical bank's open
    // strings lose their fundamental at 4-11 dB/s where the model's lost
    // theirs at 15-27, while its upper rows are samples trimmed with a fade,
    // and the benchmark prefers the fitted value (nylon training 2.0% better);
    // a blind listener preferred the longer ring over arpeggios on a held
    // bass and heard no difference on open strings left to ring.
    { 1.0f, 1.4f, 1.40369766f, 2.12267268f,
      0.0f, 1.12667139f, 0.0375f },
    { 0.749355465f, 1.53f, 0.52f, 0.643124355f,
      0.494086432f, 0.88819512f, 1.1859375f },
    -0.0706290118f, 1.0f, 0.00773577847f, -0.0597851562f, 2.28586032f,
    // The axial resonators are physically motivated, but without a measured
    // transfer level their narrow, high-Q onset reads as a pitched water-drop
    // transient rather than part of the pluck. Keep the calibrated mechanism
    // available for measurement work, but do not add that synthetic ping to
    // the shipping voice.
    0.011f, 2187.76023f, 0.00325f, 0.0f, 35.0f,
    // Zero, chosen by ear on 2026-09-24 over Woodhouse's published 0.8 mm
    // (Docs/decisions.md). He measured the correction on an open string and
    // calls its attribution tentative; carried as a fixed length it splits a
    // stopped string's planes further the higher it is fretted (4.8 cents at
    // the 14th fret, 6.8 at the 20th), and once the parallel plane radiated
    // and held most of a steel pluck, its member took over within 0.2-0.4 s
    // and high notes drifted flat as they rang, where the recordings' do not.
    // A blind listener preferred the steady pitch on all three pairs of high
    // steel and nylon melodies and chords. The benchmark prefers 0.8 mm by
    // little (Fylde training/validation/flat-top +0.15%/+0.23%/+0.68% at
    // zero, measured before the nylon share returned) while its
    // pitch-trajectory term prefers zero (1.199 -> 0.659). The mechanism
    // stays calibratable up to one string diameter.
    0.0f,
    // The plectrum, first fitted 2026-09-10 by the pick-release stage of
    // Tools/OptimizePhysicalModel.py on the picked archtop training rows
    // rendered with Pick (share 1.0, exponent 2.93, transient 0.078), then
    // refitted 2026-09-24 by the same stage from that vector when the two
    // lines of development merged: the merged Pick meets the string with the
    // narrower (0.35) and more bridgeward (0.40) contact and the continuous
    // Gaussian kernel, so the earlier values described a different release.
    // On the merged engine the search (77 evaluations to the step floor)
    // wants a smaller share growing almost linearly, 0.3125 v^0.93, and a
    // transient at 0.125 of the Finger law's full-velocity burst: archtop
    // training 6.623494 -> 6.546029 under Pick, development validation
    // 6.271120 -> 6.256259. Refitted again the same day by the same stage
    // once both polarisations radiated and the pluck left most of its energy
    // in the plane parallel to the top: the more horizontal release already
    // carries the high partials the velocity share was supplying, and the
    // search (60 evaluations to the step floor) keeps only a small, nearly
    // flat share, 0.0625 v^0.47, with the transient gain on its zero bound,
    // which is the Finger burst law: training 6.334019 -> 6.284861 under
    // Pick, development validation 5.941904 -> 5.904206.
    0.0625f, 0.46875f, 0.0f
};

} // namespace acustra
