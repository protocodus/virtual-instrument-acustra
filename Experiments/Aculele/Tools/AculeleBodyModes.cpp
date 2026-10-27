// Lists each Shape's strongest radiation modes below 2 kHz as the engine
// configures them (Mahogany unless a Wood index is given), for checking the
// coupled A0/T1 morph against reported ukulele resonances:
//
//   AculeleBodyModes [wood-index]
//
// A mode's level is its summed microphone residue over its pole's distance
// from the unit circle, the height of its peak, in dB against the Shape's
// strongest mode.

#include "DSP/AculeleEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace aculele
{
struct AculeleEngineTestAccess
{
    static void print(const AculeleEngine& engine, int shape, int wood)
    {
        const auto& configuration = engine.bodyConfigurations_[
            static_cast<std::size_t>(3 * shape + wood)];
        struct Row { double hz, q, level; };
        std::vector<Row> rows;
        for (int index = 0; index < configuration.count; ++index)
        {
            const auto& mode = configuration.modes[static_cast<std::size_t>(index)];
            const double radius = std::hypot(mode.poleReal, mode.poleImaginary);
            const double angle = std::atan2(mode.poleImaginary, mode.poleReal);
            const double hz = angle * 48000.0 / (2.0 * 3.141592653589793);
            if (hz <= 0.0 || hz > 2000.0 || radius >= 1.0)
                continue;
            const double q = angle / (2.0 * (1.0 - radius));
            const double residue = std::hypot(mode.leftReal, mode.leftImaginary)
                + std::hypot(mode.rightReal, mode.rightImaginary);
            rows.push_back({ hz, q, residue / (1.0 - radius) });
        }
        double strongest = 0.0;
        for (const auto& row : rows)
            strongest = std::max(strongest, row.level);
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.level > b.level; });
        rows.resize(std::min<std::size_t>(rows.size(), 10));
        std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.hz < b.hz; });
        static const char* names[] { "Soprano", "Concert", "Tenor" };
        std::printf("%s (air mode %.0f Hz):\n", names[shape], configuration.airModeHz);
        for (const auto& row : rows)
            std::printf("  %7.1f Hz  Q %5.1f  %+6.1f dB\n", row.hz, row.q,
                        20.0 * std::log10(row.level / strongest));
    }
};
} // namespace aculele

int main(int argc, char** argv)
{
    const int wood = argc > 1 ? std::atoi(argv[1]) : 1;
    auto engine = std::make_unique<aculele::AculeleEngine>();
    engine->prepare(48000.0, 256);
    for (int shape = 0; shape < 3; ++shape)
        aculele::AculeleEngineTestAccess::print(*engine, shape, wood);
    return 0;
}
