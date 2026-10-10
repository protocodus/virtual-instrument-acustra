#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static bool bridgePending(const AcustraEngine& engine)
    { return engine.bridgeUpdatePending_; }
    static float bridgeFade(const AcustraEngine& engine)
    { return engine.bridgeLoadFade_; }
    static float bodyFade(const AcustraEngine& engine)
    { return engine.bodyModelFade_; }
    static std::uint64_t generation(const AcustraEngine& engine)
    { return engine.voiceConfigurationGeneration_; }
    static bool voicesObservedBridge(const AcustraEngine& engine)
    {
        for (const auto& voice : engine.voices_)
            if (voice.configurationKey.generation != engine.voiceConfigurationGeneration_)
                return false;
        return true;
    }
    // Compare the tuning observer against the actual deployed digital
    // sections, independently evaluating both complex transfer matrices.
    // During a fade the observer follows its configured destination, which
    // is also the bank the runtime will reach when that fade completes.
    static double bridgeObservationError(const AcustraEngine& engine)
    {
        using Complex = std::complex<double>;
        constexpr double pi = 3.14159265358979323846;
        const auto& bridge = engine.bridgeLoad_;
        const auto& table = engine.bridgeMobilityTable();
        const double rate = engine.sampleRate_;
        double worst = 0.0;
        for (int bin = 0; bin < 48; ++bin)
        {
            const double frequency = 60.0 * std::pow(10000.0 / 60.0, bin / 47.0);
            const auto z = std::polar(1.0, -2.0 * pi * frequency / rate);
            std::array<Complex, 3> deployed {}, observed {};
            for (int active = 0; active < bridge.activeModeCount; ++active)
            {
                const auto index = bridge.activeModes[std::size_t(active)];
                const auto& mode = bridge.heaveModes[index];
                const double immediate = -mode.numerator2 / (1.0 + mode.denominator2);
                const auto response = immediate
                    + (mode.numerator1 * z + mode.numerator2 * z * z)
                        / (1.0 + mode.denominator1 * z + mode.denominator2 * z * z);
                deployed[0] += double(bridge.residueHeave[index]) * response;
                deployed[1] += double(bridge.residueCross[index]) * response;
                deployed[2] += double(bridge.residueRock[index]) * response;
            }
            const double omega = 2.0 * rate * std::tan(pi * frequency / rate);
            const auto section = [omega] (double center, double damping)
            {
                return Complex(0.0, omega)
                    / Complex(center * center - omega * omega, 2.0 * damping * omega);
            };
            for (int index = 0; index < table.count; ++index)
            {
                const auto& mode = table.modes[std::size_t(index)];
                const auto response = double(table.scale) * section(mode.omega, mode.damping);
                observed[0] += double(mode.heave) * response;
                observed[1] += double(mode.cross) * response;
                observed[2] += double(mode.rock) * response;
            }
            if (table.plate)
                observed[0] += double(table.plateWeight)
                    * section(table.plateOmega, table.plateDamping);
            for (std::size_t part = 0; part < deployed.size(); ++part)
                worst = std::max(worst, std::abs(deployed[part] - observed[part])
                    / (2e-4 * std::abs(observed[part]) + 2e-8));
        }
        return worst;
    }
    static std::array<float, 3> body(AcustraEngine& engine, float force)
    {
        const auto output = engine.renderBody(force, 0.17f * force);
        return { output.left, output.right, output.upper };
    }
    static float bridge(AcustraEngine& engine, float force)
    {
        AcustraEngine::BridgeDrive drive {};
        drive.incidentHeave = force;
        drive.impedance0 = 0.1f;
        drive.impedance2 = 0.05f;
        if (engine.bridgeLoadFade_ < 1.0f)
        {
            engine.bridgeLoad_.process(drive, engine.fadingBridgeLoad_,
                                      engine.bridgeLoadFade_);
            engine.bridgeLoadFade_ = std::min(1.0f,
                engine.bridgeLoadFade_ + engine.bridgeLoadFadeStep_);
            if (engine.bridgeLoadFade_ == 1.0f && engine.bridgeUpdatePending_)
                engine.applyPendingBridge(true);
        }
        else
            engine.bridgeLoad_.process(drive);
        return engine.bridgeLoad_.displacement;
    }
};
}

