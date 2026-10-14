// Repeated strums of one chord: sixteen E major strokes under Pick at 48 kHz,
// velocity 100, every 0.3 s at 120 BPM (so they alternate) with the keys
// released 0.2 s in. Each stroke's first 250 ms, the stereo sum aligned at
// its note-on, is correlated with every other stroke of its direction,
// leaving out the first two (nothing rings into them yet).
// Usage: StrumRepeat [strumScatter 0|1]
#include "DSP/AcustraPerformer.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
int main(int argc, char** argv)
{
    const bool scatter = argc < 2 || std::atoi(argv[1]) != 0;
    const double rate = 48000.0;
    auto p = std::make_unique<acustra::Performer>();
    acustra::PerformanceRealism realism; realism.strumScatter = scatter;
    p->engine().setPerformanceRealism(realism);
    acustra::EngineParameters par; par.picking = acustra::PickingTechnique::Pick; p->setParameters(par);
    p->prepare(rate, 64); p->setTempoBpm(120.0);
    const std::array<int, 6> chord { 40, 47, 52, 56, 59, 64 };
    const int spacing = static_cast<int>(0.3 * rate), hold = static_cast<int>(0.2 * rate), strokes = 16;
    const int total = strokes * spacing + static_cast<int>(rate);
    std::vector<float> mono(static_cast<std::size_t>(total));
    std::array<float, 1> l {}, r {};
    for (int n = 0; n < total; ++n)
    {
        p->beginBlock(l.data(), r.data(), 1);
        const int k = n / spacing, phase = n % spacing;
        if (k < strokes && phase == 0) for (int note : chord) p->noteOn(0, 1, note, 100);
        if (k < strokes && phase == hold) for (int note : chord) p->noteOff(0, 1, note, 64);
        p->endBlock();
        mono[static_cast<std::size_t>(n)] = l[0] + r[0];
    }
    const int window = static_cast<int>(0.25 * rate);
    const auto correlation = [&](int a, int b)
    {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (int i = 0; i < window; ++i)
        {
            const double x = mono[static_cast<std::size_t>(a * spacing + i)];
            const double y = mono[static_cast<std::size_t>(b * spacing + i)];
            ab += x * y; aa += x * x; bb += y * y;
        }
        return ab / std::sqrt(aa * bb);
    };
    for (const int parity : { 0, 1 })
    {
        double sum = 0.0;
        int pairs = 0;
        for (int a = 2 + parity; a < strokes; a += 2)
            for (int b = a + 2; b < strokes; b += 2) { sum += correlation(a, b); ++pairs; }
        std::printf("strumScatter %d, %s: mean correlation %.3f over %d pairs\n", scatter ? 1 : 0,
                    parity == 0 ? "downstrokes" : "upstrokes", sum / pairs, pairs);
    }
}
