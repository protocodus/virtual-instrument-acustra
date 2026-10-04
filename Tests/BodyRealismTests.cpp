#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
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
        drive.stiffness0 = 100.0f;
        drive.stiffness2 = 50.0f;
        if (engine.bridgeLoadFade_ < 1.0f)
        {
            engine.bridgeLoad_.process(drive, engine.inverseSampleRate_,
                                      engine.fadingBridgeLoad_, engine.bridgeLoadFade_);
            engine.bridgeLoadFade_ = std::min(1.0f,
                engine.bridgeLoadFade_ + engine.bridgeLoadFadeStep_);
            if (engine.bridgeLoadFade_ == 1.0f && engine.bridgeUpdatePending_)
                engine.applyPendingBridge(true);
        }
        else
            engine.bridgeLoad_.process(drive, engine.inverseSampleRate_);
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
void testCancelledModelKeepsBodyTail(double rate)
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
    other.guitarModel = acustra::GuitarModel::Bellido1978;
    changed->setParameters(other);
    changed->setParameters(original); // neither replacement has emitted audio
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
    expect(same, "same-sample model cancellation erased the measured body's tail at "
                    + std::to_string(rate));
}
void testCancelledModelKeepsBridgeTail(double rate)
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
    other.guitarModel = acustra::GuitarModel::Bellido1978;
    changed->setParameters(other);
    changed->setParameters(original);
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
    expect(same, "same-sample model cancellation erased the bridge's tail at "
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
}
int main()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        testCancelledModelKeepsBodyTail(rate);
        testCancelledModelKeepsBridgeTail(rate);
        testOnlyFinalConstructionReachesBridge(rate);
        testCancelledPendingConstructionLeavesNoLaterDip(rate);
    }
    if (failures == 0) std::cout << "Body realism continuity tests passed\n";
    return failures == 0 ? 0 : 1;
}