namespace
{
int failures = 0;
void expect(bool pass, const std::string& message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void testCancelledConstructionKeepsBodyTail(double rate)
{
    using Access = acustra::AcustraEngineTestAccess;
    acustra::EngineParameters original;
    auto reference = std::make_unique<acustra::AcustraEngine>();
    auto changed = std::make_unique<acustra::AcustraEngine>();
    for (auto* engine : { reference.get(), changed.get() })
    {
        engine->prepare(rate, 64);
        for (int n = 0; n < static_cast<int>(0.01 * rate); ++n)
            Access::body(*engine, n == 0 ? 0.2f : 0.0f);
    }
    auto other = original;
    other.shape = acustra::BodyShape::Jumbo;
    other.bodyMaterial = acustra::BodyMaterial::Maple;
    changed->setParameters(other);
    changed->setParameters(original); // neither replacement has emitted audio
    // A Shape or Wood request keeps the modes' states, so the tail alone
    // cannot show a needless fade: the cancellation must not start one.
    expect(Access::bodyFade(*changed) == 1.0f,
           "same-sample construction cancellation left a body fade running at "
               + std::to_string(rate));
    bool same = true;
    double tailEnergy = 0.0;
    for (int n = 0; n < static_cast<int>(0.1 * rate); ++n)
    {
        const auto expected = Access::body(*reference, 0.0f);
        const auto actual = Access::body(*changed, 0.0f);
        same = same && actual == expected;
        tailEnergy += double(expected[0]) * expected[0];
    }
    expect(tailEnergy > 1.0e-10, "body probe had no sounding tail");
    expect(same, "same-sample construction cancellation erased the measured body's tail at "
                    + std::to_string(rate));
}
void testCancelledConstructionKeepsBridgeTail(double rate)
{
    using Access = acustra::AcustraEngineTestAccess;
    acustra::EngineParameters original;
    auto reference = std::make_unique<acustra::AcustraEngine>();
    auto changed = std::make_unique<acustra::AcustraEngine>();
    for (auto* engine : { reference.get(), changed.get() })
    {
        engine->prepare(rate, 64);
        for (int n = 0; n < static_cast<int>(0.01 * rate); ++n)
            Access::bridge(*engine, n == 0 ? 0.2f : 0.0f);
    }
    auto other = original;
    other.shape = acustra::BodyShape::Jumbo;
    other.bodyMaterial = acustra::BodyMaterial::Maple;
    changed->setParameters(other);
    changed->setParameters(original);
    expect(Access::bridgeFade(*changed) == 1.0f && !Access::bridgePending(*changed),
           "same-sample construction cancellation left a bridge fade running at "
               + std::to_string(rate));
    bool same = true;
    double tailEnergy = 0.0;
    for (int n = 0; n < static_cast<int>(0.1 * rate); ++n)
    {
        const float expected = Access::bridge(*reference, 0.0f);
        const float actual = Access::bridge(*changed, 0.0f);
        same = same && actual == expected;
        tailEnergy += double(expected) * expected;
    }
    expect(tailEnergy > 1.0e-15, "bridge probe had no sounding tail");
    expect(same, "same-sample construction cancellation erased the bridge's tail at "
                    + std::to_string(rate));
}
void testOnlyFinalConstructionReachesBridge(double rate)
{
    using Access = acustra::AcustraEngineTestAccess;
    acustra::EngineParameters original;
    auto reference = std::make_unique<acustra::AcustraEngine>();
    auto changed = std::make_unique<acustra::AcustraEngine>();
    for (auto* engine : { reference.get(), changed.get() })
    {
        engine->prepare(rate, 64);
        for (int n = 0; n < static_cast<int>(0.1 * rate); ++n)
            Access::bridge(*engine, 0.02f * std::sin(float(n) * 0.01f));
    }
    auto intermediate = original;
    intermediate.shape = acustra::BodyShape::Parlor;
    auto final = original;
    final.bodyMaterial = acustra::BodyMaterial::Mahogany;
    reference->setParameters(final);
    changed->setParameters(intermediate);
    changed->setParameters(final);
    bool same = true;
    for (int n = 0; n < static_cast<int>(0.1 * rate); ++n)
    {
        const float force = 0.02f * std::sin(float(n) * 0.01f);
        same = same && Access::bridge(*reference, force) == Access::bridge(*changed, force);
    }
    expect(same, "a superseded body shape sounded through the bridge at "
                    + std::to_string(rate));
}
void testCancelledPendingConstructionLeavesNoLaterDip(double rate)
{
    using Access = acustra::AcustraEngineTestAccess;
    acustra::EngineParameters parlor;
    parlor.shape = acustra::BodyShape::Parlor;
    auto reference = std::make_unique<acustra::AcustraEngine>();
    auto changed = std::make_unique<acustra::AcustraEngine>();
    for (auto* engine : { reference.get(), changed.get() })
    {
        engine->prepare(rate, 64);
        engine->setParameters(parlor);
        for (int n = 0; n < static_cast<int>(0.005 * rate); ++n)
            Access::bridge(*engine, 0.02f * std::sin(float(n) * 0.01f));
    }
    auto superseded = parlor;
    superseded.shape = acustra::BodyShape::Jumbo;
    changed->setParameters(superseded);
    changed->setParameters(parlor); // cancel while the Parlor bank is sounding
    bool same = true;
    for (int n = 0; n < static_cast<int>(0.08 * rate); ++n)
    {
        const float force = 0.02f * std::sin(float(n) * 0.01f);
        same = same && Access::bridge(*reference, force) == Access::bridge(*changed, force);
    }
    expect(same, "a cancelled body request restarted a second bridge fade at "
                    + std::to_string(rate));
}
void testQueuedConstructionKeepsItsTuningObserver(double rate)
{
    using Access = acustra::AcustraEngineTestAccess;
    using Engine = acustra::AcustraEngine;
    const auto process = [] (Engine& engine, int samples)
    {
        std::array<float, 64> left {}, right {};
        while (samples > 0)
        {
            const int count = std::min(samples, int(left.size()));
            engine.process(left.data(), right.data(), count);
            samples -= count;
        }
    };
    // Cover each independently queued construction field, plus both at
    // once. The measured matrix comparison catches a hybrid table even where
    // a selected note happens to be far from its bad poles.
    for (int change = 0; change < 3; ++change)
    {
        acustra::EngineParameters initial;
        auto sounding = initial;
        sounding.shape = acustra::BodyShape::Parlor;
        auto queued = sounding;
        if (change == 0)
            queued.shape = acustra::BodyShape::Jumbo;
        else if (change == 1)
            queued.bodyMaterial = acustra::BodyMaterial::Maple;
        else
        {
            queued.shape = acustra::BodyShape::Jumbo;
            queued.bodyMaterial = acustra::BodyMaterial::Mahogany;
        }
        auto engine = std::make_unique<Engine>();
        engine->setParameters(initial);
        engine->prepare(rate, 64);
        engine->noteOn(55, 0.8f);
        process(*engine, int(0.1 * rate));
        const auto check = [&] (const char* phase)
        {
            expect(Access::bridgeObservationError(*engine) <= 1.0,
                std::string("tuning observer differs from configured bridge ") + phase
                + " at " + std::to_string(rate) + ", change " + std::to_string(change));
        };
        check("before automation");
        engine->setParameters(sounding);
        process(*engine, int(0.005 * rate));
        check("during first fade");
        engine->setParameters(queued);
        expect(Access::bridgePending(*engine), "construction probe did not queue its request");
        check("with queued request");
        process(*engine, 65); // include at least one voice control update
        check("after queued voice update");
        engine->setParameters(sounding);
        expect(!Access::bridgePending(*engine), "returning to current target did not cancel queue");
        check("after cancellation");
        engine->setParameters(queued);
        const auto generation = Access::generation(*engine);
        int waited = 0;
        while (Access::bridgePending(*engine) && waited++ < int(0.021 * rate))
            process(*engine, 1);
        expect(!Access::bridgePending(*engine) && Access::bridgeFade(*engine) == 0.0f,
               "queued construction did not start at the preceding bridge fade boundary");
        expect(Access::generation(*engine) > generation,
               "applying queued bridge did not invalidate cached string tuning");
        check("at queued apply boundary");
        process(*engine, 65);
        expect(Access::voicesObservedBridge(*engine),
               "strings retained the preceding bridge's cached tuning after apply");
        check("after apply voice update");
        process(*engine, int(0.05 * rate));
        check("after settling");
        engine->reset();
        check("after reset");
    }
}
}
int main()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        testCancelledConstructionKeepsBodyTail(rate);
        testCancelledConstructionKeepsBridgeTail(rate);
        testOnlyFinalConstructionReachesBridge(rate);
        testCancelledPendingConstructionLeavesNoLaterDip(rate);
        testQueuedConstructionKeepsItsTuningObserver(rate);
    }
    if (failures == 0) std::cout << "Body realism continuity tests passed\n";
    return failures == 0 ? 0 : 1;
}
