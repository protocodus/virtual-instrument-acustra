// Paired callback cost of the 2026-10-13/14 mechanisms on played passages,
// switches on against off, one build: a picked song (full, open-voiced and
// three-string strums, power chords, single notes) and a fingerstyle one
// (bass notes under three- and four-string chords), each looped for 14 s.
#include "DSP/AcustraPerformer.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
struct Ev { int frame, kind, note, vel; };
std::vector<Ev> passage(double rate, bool finger)
{
    std::vector<Ev> ev;
    const double beat = 60.0 / 96.0, eighth = beat / 2;
    auto note = [&](double t, double len, int n, int v) {
        ev.push_back({ int(std::lround(t * rate)), 1, n, v });
        ev.push_back({ int(std::lround((t + len) * rate)), 0, n, 64 }); };
    auto chord = [&](double t, double len, std::vector<int> c, int v) { for (int n : c) note(t, len, n, v); };
    double t = 0.1;
    for (int rep = 0; rep < 3; ++rep)
    {
        if (finger)
            for (auto [bass, c] : std::vector<std::pair<int, std::vector<int>>>{
                     { 45, { 57, 60, 64 } }, { 50, { 57, 62, 65 } }, { 40, { 52, 56, 62, 64 } } })
            {
                note(t, 0.95 * beat, bass, 88);
                chord(t + eighth, beat - 0.03, c, 80);
                note(t + 2 * beat, 0.95 * beat, bass + 7, 84);
                chord(t + 2 * beat + eighth, 0.5 * beat, c, 78);
                chord(t + 3 * beat + eighth, 0.5 * beat - 0.03, c, 74);
                t += 4 * beat;
            }
        else
            for (auto [bass, c] : std::vector<std::pair<int, std::vector<int>>>{
                     { 43, { 47, 50, 55, 59, 67 } }, { 40, { 59, 64 } }, { 48, { 55, 60, 64 } } })
            {
                note(t, 0.95 * beat, bass, 100);
                chord(t + beat, eighth - 0.02, c, 92); chord(t + beat + eighth, eighth - 0.02, c, 78);
                note(t + 2 * beat, 0.95 * beat, bass + 7, 96);
                chord(t + 3 * beat, eighth - 0.02, c, 92); chord(t + 3 * beat + eighth, eighth - 0.02, c, 78);
                t += 4 * beat;
                for (auto pair : std::vector<std::vector<int>>{ { 45, 52 }, { 47, 54 }, { 40, 59, 64 }, { 43, 50 } })
                { chord(t, 0.9 * eighth, pair, 94); t += eighth; }
            }
    }
    std::stable_sort(ev.begin(), ev.end(), [](auto& a, auto& b) { return a.frame < b.frame; });
    return ev;
}
std::vector<double> run(bool on, bool finger, double rate, int block)
{
    auto p = std::make_unique<acustra::Performer>();
    acustra::PerformanceRealism r;
    r.fingerChords = r.mutedStrings = r.strumScatter = r.twoStringStrokes = r.strokeSpanLaw = r.strokeEdges = on;
    p->engine().setPerformanceRealism(r);
    acustra::EngineParameters par;
    par.picking = finger ? acustra::PickingTechnique::Finger : acustra::PickingTechnique::Pick;
    par.outputGain = 1.0f; par.room = 0.5f; par.piezoMix = 0.2f; par.releaseNoise = 0.1f;
    p->setParameters(par); p->prepare(rate, block); p->engine().setPortObserversEnabled(false);
    p->setTempoBpm(96.0);
    auto ev = passage(rate, finger);
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
    for (const bool finger : { false, true })
        for (auto [rate, block] : { std::pair{ 48000.0, 64 }, std::pair{ 96000.0, 32 } })
        {
            std::vector<double> onMed, offMed, on, off;
            run(true, finger, rate, block); // warm-up
            for (int pair = 0; pair < 8; ++pair)
                for (int k = 0; k < 2; ++k)
                {
                    const bool o = (k == 0) == (pair % 2 == 0);
                    auto ns = run(o, finger, rate, block);
                    (o ? onMed : offMed).push_back(pct(ns, 0.5));
                    (o ? on : off).insert((o ? on : off).end(), ns.begin(), ns.end());
                }
            std::printf("%s, %.0f Hz / %d (budget %.1f us): median of run medians on %.2f off %.2f (%+.1f%%); p95 on %.2f off %.2f; p99 on %.2f off %.2f; max on %.1f off %.1f\n",
                finger ? "fingerstyle" : "picked", rate, block, 1e6 * block / rate,
                pct(onMed, .5) / 1e3, pct(offMed, .5) / 1e3, 100 * (pct(onMed, .5) / pct(offMed, .5) - 1),
                pct(on, .95) / 1e3, pct(off, .95) / 1e3, pct(on, .99) / 1e3, pct(off, .99) / 1e3,
                pct(on, 1.0) / 1e3, pct(off, 1.0) / 1e3);
        }
}
