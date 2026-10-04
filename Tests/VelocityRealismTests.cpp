// The release's brightness follows the actual stroke, while its reference
// remains the nominal full-velocity contact. No fitted parameters are changed.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Release
    {
        double pole, reference, position, burst, displacement, brightness;
    };
    static Release inspect(AcustraEngine& engine)
    {
        const auto& voice = engine.voices_[0];
        const auto& loop = voice.loops[0];
        const int length = static_cast<int>(std::round(loop.currentDelay));
        const auto at = [&] (int n)
        { return double(loop.delay[static_cast<std::size_t>(AcustraEngine::maximumDelaySamples - n - 1)]); };
        double mean = 0.0, variance = 0.0, slopes = 0.0;
        for (int n = 0; n < length; ++n) mean += at(n);
        mean /= length;
        for (int n = 0; n < length; ++n)
        {
            variance += (at(n) - mean) * (at(n) - mean);
            const double d = at(n) - at((n + length - 1) % length);
            slopes += d * d;
        }
        // The release step is the static saddle force shed by the hand;
        // recover its held displacement using the triangle's steep flank.
        const double displacement = voice.releaseStepRise
            * voice.releaseShapePosition[0] * length
            / (0.8 * std::sqrt(voice.polarisationMix));
        return { voice.releaseSlipPole, voice.releaseReferencePole,
                 voice.pluckPoint, voice.excitationEnvelope, displacement,
                 length * length * slopes / std::max(variance, 1.0e-30) };
    }
    static void seed(AcustraEngine& engine, std::uint32_t seed)
    { engine.voices_[0].randomState = seed; }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{ if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

auto fresh(double rate, acustra::PickingTechnique picking,
           float transientGain = 0.0f)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    auto calibration = acustra::fittedPhysicalCalibration;
    calibration.pickTransientGain = transientGain;
    engine->setPhysicalCalibration(calibration);
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    engine->setParameters(parameters);
    return engine;
}

void testNominalReferenceAndForceSensitiveBrightness()
{
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                  acustra::PickingTechnique::Thumb })
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            std::vector<Access::Release> releases;
            double smallestReference = 1.0e30, largestReference = 0.0;
            for (unsigned take = 1; take <= 48; ++take)
            {
                auto engine = fresh(rate, technique);
                Access::seed(*engine, 0x6d2b79f5u * take);
                engine->beginStrum();
                engine->noteOn(40, 0.12f, 1, 0, true);
                const auto state = Access::inspect(*engine);
                expect(state.pole > state.reference && state.reference > 0.0,
                       "soft stroke did not have a slower release than its reference");
                // r/u divided by p(1-p) is constant at one nominal force:
                // u = c*y/[2*L*p(1-p)]. A reference that includes the
                // random stroke gain violates this invariant.
                const double nominal = -1.0 / std::log(state.reference)
                    / (state.position * (1.0 - state.position));
                smallestReference = std::min(smallestReference, nominal);
                largestReference = std::max(largestReference, nominal);
                releases.push_back(state);
            }
            expect(largestReference / smallestReference < 1.00002,
                   "full-velocity reference followed random stroke intensity");
            std::sort(releases.begin(), releases.end(), [] (const auto& a, const auto& b)
                { return a.displacement < b.displacement; });
            double weak = 0.0, strong = 0.0;
            for (std::size_t n = 0; n < 12; ++n)
            {
                weak += releases[n].brightness;
                strong += releases[releases.size() - n - 1].brightness;
            }
            expect(strong > 1.04 * weak,
                   "firmer same-velocity strokes did not excite brighter string spectra");
            std::cout << "Force-sensitive release rate=" << rate
                      << " reference spread=" << largestReference / smallestReference
                      << " strong/weak spectral moment=" << strong / weak << '\n';
        }
}

void testPickImpactTracksTheStringStroke()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (float velocity : { 0.12f, 0.40f, 0.80f })
        {
            double minimum = 1.0e30, maximum = 0.0;
            for (unsigned take = 1; take <= 24; ++take)
            {
                // A nonzero public calibration exercises the otherwise
                // disabled impact path without changing shipping defaults.
                auto engine = fresh(rate, acustra::PickingTechnique::Pick, 0.125f);
                Access::seed(*engine, 0x9e3779b9u * take);
                engine->beginStrum();
                engine->noteOn(40, velocity, 1, 0, true);
                const auto state = Access::inspect(*engine);
                const double ratio = state.burst / state.displacement;
                minimum = std::min(minimum, ratio);
                maximum = std::max(maximum, ratio);
                std::array<float, 128> left {}, right {};
                engine->process(left.data(), right.data(), 128);
                expect(std::all_of(left.begin(), left.end(), [] (float x)
                    { return std::isfinite(x); }), "picked force render was nonfinite");
            }
            std::cout << "Pick rate=" << rate << " v=" << velocity << " ratio spread=" << maximum / minimum << "\n";
            expect(maximum / minimum < 1.00002,
                   "pick click and held string received different stroke gains");
        }
}

void testFullVelocityNominalReleaseRemainsUnfiltered()
{
    for (const auto technique : { acustra::PickingTechnique::Finger,
                                  acustra::PickingTechnique::Thumb })
    {
        auto engine = fresh(48000.0, technique);
        engine->noteOn(40, 1.0f, 1);
        const auto state = Access::inspect(*engine);
        expect(state.pole == 0.0 && state.reference == 0.0,
               "nominal full-velocity release no longer keeps its approved shape");
    }
    expect(acustra::AcustraEngine::outputLatencySamples() == 7,
           "release correction changed output latency");
}
}

int main()
{
    testNominalReferenceAndForceSensitiveBrightness();
    testPickImpactTracksTheStringStroke();
    testFullVelocityNominalReleaseRemainsUnfiltered();
    return failures == 0 ? 0 : 1;
}
