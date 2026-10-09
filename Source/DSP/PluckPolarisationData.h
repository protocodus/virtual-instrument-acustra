#pragma once
#include <algorithm>
#include <array>
namespace acustra::detail {
// The Original's share was selected on the steel corpus and retained by ear
// on 2026-09-24. Classic's measured pressure reveals its parallel tails more
// strongly (Docs/classic78-parallel-tail-ablation-2026-10-09.json).
// This authored release profile changes no measured body data.
inline constexpr float classicNormalShareOffset = 0.30f;
inline float classicPluckStringWeight(int stringIndex) noexcept
{
    // Authored string/gauge proxy, fixed to Standard open notes: tuning and
    // fret changes do not alter the direction of the same string stroke.
    constexpr std::array<int, 6> standardOpenMidi { 40, 45, 50, 55, 59, 64 };
    const float profilePitch = static_cast<float>(standardOpenMidi[
        static_cast<std::size_t>(std::clamp(stringIndex, 0, 5))]);
    const float position = std::clamp((profilePitch - 52.0f) / 5.0f, 0.0f, 1.0f);
    return position * position * (3.0f - 2.0f * position);
}
inline float pluckNormalShareForModel(float legacyShare, bool classic,
                                     int stringIndex) noexcept
{
    if (!classic)
        return legacyShare;
    const float offset = classicNormalShareOffset * classicPluckStringWeight(stringIndex);
    if (!(offset > 0.0f))
        return legacyShare;
    return std::clamp(legacyShare + offset, 0.17f, 0.95f);
}
}
