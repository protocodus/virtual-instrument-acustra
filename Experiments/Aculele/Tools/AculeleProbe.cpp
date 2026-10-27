// Renders one MIDI file through one factory preset, for listening and for the
// offline analysis in Tools/analyse_probe.py:
//
//   AculeleProbe <preset-index> <in.mid> <out.wav> [seconds] [--float]
//                [--shape N] [--wood N] [--strings N] [--picking N]
//                [--position X]
//
// The --options replace one field of the preset (the enums' numbers in
// AculeleEngine.h, Pluck Position 0-1), so one phrase can be compared across
// constructions without a preset for each.
// The phrase plays through the Performer exactly as the demo renderer drives
// it (48 kHz, 256-sample blocks, events at their sample). The output is
// 48 kHz stereo, 24-bit PCM, or 32-bit float with --float, unnormalised.

#include "DSP/AculelePerformer.h"
#include "DSP/AculelePresets.h"
#include "PresetDemoRenderer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
void writeWav(const std::string& path, const std::vector<float>& left,
              const std::vector<float>& right, bool asFloat)
{
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&](std::uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    const auto put16 = [&](std::uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const std::uint32_t frames = static_cast<std::uint32_t>(left.size());
    const std::uint16_t bytes = asFloat ? 4 : 3;
    const std::uint32_t dataSize = frames * 2u * bytes;
    out.write("RIFF", 4); put32(36 + dataSize); out.write("WAVEfmt ", 8);
    put32(16); put16(asFloat ? 3 : 1); put16(2); put32(48000);
    put32(48000u * 2u * bytes); put16(static_cast<std::uint16_t>(2 * bytes));
    put16(static_cast<std::uint16_t>(8 * bytes));
    out.write("data", 4); put32(dataSize);
    for (std::uint32_t i = 0; i < frames; ++i)
        for (const float s : { left[i], right[i] })
        {
            if (asFloat)
                out.write(reinterpret_cast<const char*>(&s), 4);
            else
            {
                const double c = std::max(-1.0, std::min(1.0, static_cast<double>(s)));
                const auto v = static_cast<std::int32_t>(std::lround(c * 8388607.0));
                const char b[3] { static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff),
                                  static_cast<char>((v >> 16) & 0xff) };
                out.write(b, 3);
            }
        }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: AculeleProbe <preset> <in.mid> <out.wav> [seconds] [--float]\n");
        return 2;
    }
    const auto preset = static_cast<std::size_t>(std::atoi(argv[1]));
    if (preset >= aculele::factoryPresets.size())
    {
        std::fprintf(stderr, "preset %zu out of range\n", preset);
        return 2;
    }
    const auto events = protocodus::demos::ReadMidi(argv[2]);
    double seconds = 0.0;
    bool asFloat = false;
    auto parameters = aculele::engineParameters(aculele::factoryPresets[preset]);
    for (int i = 4; i < argc; ++i)
    {
        const bool valued = i + 1 < argc;
        if (std::strcmp(argv[i], "--float") == 0)
            asFloat = true;
        else if (valued && std::strcmp(argv[i], "--shape") == 0)
            parameters.shape = static_cast<aculele::BodyShape>(std::atoi(argv[++i]));
        else if (valued && std::strcmp(argv[i], "--wood") == 0)
            parameters.bodyMaterial = static_cast<aculele::BodyMaterial>(std::atoi(argv[++i]));
        else if (valued && std::strcmp(argv[i], "--strings") == 0)
            parameters.strings = static_cast<aculele::StringMaterial>(std::atoi(argv[++i]));
        else if (valued && std::strcmp(argv[i], "--picking") == 0)
            parameters.picking = static_cast<aculele::PickingTechnique>(std::atoi(argv[++i]));
        else if (valued && std::strcmp(argv[i], "--position") == 0)
            parameters.pluckPosition = static_cast<float>(std::atof(argv[++i]));
        else
            seconds = std::atof(argv[i]);
    }
    if (seconds <= 0.0)
        seconds = (events.empty() ? 0.0 : events.back().seconds) + 3.0;

    constexpr int block = 256;
    auto performer = std::make_unique<aculele::Performer>();
    performer->setParameters(parameters);
    performer->prepare(48000.0, block);
    performer->setParameters(parameters);
    performer->setTempoBpm(120.0);

    const auto total = static_cast<std::size_t>(seconds * 48000.0);
    std::vector<float> left(total), right(total);
    // As the shared demo renderer does: each event starts a sub-block, so it
    // is played at that sub-block's first sample.
    const auto due = [&](std::size_t i)
    { return static_cast<std::size_t>(std::llround(events[i].seconds * 48000.0)); };
    std::size_t next = 0, done = 0;
    while (done < total)
    {
        const std::size_t blockEnd = std::min(total, done + static_cast<std::size_t>(block));
        while (done < blockEnd)
        {
            std::vector<std::size_t> now;
            while (next < events.size() && due(next) <= done)
                now.push_back(next++);
            const std::size_t until = next < events.size() ? std::min(blockEnd, due(next)) : blockEnd;
            performer->beginBlock(left.data() + done, right.data() + done, static_cast<int>(until - done));
            for (const auto i : now)
                performer->handleMidi(0, events[i].bytes, events[i].size);
            performer->endBlock();
            done = until;
        }
    }
    writeWav(argv[3], left, right, asFloat);
    return 0;
}
