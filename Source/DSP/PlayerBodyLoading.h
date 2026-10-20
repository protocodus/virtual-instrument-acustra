#pragma once

namespace acustra::detail
{
// Fixed playing-posture loss, added to the already calibrated body modes.
// These two bounded frequency envelopes approximate torso/back contact and
// forearm/top contact. Their magnitudes and bands are authored, not fitted
// tissue parameters or measured mode-shape participation. Suppress the
// mainly-air band and roll off above the structural band to preserve the
// existing high-frequency drain. See Docs/research/player-contact-2026-10-20.md.
//
// Independent passive losses add as inverse Q. Existing pole frequencies
// and mechanical residues stay fixed, so each positive-real bridge mode
// retains its positive-semidefinite residue matrix. This helper runs only
// while configuring a body or building the tuning observer, never per sample.
inline float playerLoadedBodyQ(float frequency, float q, bool enabled) noexcept
{
    if (!enabled)
        return q;
    const float f2 = frequency * frequency;
    const float torso = 0.004f * f2 / (f2 + 180.0f * 180.0f)
        * (700.0f * 700.0f) / (f2 + 700.0f * 700.0f);
    const float forearm = 0.002f * f2 / (f2 + 300.0f * 300.0f)
        * (1800.0f * 1800.0f) / (f2 + 1800.0f * 1800.0f);
    return q / (1.0f + q * (torso + forearm));
}
} // namespace acustra::detail
