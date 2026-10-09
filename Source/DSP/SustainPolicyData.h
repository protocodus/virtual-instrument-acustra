#pragma once

namespace acustra::detail {
// Authored broad-loss slope selected from the gentle sustain audition.
// This is an intrinsic string profile: every Classic string, including an
// idle sympathetic string, retains it when the player's technique changes.
// Public fitted calibration and measured body/bridge data are unchanged.
inline constexpr float classicBroadLossCornerScale = 0.75f;

inline float broadLossCornerScaleForModel(bool classic) noexcept
{
    return classic ? classicBroadLossCornerScale : 1.0f;
}
}
