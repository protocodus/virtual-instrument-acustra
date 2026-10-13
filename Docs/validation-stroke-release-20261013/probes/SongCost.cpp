// Paired callback cost of all three mechanisms on a played passage (the
// review's song shape, looped): switches on against off, one build.
#include "DSP/AcustraPerformer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
struct Ev { int frame, kind, note, vel; };
std::vector<Ev> song(double rate)
{
    std::vector<Ev> ev;
    const double beat = 60.0 / 96.0, eighth = beat / 2;
    auto note = [&](double t, double len, int n, int v) {
        ev.push_back({ int(std::lround(t * rate)), 1, n, v });
        ev.push_back({ int(std::lround((t + len) * rate)), 0, n, 64 }); };
    auto chord = [&](double t, double len, std::vector<int> c, int v) { for (int n : c) note(t, len, n, v); };
    double t = 0.1;
    for (int rep = 0; rep < 3; ++rep)
        for (auto [bass, c] : std::vector<std::pair<int, std::vector<int>>>{
                 { 43, { 47, 50, 55, 59, 67 } }, { 48, { 52, 55, 60, 64 } } })
        {
            note(t, 0.95 * beat, bass, 100);
            chord(t + beat, eighth - 0.02, c, 92); chord(t + beat + eighth, eighth - 0.02, c, 78);
            note(t + 2 * beat, 0.95 * beat, bass + 7, 96);
            chord(t + 3 * beat, eighth - 0.02, c, 92); chord(t + 3 * beat + eighth, eighth - 0.02, c, 78);
            t += 4 * beat;
            for (int n : { 62, 64, 66, 67 }) { note(t, 0.9 * eighth, n, 94); t += eighth; }
        }
    std::stable_sort(ev.begin(), ev.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    return ev;
}
std::vector<double> run(bool on, double rate, int block)
{
    auto p = std::make_unique<acustra::Performer>();
    acustra::PerformanceRealism r; r.strokeDepth = r.alternatePicking = r.releasePitchFall = on;
    p->engine().setPerformanceRealism(r);
    acustra::EngineParameters par; par.picking = acustra::PickingTechnique::Pick;
    par.outputGain = 1.0f; par.room = 0.5f; par.piezoMix = 0.2f; par.releaseNoise = 0.1f;
    p->setParameters(par); p->prepare(rate, block); p->engine().setPortObserversEnabled(false);
    p->setTempoBpm(96.0);
    auto ev = song(rate);
    const int frames = int(14.0 * rate);
    std::vector<float> l(block), rr(block);
    std::vector<double> ns; std::size_t next = 0;
    for (int f = 0; f < frames; f += block)
    {
        const auto t0 = std::chrono::steady_clock::now();
        p->beginBlock(l.data(), rr.data(), block);
        while (next < ev.size() && ev[next].frame < f + block)
        {
            auto& e = ev[next++];
            if (e.kind) p->noteOn(e.frame - f, 1, e.note, e.vel); else p->noteOff(e.frame - f, 1, e.note, e.vel);
        }
        p->endBlock();
        ns.push_back(std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count());
    }
    return ns;
}
double pct(std::vector<double> v, double q) { std::sort(v.begin(), v.end()); return v[size_t(q * (v.size() - 1))]; }
int main()
{
    for (auto [rate, block] : { std::pair{ 48000.0, 64 }, std::pair{ 96000.0, 32 } })
    {
        std::vector<double> onMed, offMed, on, off;
        for (int pair = 0; pair < 8; ++pair)
            for (int k = 0; k < 2; ++k)
            {
                const bool o = (k == 0) == (pair % 2 == 0);
                auto ns = run(o, rate, block);
                (o ? onMed : offMed).push_back(pct(ns, 0.5));
                (o ? on : off).insert((o ? on : off).end(), ns.begin(), ns.end());
            }
        std::printf("%.0f Hz / %d (budget %.1f us): median of run medians on %.2f off %.2f (%+.1f%%); p95 on %.2f off %.2f; p99 on %.2f off %.2f; max on %.1f off %.1f\n",
            rate, block, 1e6 * block / rate, pct(onMed, .5) / 1e3, pct(offMed, .5) / 1e3, 100 * (pct(onMed, .5) / pct(offMed, .5) - 1),
            pct(on, .95) / 1e3, pct(off, .95) / 1e3, pct(on, .99) / 1e3, pct(off, .99) / 1e3, pct(on, 1.0) / 1e3, pct(off, 1.0) / 1e3);
    }
}
