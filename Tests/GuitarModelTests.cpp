// The Original guitar's measured body, under the suite's historical name
// (Acustra.GuitarModels): the microphones' broad brightness tilt
// (MicrophoneBalanceData.h) changes only what the microphones observe, never
// the strings, the bridge or the piezo; the coupled body stays passive and
// bounded under host requests faster than its fade, and All Sound Off clears
// it; and Spruce plays the measured bank as measured, while the radiation
// moves by Body Material's frequency factor.
#include "DSP/AcustraEngine.h"
#include "DSP/MicrophoneBalanceData.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static std::array<float, 4> woodFactors(const EngineParameters& p)
    { return AcustraEngine::bodyWoodFactors(p); }
    static std::array<float, 2> radiationPole(const EngineParameters& p, int index)
    { return AcustraEngine::radiationModePole(p, fittedPhysicalCalibration, index); }
    static bool hasBody(const AcustraEngine& e, BodyShape shape, BodyMaterial wood)
    { return e.configuredBodyShape_ == shape && e.configuredBodyMaterial_ == wood; }
    // Remove only the microphone tilt from a prepared, silent engine.
    // Reload the configured record so both test engines retain identical
    // poles, states, slot ordering and physical bridge/string coefficients.
    static bool removeMicrophoneTilt(AcustraEngine& e)
    {
        const auto poleReal = e.bodyBank_.poleReal;
        const auto poleImaginary = e.bodyBank_.poleImaginary;
        const int count = e.bodyBank_.count, ordered = e.bodyBank_.ordered;
        constexpr float twoPi = 6.28318530717958647692f;
        for (int index = 0; index < count; ++index)
        {
            auto& mode = e.bodyModes_[static_cast<std::size_t>(index)];
            if (mode.poleImaginary == 0.0f)
                continue;
            const float frequency = std::atan2(mode.poleImaginary, mode.poleReal)
                * static_cast<float>(e.sampleRate_) / twoPi;
            const float gain = detail::microphoneTiltGain(frequency);
            if (!std::isfinite(gain) || gain <= 0.0f)
                return false;
            for (float* residue : { &mode.leftReal, &mode.leftImaginary,
                    &mode.rightReal, &mode.rightImaginary,
                    &mode.leftMomentReal, &mode.leftMomentImaginary,
                    &mode.rightMomentReal, &mode.rightMomentImaginary })
                *residue /= gain;
        }
        e.bodyBank_.load(e.bodyModes_, count, ordered);
        e.bodyBank_.reset();
        return e.bodyBank_.count == count && e.bodyBank_.ordered == ordered
            && e.bodyBank_.poleReal == poleReal
            && e.bodyBank_.poleImaginary == poleImaginary;
    }
};
}
namespace
{
int failures = 0;
void expect(bool value, const char* message)
{
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// The Original on the Auditorium body, as these checks have always played it.
acustra::EngineParameters auditorium()
{
    acustra::EngineParameters p;
    p.shape = acustra::BodyShape::Auditorium;
    return p;
}

void testMicrophoneTiltOnlyChangesObservation()
{
    using namespace acustra;
    for (float frequency : { 45.0f, 80.0f, 120.0f, 250.0f, 700.0f,
                             1200.0f, 2500.0f, 5000.0f, 9000.0f, 18000.0f })
    {
        const float gain = detail::microphoneTiltGain(frequency);
        expect(std::isfinite(gain) && gain > 0.0f,
               "microphone tilt gain must remain finite and positive");
    }
    for (const int rate : { 48000, 96000 })
        for (const auto capture : { CaptureType::StereoMic,
                                   CaptureType::MonoMic, CaptureType::Piezo })
        {
            auto p = auditorium();
            p.capture = capture;
            p.outputGain = 0.2f;
            p.room = 0.0f;
            p.piezoMix = 0.0f;
            auto tilted = std::make_unique<AcustraEngine>();
            auto untilted = std::make_unique<AcustraEngine>();
            tilted->setParameters(p);
            untilted->setParameters(p);
            tilted->prepare(rate, 64);
            untilted->prepare(rate, 64);
            expect(AcustraEngineTestAccess::removeMicrophoneTilt(*untilted),
                   "bypassing the microphone tilt changed body poles or mode count");
            bool samePhysicalInstrument = true, samePiezo = true;
            bool finiteAndBounded = true;
            double differenceEnergy = 0.0, referenceEnergy = 0.0;
            double piezoEnergy = 0.0;
            for (int n = 0; n < rate / 3; ++n)
            {
                // Open B3 alongside fretted B2, A3 and B4, then a
                // release and repick. These exercise the physical
                // sympathetic junction as well as the two sensors.
                if (n == 0)
                    for (int note : { 47, 57, 59, 71 })
                    {
                        tilted->noteOn(note, 0.75f);
                        untilted->noteOn(note, 0.75f);
                    }
                if (n == rate / 16)
                {
                    tilted->noteOff(59);
                    untilted->noteOff(59);
                }
                if (n == rate / 8)
                {
                    tilted->noteOn(71, 0.94f);
                    untilted->noteOn(71, 0.94f);
                }
                if (n == 3 * rate / 16)
                    for (int note : { 47, 57, 71 })
                    {
                        tilted->noteOff(note);
                        untilted->noteOff(note);
                    }
                float vl, vr, vp, ul, ur, up;
                tilted->process(&vl, &vr, AcustraEngine::OutputBuses { &vp }, 1);
                untilted->process(&ul, &ur, AcustraEngine::OutputBuses { &up }, 1);
                samePhysicalInstrument = samePhysicalInstrument
                    && tilted->getLastBridgeVelocity() == untilted->getLastBridgeVelocity()
                    && tilted->getLastBridgeReactionForce() == untilted->getLastBridgeReactionForce()
                    && tilted->getLastBridgeBodyForce() == untilted->getLastBridgeBodyForce()
                    && tilted->getLastBridgePower() == untilted->getLastBridgePower()
                    && tilted->getLastBridgeBodyPower() == untilted->getLastBridgeBodyPower();
                samePiezo = samePiezo && vp == up;
                finiteAndBounded = finiteAndBounded
                    && std::isfinite(vl) && std::isfinite(vr) && std::isfinite(vp)
                    && std::isfinite(ul) && std::isfinite(ur) && std::isfinite(up)
                    && std::max({ std::abs(vl), std::abs(vr), std::abs(vp),
                                  std::abs(ul), std::abs(ur), std::abs(up) }) <= 1.0f;
                const double dl = double(vl) - ul, dr = double(vr) - ur;
                differenceEnergy += dl * dl + dr * dr;
                referenceEnergy += double(ul) * ul + double(ur) * ur;
                piezoEnergy += double(up) * up;
            }
            std::cout << "microphone tilt rate=" << rate
                      << " capture=" << int(capture) << " relative difference="
                      << std::sqrt(differenceEnergy / std::max(referenceEnergy, 1e-30))
                      << '\n';
            expect(samePhysicalInstrument,
                   "the microphone tilt altered the physical string/bridge response");
            expect(samePiezo && piezoEnergy > 1e-12,
                   "the microphone tilt altered or silenced the dedicated piezo output");
            expect(finiteAndBounded, "the microphone tilt produced nonfinite or unbounded audio");
            expect(referenceEnergy > 1e-12, "the microphone tilt observation test was silent");
            if (capture == CaptureType::Piezo)
                expect(differenceEnergy == 0.0,
                       "the microphone tilt altered Main with the piezo selected");
            else
                expect(differenceEnergy > referenceEnergy * 1e-6,
                       "the microphone tilt failed to change the microphone observation");
        }
}

void testCoupledBody()
{
    for (int rate : { 44100, 96000 })
    {
        auto engine = std::make_unique<acustra::AcustraEngine>();
        auto p = auditorium();
        engine->setParameters(p);
        engine->prepare(rate, 64);
        for (int note : { 40, 47, 52, 55, 59, 64 }) engine->noteOn(note, 1.f);
        double energy = 0, minimum = 0, audioEnergy = 0;
        float peak = 0;
        for (int n = 0; n < rate / 2; ++n)
        {
            float l, r; engine->process(&l, &r, 1);
            energy += engine->getLastBridgeBodyPower() / rate;
            minimum = std::min(minimum, energy);
            audioEnergy += double(l)*l + double(r)*r;
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
            expect(std::isfinite(energy) && std::isfinite(l) && std::isfinite(r), "coupled body must remain finite");
        }
        std::cout << "coupled body rate=" << rate << " minimum work=" << minimum << " peak=" << peak << '\n';
        expect(minimum >= -1e-14, "passive body must not generate port energy");
        expect(audioEnergy > 1e-9 && peak < .89f, "measured body must sound without relying on the limiter");
        // A host can move faster than the radiation fade. Verify the final
        // request wins, including its delay history, while a chord rings.
        // The third request returns to the sounding construction.
        using acustra::BodyMaterial;
        using acustra::BodyShape;
        struct Request { BodyShape shape; BodyMaterial wood; };
        for (const auto next : { Request { BodyShape::Jumbo, BodyMaterial::Maple },
                                 Request { BodyShape::Parlor, BodyMaterial::Mahogany },
                                 Request { BodyShape::Auditorium, BodyMaterial::Spruce },
                                 Request { BodyShape::Parlor, BodyMaterial::Maple } })
        {
            p.shape = next.shape;
            p.bodyMaterial = next.wood;
            engine->setParameters(p);
            float l[64], r[64]; engine->process(l, r, 64);
        }
        for (int n = 0; n < rate / 8; n += 64)
        {
            float l[64], r[64]; engine->process(l, r, 64);
            for (float value : l) expect(std::isfinite(value) && std::abs(value) <= 1, "rapid construction switching must remain bounded");
        }
        expect(acustra::AcustraEngineTestAccess::hasBody(*engine, BodyShape::Parlor, BodyMaterial::Maple),
               "coalesced construction change lost the last host request");
        engine->allSoundOff();
        float l[512], r[512]; engine->process(l, r, 512);
        for (int n = 0; n < 512; ++n) expect(l[n] == 0 && r[n] == 0, "all sound off must clear radiation delay too");
    }
}
}
// Body Material moves the measured body relative to the wood it was built
// of, so at that wood it is heard as measured: g21, the Original's body, at
// Spruce.
void testSpruceIsAsMeasured()
{
    using namespace acustra;
    EngineParameters original;
    original.bodyMaterial = BodyMaterial::Spruce;
    expect(AcustraEngineTestAccess::woodFactors(original)
               == std::array<float, 4> { 1.0f, 1.0f, 1.0f, 1.0f },
           "Body Material warps the Original's measured body at its own wood");
    for (int wood = 0; wood < 3; ++wood)
    {
        EngineParameters p;
        p.bodyMaterial = static_cast<BodyMaterial>(wood);
        const auto factors = AcustraEngineTestAccess::woodFactors(p);
        // The radiation takes the frequency factor, against the body at
        // its own wood (the factor's identity).
        auto own = p; own.bodyMaterial = BodyMaterial::Spruce;
        const auto ownFactors = AcustraEngineTestAccess::woodFactors(own);
        const double ratio = double(AcustraEngineTestAccess::radiationPole(p, 3)[0])
            / AcustraEngineTestAccess::radiationPole(own, 3)[0];
        expect(std::abs(ratio / (double(factors[0]) / ownFactors[0]) - 1.0) < 1.0e-6,
               "the radiation does not move by Body Material's frequency factor");
    }
}

int main()
{
    testMicrophoneTiltOnlyChangesObservation();
    testCoupledBody();
    testSpruceIsAsMeasured();
    return failures == 0 ? 0 : 1;
}
