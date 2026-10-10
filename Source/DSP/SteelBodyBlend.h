// Steel's body as a weighted blend of Blind Set 18's candidates.
//
// The listener chose Set 18's B ("one body at the saddle") and D ("the
// plate-Q rule down through T1") and asked whether the other model
// enhancements could come in too, weighted: "all 10-20% but B and D maybe
// 70%". Blind Set 19 heard that blend (B 1, D 0.7, C 0.15, E 0.15) against
// B+D and chose B+D, with "something between B and C is the best". Blind
// Set 21 heard the midpoint (B 1, D 0.85, C 0.075, E 0.075) against B+D and
// preferred the midpoint, "not a big difference"; the listener then asked
// for the cheaper option, "but try to get as close to my preference as
// possible" (Docs/decisions.md, 2026-09-29). The midpoint cost about +40%
// (strums) / +55% (a held chord) CPU over B+D. This is a lighter body that
// measures close to it, a measured stand-in for the choice made by ear, not a
// choice made by ear itself:
// - C, the decay-Q grid, is gone: at 0.15 its difference sat 24-26 dB
//   below the music, and it cost 234 modes.
// - E plays only its modes below steelBlendJointBandHz: 9 of its 131
//   radiation modes and 8 of its 56 bridge modes, the ones that carry what
//   the midpoint changes as heard (body under the treble notes, 63-80 Hz,
//   the thinner 159 Hz early).
// - Above that band E is not played, so B's bridge is whole there and g21's
//   radiation gives up only steelBlendHighJointFraction of E's share, where
//   the midpoint's balance of highs against 100-400 Hz is matched.
//
// Each weight blends a passive part with a passive part, so the result stays
// passive: a non-negative sum of positive-real driving-point admittances is
// positive real, and a radiation sum only adds outputs. Passive is not
// linear, though. The strings do not drain into the bridge admittance Y but
// into the port it makes with the anchors, (Y^-1 + K/s)^-1, whose peaks sit
// at the summed Y's zeros. A part in parallel that rings at other
// frequencies moves those peaks, so w of a bridge is not w of what the string
// hears. testSteelBlend (Tests/BodyShapeTests.cpp) checks that at each of B's
// aligned poles the port keeps at least B's share of B's own conductance.
//
//   B                              The steel bridge is B's aligned bridge
//                                  (g21's own, on its radiation's poles, at
//                                  steel-string mobility), whole. At the 0.7
//                                  first asked for, the other 30% (the
//                                  measured Fylde bridge, whose modes are not
//                                  on the radiation's poles) left the port
//                                  0.37 of B's conductance at the 189.7 Hz
//                                  radiation pole and a new drain peak at
//                                  209 Hz (Docs/decisions.md, 2026-09-28), so
//                                  it is not blended. B's poles are never
//                                  moved part of the way either: a 30%
//                                  misalignment leaves about 0.6 semitone
//                                  between a bridge mode and its radiation,
//                                  wider than the resonance, which is the
//                                  band-pass ringing B removed.
//   D  steelBlendT1PlateQWeight    For the modes the plate-Q rule reaches only
//                                  when it runs down to 150 Hz (T1 and the
//                                  rocking modes, 150-300 Hz), Q moves this
//                                  share of the way in log terms:
//                                  Q = Q_asfitted^(1-w) * Q_D^w. Offline data:
//                                  Tools/GenerateBodyForcePair.py reads this
//                                  value as its --plate-q-t1-weight default
//                                  and records it in MeasuredBodyData.h, which
//                                  must be regenerated when it changes (the
//                                  static_assert in AcustraEngine.cpp).
//   E  steelBlendJointBodyWeight   A parallel copy of the joint-pole body
//                                  (MeasuredJointBodyData.h), its modes below
//                                  steelBlendJointBandHz only: their radiation
//                                  and their bridge at w, g21's radiation and
//                                  B's bridge below the band at (1-w). Each
//                                  kept mode is E's own, at its index in the
//                                  joint array and on the joint body's own
//                                  Shape morph, so it rings exactly as it did
//                                  in the midpoint; its plate-Q rule starts
//                                  at 300 Hz as E was fitted, so D does not
//                                  damp its T1 and rocking modes (185.9 Hz
//                                  Q 50.5 beside g21's 189.7 Hz at Q 30.0).
//      steelBlendJointBandHz       550 Hz: E's 9 modes from 83 to 516 Hz,
//                                  8 of them with a bridge residue. 450 Hz
//                                  (7 modes) and 500 Hz (8) measured further
//                                  from the midpoint (Docs/decisions.md).
//      steelBlendHighJointFraction Above the band g21's radiation plays at
//                                  1 - w * this (0.2: 0.985 at w 0.075). At 1
//                                  (0.925, as in the midpoint) this body's
//                                  energy above 2 kHz against 100-400 Hz is
//                                  0.5-0.6 dB under the midpoint's on Set 21's
//                                  pieces, at 0 (g21 whole) 0.1-0.2 dB over;
//                                  at 0.2 within 0.06.
//
// So, on the Original guitar, with E_lo the joint modes below the band and f
// the measured frequency of each g21 or B mode:
//   bridge = w E_lo + [f < band ? (1-w) : 1] Y_own
//   body   = w E_lo + [f < band ? (1-w) : 1 - 0.2 w] g21
//
// To hear E changed, rebuild with its macros, for example
//   cmake -DCMAKE_CXX_FLAGS='-DACUSTRA_STEEL_BLEND_JOINT_BAND_HZ=20000.0f
//       -DACUSTRA_STEEL_BLEND_HIGH_JOINT_FRACTION=1.0f
//       -DACUSTRA_BODY_MODE_COUNT=263 -DACUSTRA_BRIDGE_MODE_COUNT=103' ...
// which is the Set 21 midpoint without its decay grid (all 131 joint modes,
// every g21 and B mode at 1-w). E at 0 leaves the joint body out entirely
// and every share at 1: B=1, D=1, E=0 is Set 18's B+D bit for bit. D is data:
// change ACUSTRA_STEEL_BLEND_T1_PLATE_Q below (the generator reads this
// line), then
//   python3 Tools/GenerateBodyForcePair.py --reweight <an earlier --plate-q
//       median fit directory> --output <dir>
// (seconds; or refit from the archive with --raw-mat ... --plate-q median)
// and copy <dir>/MeasuredBodyData.h over Source/DSP/MeasuredBodyData.h.

