#pragma once

#include "DSP/AcustraEngine.h"

namespace acustra::test
{
// Independent double-precision expectation for the calibrated single-note
// force. The audio contracts measure the displacement or contact it produces.
constexpr double velocityLevel(double velocity, PickingTechnique technique)
{
    const double light = technique == PickingTechnique::Pick
        ? 0.70 * (1.0 - velocity * velocity) : 0.30;
    return 1.0 - (1.0 - velocity) * light;
}

constexpr double forceGain(double velocity, PickingTechnique technique = PickingTechnique::Finger)
{
    return (0.2 + 0.8 * velocity * velocity * (3.0 - 2.0 * velocity))
        * velocityLevel(velocity, technique);
}

constexpr double fingerForce(double velocity) { return velocity * forceGain(velocity); }
}
