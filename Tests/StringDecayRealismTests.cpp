#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct State
    {
        double inharmonicity;
        // The bending section's g, a1, a2.
        double gain;
        double a1;
        double a2;
        // The constant-loss section's g, a1, a2, n2.
        double constantGain;
        double constantA1;
        double constantA2;
        double constantN2;
    };
    static int solveCursor(const AcustraEngine& e)
    {
        return e.nextDispersionSolve_;
    }
    static State bendingSection(int string, int fret, double rate, float bend)
    {
        auto e = std::make_unique<AcustraEngine>();
        auto calibration = fittedPhysicalCalibration;
        // Every string's bending section at one factor, wound and plain, with
        // no winding friction to take from a wound string's.
        calibration.steelWoundFrictionLoss = 0.0f;
        calibration.steelPlainBendingLoss = 0.006f;
        calibration.steelWoundBendingLoss = 0.006f;
        e->setPhysicalCalibration(calibration);
        e->prepare(rate, 64);
        e->setLowerZoneMemberCount(15);
        auto& v = e->voices_[static_cast<std::size_t>(string)];
        v.played = v.keyDown = true;
        v.mpeMember = true;
        v.midiChannel = 2;
        v.midiNote = v.openMidi + fret;
        v.fret = fret;
        v.attackPitchCents = 0.0f;
        e->setPitchBend(bend, 2);
        e->configureVoice(v, string, v.midiNote, false);
        const auto& loop = v.loops[0];
        return { v.dispersionDesignInharmonicity, loop.bendingLossGain,
                 loop.bendingLossA1, loop.bendingLossA2, loop.constantLossGain,
                 loop.constantLossA1, loop.constantLossA2, loop.constantLossN2 };
    }
};
}

namespace
{
int failures = 0;
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void testBendingLossFollowsTheSoundingFrequency()
{
    // Valette's viscoelastic loss: 1/Q_n = eta B n^2/(1+B n^2).
    // Its physical decay is pi*f_n/Q_n nepers/second. A wheel that raises
    // the note must still meet that law at the partials now being heard,
    // at every host rate; evaluating the old-frequency filter further up
    // its curve adds a spurious loss to an otherwise unchanged string.
    // Every string bends on steel at the plain factor since 2026-10-10 (a
    // wound string on its core; it took a factor of its own, 0.1 here,
    // before). The constant loss angle beside it (constantLossSection),
    // dislocation here, 1/Q = steel.frequencyLossScale / 5500, must follow
    // the sounding frequency too, within the 20% its two relaxations keep
    // over 5 to 250 dB/s.
    constexpr int openNotes[] { 40, 45, 50, 55, 59, 64 };
    const double pi = std::acos(-1.0);
    const double dislocation
        = acustra::fittedPhysicalCalibration.steel.frequencyLossScale / 5500.0;
    double worst = 0.0;
    double worstConstant = 0.0;
    int checked = 0;
    int checkedConstant = 0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const int string : { 0, 1, 4, 5 })
            for (const int fret : { 0, 5 })
                for (const float bend : { -2.0f, 0.0f, 2.0f, 4.0f })
                {
                    const auto section = acustra::AcustraEngineTestAccess::bendingSection(
                        string, fret, rate, bend);
                    const double fundamental = 440.0 * std::exp2(
                        (openNotes[string] + fret + bend - 69.0) / 12.0);
                    for (int partial = 1; partial < 400; ++partial)
                    {
                        const double bending = section.inharmonicity * partial * partial;
                        const double partialFrequency = fundamental * partial
                            * std::sqrt((1.0 + bending) / (1.0 + section.inharmonicity));
                        if (partialFrequency > 0.3 * rate)
                            break;
                        const double omega = 2.0 * pi * partialFrequency / rate;
                        const std::complex<double> z = std::polar(1.0, -omega);
                        const double lawDbPerSecond = (20.0 / std::log(10.0))
                            * pi * partialFrequency * 0.006 * bending / (1.0 + bending);
                        if (lawDbPerSecond >= 20.0 && lawDbPerSecond <= 160.0)
                        {
                            const double magnitude = section.gain / std::abs(
                                1.0 + section.a1 * z + section.a2 * z * z);
                            const double actualDbPerSecond = -20.0 * std::log10(magnitude)
                                * fundamental;
                            const double error = std::abs(actualDbPerSecond / lawDbPerSecond - 1.0);
                            worst = std::max(worst, error);
                            ++checked;
                        }
                        const double constantDbPerSecond = (20.0 / std::log(10.0))
                            * pi * partialFrequency * dislocation;
                        if (partial >= 2 && constantDbPerSecond >= 5.0
                            && constantDbPerSecond <= 250.0)
                        {
                            // The section as the loop runs it (StringLoop::advance),
                            // 1 - (1 - z^-1)(g0 + g1 z^-1)/A, g0 = 1 - g, g1 = n2 - a2.
                            const double g0 = static_cast<double>(
                                1.0f - static_cast<float>(section.constantGain));
                            const double g1 = static_cast<double>(
                                static_cast<float>(section.constantN2)
                                - static_cast<float>(section.constantA2));
                            const double magnitude = std::abs(1.0 - (1.0 - z) * (g0 + g1 * z)
                                / (1.0 + section.constantA1 * z + section.constantA2 * z * z));
                            const double actualDbPerSecond = -20.0 * std::log10(magnitude)
                                * fundamental;
                            worstConstant = std::max(worstConstant,
                                std::abs(actualDbPerSecond / constantDbPerSecond - 1.0));
                            ++checkedConstant;
                        }
                    }
                }
    std::cout << "Bent-string partial loss: " << checked << " bending-law comparisons, "
              << 100.0 * worst << "% worst error; " << checkedConstant
              << " constant-loss comparisons, " << 100.0 * worstConstant
              << "% worst error\n";
    expect(checked > 200, "insufficient partials reached the meaningful decay band");
    expect(worst < 0.12, "pitch bend added spurious upper-partial loss");
    expect(checkedConstant > 200, "insufficient partials reached the constant-loss band");
    expect(worstConstant < 0.20, "pitch bend moved the constant loss off its law");
}

