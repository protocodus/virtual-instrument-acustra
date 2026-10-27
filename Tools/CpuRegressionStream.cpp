// Fixed regression stream for CPU work on the canonical DSP: plays the
// committed factory-preset phrases through the Performer, the way the Reason
// Rack Extension drives it, and writes the raw float32 Main L/R output so a
// candidate can be compared byte for byte against its baseline and its
// instructions counted under callgrind.
//
//   AcustraCpuRegressionStream <midi-dir> <out.f32> [rate=96000] [block=128]
//                              [seconds=4] [presets=all, e.g. 0,3,7]
//
// Measurement protocol (Docs/benchmarks/cpu-instruction-trims-2026-10-25.md):
//
// - Workload. Each listed factory preset (Source/DSP/AcustraPresets.h) is
//   prepared exactly as Tools/RenderPresetDemos.cpp prepares it, its tempo
//   read from the phrase, then Docs/audio/midi/<NN>-<slug>.mid plays from
//   the start for `seconds` seconds. Every event reaches the Performer at its
//   own sample offset inside a fixed-size block (Performer::handleMidi), as
//   the Rack wrapper delivers it. The product configuration is the Rack's
//   default 2x quality at 48 kHz: the engine runs at 96 kHz in 128-frame
//   blocks (64-frame Rack batches times the factor). The heaviest supported
//   quality is 4x at 48 kHz: 192 kHz, 256 frames, which needs the
//   ACUSTRA_EXTENDED_SAMPLE_RATES layout the Rack compiles.
// - Build. The Rack-like build is clang++ -std=c++17 -O3 -fno-builtin
//   -DACUSTRA_EXTENDED_SAMPLE_RATES=1 -DNDEBUG for the engine, Performer and
//   this file (Tools/BuildCpuRegressionStream.sh). The host build drops
//   -fno-builtin. No fast-math, FMA or LTO flag: they are not the Rack's.
// - Count. valgrind --tool=callgrind --collect-atstart=no
//   --toggle-collect=acustraCpuRenderStream: only rendering is counted;
//   reading MIDI, construction, prepare and writing the file are not.
//   Instruction counts (Ir) are reproducible on a shared, contended machine;
//   wall or thread-CPU timings there are not, and are not used.
// - Parity. The output file is every preset's Main L/R float32 frames,
//   interleaved, concatenated in preset order. A candidate is graded "exact"
//   or "last bit" only when its file is byte-identical to the baseline's
//   (cmp), built with the same compiler and flags, on every preset listed.

#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"
#include "PresetDemoRenderer.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace
{
namespace demos = protocodus::demos;

double phraseTempo(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const std::vector<unsigned char> data((std::istreambuf_iterator<char>(file)), {});
    for (std::size_t i = 0; i + 6 <= data.size(); ++i)
        if (data[i] == 0xFF && data[i + 1] == 0x51 && data[i + 2] == 0x03)
        {
            const auto microseconds = data[i + 3] << 16 | data[i + 4] << 8 | data[i + 5];
            if (microseconds > 0)
                return 60.0e6 / microseconds;
        }
    return 120.0;
}

struct Stream
{
    std::unique_ptr<acustra::Performer> performer = std::make_unique<acustra::Performer>();
    std::vector<demos::MidiEvent> events;
    std::vector<float> left, right;
};

// The only function callgrind counts.
#if defined(_MSC_VER)
__declspec(noinline)
#else
__attribute__((noinline))
#endif
void acustraCpuRenderStream(std::vector<Stream>& streams,
                                                      double rate, int block)
{
    for (auto& stream : streams)
    {
        const auto frames = stream.left.size();
        const auto due = [&](std::size_t i)
        { return static_cast<std::size_t>(std::llround(stream.events[i].seconds * rate)); };
        std::size_t next = 0;
        for (std::size_t start = 0; start < frames; start += static_cast<std::size_t>(block))
        {
            const auto count = std::min(frames - start, static_cast<std::size_t>(block));
            stream.performer->beginBlock(&stream.left[start], &stream.right[start],
                                         static_cast<int>(count));
            while (next < stream.events.size() && due(next) < start + count)
            {
                const auto at = due(next) > start ? due(next) - start : 0;
                stream.performer->handleMidi(static_cast<int>(at), stream.events[next].bytes,
                                             stream.events[next].size);
                ++next;
            }
            stream.performer->endBlock();
        }
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: " << argv[0]
                  << " <midi-dir> <out.f32> [rate=96000] [block=128] [seconds=4] [presets=all]\n";
        return 2;
    }
    const std::filesystem::path midiDir = argv[1];
    const double rate = argc > 3 ? std::atof(argv[3]) : 96000.0;
    const int block = argc > 4 ? std::atoi(argv[4]) : 128;
    const double seconds = argc > 5 ? std::atof(argv[5]) : 4.0;
    std::vector<std::size_t> presets;
    if (argc > 6 && std::string(argv[6]) != "all")
    {
        std::stringstream list(argv[6]);
        for (std::string item; std::getline(list, item, ',');)
            presets.push_back(static_cast<std::size_t>(std::stoul(item)));
    }
    else
        for (std::size_t i = 0; i < acustra::factoryPresets.size(); ++i)
            presets.push_back(i);

    demos::Instrument instrument;
    for (const auto& preset : acustra::factoryPresets)
        instrument.presets.emplace_back(preset.name);

    std::vector<Stream> streams(presets.size());
    for (std::size_t s = 0; s < presets.size(); ++s)
    {
        const auto index = presets[s];
        if (index >= acustra::factoryPresets.size())
            return 2;
        const auto path = midiDir / (demos::detail::Stem(instrument, index) + ".mid");
        auto& stream = streams[s];
        stream.events = demos::ReadMidi(path);
        const auto parameters = acustra::engineParameters(acustra::factoryPresets[index]);
        stream.performer->setParameters(parameters);
        stream.performer->prepare(rate, block);
        stream.performer->setParameters(parameters);
        stream.performer->setTempoBpm(phraseTempo(path));
        if (stream.performer->engine().sampleRate() != rate)
        {
            std::cerr << "rate " << rate << " is not modelled by this build\n";
            return 2;
        }
        const auto frames = static_cast<std::size_t>(std::llround(seconds * rate));
        stream.left.assign(frames, 0.0f);
        stream.right.assign(frames, 0.0f);
    }

    acustraCpuRenderStream(streams, rate, block);

    std::ofstream out(argv[2], std::ios::binary);
    for (std::size_t s = 0; s < streams.size(); ++s)
    {
        const auto& stream = streams[s];
        double sum = 0.0;
        float peak = 0.0f;
        for (std::size_t i = 0; i < stream.left.size(); ++i)
        {
            const float frame[2] { stream.left[i], stream.right[i] };
            out.write(reinterpret_cast<const char*>(frame), sizeof frame);
            sum += double(frame[0]) * frame[0] + double(frame[1]) * frame[1];
            peak = std::max({ peak, std::abs(frame[0]), std::abs(frame[1]) });
        }
        const double rms = std::sqrt(sum / (2.0 * double(stream.left.size())));
        std::printf("%s: rms %.2f dBFS, peak %.2f dBFS\n",
                    acustra::factoryPresets[presets[s]].slug, 20.0 * std::log10(rms + 1e-30),
                    20.0 * std::log10(double(peak) + 1e-30));
    }
    return out ? 0 : 1;
}
