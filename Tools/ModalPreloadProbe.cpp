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

            // The saddle-to-pin segment holds the bridge against nothing
            // since 2026-10-10 (bridgePortMobility): its stiffness matrix is
            // exported as zero so the prototype's layout is unchanged.
            constexpr float stiffness0 = 0.0f, stiffness1 = 0.0f, stiffness2 = 0.0f;
            std::cout << "{\"kind\":\"configuration\",\"rate\":" << rate
                      << ",\"variant\":" << variant << ",\"K\":["
                      << stiffness0 << ',' << stiffness1 << ',' << stiffness2
                      << "]}\n";

            // Invert the configured digital denominator itself: this exports
            // the actual prototype for both shared damped-sinusoid poles and
            // unpaired natural oscillators, with all Shape/Wood/floor factors.
            // Reconstructing it from stored f/Q silently conflated those two
            // conventions and duplicated the engine's construction mapping.
            const auto& bridge = engine->bridgeLoad_;
            for (std::size_t index = 0; index < bridge.heaveModes.size(); ++index)
            {
                const float heave = bridge.residueHeave[index];
                const float cross = bridge.residueCross[index];
                const float rock = bridge.residueRock[index];
                if (heave == 0.0f && cross == 0.0f && rock == 0.0f)
                    continue;

                const auto& mode = bridge.heaveModes[index];
                const double d1 = mode.denominator1, d2 = mode.denominator2;
                const double bilinear = 2.0 * rate;
                const double denominator = 1.0 - d1 + d2;
                const double omega = bilinear * std::sqrt((1.0+d1+d2)/denominator);
                const double damping = bilinear * (1.0-d2)/denominator;
                const double q = omega/(2.0*damping);
                std::cout << "{\"kind\":\"bridge_mode\",\"rate\":" << rate
                          << ",\"variant\":" << variant << ",\"mode\":" << index
                          << ",\"omega_prewarped\":" << omega << ",\"q\":" << q
                          << ",\"R\":[" << heave << ',' << cross << ',' << rock
                          << "]}\n";
            }

            // Only the low-E normal polarization speaks in this reference.
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
