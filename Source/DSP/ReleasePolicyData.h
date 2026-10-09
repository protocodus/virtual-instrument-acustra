#pragma once

namespace acustra::detail {
// Classic attack policy selected from controlled release-source auditions.
// Public fitted calibration and measured body/string data remain unchanged.
inline constexpr float classicPickVelocityShareScale = 0.5f;

inline float pickedVelocityShareForModel(float calibratedShare, bool classic) noexcept
{
    return classic ? calibratedShare * classicPickVelocityShareScale : calibratedShare;
}

inline bool fullSoftContactSlip(bool finger, bool thumb, bool classic) noexcept
{
    return finger || (classic && thumb);
}
}
