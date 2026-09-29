// Acustra: bounded offline-fit parameters for the physical model.

#pragma once

namespace acustra
{

struct MaterialCalibration
{
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
    MaterialCalibration steel;
    float apertureRegisterExponent { 1.0f };
    // Radiation gain on steel's measured air mode (the g21 modes between 85
    // and 145 Hz) at the bridge microphone of the Stereo pair only; the
    // upper-bout microphone and the named models hear it as measured.
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
    // Swept on the classical bridge then shipping, over E, G, Am, D and C
    // chords. Worst separable note's pull early/late in
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
    // c_long = sqrt(EA/mu). For this steel set that is 1.5 to 3.9 kHz, from
    // the construction data the transverse model already uses. Because the drive is a squared slope it carries the
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
    // the order of the string diameter - taken as the 0.82 mm nylon B string
    // the 0.8 mm was measured on.
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
    // The Pick technique only. The radius of the plectrum edge the string
    // slides round as it is let go (AcustraEngine::plectrumSlipPole): the
    // release takes r / u, u the speed the string's own held force gives it,
    // so it low-passes the pluck at a corner that rises with the stroke's
    // force. Zero is the instant release.
    float pickEdgeRadiusMetres { 0.0f };
    // The loss factor eta of a string's viscoelastic bending stiffness,
    // EI(1 + i eta), one per construction: Valette's and Woodhouse's bending
    // loss 1/Q_n = eta B n^2 / (1 + B n^2) (bendingLossSection in
    // AcustraEngine.cpp). Zero is an exact no-op.
    float steelWoundBendingLoss { 0.0f };
    float steelPlainBendingLoss { 0.0f };
    // The noise a fingertip, nail or plectrum makes as it leaves the string
    // (AcustraEngine::initialiseContactNoise and renderContactNoise). One
    // random contact force per pluck, along the stroke: white between the
    // string's fundamental and a corner, above which the contact smooths it,
    // with the corner at the tool's full-velocity value times the MIDI
    // velocity v (sliding-contact noise moves up in frequency with the
    // sliding speed and grows with it: Akay, "Acoustics of friction", JASA 111
    // (2002) 1525-1548), an RMS of v^contactNoiseVelocityExponent times the
    // force F0 the hand held, and one decay from the release. It reaches the
    // microphones two ways. Its string-borne part (contactNoiseFinger,
    // contactNoisePick: the RMS as a fraction of F0 at v = 1) enters the string at the
    // contact point as velocity waves F / (2Z) both ways, so it reaches the
    // bridge and body as the string's first arrivals do. Its airborne part
    // (contactClickFinger, contactClickPick) is the tool's own click, a small
    // source at the contact whose pressure follows the force's rate of
    // change up to where a 3 mm radiator stops being small (18 kHz), heard
    // through the direct path after its flight to the microphone, without
    // touching the string or the body. The levels are per tool (a finger, a
    // plectrum); Thumb takes
    // the finger's with its corner lowered by the ratio of the two contact
    // widths. Zero levels are an exact no-op.
    //
    // What the recordings asked for (Tools/MeasureAttackTransient.py on the
    // bank's training rows, 2026-09-28): in the first 12 ms a loud picked
    // note carries 14-29 dB more energy between its partials at 1-12.5 kHz
    // than the engine renders and a soft one 7-20 dB, rising with frequency; that energy falls at a median 420 dB/s over
    // 12-40 ms (contactNoiseDecaySeconds, 20.7 ms, is that decay, not a fit);
    // and on the loud layer it does not recur at the string's period (its
    // 2-14 kHz content correlates 0.18-0.54 with itself a period later, where
    // the engine's and the soft layer's correlate 0.80-0.96), so it is not the string's own
    // vibration nor a room's. A force launched into the string recurs with the
    // string, and the fits drive the string-borne levels to zero.
    float contactNoiseFinger { 0.0f };
    float contactNoisePick { 0.0f };
    float contactNoiseVelocityExponent { 1.0f };
    float contactNoiseCornerHz { 4000.0f };
    float pickContactNoiseCornerHz { 8000.0f };
    float contactNoiseDecaySeconds { 0.0207f };
    float contactClickFinger { 0.0f };
    float contactClickPick { 0.0f };
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
// Nylon strings were retired on 2026-09-29 (Docs/decisions.md); the notes
// below that cite nylon evidence record how a value still shared by the
// steel strings was chosen.
inline constexpr PhysicalCalibration fittedPhysicalCalibration {
    1.0f, 1.0f, 0.754677154f, 0.0f, 0.0f,
    // Steel's pluck distance scale is 1.8, chosen by ear on 2026-09-25 over
    // the fitted 0.888 (Docs/decisions.md): at the default Pluck Position a
    // finger meets the string 149 mm from the bridge instead of 74 mm, at
    // the same fitted displacement. The 0.888 was fitted on archtop rows
    // played with a pick near the bridge; the finger-played flat-top rows
    // prefer 149 mm, and a blind listener chose it on all three pairs. Pick
    // keeps 0.40 of the Finger's distance, 60 mm, and the picked archtop
    // rows rendered with it improve (steel training 6.106 -> 5.803).
    // Steel's contact width (aperture), finger burst (transient) and velocity
    // brightness, with the plectrum's release and the strings' bending loss
    // below, were chosen by ear on 2026-09-28 (Docs/decisions.md, Set 14) at
    // 70% of the way from that set's C toward its B, the listener's own
    // "something between B and C ... dial it to 70%". C is the joint refit's
    // optimum (Tools/OptimizePhysicalModel.py, snap-steel over Finger and
    // Pick on the Fylde bridge's training rows, 377 evaluations from
    // shipping): aperture 0.811093105, burst 0.376898932, velocity brightness
    // 1.0734375. B holds the steel bending loss at half what the recordings'
    // 20-300 ms decay measures and refits the pluck around it
    // (snap-steel-pluck, 290 evaluations): aperture 0.35 (its floor), burst
    // 3.0 (its ceiling), velocity brightness 1.1203125. Shipping was 0.643,
    // 0.494 and 1.186. D = C + 0.7 (B - C) is a direction chosen by ear, not a
    // fit, and none of its values is refit.
    { 0.749355465f, 1.53f, 0.52f, 0.4883279315f,
      2.2130696796f, 1.8f, 1.10625f },
    // lowBodyModeGain 4 (+12 dB), chosen by ear on 2026-09-25 over 1 on all
    // three pairs (Docs/decisions.md): the Eastman dreadnought's picked E2
    // and A#2 stand 11-13 dB stronger against their 2nd and 3rd harmonics
    // than the engine rendered them through the bridge microphones.
    -0.0706290118f, 4.0f, 0.00773577847f, -0.0597851562f, 2.28586032f,
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
    // Pick, development validation 5.941904 -> 5.904206. Refitted on
    // 2026-09-25 by the same stage (67 evaluations to the step floor) once
    // the plectrum followed the Finger to 60 mm from the bridge and the
    // Stereo pair took the upper-bout microphone: a still smaller share,
    // 0.0117 v^0.45, and a pick transient at 0.031 of the Finger burst:
    // training 6.133475 -> 6.103277 under Pick, development validation
    // 5.742850 -> 5.702917.
    // 2026-09-27: once the string slides off the tip's edge (below), the
    // same stage, run from the shipping vector at edge radii 0.10, 0.15 and
    // 0.20 mm (42 evaluations each to the step floor), sends the share and
    // the transient gain to their zero bounds at every radius: the release
    // velocity's slower-falling partials were standing in for the dynamic
    // brightness the slip now supplies, and the pick's own burst for the
    // Finger law. With the share at zero the exponent reads nothing.
    // 2026-09-28, by ear (Docs/decisions.md, Set 14): the share and its
    // exponent at 70% of the way from C, the joint refit's 0.0078125
    // v^0.4921875, toward B, the half-loss snap's 0.828125 v^1.015625, so
    // 0.58203125 v^0.85859375. The listener heard B's pick as "too intense"
    // and asked for 70% of it. The pick burst stays at zero (both endpoints).
    0.58203125f, 0.85859375f, 0.0f,
    // The plectrum edge, fitted on the picked archtop training rows rendered
    // with Pick (the pick-release stage's steel objective 5.917 / 5.915 /
    // 5.943 at 0.10 / 0.15 / 0.20 mm before and 5.3985 / 5.3664 / 5.3842
    // after refitting the three values above): 0.15 mm. Against the whole
    // shipping Pick, archtop training 5.9391 -> 5.5639 and held-out
    // development validation 5.5588 -> 5.4839 (steel rows). Commercial picks
    // run about 0.4-1.5 mm thick, so a rounded edge of 0.2-0.75 mm; the
    // fitted radius sits below that because it also absorbs the pick's own
    // speed, which shortens the slip and is not measured here, and because
    // the displacement scale it is read against is known to within a factor.
    // 2026-09-28, by ear (Docs/decisions.md, Set 14): 0.1162109375 mm, 70% of
    // the way from C's 0.1216796875 mm toward B's 0.1138671875 mm, both
    // refitted with the pluck around their bending loss on the Fylde
    // bridge's training rows.
    0.1162109375e-3f,
    // The strings' bending loss, chosen by ear on 2026-09-28
    // (Docs/decisions.md, Set 14). The recordings' 20-300 ms upper-partial
    // decay measures steel wound 0.10 and plain 0.006. B held half of it,
    // wound 0.05 and plain 0.003, with the pluck refitted around it; C, the
    // joint refit's optimum, took the wound loss to 0 and the plain to
    // 0.00078125. Steel ships 70% of the way from C toward B: wound 0.035,
    // plain 0.002334375, so its upper partials die at about 70% of B's added
    // rate and 35% of the rate the recordings measure. Neither is refit.
    0.035f, 0.002334375f
    // The contact noise and click levels keep their zero defaults: the click
    // was rejected by ear on 2026-09-28 (Set 16, "the pick is TOO LOUD").
};

} // namespace acustra
