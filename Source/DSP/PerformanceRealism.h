#pragma once

namespace acustra
{

// Offline comparison switches, not performance controls or saved plug-in
// parameters. Production uses the defaults. Configure before prepare(), or
// while stopped: changing these options clears the instrument's sound state.
// Keeping the mechanisms independent makes their audible/CPU contribution
// testable without maintaining separate engine implementations.
struct PerformanceRealism
{
    bool contactRelease { true };
    bool coherentHand { true };
    bool gestureDamping { true };
    bool playerBodyLoading { true };
};

} // namespace acustra
