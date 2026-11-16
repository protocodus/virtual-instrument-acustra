#pragma once

#include <array>
#include <string_view>

namespace acustra::test
{
struct ReleasedFactoryLevel
{
    std::string_view name;
    double normalMedianVu;
    double heldOpenERmsDbfs;
    double normalToleranceVu;
    double heldOpenEToleranceDb;
};

// Measured on the released 1.3.3 arm64 Rack module at 48 kHz, 64 frames,
// with its original factory settings. Normal is the median of 27 max-300 ms
// stereo RMS readings (+12 dB VU reference); the open E chord is held for
// two seconds at velocity 127 with the bridge hand lifted. The tolerances
// cover the measured stroke differences after matching whole-song loudness,
// including the canonical MIDI and Rack bridge-hand paths at native rates.
inline constexpr std::array<ReleasedFactoryLevel, 8> factoryLevels133 {{
    { "Dreadnought Steel",          -14.844, -16.682, 3.50, 3.50 },
    { "Parlor Steel Fingerstyle",   -20.226, -23.037, 1.50, 1.75 },
    { "Mahogany Steel Thumb",       -18.786, -23.785, 0.75, 1.00 },
    { "Jumbo Steel Pick",           -18.719, -23.283, 2.25, 2.00 },
    { "Palm Muted Steel Pick",      -21.656, -22.833, 2.00, 1.25 },
    { "DADGAD Mono Mic",            -23.030, -24.360, 2.25, 2.00 },
    { "Open G Slide-ready",         -19.164, -23.493, 4.00, 2.00 },
    { "Piezo Stage Steel",          -22.541, -27.465, 1.75, 1.00 },
}};
}
