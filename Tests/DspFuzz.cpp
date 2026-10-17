// U4 input fuzzing of the JUCE-free front-end boundary, acustra::Performer,
// with the shared Tests/FuzzHarness.h: from a fresh Performer every input,
// at most 64 commands of raw and extreme controls (invalid enums and every
// float bit pattern included), arbitrary MIDI bytes at in-block offsets,
// tempo, tuning, the panel switches, reset, suspend and re-prepare at any
// rate, each followed by a block of up to 256 frames on Main and Piezo.
// Every sample must stay finite and inside the frames asked for.
//
//   AcustraDspFuzz -max_len=4096 -rss_limit_mb=4096 <corpus-dir>     (libFuzzer)
//   AcustraDspFuzzReplay [-seed=N] [-runs=N] [file ...]               (any compiler)
//
// A crash leaves a crash-* reproducer; once fixed, commit it under
// Tests/Fixtures/fuzz/, where Acustra.DspFuzzReplay replays it.

#include "DSP/AcustraPerformer.h"

#include "FuzzHarness.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

namespace
{
constexpr int maxBlock = 256;

// prepare() takes about 50 ms, which would hold libFuzzer to a few inputs a
// second under the sanitizers. Prepare is deterministic and a Performer is a
// flat object with no pointers into itself, so a copy of one prepared at the
// same rate is the fresh Performer, bit for bit.
const acustra::Performer& preparedAt (double rate)
{
    struct Entry
    {
        std::uint64_t bits;
        std::unique_ptr<acustra::Performer> performer;
    };
    static std::vector<Entry> cache;
    std::uint64_t bits;
    std::memcpy (&bits, &rate, sizeof bits);
    for (const auto& entry : cache)
        if (entry.bits == bits)
            return *entry.performer;
    auto performer = std::make_unique<acustra::Performer>();
    performer->prepare (rate, maxBlock);
    cache.push_back ({ bits, std::move (performer) });
    return *cache.back().performer;
}

template <typename Enum>
Enum anyEnum (protocodus::fuzz::Input& input)
{
    return static_cast<Enum> (static_cast<int> (input.Byte()) - 64);
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    using namespace protocodus::fuzz;
    Input input { data, size };
    auto performer = std::make_unique<acustra::Performer> (preparedAt (input.Rate()));
    acustra::EngineParameters parameters;
    Sentinels<maxBlock> main;
    Sentinels<maxBlock> piezo;

    for (int command = 0; command < 64 && ! input.Empty(); ++command)
    {
        // One control change before the block, where the plug-in makes it.
        switch (input.Pick (16))
        {
            case 0: parameters.shape = anyEnum<acustra::BodyShape> (input); break;
            case 1: parameters.bodyMaterial = anyEnum<acustra::BodyMaterial> (input); break;
            case 2: parameters.capture = anyEnum<acustra::CaptureType> (input); break;
            case 3: parameters.tuning = anyEnum<acustra::Tuning> (input); break;
            case 4: parameters.picking = anyEnum<acustra::PickingTechnique> (input); break;
            case 5:
            {
                float* const controls[] { &parameters.stringAge, &parameters.pluckPosition,
                                          &parameters.touch, &parameters.bodyAmount,
                                          &parameters.stereoWidth, &parameters.outputGain,
                                          &parameters.piezoMix, &parameters.releaseNoise,
                                          &parameters.room, &parameters.detune };
                *controls[input.Pick (std::size (controls))] = input.Scalar();
                break;
            }
            case 6: parameters.piezoPolarityInverted = input.Byte() & 1; break;
            case 7: performer->setTempoBpm (static_cast<double> (input.Scalar()) * 300.0); break;
            case 8: performer->setMasterTuneCents (input.Scalar() * 200.0f - 100.0f); break;
            case 9: performer->setSustainHold (input.Byte() & 1); break;
            case 10: performer->setKeyswitchSustain (input.Byte() & 1); break;
            case 11: performer->setGatherChords (input.Byte() & 1); break;
            case 12:
                performer->setStrokeMode (static_cast<acustra::Performer::StrokeMode> (input.Pick (3)));
                break;
            default: break;
        }
        performer->setParameters (parameters);

        const auto frames = static_cast<int> (1 + input.Pick (maxBlock));
        main.Arm();
        piezo.Arm();
        acustra::AcustraEngine::OutputBuses buses;
        if (input.Byte() & 1)
            buses.piezo = piezo.Left();
        performer->beginBlock (main.Left(), main.Right(), buses, frames);
        // Messages and tempo changes at non-decreasing offsets, as a
        // MidiBuffer and a host's tempo map deliver them.
        int offset = 0;
        for (auto events = input.Pick (6); events > 0; --events)
        {
            offset += static_cast<int> (input.Pick (static_cast<std::size_t> (frames)));
            if (input.Pick (8) == 0)
            {
                performer->setTempoBpmAt (offset, static_cast<double> (input.Scalar()) * 300.0);
                continue;
            }
            const std::uint8_t message[3] { input.Byte(), input.Byte(), input.Byte() };
            performer->handleMidi (offset, message, 1 + static_cast<int> (input.Pick (3)));
        }
        performer->endBlock();
        main.Check (static_cast<std::size_t> (frames));
        if (buses.piezo != nullptr)
            piezo.Check (static_cast<std::size_t> (frames));

        // Lifecycle calls between blocks, where a host makes them.
        switch (input.Pick (128))
        {
            case 0: case 1: case 2: case 3: performer->reset(); break;
            case 4: case 5: case 6: case 7: performer->engine().suspend(); break;
            case 8: case 9: case 10: case 11: performer->engine().resume(); break;
            // A real prepare, which keeps the channel layout, unlike a fresh one.
            case 12: performer->prepare (input.Rate(), maxBlock); break;
            default: break;
        }
    }
    return 0;
}
