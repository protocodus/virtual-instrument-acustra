#pragma once

namespace acustra::detail
{

// A bounded, authored held-player approximation for the free-back g35
// measurement. This is additional structural loss, not a fit to a held/free
// comparison. Original's recording-derived damping already includes an
// unknown player load, so its caller leaves this disabled.
//
// Contact with the back/side is represented as a positive modal loss angle.
// The smooth participation law favors the lower plate band, attenuates the
// mainly-air group, and rolls off before the already-fitted high-band drain.
// Its 180/900 Hz scales and magnitude are authored, not measured geometry.
// Mass, stiffness, modal force/moment residues and resonance centers stay
// fixed: 1/Q_loaded = 1/Q_measured + added_loss. Every mode therefore keeps
// positive damping and its bridge residue matrix stays positive semidefinite.
// One fixed posture is configured with the construction; no note-time body
// rebuild, random pole motion, new user control or room feedback is involved.
inline float playerLoadedBodyQ(float frequency, float q, bool enabled,
                              float lossAngle = 0.006f) noexcept
{
    if (!enabled)
        return q;
    const float lowRatio = frequency / 180.0f;
    const float highRatio = frequency / 900.0f;
    const float participation = lowRatio * lowRatio
        / (1.0f + lowRatio * lowRatio)
        / (1.0f + highRatio * highRatio);
    return q / (1.0f + q * lossAngle * participation);
}

} // namespace acustra::detail
