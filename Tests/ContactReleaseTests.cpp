// Independent force/geometry, filter and state contracts for the authored
// coupling between Finger's release time and its existing contact burst.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Contact
    {
        double predictedPole;
        float base, effective, colour, envelope, decay, force;
        std::array<float, AcustraEngine::maximumDelaySamples> wave;
    };
    static Contact inspect(const AcustraEngine& engine)
    {
        const auto& v = engine.voices_[0];
        const double period = std::round(v.loops[0].currentDelay);
        // Recover the held displacement from its independently stored
        // saddle-force step, then calculate F/(2Z)=c*y*(1/a+1/(L-a))/2.
        const double displacement = v.releaseStepRise * v.releaseShapePosition[0]
            * period / (0.8 * std::sqrt(v.polarisationMix));
        const double held = displacement
            * engine.physicalCalibration_.steelDisplacementScaleMetres;
        const double a = v.pluckPoint * v.speakingLengthMetres;
        const double c = 2.0 * 0.648 * 440.0
            * std::exp2((v.openMidi - 69.0) / 12.0) * v.bendImpedanceScale;
        const double speed = 0.5 * c * held
            * (1.0 / a + 1.0 / (v.speakingLengthMetres - a));
        const double radius = 0.2e-3 * std::exp2(2.0 * (0.58 - engine.parameters_.touch));
        return { std::exp(-speed / (radius * engine.sampleRate_)),
                 v.excitationCoefficient, v.excitationReleaseCoefficient,
                 v.excitationColour, v.excitationEnvelope, v.excitationDecay,
                 v.releaseStepRise, v.loops[0].delay };
    }
    static void testBurst(AcustraEngine& engine, std::vector<float>& white,
                           std::vector<float>& filtered)
    {
        // Observe the same noise draws through an independent white path;
        // the expectation below does not copy the production RNG.
        auto whiteVoice = std::make_unique<AcustraEngine::Voice>(engine.voices_[0]);
        auto filteredVoice = std::make_unique<AcustraEngine::Voice>(engine.voices_[0]);
        whiteVoice->excitationWhite = true;
        filteredVoice->excitationWhite = false;
        for (int n = 0; n < 64; ++n)
        {
            white.push_back(engine.renderExcitation(*whiteVoice));
            filtered.push_back(engine.renderExcitation(*filteredVoice));
        }
    }
    static bool retainsTravel(AcustraEngine& engine)
    {
        auto& voice = engine.voices_[0];
        auto before = std::make_unique<AcustraEngine::ContactTravel>(voice.contactTravel);
        engine.captureTail(voice);
        const auto& after = voice.tailContactTravel;
        if (before->history != after.history || before->writeIndex != after.writeIndex
            || before->historyLength != after.historyLength
            || before->historyRemaining != after.historyRemaining
            || before->active != after.active) return false;
        for (std::size_t n = 0; n < 2; ++n)
        {
            const auto& a = before->taps[n];
            const auto& b = after.taps[n];
            if (a.whole != b.whole || a.order != b.order || a.a1 != b.a1
                || a.a2 != b.a2 || a.y1 != b.y1 || a.y2 != b.y2) return false;
        }
        return true;
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
int failures = 0;
void expect(bool condition, const char* message)
{ if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

auto fresh(double rate, acustra::PickingTechnique technique, bool coupled,
           float touch = 0.58f, int velocity = 91)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    auto options = engine->performanceRealism();
    options.contactRelease = coupled;
    options.coherentHand = false;
    options.gestureDamping = false;
    options.playerBodyLoading = false;
    engine->setPerformanceRealism(options);
    engine->prepare(rate, 128);
    engine->setStringPerChannelMode(true);
    acustra::EngineParameters p;
    p.picking = technique;
    p.touch = touch;
    p.room = 0.0f;
    engine->setParameters(p);
    engine->noteOn(40, velocity / 127.0f, 1, 0, false);
    return engine;
}

void testIndependentReleaseCornerAndForceContinuity()
{
    for (double rate : { 8000.0, 24000.0, 44100.0, 48000.0, 96000.0,
                         192000.0, 384000.0 })
        for (float touch : { 0.0f, 0.58f, 1.0f })
        {
            float prior = -1.0f;
            for (int velocity : { 16, 40, 64, 91, 112, 127 })
            {
                auto engine = fresh(rate, acustra::PickingTechnique::Finger,
                                    true, touch, velocity);
                const auto state = Access::inspect(*engine);
                const double reference = 0.05 + 0.42 * state.colour;
                const double base = 1.0 - std::exp(std::log(1.0 - reference) * 48000.0 / rate);
                // Independently specify the complete two-stage filter's
                // sampled first moment, then solve each stage's coefficient.
                const double mean = state.predictedPole / (1.0 - state.predictedPole);
                const double expected = std::min(base, 1.0 / (1.0 + 0.5 * mean));
                expect(std::abs(state.base - base) < 2.0e-7
                           && std::abs(state.effective - expected) < 2.0e-6,
                       "Finger corner violated independently recovered force/geometry law");
                expect(state.effective > 0.0f && state.effective <= state.base,
                       "Finger corner left its passive colour/release bounds");
                expect(state.effective >= prior,
                       "firmer velocity made the contact burst slower");
                prior = state.effective;
                auto baseline = fresh(rate, acustra::PickingTechnique::Finger,
                                      false, touch, velocity);
                const auto old = Access::inspect(*baseline);
                expect(old.effective == old.base && old.wave == state.wave
                           && old.force == state.force && old.envelope == state.envelope
                           && old.decay == state.decay && old.colour == state.colour,
                       "contact coupling changed released wave, held force or noise envelope");
            }
        }
}

void testIndependentNoiseFilterAndRetainedArrivals()
{
    for (double rate : { 24000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        auto engine = fresh(rate, acustra::PickingTechnique::Finger, true);
        const auto state = Access::inspect(*engine);
        std::vector<float> white, filtered;
        Access::testBurst(*engine, white, filtered);
        double stage1 = 0.0, stage2 = 0.0, envelope = state.envelope;
        for (std::size_t n = 0; n < white.size(); ++n)
        {
            const double noise = white[n] / envelope;
            stage1 = (1.0 - state.effective) * stage1 + state.effective * noise;
            stage2 = (1.0 - state.effective) * stage2 + state.effective * stage1;
            const double expected = stage2 * envelope;
            expect(std::abs(filtered[n] - expected) < 5.0e-10 + 3.0e-6 * std::abs(expected),
                   "actual contact burst ignored its coupled coefficient or cascade");
            envelope *= state.decay;
        }
        std::array<float, 128> left {}, right {};
        engine->process(left.data(), right.data(), 17);
        expect(Access::retainsTravel(*engine),
               "retained arrivals changed already filtered source or travel history");
        // Different-force re-plucks must revise the release corner even
        // though the cached colour and host rate remain identical.
        float smallest = 1.0f, largest = 0.0f, base = 0.0f;
        for (int stroke = 0; stroke < 16; ++stroke)
        {
            engine->beginStrum();
            engine->noteOn(40, 64.0f / 127.0f, 1, 0, true);
            int remaining = int(std::ceil(0.03 * rate));
            while (remaining > 0)
            {
                const int n = std::min(128, remaining);
                engine->process(left.data(), right.data(), n);
                remaining -= n;
            }
            const auto state = Access::inspect(*engine);
            if (stroke == 0) base = state.base;
            expect(state.base == base,
                   "same-velocity force draws changed the cached colour corner");
            smallest = std::min(smallest, state.effective);
            largest = std::max(largest, state.effective);
        }
        expect(largest > 1.01f * smallest,
               "same-colour cache reused a preceding contact's force corner");
    }
}

void testScheduledAndInProgressContactControls()
{
    for (double rate : { 24000.0, 48000.0, 96000.0, 192000.0 })
    {
        auto direct = fresh(rate, acustra::PickingTechnique::Finger, true);
        auto queued = fresh(rate, acustra::PickingTechnique::Finger, true);
        direct->allSoundOff(1);
        queued->allSoundOff(1);
        queued->noteOn(47, 0.4f, 1, 1);
        acustra::EngineParameters changed;
        changed.picking = acustra::PickingTechnique::Finger;
        changed.touch = 0.2f;
        changed.pluckPosition = 0.8f;
        direct->setParameters(changed);
        queued->setParameters(changed);
        direct->setPitchBend(0.5f, 1);
        queued->setPitchBend(0.5f, 1);
        direct->noteOn(47, 0.4f, 1);
        std::array<float, 2> left {}, right {};
        direct->process(left.data(), right.data(), 1);
        queued->process(left.data(), right.data(), 2);
        const auto a = Access::inspect(*direct);
        const auto b = Access::inspect(*queued);
        expect(a.base == b.base && a.effective == b.effective && a.colour == b.colour,
               "queued contact corner used controls from scheduling instead of contact");
        // A later panel edit changes the next contact, not the already
        // emitted burst's frozen technique/force release coefficient.
        changed.picking = acustra::PickingTechnique::Pick;
        changed.touch = 1.0f;
        queued->setParameters(changed);
        expect(Access::inspect(*queued).effective == b.effective,
               "live style edit rewrote the current contact's corner");
    }
}

std::vector<float> render(double rate, acustra::PickingTechnique technique,
                          bool coupled, int block)
{
    auto engine = fresh(rate, technique, coupled);
    const int count = int(rate * 0.1);
    std::vector<float> signal(std::size_t(count) * 2);
    std::array<float, 128> left {}, right {};
    for (int offset = 0; offset < count; offset += block)
    {
        const int frames = std::min(block, count - offset);
        engine->process(left.data(), right.data(), frames);
        for (int n = 0; n < frames; ++n)
        {
            signal[std::size_t(2 * (offset + n))] = left[std::size_t(n)];
            signal[std::size_t(2 * (offset + n) + 1)] = right[std::size_t(n)];
        }
    }
    return signal;
}

void testTechniqueIsolationAndBlockPartition()
{
    for (double rate : { 8000.0, 24000.0, 44100.0, 48000.0, 96000.0,
                         192000.0, 384000.0 })
        for (auto technique : { acustra::PickingTechnique::Finger,
                                acustra::PickingTechnique::Thumb,
                                acustra::PickingTechnique::Pick })
        {
            const auto before = render(rate, technique, false, 128);
            const auto after = render(rate, technique, true, 128);
            expect((before != after) == (technique == acustra::PickingTechnique::Finger),
                   "coupling changed another technique or made no Finger difference");
            expect(after == render(rate, technique, true, 7),
                   "contact release changed with process block partition");
            expect(std::all_of(after.begin(), after.end(), [] (float x)
                { return std::isfinite(x) && std::abs(x) <= 1.0f; }),
                "coupled-contact audio became nonfinite or unbounded");
            const auto first = [] (const auto& signal)
            { return std::find_if(signal.begin(), signal.end(), [] (float x)
                { return x != 0.0f; }) - signal.begin(); };
            expect(first(before) == first(after),
                   "coupling shifted the first sounding sample");
        }
}
}

int main()
{
    testIndependentReleaseCornerAndForceContinuity();
    testIndependentNoiseFilterAndRetainedArrivals();
    testScheduledAndInProgressContactControls();
    testTechniqueIsolationAndBlockPartition();
    if (failures == 0) std::cout << "Contact-release contracts passed\n";
    return failures == 0 ? 0 : 1;
}
