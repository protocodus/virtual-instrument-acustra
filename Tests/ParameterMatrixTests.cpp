// The U4 parameter-extremes matrix (Tests/ParameterMatrix.h) over the plug-in
// processor: every automatable parameter at its extremes, alone, in pairs and
// on every factory preset, at three rates and block sizes.
//
//   AcustraParameterMatrixTests [--jobs N] [--rotation] [--shard K/N]
//                               [--case LABEL] [--count] [--list]
//   AcustraParameterMatrixTests --self-test

#include "PluginProcessor.h"

#include "ParameterMatrix.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace
{
namespace matrix = protocodus::matrix;

class AcustraDevice : public matrix::JuceDevice
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

    void Prepare (double rate, int maxBlockFrames) override
    {
        sampleRate = rate;
        JuceDevice::Prepare (rate, maxBlockFrames);
    }

    matrix::Limits LimitsFor (const matrix::Case& c) const override
    {
        matrix::Limits limits;
        // Sustain rings every released note on, as under the pedal.
        if (c.values[sustain] >= 0.5f || c.base[sustain] >= 0.5f)
            limits.releaseSeconds = -1.0;
        else
        {
            // Ordinary MIDI key-up retains held-string physics through an
            // inclusive 1/32-note join window (7.5/BPM seconds), then the
            // damping hand starts. With no playhead the processor uses
            // 120 BPM. Keep the full two-second physical-release bound and
            // the existing -60 dB floor; start that clock at the first
            // damped sample, observed through the declared output latency.
            // Docs/research/release-join-2026-10-05.md specifies this rule.
            const double graceSamples = std::ceil (7.5 * sampleRate / 120.0);
            limits.releaseSeconds += (graceSamples + 1.0
                + processor->getLatencySamples()) / sampleRate;
        }
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
    double sampleRate = 48000.0;
};

// A real processor with the release events deliberately lost. The protocol
// must still reject a stuck note after accounting for the musical join window.
class LostNoteOffDevice final : public AcustraDevice
{
public:
    void Process (float* left, float* right, int frames,
                  const matrix::Event* events, int eventCount) override
    {
        std::vector<matrix::Event> filtered;
        for (int i = 0; i < eventCount; ++i)
            if ((events[i].bytes[0] & 0xf0) != 0x80)
                filtered.push_back (events[i]);
        AcustraDevice::Process (left, right, frames, filtered.data(),
                               static_cast<int> (filtered.size()));
    }
};

int checkReleaseProtocol()
{
    const auto probe = std::make_unique<AcustraDevice>();
    probe->Prepare (48000.0, 64);
    const auto cases = matrix::Cases (*probe);
    const auto found = std::find_if (cases.begin(), cases.end(), [] (const auto& c)
    {
        return c.label == "default";
    });
    if (found == cases.end())
        return 1;
    const auto& c = *found;
    const auto limits = probe->LimitsFor (c);
    const double expectedSeconds = 2.0 + (3000.0 + 1.0 + 7.0) / 48000.0;
    if (std::abs (limits.releaseSeconds - expectedSeconds) > 1.0e-12
        || std::abs (limits.silenceFloor - 1.0e-3f) > 1.0e-10f)
    {
        std::fprintf (stderr, "release protocol changed its physical-time or silence bound\n");
        return 1;
    }
    const auto positive = matrix::Play ([] { return std::make_unique<AcustraDevice>(); }, c, 48000.0, 64);
    if (! positive.empty())
    {
        std::fprintf (stderr, "ordinary release failed: %s\n", positive.c_str());
        return 1;
    }
    const auto negative = matrix::Play ([] { return std::make_unique<LostNoteOffDevice>(); }, c, 48000.0, 64);
    if (negative.find ("s after release") == std::string::npos)
    {
        std::fprintf (stderr, "lost note-off was not rejected by the release bound: %s\n", negative.c_str());
        return 1;
    }
    std::printf ("Release protocol passes ordinary release and rejects lost note-off (%s)\n", negative.c_str());
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    // --self-test checks the release protocol the limits above rely on; any
    // other command line is the shared matrix CLI (U9).
    if (argc == 2 && std::strcmp (argv[1], "--self-test") == 0)
        return checkReleaseProtocol();
    return matrix::Main (argc, argv, [] { return std::make_unique<AcustraDevice>(); });
}
