// Small before/after listening cases through the shared MIDI performer.
// Build against each frozen DSP library with that library's own headers:
// c++ -std=c++20 -O3 -ISource Tools/RenderRealismReview.cpp \
//     BUILD/libAcustraDSP.a -o BUILD/AcustraRenderRealismReview
// Usage: AcustraRenderRealismReview NEW_OUTPUT_DIRECTORY
// Native relative levels are retained; no separate normalisation or processing.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
constexpr int rate = 48000;
struct Event { int frame, kind, channel, a, b; };

void writeWave(const std::filesystem::path& path, const std::vector<float>& audio)
{
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>((value >> (8 * i)) & 255u));
    };
    const auto bytes = static_cast<std::uint32_t>(audio.size() * 2);
    out.write("RIFF", 4); word(36 + bytes, 4); out.write("WAVEfmt ", 8);
    word(16, 4); word(1, 2); word(2, 2); word(rate, 4);
    word(rate * 4, 4); word(4, 2); word(16, 2);
    out.write("data", 4); word(bytes, 4);
    for (const float sample : audio)
    {
        if (!std::isfinite(sample) || std::abs(sample) > 1.0f)
            throw std::runtime_error("invalid rendered audio");
        word(static_cast<std::uint16_t>(static_cast<std::int16_t>(
            std::lround(sample * 32767.0f))), 2);
    }
    if (!out) throw std::runtime_error("could not write audio");
}

void render(const std::filesystem::path& path, std::vector<Event> events,
            bool stringChannels = false, float room = 0.5f)
{
    std::stable_sort(events.begin(), events.end(),
        [](const Event& a, const Event& b) { return a.frame < b.frame; });
    auto player = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = acustra::PickingTechnique::Pick;
    parameters.room = room;
    player->setParameters(parameters);
    player->prepare(rate, 64);
    player->setGatherChords(false);
    player->engine().setStringPerChannelMode(stringChannels);
    constexpr int frames = 5 * rate;
    std::vector<float> audio(2 * frames);
    std::array<float, 64> left {}, right {};
    std::size_t event = 0;
    for (int frame = 0; frame < frames;)
    {
        int count = std::min(64, frames - frame);
        if (event < events.size() && events[event].frame > frame)
            count = std::min(count, events[event].frame - frame);
        player->beginBlock(left.data(), right.data(), count);
        while (event < events.size() && events[event].frame == frame)
        {
            const auto& e = events[event++];
            if (e.kind == 0x90) player->noteOn(0, e.channel, e.a, e.b);
            else if (e.kind == 0x80) player->noteOff(0, e.channel, e.a, e.b);
            else player->controlChange(0, e.channel, e.a, e.b);
        }
        player->endBlock();
        for (int i = 0; i < count; ++i)
        {
            audio[static_cast<std::size_t>(2 * (frame + i))] = left[std::size_t(i)];
            audio[static_cast<std::size_t>(2 * (frame + i) + 1)] = right[std::size_t(i)];
        }
        frame += count;
    }
    writeWave(path, audio);
}

std::vector<Event> chords(std::initializer_list<int> notes)
{
    std::vector<Event> events;
    for (const int start : { rate / 5, 3 * rate / 2, 14 * rate / 5 })
        for (const int note : notes)
        {
            events.push_back({ start, 0x90, 1, note, 100 });
            events.push_back({ start + rate * 3 / 4, 0x80, 1, note, 64 });
        }
    return events;
}

std::vector<Event> rolledChords(std::initializer_list<int> notes)
{
    std::vector<Event> events;
    for (const int start : { rate / 5, 3 * rate / 2, 14 * rate / 5 })
    {
        int offset = 0;
        for (const int note : notes)
        {
            events.push_back({ start + offset, 0x90, 1, note, 100 });
            events.push_back({ start + rate * 3 / 4, 0x80, 1, note, 64 });
            offset += rate / 100;
        }
    }
    return events;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("give a new output directory");
        const std::filesystem::path directory(argv[1]);
        if (std::filesystem::exists(directory))
            throw std::runtime_error("output directory already exists");
        std::filesystem::create_directories(directory);
        render(directory / "01-crossed-voicing.wav", chords({ 60, 64, 67 }));
        render(directory / "02-skipped-strings.wav", chords({ 40, 59, 64 }));
        render(directory / "03-repluck-palm-mute.wav", {
            { rate / 5, 0x90, 6, 64, 100 },
            { rate * 4 / 5, 0x90, 6, 67, 100 },
            { rate * 81 / 100, 0xb0, 1, 2, 127 },
            { rate * 88 / 100, 0xb0, 1, 2, 0 },
            { rate, 0xb0, 1, 2, 127 },
            { rate * 8 / 5, 0xb0, 1, 2, 0 },
            { rate * 9 / 5, 0x90, 6, 69, 100 },
            { rate * 21 / 10, 0xb0, 1, 2, 127 },
            { rate * 13 / 5, 0x80, 6, 69, 64 },
            { rate * 3, 0xb0, 1, 2, 0 }
        }, true);
        render(directory / "04-high-notes-air-decay.wav", chords({ 72 }), false, 0.0f);
        render(directory / "05-live-rolled-chord.wav", rolledChords({ 60, 64, 67 }), false, 0.0f);
        render(directory / "06-dense-live-roll.wav", rolledChords({ 47, 55, 60, 64, 71 }), false, 0.0f);
        std::cout << "Six 5-second stereo review cases at 48 kHz; strums/mute\n"
                     "at Room 50%, high-note decay and live rolls dry.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
