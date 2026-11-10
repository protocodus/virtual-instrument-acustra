// The U4 parameter-extremes matrix (Tests/ParameterMatrix.h) over the plug-in
// processor: every automatable parameter at its extremes, alone, in pairs and
// on every factory preset, at three rates and block sizes.
//
//   AcustraParameterMatrixTests [--jobs N] [--rotation] [--shard K/N]
//                               [--case LABEL] [--count] [--list]
//   AcustraParameterMatrixTests --reach [--jobs N] [--case PARAMETER]
//   AcustraParameterMatrixTests --self-test
//
// --reach is the U11 reach test over the same device: every parameter change
// must reach the sound after long play.

#include "PluginProcessor.h"

#include "MatrixRelease.h"
#include "ParameterMatrix.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

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
            // The two-second bound starts at the first damped sample, after
            // the 1/32-note join window; with no playhead the processor uses
            // 120 BPM. The floor is the open strings' ring, not the shared
            // -60 dBFS: Tests/MatrixRelease.h has both rules and the
            // measurements behind them.
            limits.releaseSeconds = acustra::matrixrelease::releaseSeconds (
                sampleRate, 120.0, processor->getLatencySamples());
            limits.silenceFloor = acustra::matrixrelease::silenceFloor;
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

    // U11. Every other parameter must reach notes already sounding.
    matrix::ReachRule ReachFor (std::size_t index, float) const override
    {
        std::vector<matrix::Parameter> all = Parameters();
        for (const auto& p : EditorOnlyParameters())
            all.push_back (p);
        const std::string& name = all[index].name;
        // Each is read only where a pluck starts (AcustraEngine::initialisePluck).
        if (name == "Pluck Position")
            return { matrix::Reach::NewNotes, "where the finger excites each new pluck" };
        if (name == "Touch")
            return { matrix::Reach::NewNotes, "the finger's contact and let-go at each new pluck" };
        if (name == "Picking")
            return { matrix::Reach::NewNotes, "the technique of each new pluck; a sounding string "
                                              "only takes its construction loudness trim" };
        // AcustraEngine::applyDiscreteParameters moves an idle string to its
        // new open pitch with the waves it carries (returnToOpenString), so
        // the open strings ring on with what the old tuning's playing left.
        if (name == "Tuning")
            return { matrix::Reach::Held, "", {},
                     "the open strings ring on with what the old tuning's playing left" };
        // Not on the editor and not automatable: setStateInformation reads
        // them once, to migrate a session saved before the controls that
        // replaced them.
        if (name == "Legacy capture" || name == "Legacy upper mic" || name == "Legacy piezo loading")
            return { matrix::Reach::Exempt, "retired; migrated into Capture when an old session loads" };
        if (name == "Legacy guitar model")
            return { matrix::Reach::Exempt, "retired; nothing reads it (Docs/decisions.md, 2026-10-10)" };
        return {};
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
    // 7 sensor-alignment samples plus the approved 2 ms output limiter.
    constexpr double latencyAt48k = 7.0 + 96.0;
    const double expectedSeconds = 2.0 + (3000.0 + 1.0 + latencyAt48k) / 48000.0;
    if (std::abs (limits.releaseSeconds - expectedSeconds) > 1.0e-12
        || std::abs (limits.silenceFloor - 2.0e-3f) > 1.0e-10f)
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
