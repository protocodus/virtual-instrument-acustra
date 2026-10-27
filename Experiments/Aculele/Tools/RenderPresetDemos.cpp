// Renders one demo per factory preset (README G3) from the committed phrases in
// Docs/audio/midi/, through the Performer the plug-in plays, with the shared
// renderer in Tools/PresetDemoRenderer.h:
//
//   RenderAculelePresetDemos [--jobs N] Docs/audio/midi <out-dir>
//   RenderAculelePresetDemos [--jobs N] --check Docs/audio/midi
//
// Strum alternation follows the host tempo, so each preset plays at the tempo
// its phrase was written at, read from the phrase's first Set Tempo event.

#include "DSP/AculelePerformer.h"
#include "DSP/AculelePresets.h"
#include "PresetDemoRenderer.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace
{
namespace demos = protocodus::demos;

// The first Set Tempo meta event (FF 51 03 tt tt tt) in bpm; 120 if none.
double phraseTempo (const std::filesystem::path& path)
{
    std::ifstream file (path, std::ios::binary);
    const std::vector<unsigned char> data ((std::istreambuf_iterator<char> (file)), {});
    for (std::size_t i = 0; i + 6 <= data.size(); ++i)
        if (data[i] == 0xFF && data[i + 1] == 0x51 && data[i + 2] == 0x03)
        {
            const auto microseconds = data[i + 3] << 16 | data[i + 4] << 8 | data[i + 5];
            if (microseconds > 0)
                return 60.0e6 / microseconds;
        }
    return 120.0;
}

class PresetVoice final : public demos::Voice
{
public:
    PresetVoice (std::size_t preset, double sampleRate, int maxBlockFrames, double bpm)
    {
        // The plug-in's prepare sequence: the construction is known before
        // prepare sizes the strings, then set again so the controls start there.
        const auto parameters = aculele::engineParameters (aculele::factoryPresets[preset]);
        performer->setParameters (parameters);
        performer->prepare (sampleRate, maxBlockFrames);
        performer->setParameters (parameters);
        performer->setTempoBpm (bpm);
    }

    // The renderer delivers each message at the sample it is due, before the
    // block that starts there, so it is played at that block's first sample.
    void Midi (const std::uint8_t* bytes, int size) override
    {
        if (pendingCount < pending.size())
            pending[pendingCount++] = { { bytes[0], size > 1 ? bytes[1] : std::uint8_t {},
                                          size > 2 ? bytes[2] : std::uint8_t {} }, size };
    }

    void Render (float* left, float* right, int frames) override
    {
        performer->beginBlock (left, right, frames);
        for (std::size_t i = 0; i < pendingCount; ++i)
            performer->handleMidi (0, pending[i].bytes.data(), pending[i].size);
        pendingCount = 0;
        performer->endBlock();
    }

private:
    struct Message
    {
        std::array<std::uint8_t, 3> bytes;
        int size;
    };
    // The densest phrase sends a 15-message RPN and glide burst on one sample.
    std::array<Message, 64> pending {};
    std::size_t pendingCount = 0;
    std::unique_ptr<aculele::Performer> performer = std::make_unique<aculele::Performer>();
};
} // namespace

int main (int argc, char** argv)
{
    demos::Instrument instrument;
    instrument.name = "Aculele";
    for (const auto& preset : aculele::factoryPresets)
        instrument.presets.emplace_back (preset.name);

    // The MIDI directory is the first argument that is not an option.
    std::filesystem::path midiDir;
    for (int i = 1; i < argc; ++i)
    {
        const std::string argument = argv[i];
        if (argument == "--jobs")
            ++i;
        else if (argument.rfind ("--", 0) != 0)
        {
            midiDir = argument;
            break;
        }
    }
    std::vector<double> tempos;
    for (std::size_t preset = 0; preset < instrument.presets.size(); ++preset)
        tempos.push_back (phraseTempo (midiDir / (demos::detail::Stem (instrument, preset) + ".mid")));

    instrument.open = [tempos] (std::size_t preset, double sampleRate, int maxBlockFrames, std::uint32_t)
    {
        return std::make_unique<PresetVoice> (preset, sampleRate, maxBlockFrames, tempos[preset]);
    };
    return demos::Main (argc, argv, instrument);
}
