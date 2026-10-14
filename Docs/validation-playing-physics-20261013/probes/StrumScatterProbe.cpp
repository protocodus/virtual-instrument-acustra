// Each string's bridge reaction force, a stand-in for a hex pickup channel,
// through a picked comping pattern: eight chords of four to six strings,
// strummed every 0.3 s at 100 BPM with the keys released 20 ms before the
// next stroke, velocities 70-110 from a fixed seed, at 44.1 kHz. Built
// against a copy of Source/DSP with bridge-force-probe.patch applied, which
// stores the force each sample and counts each voice's plucks; the probe
// writes the forces (float32, six interleaved channels) and each pluck as
// "stroke string sample". Usage: StrumScatterProbe forces.f32 plucks.txt [strums per chord]
#include "DSP/AcustraPerformer.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>
namespace acustra { struct AcustraEngineTestAccess {
    static auto& voice(AcustraEngine& e, int s) { return e.voices_[static_cast<std::size_t>(s)]; } }; }
using A = acustra::AcustraEngineTestAccess;
int main(int argc, char** argv)
{
    if (argc < 3) return 1;
    const double rate = 44100.0;
    auto p = std::make_unique<acustra::Performer>();
    acustra::EngineParameters par; par.picking = acustra::PickingTechnique::Pick; p->setParameters(par);
    if (argc > 4) { acustra::PerformanceRealism realism; realism.strumScatter = std::atoi(argv[4]) != 0; p->engine().setPerformanceRealism(realism); }
    p->prepare(rate, 64); p->setTempoBpm(100.0);
    const std::vector<std::vector<int>> chords {
        {40, 47, 52, 56, 59, 64}, {45, 52, 57, 61, 64}, {50, 57, 62, 66},
        {43, 47, 50, 55, 59, 67}, {48, 52, 55, 60, 64}, {52, 59, 64, 67}, {45, 52, 57, 60, 64}, {40, 47, 52, 55, 59, 64}};
    const int strumsPerChord = argc > 3 ? std::atoi(argv[3]) : 8;
    const int spacing = static_cast<int>(0.3 * rate);
    const int total = static_cast<int>(chords.size()) * strumsPerChord * spacing + static_cast<int>(rate);
    std::vector<float> force(static_cast<std::size_t>(total) * 6);
    std::FILE* events = std::fopen(argv[2], "w");
    std::array<long long, 6> plucks {};
    std::array<float, 1> l {}, r {};
    std::srand(7);
    int stroke = -1;
    for (int n = 0; n < total; ++n)
    {
        p->beginBlock(l.data(), r.data(), 1);
        const int k = n / spacing, phase = n % spacing;
        if (k < static_cast<int>(chords.size()) * strumsPerChord)
        {
            const auto& chord = chords[static_cast<std::size_t>(k / strumsPerChord)];
            if (phase == spacing - static_cast<int>(0.02 * rate))
                for (int note : chord) p->noteOff(0, 1, note, 64);
            if (phase == 0)
            {
                const int velocity = 70 + std::rand() % 41;
                for (int note : chord) p->noteOn(0, 1, note, velocity);
                ++stroke;
            }
        }
        p->endBlock();
        for (int s = 0; s < 6; ++s)
        {
            auto& v = A::voice(p->engine(), s);
            force[static_cast<std::size_t>(n) * 6 + static_cast<std::size_t>(s)] = v.probeForce;
            if (v.probePlucks != plucks[static_cast<std::size_t>(s)])
            {
                plucks[static_cast<std::size_t>(s)] = v.probePlucks;
                std::fprintf(events, "%d %d %d\n", stroke, s, n);
            }
        }
    }
    std::fclose(events);
    std::FILE* out = std::fopen(argv[1], "wb");
    std::fwrite(force.data(), sizeof(float), force.size(), out);
    std::fclose(out);
}
