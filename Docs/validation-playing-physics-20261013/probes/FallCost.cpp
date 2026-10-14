// Paired callback cost of the release fall: one build, the switch alone.
// The 2026-10-13 probe of the same name with each run's mean added: the
// median callback over the window moves with whether the fall is active in
// more or fewer than half of its callbacks, the mean with its whole cost.
#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using clock_type = std::chrono::steady_clock;

struct Case { double rate; int block; };

// Returns per-callback nanoseconds over the 0.45 s after the damping begins.
std::vector<double> run(bool fall, const Case& c)
{
    auto e = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.releasePitchFall = fall;
    e->setPerformanceRealism(realism);
    acustra::EngineParameters p;
    p.outputGain = 1.0f; p.room = 0.5f; p.piezoMix = 0.2f; p.releaseNoise = 0.1f;
    e->setParameters(p);
    e->prepare(c.rate, c.block);
    e->setPortObserversEnabled(false);
    e->setTempoBpm(120.0);
    e->setStringPerChannelMode(true);
    std::vector<float> l(static_cast<std::size_t>(c.block)), r(l.size());
    const int chord[6] = { 41, 48, 53, 57, 60, 65 }; // F barre, every string fretted
    for (int s = 0; s < 6; ++s) e->noteOn(chord[s], 0.8f, s + 1);
    const int hold = static_cast<int>(0.5 * c.rate) / c.block;
    for (int b = 0; b < hold; ++b) e->process(l.data(), r.data(), c.block);
    for (int s = 0; s < 6; ++s) e->noteOff(chord[s], s + 1);
    const int window = static_cast<int>(std::ceil(0.0625 * c.rate) + 1) / c.block + 1;
    for (int b = 0; b < window; ++b) e->process(l.data(), r.data(), c.block);
    const int timed = static_cast<int>(0.45 * c.rate) / c.block;
    std::vector<double> ns;
    ns.reserve(static_cast<std::size_t>(timed));
    for (int b = 0; b < timed; ++b)
    {
        const auto t0 = clock_type::now();
        e->process(l.data(), r.data(), c.block);
        const auto t1 = clock_type::now();
        ns.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count());
    }
    return ns;
}

double percentile(std::vector<double> v, double q)
{
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(q * (v.size() - 1))];
}

int main()
{
    for (const Case c : { Case { 48000.0, 64 }, Case { 96000.0, 32 } })
    {
        std::vector<double> on, off, onMedian, offMedian, onMean, offMean;
        for (int pair = 0; pair < 40; ++pair)
        {
            const bool onFirst = pair % 2 == 0;
            for (int k = 0; k < 2; ++k)
            {
                const bool fall = (k == 0) == onFirst;
                auto ns = run(fall, c);
                (fall ? onMedian : offMedian).push_back(percentile(ns, 0.5));
                double sum = 0.0;
                for (const double v : ns) sum += v;
                (fall ? onMean : offMean).push_back(sum / static_cast<double>(ns.size()));
                auto& all = fall ? on : off;
                all.insert(all.end(), ns.begin(), ns.end());
            }
        }
        const double budget = 1.0e9 * c.block / c.rate;
        std::printf("%.0f Hz / %d frames (budget %.1f us): median of run medians on %.2f us, off %.2f us (%+.1f%%); "
                    "pooled p95 on %.2f us, off %.2f us; p99 on %.2f, off %.2f\n",
                    c.rate, c.block, budget / 1000.0,
                    percentile(onMedian, 0.5) / 1000.0, percentile(offMedian, 0.5) / 1000.0,
                    100.0 * (percentile(onMedian, 0.5) / percentile(offMedian, 0.5) - 1.0),
                    percentile(on, 0.95) / 1000.0, percentile(off, 0.95) / 1000.0,
                    percentile(on, 0.99) / 1000.0, percentile(off, 0.99) / 1000.0);
        std::printf("    median of run means on %.2f us, off %.2f us (%+.1f%%)\n",
                    percentile(onMean, 0.5) / 1000.0, percentile(offMean, 0.5) / 1000.0,
                    100.0 * (percentile(onMean, 0.5) / percentile(offMean, 0.5) - 1.0));
    }
}
