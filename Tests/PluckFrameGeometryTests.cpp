// Recover the released triangle's kink from its waveform. The physical
// contact fraction must survive conversion to a shorter delay-line frame,
// including MPE bridgeward/neckward contacts and held tension bends.
#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Release
    {
        double point {}, period {};
        std::array<std::vector<float>, 2> waves;
    };
    static Release release(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        Release result;
        result.point = voice.pluckPoint;
        result.period = voice.contactPeriodSamples;
        for (std::size_t plane = 0; plane < result.waves.size(); ++plane)
        {
            const auto& loop = voice.loops[plane];
            const int length = std::max(8, static_cast<int>(std::round(loop.currentDelay)));
            const auto mask = static_cast<unsigned>(loop.delay.size()) - 1u;
            for (int sample = 0; sample < length; ++sample)
                result.waves[plane].push_back(loop.delay[
                    static_cast<unsigned>(loop.writeIndex - sample - 1) & mask]);
        }
        return result;
    }
};
} // namespace acustra

namespace
{
int failures = 0;
double kinkSample(const std::vector<float>& wave)
{
    const auto peak = std::max_element(wave.begin(), wave.end(),
        [](float a, float b) { return std::abs(a) < std::abs(b); }) - wave.begin();
    const double sign = wave[static_cast<std::size_t>(peak)] < 0.0f ? -1.0 : 1.0;
    const int radius = std::max(3, static_cast<int>(wave.size() / 8));
    double mass = 0.0, moment = 0.0;
    // Symmetric spatial smoothing preserves the curvature centroid. Avoid
    // the opposite endpoint curvature; no stored model-position field is read.
    for (int i = std::max(1, static_cast<int>(peak) - radius);
         i <= std::min(static_cast<int>(wave.size()) - 2, static_cast<int>(peak) + radius); ++i)
    {
        const double curvature = std::max(0.0, sign *
            (2.0 * wave[static_cast<std::size_t>(i)]
             - wave[static_cast<std::size_t>(i - 1)]
             - wave[static_cast<std::size_t>(i + 1)]));
        mass += curvature;
        moment += i * curvature;
    }
    return mass > 0.0 ? moment / mass : -1.0;
}
} // namespace

int main()
{
    using Engine = acustra::AcustraEngine;
    int above = 0, below = 0, cases = 0, unresolved = 0;
    double worst = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int shape = 0; shape < 4; ++shape)
        {
            auto engine = std::make_unique<Engine>();
            auto calibration = acustra::fittedPhysicalCalibration;
            // Isolate the displacement triangle's location. Remove release
            // velocity/slip and the post-release tension transient, keeping
            // each construction's physical stiffness and complete loop phase.
            calibration.pickReleaseVelocityShare = 0.0f;
            calibration.pickEdgeRadiusMetres = 0.0f;
            calibration.steel.apertureScale = 0.35f;
            calibration.steelDisplacementScaleMetres = 0.0f;
            engine->setPhysicalCalibration(calibration);
            auto realism = engine->performanceRealism();
            realism.frettingPitch = false;
            engine->setPerformanceRealism(realism);
            acustra::EngineParameters parameters;
            parameters.shape = static_cast<acustra::BodyShape>(shape);
            parameters.picking = acustra::PickingTechnique::Pick;
            parameters.pluckPosition = 0.0f;
            engine->setParameters(parameters);
            engine->prepare(rate, 64);
            engine->setStringPerChannelMode(true);
            engine->setLowerZoneMemberCount(15);
            constexpr int open[] { 40, 45, 50, 55, 59, 64 };
            for (float timbre : { 0.0f, 1.0f })
                for (float bend : { 0.0f, 12.0f })
                    for (int string = 0; string < 6; ++string)
                        for (int fret : { 0, 10, 20 })
                        {
                            engine->reset();
                            engine->setMpeTimbre(timbre, string + 1);
                            engine->setPitchBend(bend, string + 1);
                            engine->noteOn(open[string] + fret, 0.85f, string + 1);
                            const auto release = acustra::AcustraEngineTestAccess::release(*engine, string);
                            for (std::size_t plane = 0; plane < release.waves.size(); ++plane)
                            {
                                for (float sample : release.waves[plane])
                                    if (!std::isfinite(sample))
                                        ++failures;
                                const double expected = release.point + (plane == 0 ? -0.006 : 0.009);
                                const double expectedSample = expected * release.period;
                                // At fewer than three samples from a string
                                // endpoint the Gaussian/bending corner and
                                // endpoint curvature overlap. A separated
                                // curvature centroid is not an oracle there.
                                // Keep those audible short-loop cases in the
                                // render/rate checks, not this geometric fit.
                                if (expectedSample < 3.0
                                    || release.waves[plane].size() - expectedSample < 3.0)
                                {
                                    ++unresolved;
                                    continue;
                                }
                                const double mapped = expected * release.period / release.waves[plane].size();
                                const double observed = kinkSample(release.waves[plane]) / release.period;
                                above += mapped > 0.60;
                                below += mapped < 0.05;
                                const double error = std::abs(observed - expected);
                                worst = std::max(worst, error);
                                if (error > 0.0005)
                                {
                                    if (failures < 12)
                                        std::fprintf(stderr, "Kink mismatch rate=%.0f shape=%d string=%d fret=%d timbre=%.0f bend=%.0f plane=%zu: %.9g vs %.9g\n",
                                            rate, shape, string, fret, timbre, bend, plane, observed, expected);
                                    ++failures;
                                }
                                ++cases;
                            }
                        }
        }
    if (above < 10 || below < 10)
    {
        std::fprintf(stderr, "Frame-clamp coverage missing: %d above, %d below\n", above, below);
        ++failures;
    }
    std::printf("Recovered release kink: %d resolved cases (%d overlapping endpoints), worst %.9g of string, %d above and %d below old numerical bounds, %d failures\n",
                cases, unresolved, worst, above, below, failures);
    return failures == 0 ? 0 : 1;
}
