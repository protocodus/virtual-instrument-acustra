// The U4 parameter-extremes matrix (Tests/ParameterMatrix.h) over the plug-in
// processor: every automatable parameter at its extremes, alone, in pairs and
// on every factory preset, at three rates and block sizes.
//
//   AcustraParameterMatrixTests [--jobs N]

#include "PluginProcessor.h"

#include "ParameterMatrix.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

namespace
{
namespace matrix = protocodus::matrix;

class AcustraDevice final : public matrix::JuceDevice
{
public:
    AcustraDevice() : JuceDevice (std::make_unique<AcustraAudioProcessor>())
    {
        const auto parameters = Parameters();
        for (std::size_t i = 0; i < parameters.size(); ++i)
        {
            if (parameters[i].name == "Sustain")
                sustain = i;
            else if (parameters[i].name == "Output")
                output = i;
        }
    }

    matrix::Limits LimitsFor (const matrix::Case& c) const override
    {
        matrix::Limits limits;
        // Sustain rings every released note on, as under the pedal.
        if (c.values[sustain] >= 0.5f || c.base[sustain] >= 0.5f)
            limits.releaseSeconds = -1.0;
        // Output is a plain gain from -24 to +6 dB after the engine, so the
        // ceiling and the silence floor move with it; automation ramps
        // towards base, so the louder of the two decides.
        const auto decibels = [] (float normalised) { return -24.0f + 30.0f * normalised; };
        const float gain = std::pow (10.0f, std::max (decibels (c.values[output]),
                                                      decibels (c.base[output])) / 20.0f);
        if (gain > 1.0f)
        {
            limits.ceiling *= gain;
            limits.silenceFloor *= gain;
            limits.dcLimit *= gain;
        }
        return limits;
    }

private:
    std::size_t sustain = 0;
    std::size_t output = 0;
};
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    matrix::Options options;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--jobs") == 0 && i + 1 < argc)
            options.jobs = std::atoi (argv[++i]);
        else
        {
            std::fprintf (stderr, "usage: %s [--jobs N]\n", argv[0]);
            return 2;
        }
    }
    return matrix::Run ([] { return std::make_unique<AcustraDevice>(); }, options) == 0 ? 0 : 1;
}