void testHighSlidesKeepTheExpensiveFitBounded()
{
    using Access = acustra::AcustraEngineTestAccess;
    constexpr int notes[] { 40, 45, 50, 55, 59, 64 };
    for (const double rate : { 48000.0, 96000.0 })
    {
        auto e = std::make_unique<acustra::AcustraEngine>();
        e->prepare(rate, 64);
        e->setStringPerChannelMode(true);
        for (int string = 0; string < 6; ++string)
            e->noteOn(notes[string] + 20, 0.8f, string + 1);
        std::array<float, 64> left {}, right {};
        std::vector<double> callbackMicroseconds;
        int solves = 0;
        bool finite = true;
        constexpr int frames = 512;
        for (int frame = 0; frame < frames; ++frame)
        {
            const float slide = 36.0f + 12.0f * frame / (frames - 1.0f);
            for (int string = 0; string < 6; ++string)
                e->setPitchBend(slide, string + 1);
            const int before = Access::solveCursor(*e);
            const auto start = std::chrono::steady_clock::now();
            e->process(left.data(), right.data(), 64);
            callbackMicroseconds.push_back(std::chrono::duration<double, std::micro>(
                std::chrono::steady_clock::now() - start).count());
            // A block contains at most twelve solves, so the 64-slot ring
            // cursor cannot wrap unseen; count misses without a DSP counter.
            solves += (Access::solveCursor(*e) - before + 64) % 64;
            for (int n = 0; n < 64; ++n)
                finite = finite && std::isfinite(left[n]) && std::isfinite(right[n]);
        }
        std::sort(callbackMicroseconds.begin(), callbackMicroseconds.end());
        std::cout << "Six-string high slide at " << rate << " Hz: " << solves
                  << " fit misses, callback p95 " << callbackMicroseconds[frames * 95 / 100]
                  << " us (64-frame budget " << 64.0e6 / rate << " us)\n";
        // Once B saturates outside the physical fretboard, a pitch-only
        // movement needs the inexpensive loss/phase update, not thousands
        // of iterative fits. This deterministic bound is portable across
        // hardware; reported callback times remain diagnostic.
        expect(solves <= 12, "high slides repeatedly ran the iterative dispersion fit");
        expect(finite, "bounded high-slide design produced non-finite output");
    }
}
}

int main()
{
    testBendingLossFollowsTheSoundingFrequency();
    testHighSlidesKeepTheExpensiveFitBounded();
    if (failures == 0)
        std::cout << "All string decay realism tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
