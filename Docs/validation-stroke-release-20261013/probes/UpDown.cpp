// One note, fresh engine, as a downstroke and as an upstroke under Pick:
// stereo RMS over its first 300 ms at the plug-in's defaults.
#include "DSP/AcustraEngine.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
double render(int note, bool up, int channel)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters p;
    p.picking = acustra::PickingTechnique::Pick;
    p.outputGain = 1.0f; p.room = 0.0f; p.piezoMix = 0.2f; p.releaseNoise = 0.1f;
    e->setParameters(p);
    e->prepare(48000.0, 64);
    e->setStringPerChannelMode(channel > 0);
    e->noteOn(note, 0.75f, channel > 0 ? channel : 1, 0, false, up);
    std::vector<float> l(14400), r(14400);
    e->process(l.data(), r.data(), 14400);
    double s = 0.0;
    for (std::size_t i = 0; i < l.size(); ++i) s += 0.5 * (l[i] * l[i] + r[i] * r[i]);
    return 10.0 * std::log10(s / l.size());
}
int main()
{
    const int open[6] = { 40, 45, 50, 55, 59, 64 };
    for (int string = 0; string < 6; ++string)
    {
        std::printf("string %d:", string);
        double sum = 0.0; int count = 0;
        for (int fret = 0; fret <= 12; fret += 2)
        {
            const int note = open[string] + fret;
            const double d = render(note, true, string + 1) - render(note, false, string + 1);
            std::printf(" %+5.1f", d);
            sum += d; ++count;
        }
        std::printf("   mean %+5.2f dB (up minus down, frets 0-12 by 2)\n", sum / count);
    }
}
