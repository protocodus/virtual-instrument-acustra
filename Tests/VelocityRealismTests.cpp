// Finger uses the actual release slip; Thumb retains its nominal reference
// ratio. Both keep force-sensitive brightness without fitting new parameters.
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
        double pole, reference, position, displacement, brightness;
        double absolutePole;
    };
    static Release inspect(AcustraEngine& engine, int string = 0)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
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
            / (AcustraEngine::releaseStepShare * std::sqrt(voice.polarisationMix));
        // Independently predict the default-Touch 0.2 mm contact's r/u
        // release from observed held force and string geometry, rather than
        // consulting plectrumSlipPole or the initializer's pole metadata.
        const double speakingLength = voice.speakingLengthMetres;
        const double a = voice.pluckPoint * speakingLength;
        const double waveSpeed = 2.0 * 0.648 * 440.0
            * std::exp2((voice.openMidi - 69.0) / 12.0)
            * voice.bendImpedanceScale;
        const double heldMetres = displacement
            * engine.physicalCalibration_.steelDisplacementScaleMetres;
        const double releaseSpeed = 0.5 * waveSpeed * heldMetres
            * (1.0 / a + 1.0 / (speakingLength - a));
        const double absolutePole = std::exp(-releaseSpeed
            / (0.2e-3 * engine.sampleRate_));
        return { voice.releaseSlipPole, voice.releaseReferencePole,
                 voice.pluckPoint, displacement,
                 length * length * slopes / std::max(variance, 1.0e-30),
                 absolutePole };
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

auto fresh(double rate, acustra::PickingTechnique picking)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    engine->setParameters(parameters);
    return engine;
}

void testContactLawAndForceSensitiveBrightness()
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
                const bool finger = technique == acustra::PickingTechnique::Finger;
                if (finger)
                    expect(state.pole > 0.0 && state.reference == 0.0
                               && std::abs(state.pole - state.absolutePole) < 1.0e-6,
                           "Finger did not apply its physical release slip in full");
                else
                    expect(state.pole > state.reference && state.reference > 0.0,
                           "soft Thumb did not have a slower release than its reference");
                // tau*y/[p(1-p)] is constant for Finger's actual force;
                // tau/[p(1-p)] is constant for Thumb's nominal force.
                const double observedPole = finger ? state.pole : state.reference;
                const double nominal = -1.0 / std::log(observedPole)
                    / (state.position * (1.0 - state.position))
                    * (finger ? state.displacement : 1.0);
                smallestReference = std::min(smallestReference, nominal);
                largestReference = std::max(largestReference, nominal);
                releases.push_back(state);
            }
            expect(largestReference / smallestReference < 1.00002,
                   "release time violated its force/geometry invariant");
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
                      << " force/geometry spread=" << largestReference / smallestReference
                      << " strong/weak spectral moment=" << strong / weak << '\n';
        }
}

void testFullVelocityFingerStillSlipsAndThumbKeepsItsShape()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int string : { 0, 5 })
    {
        const int note = string == 0 ? 40 : 64;
        auto finger = fresh(rate, acustra::PickingTechnique::Finger);
        finger->noteOn(note, 1.0f, string + 1);
        const auto state = Access::inspect(*finger, string);
        expect(state.pole > 0.0 && state.reference == 0.0
                   && std::abs(state.pole - state.absolutePole) < 1.0e-6,
               "full-velocity Finger cancelled its absolute physical slip");
        auto thumb = fresh(rate, acustra::PickingTechnique::Thumb);
        thumb->noteOn(note, 1.0f, string + 1);
        const auto thumbState = Access::inspect(*thumb, string);
        expect(thumbState.pole == 0.0 && thumbState.reference == 0.0,
               "nominal full-velocity Thumb no longer keeps its approved shape");
    }
    expect(acustra::AcustraEngine::outputLatencySamples() == 7,
           "release correction changed output latency");
}
}

int main()
{
    testContactLawAndForceSensitiveBrightness();
    testFullVelocityFingerStillSlipsAndThumbKeepsItsShape();
    return failures == 0 ? 0 : 1;
}
