// Steel's body as a weighted blend of Blind Set 18's candidates.
//
// The listener chose Set 18's B ("one body at the saddle") and D ("the
// plate-Q rule down through T1") and asked whether the other model
// enhancements could come in too, weighted: "all 10-20% but B and D maybe
// 70%". These four weights are that blend. They are by-ear candidate values
// awaiting Blind Set 19 (Docs/decisions.md, 2026-09-28), not a verdict.
//
// Each weight blends a passive part with a passive part, so the result stays
// passive: a non-negative sum of positive-real driving-point admittances is
// positive real, and a radiation sum only adds outputs.
//
//   B  steelBlendOwnBridgeWeight   The steel bridge is this share of B's
//                                  aligned bridge (g21's own, on its
//                                  radiation's poles, at steel-string
//                                  mobility) in parallel with the rest of the
//                                  measured Fylde bridge the steel presets
//                                  played before B. B's poles are never moved
//                                  part of the way: a 30% misalignment leaves
//                                  about 0.6 semitone between a bridge mode
//                                  and its radiation, wider than the
//                                  resonance, which is the band-pass ringing
//                                  B removed.
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
//   C  steelBlendDecayGridWeight   Above 1 kHz the radiation is (1-w) of g21's
//                                  fitted top band plus w of the decay-Q grid
//                                  (MeasuredBodyDecayGridData.h). Both
//                                  reproduce the same measured response, so
//                                  the sum does too; what w brings is the
//                                  grid's longer ring.
//   E  steelBlendJointBodyWeight   A parallel copy of the joint-pole body
//                                  (MeasuredJointBodyData.h): its radiation
//                                  and its bridge at w, everything above at
//                                  (1-w).
//
// So, on steel's own bridge (steel, the Original guitar, the Original
// bridge, which the steel presets select):
//   bridge = (1-wE) [wB Y_own + (1-wB) Y_Fylde] + wE Y_joint
//   body   = (1-wE) [g21 below 1 kHz + (1-wC) g21 above + wC grid] + wE joint
// The Fylde bridge choice and nylon are not blended.
//
// To hear one weight changed, rebuild with its macro, for example
//   cmake -DCMAKE_CXX_FLAGS='-DACUSTRA_STEEL_BLEND_DECAY_GRID=0.3f' ...
// A weight of 0 leaves its part out entirely (no modes, no cost), and B=1,
// D=1, C=0, E=0 is Set 18's B+D bit for bit. D is data: change
// ACUSTRA_STEEL_BLEND_T1_PLATE_Q below (the generator reads this line), then
//   python3 Tools/GenerateBodyForcePair.py --reweight <an earlier --plate-q
//       median fit directory> --output <dir>
// (seconds; or refit from the archive with --raw-mat ... --plate-q median),
// copy <dir>/MeasuredBodyData.h over Source/DSP/MeasuredBodyData.h, and
// refit the grid beside it (Tools/GenerateBodyDecayGrid.py; at D's full
// swing its residues move 0.09%).

#pragma once

#if !defined(ACUSTRA_STEEL_BLEND_OWN_BRIDGE)
#define ACUSTRA_STEEL_BLEND_OWN_BRIDGE 0.7f
#endif
#if !defined(ACUSTRA_STEEL_BLEND_T1_PLATE_Q)
#define ACUSTRA_STEEL_BLEND_T1_PLATE_Q 0.7f
#endif
#if !defined(ACUSTRA_STEEL_BLEND_DECAY_GRID)
#define ACUSTRA_STEEL_BLEND_DECAY_GRID 0.15f
#endif
#if !defined(ACUSTRA_STEEL_BLEND_JOINT_BODY)
#define ACUSTRA_STEEL_BLEND_JOINT_BODY 0.15f
#endif

namespace acustra::detail
{
inline constexpr float steelBlendOwnBridgeWeight = ACUSTRA_STEEL_BLEND_OWN_BRIDGE;
inline constexpr float steelBlendT1PlateQWeight = ACUSTRA_STEEL_BLEND_T1_PLATE_Q;
inline constexpr float steelBlendDecayGridWeight = ACUSTRA_STEEL_BLEND_DECAY_GRID;
inline constexpr float steelBlendJointBodyWeight = ACUSTRA_STEEL_BLEND_JOINT_BODY;

static_assert(steelBlendOwnBridgeWeight >= 0.0f && steelBlendOwnBridgeWeight <= 1.0f
                  && steelBlendT1PlateQWeight >= 0.0f && steelBlendT1PlateQWeight <= 1.0f
                  && steelBlendDecayGridWeight >= 0.0f && steelBlendDecayGridWeight <= 1.0f
                  && steelBlendJointBodyWeight >= 0.0f && steelBlendJointBodyWeight <= 1.0f,
              "a blend weight is a share between 0 and 1: a negative one would "
              "subtract an admittance and could make the bridge active");
} // namespace acustra::detail