#pragma once

#if !defined(ACUSTRA_STEEL_BLEND_T1_PLATE_Q)
#define ACUSTRA_STEEL_BLEND_T1_PLATE_Q 0.85f
#endif
#if !defined(ACUSTRA_STEEL_BLEND_JOINT_BODY)
#define ACUSTRA_STEEL_BLEND_JOINT_BODY 0.075f
#endif
#if !defined(ACUSTRA_STEEL_BLEND_JOINT_BAND_HZ)
#define ACUSTRA_STEEL_BLEND_JOINT_BAND_HZ 550.0f
#endif
#if !defined(ACUSTRA_STEEL_BLEND_HIGH_JOINT_FRACTION)
#define ACUSTRA_STEEL_BLEND_HIGH_JOINT_FRACTION 0.2f
#endif

namespace acustra::detail
{
inline constexpr float steelBlendT1PlateQWeight = ACUSTRA_STEEL_BLEND_T1_PLATE_Q;
inline constexpr float steelBlendJointBodyWeight = ACUSTRA_STEEL_BLEND_JOINT_BODY;
inline constexpr float steelBlendJointBandHz = ACUSTRA_STEEL_BLEND_JOINT_BAND_HZ;
inline constexpr float steelBlendHighJointFraction = ACUSTRA_STEEL_BLEND_HIGH_JOINT_FRACTION;

static_assert(steelBlendT1PlateQWeight >= 0.0f && steelBlendT1PlateQWeight <= 1.0f
                  && steelBlendJointBodyWeight >= 0.0f && steelBlendJointBodyWeight <= 1.0f
                  && steelBlendHighJointFraction >= 0.0f && steelBlendHighJointFraction <= 1.0f,
              "a blend weight is a share between 0 and 1: a negative one would "
              "subtract an admittance and could make the bridge active");
} // namespace acustra::detail
