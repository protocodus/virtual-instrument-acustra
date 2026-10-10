// Export the prepared mechanical coefficients for PrototypeModalPreload.py.
// Standalone analysis only: includes the engine implementation for friend access.
// From the repository root:
//   mkdir -p build
//   c++ -std=c++20 -O2 -I Source Tools/ModalPreloadProbe.cpp -o build/ModalPreloadProbe
//   shasum -a 256 Tools/ModalPreloadProbe.cpp Source/DSP/AcustraEngine.cpp \
//     Source/DSP/AcustraEngine.h Source/DSP/FittedPhysicalData.h \
//     Source/DSP/MeasuredBodyData.h Source/DSP/MeasuredBridgeData.h \
//     > build/modal-preload-source-hashes.txt
//   build/ModalPreloadProbe > build/modal-preload-coefficients.jsonl
//   python3 Tools/PrototypeModalPreload.py \
//     --coefficients build/modal-preload-coefficients.jsonl \
//     --output build/modal-preload-analysis
// Preserve the hash record with the export. The Python tool's workspace hashes
// are observation context, not proof of the supplied coefficients' origin.
// Only the steel-strung Original guitar is exported. Its rows keep
// "variant":1, the number they carried when a nylon variant 0 was exported
// beside them, so a steel export is unchanged line for line.

#include "DSP/AcustraEngine.cpp"

#include <array>
#include <iomanip>
#include <iostream>
#include <memory>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static void exportCoefficients()
    {
        constexpr int rate = 48000;
        constexpr int variant = 1;
        {
            auto engine = std::make_unique<AcustraEngine>();
            EngineParameters parameters;
            engine->setParameters(parameters);
            engine->prepare(rate, 64);

            float stiffness0, stiffness1, stiffness2;
            engine->bridgeAnchorMoments(stiffness0, stiffness1, stiffness2);
            std::cout << "{\"kind\":\"configuration\",\"rate\":" << rate
                      << ",\"variant\":" << variant << ",\"K\":["
                      << stiffness0 << ',' << stiffness1 << ',' << stiffness2
                      << "]}\n";

            // Each slot's pole as configureBridge set it: the steel blend's
            // bridge part after part (B's modes, then the parallel parts),
            // shaped by the body's morph and wood, then the plate floor in
            // the last slot. Slots the blend leaves empty carry no residue.
            std::array<float, AcustraEngine::bridgeModeCount + 1> frequencies {};
            std::array<float, AcustraEngine::bridgeModeCount + 1> qs {};
            {
                const auto& configured = engine->parameters_;
                const AnchorTransform& anchor = wideSteelAnchorTransform;
                const auto morph = bodyShapeMorph(
                    detail::measuredSteelBodyModes, anchor, anchorBody(),
                    targetBodyFor(configured.shape));
                std::size_t slot = 0;
                visitSteelBlendBridge(engine->bridgeShapeA0_,
                    engine->bridgeShapeT1_, engine->bridgeShapePlate_,
                    engine->bridgeShapeT1UpperHz_, anchor, morph,
                    steelJointMorph(anchor, configured.shape),
                    woodFactorsFor(configured.bodyMaterial),
                    engine->physicalCalibration_,
                    [&] (const detail::MeasuredBridgeMode&,
                         const detail::MeasuredBridgeMode& measured, float, bool)
                    {
                        frequencies[slot] = measured.frequency;
                        qs[slot] = measured.q;
                        ++slot;
                    });
                const auto plate = plateConductanceMode(engine->physicalCalibration_);
                frequencies.back() = plate.frequency;
                qs.back() = plate.q;
            }
            const auto& bridge = engine->bridgeLoad_;
            for (std::size_t index = 0; index < bridge.heaveModes.size(); ++index)
            {
                const float heave = bridge.residueHeave[index];
                const float cross = bridge.residueCross[index];
                const float rock = bridge.residueRock[index];
                if (heave == 0.0f && cross == 0.0f && rock == 0.0f)
                    continue;

                const float frequency = frequencies[index];
                const float q = qs[index];
                // Retain the native float prewarp and its operation order.
                const float fs = static_cast<float>(rate);
                const float bilinear = 2.0f * fs;
                const float omega = bilinear * std::tan(pi * frequency / fs);
                std::cout << "{\"kind\":\"bridge_mode\",\"rate\":" << rate
                          << ",\"variant\":" << variant << ",\"mode\":" << index
                          << ",\"omega_prewarped\":" << omega << ",\"q\":" << q
                          << ",\"R\":[" << heave << ',' << cross << ',' << rock
                          << "]}\n";
            }

            // Only the low-E normal polarization speaks in this reference.
            // bridgeAnchorMoments above still includes all six fixed tails.
            const auto& voice = engine->voices_[0];
            const auto& loop = voice.loops[0];
            const double length = double(0.648f);
            std::cout << "{\"kind\":\"string\",\"rate\":" << rate
                      << ",\"variant\":" << variant << ",\"string\":0"
                      << ",\"gain\":" << loop.loopGain
                      << ",\"Z\":" << double(voice.characteristicImpedance)
                      << ",\"T\":" << voice.tensionNewtons
                      << ",\"L\":" << length
                      << ",\"arm\":" << saddleLeverArm(0)
                      << ",\"B\":" << voice.dispersionDesignInharmonicity
                      << ",\"broad_coefficient\":" << loop.broadLossCoefficient
                      << ",\"broad_mix\":" << loop.broadLossMix
                      << ",\"high_coefficient\":" << loop.lowpassCoefficient
                      << ",\"high_mix\":" << loop.highLossMix << "}\n";
        }
    }
};
}

int main()
{
    std::cout << std::setprecision(17);
    acustra::AcustraEngineTestAccess::exportCoefficients();
    return std::cout ? 0 : 1;
}
