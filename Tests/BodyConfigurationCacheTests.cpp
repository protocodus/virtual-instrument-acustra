#include "DSP/AcustraEngine.h"

#include <array>
#include <cstring>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static bool coefficientsMatch(const AcustraEngine& engine)
    {
        AcustraEngine::BodyConfiguration direct;
        for (int shape = 0; shape < 4; ++shape)
            for (int wood = 0; wood < 3; ++wood)
            {
                engine.buildBodyConfiguration(direct,
                    static_cast<BodyShape>(shape), static_cast<BodyMaterial>(wood));
                const auto& cached = engine.bodyConfigurations_[3 * shape + wood];
                if (cached.count != direct.count || cached.ordered != direct.ordered
                    || std::memcmp(cached.modes.data(), direct.modes.data(),
                        sizeof(direct.modes)) != 0)
                    return false;
            }
        return engine.bodyConfigurationsReady_;
    }

    static void recompute(AcustraEngine& engine)
    {
        // Recompute every coefficient while retaining all sounding histories.
        engine.prepareBodyConfigurations();
    }

    static bool resetReusesCache(AcustraEngine& engine)
    {
        const auto slot = 3 * static_cast<std::size_t>(engine.parameters_.shape)
            + static_cast<std::size_t>(engine.parameters_.bodyMaterial);
        const auto saved = engine.bodyConfigurations_[slot].modes[0].leftReal;
        engine.bodyConfigurations_[slot].modes[0].leftReal = 0.1234567f;
        engine.reset();
        const bool reused = engine.bodyModes_[0].leftReal == 0.1234567f;
        engine.bodyConfigurations_[slot].modes[0].leftReal = saved;
        engine.reset();
        return reused;
    }
};
}

int main()
{
    using namespace acustra;
    auto cached = std::make_unique<AcustraEngine>();
    auto direct = std::make_unique<AcustraEngine>();
    bool okay = true;
    // Reusing the same objects makes stale-rate/stale-calibration entries
    // observable, including calibration supplied before the first prepare.
    auto calibration = fittedPhysicalCalibration;
    calibration.residueTiltDbPerOctave = 0.3f;
    cached->setPhysicalCalibration(calibration);
    direct->setPhysicalCalibration(calibration);
    for (double rate : { 8000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        cached->prepare(rate, 64);
        direct->prepare(rate, 64);
        okay &= AcustraEngineTestAccess::coefficientsMatch(*cached);
        okay &= AcustraEngineTestAccess::resetReusesCache(*cached);
        // Restore deterministic draws after the cache-reuse witness resets.
        direct->reset();
        direct->reset();
        for (int construction = 0; construction < 12; ++construction)
        {
            EngineParameters parameters;
            parameters.shape = static_cast<BodyShape>(construction / 3);
            parameters.bodyMaterial = static_cast<BodyMaterial>(construction % 3);
            parameters.room = 0.0f;
            AcustraEngineTestAccess::recompute(*direct);
            cached->setParameters(parameters);
            direct->setParameters(parameters);
            if (construction % 3 == 0)
            {
                cached->noteOn(52 + construction, 0.7f);
                direct->noteOn(52 + construction, 0.7f);
            }
            // Interrupt one fade and complete the next, then reverse to a
            // previously used construction while the string is still ringing.
            const int blocks = construction % 2 ? 90 : 1;
            for (int block = 0; block < blocks; ++block)
            {
                std::array<float, 64> aLeft {}, aRight {}, bLeft {}, bRight {};
                cached->process(aLeft.data(), aRight.data(), 64);
                direct->process(bLeft.data(), bRight.data(), 64);
                okay &= std::memcmp(aLeft.data(), bLeft.data(), sizeof(aLeft)) == 0
                    && std::memcmp(aRight.data(), bRight.data(), sizeof(aRight)) == 0;
            }
        }
        calibration.residueTiltDbPerOctave += 0.125f;
        calibration.lowBodyModeGain *= 0.95f;
        cached->setPhysicalCalibration(calibration);
        direct->setPhysicalCalibration(calibration);
        okay &= AcustraEngineTestAccess::coefficientsMatch(*cached);
    }
    // Copying an engine carries the complete setup cache without sharing state.
    auto copy = std::make_unique<AcustraEngine>(*cached);
    okay &= AcustraEngineTestAccess::coefficientsMatch(*copy);
    std::cout << (okay ? "PASS" : "FAIL")
        << ": construction cache lifecycle and exact rendered parity\n";
    return okay ? 0 : 1;
}
