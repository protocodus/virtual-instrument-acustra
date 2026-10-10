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
    // junction active. In the measured flamenca's units, like the modal
    // residues it continues: the bridge scales both to steel level
    // (plateConductanceMode in AcustraEngine.cpp).
    float bridgeConductanceFloor { 0.0f };
    float bridgeConductanceCornerHz { 1000.0f };
    // Retired on 2026-10-10 (Docs/saddle-termination-2026-10-10.md): the
    // engine reads nothing from it. It was the length of string between the
    // saddle and its anchor, whose stiffness T/L each string presented to
    // ground at the saddle (DAFx-26's fixed-end tail, shipped at its 3.25 mm
    // stub). On a pin bridge both ends of that segment are on the bridge, so
    // it holds the bridge against nothing; as a spring to ground it
    // resonated with the body's mass above each body mode and drained the
    // strings at 6 to 34 times the bridge's own conductance 2-5 semitones
    // above A0 and T1. The slot, its bounds and its place in the calibration
    // vector stay, so saved calibration files and the fitting tools keep
    // their layout; Docs/decisions.md (2026-09-02, 2026-09-04) records how
    // its values were swept and chosen while it was live.
    float bridgeTailLengthMetres { 0.020f };
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
    // which is the tilt that grows). Zero is the exact legacy pluck.
    float pickReleaseVelocityShare { 0.0f };
    float pickReleaseVelocityExponent { 2.0f };
    // The Pick technique only. The radius of the plectrum edge the string
    // slides round as it is let go (AcustraEngine::plectrumSlipPole): the
    // release takes r / u, u the speed the string's own held force gives it,
    // so it low-passes the pluck at a corner that rises with the stroke's
    // force. Zero is the instant release.
    float pickEdgeRadiusMetres { 0.0f };
    // The wound strings' winding friction, a constant loss angle delta_W:
    // dry friction between successive turns acts as a complex tension
    // T(1 + j delta_W) (Paté, Le Carrou and Fabre, JASA 135 (2014) 3045,
    // Sec. III.A, after Valette, who add 1/Q_W = delta_W to a flexible
    // string's loss). On a stiff string tension carries 1/(1 + B n^2) of
    // partial n's restoring force, so it loses delta_W / (1 + B n^2), as the
    // bending loss below acts on the rest (constantLossSection in
    // AcustraEngine.cpp); String Age scales it.
    float steelWoundFrictionLoss { 0.0f };
    // The loss factor eta of a steel string's visco- and thermo-elastic
    // bending stiffness, EI(1 + i eta): Valette's and Woodhouse's bending loss
    // 1/Q_n = eta B n^2 / (1 + B n^2). Every string's, a wound one's on its
    // plain steel core.
    float steelPlainBendingLoss { 0.0f };
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
    1.0f, 1.0f, 0.754677154f, 0.0f,
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
    // bridgeTailLengthMetres, retired (above): 3.25 mm, inert.
    0.011f, 2187.76023f, 0.00325f,
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
    0.58203125f, 0.85859375f,
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
    // The wound strings' winding friction, 2026-10-10
    // (Docs/string-hf-loss-2026-10-10.md). Until then the wound strings lost
    // their grime and friction through the bending law at a by-ear 0.035
    // (Set 14, 70% of the way from C's 0 toward B's 0.05), which rises as the
    // cube of frequency and spread 8:1 across the four wound strings at
    // 2.8 kHz; winding friction acts on the tension instead and gives a
    // constant loss angle. With the dislocation loss every string carries
    // (constantLossSection), swept on its own: the fitted splits are flat
    // within 0.1% from 1.3e-4 to 1.8e-4 and 0.3-0.4% worse at 0.6e-4, which
    // the flat-top rows prefer by 0.7%. At 1.3e-4 the never-fitted flat-top
    // rows' wound register (MIDI 40-58, 0.15-1.2 s per-partial decay)
    // decays within 2.4 dB/s of the recordings at 1-2.2 kHz and 5.5-7.6 dB/s
    // faster at 2.2-5 kHz, where under the bending law it decayed 1.3 to 2.1
    // times as fast as they do, and the archtop audit's bands from 2 to 9 kHz
    // come within 2.6 dB/s. A blind listener compared 1.3e-4 with main on
    // 2026-10-10: preferred main on the open and wound strings, heard
    // 1.3e-4 as "too much" on the picked open and the wound strings and main
    // as too bright on the fingered open ones, "better if more blended", and
    // asked for a blend. 4.5e-4 keeps the 0.035 bending law's geometric-mean
    // early decay over the wound partials at 0.5-4 kHz at the default
    // controls. Its 1.5-6 kHz brightness over 0.1-1 kHz sits 0.44-0.59 of
    // the way from main to 1.3e-4 on open and wound strings, which is the
    // blend. It awaits that listener's A/B.
    4.5e-4f,
    // The strings' bending loss, chosen by ear on 2026-09-28
    // (Docs/decisions.md, Set 14). The recordings' 20-300 ms upper-partial
    // decay measures steel plain 0.006. B held half of it, 0.003, with the
    // pluck refitted around it; C, the joint refit's optimum, took it to
    // 0.00078125. Steel ships 70% of the way from C toward B, 0.002334375; it
    // is not refit, and since 2026-10-10 it is every string's (a wound
    // string's on its core).
    0.002334375f
};

} // namespace acustra
