// Identical authored scores for before/after physical-hand listening.
// Compile this same file against each frozen DSP tree. Writes stereo float
// WAVs at native level; no normalization, limiting, EQ, or timing alignment.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int rate = 48000, frames = 8 * rate;
struct Event { int frame, kind, channel, a, b; };
struct Score
{
    std::string name;
    acustra::EngineParameters parameters;
    bool strings = true, mpe = false;
    std::vector<Event> events;
    void note(double time, double length, int channel, int note, int velocity)
    {
        events.push_back({ static_cast<int>(std::lround(time * rate)), 0x90, channel, note, velocity });
        events.push_back({ static_cast<int>(std::lround((time + length) * rate)), 0x80, channel, note, 64 });
    }
    void bend(double time, int channel, int semitones)
    {
        events.push_back({ static_cast<int>(std::lround(time * rate)), 0xe0, channel, semitones, 0 });
    }
};
void writeWave(const std::filesystem::path& path, const std::vector<float>& audio)
{
    std::ofstream stream(path, std::ios::binary);
    const auto u16 = [&] (std::uint16_t x)
    { for (int n = 0; n < 2; ++n) stream.put(static_cast<char>(x >> (8 * n))); };
    const auto u32 = [&] (std::uint32_t x)
    { for (int n = 0; n < 4; ++n) stream.put(static_cast<char>(x >> (8 * n))); };
    const auto bytes = static_cast<std::uint32_t>(audio.size() * sizeof(float));
    stream.write("RIFF", 4); u32(36 + bytes); stream.write("WAVEfmt ", 8);
    u32(16); u16(3); u16(2); u32(rate); u32(rate * 8); u16(8); u16(32);
    stream.write("data", 4); u32(bytes);
    for (const float value : audio)
    {
        static_assert(sizeof(float) == 4);
        std::uint32_t bits = 0;
        const auto* source = reinterpret_cast<const unsigned char*>(&value);
        auto* dest = reinterpret_cast<unsigned char*>(&bits);
        std::copy(source, source + 4, dest);
        u32(bits);
    }
    if (!stream) throw std::runtime_error("cannot write WAV");
}
void render(const Score& score, const std::filesystem::path& directory)
{
    auto events = score.events;
    std::stable_sort(events.begin(), events.end(), [] (const Event& a, const Event& b)
    { return a.frame < b.frame; });
    auto player = std::make_unique<acustra::Performer>();
    player->setParameters(score.parameters);
    player->prepare(rate, 64);
    player->engine().setStringPerChannelMode(score.strings);
    if (score.mpe) player->engine().setLowerZoneMemberCount(15);
    std::vector<float> audio(static_cast<std::size_t>(frames * 2));
    std::array<float, 64> left {}, right {};
    std::size_t next = 0;
    for (int frame = 0; frame < frames; frame += 64)
    {
        player->beginBlock(left.data(), right.data(), 64);
        while (next < events.size() && events[next].frame < frame + 64)
        {
            const auto& e = events[next++];
            const int offset = e.frame - frame;
            if (e.kind == 0x90) player->noteOn(offset, e.channel, e.a, e.b);
            else if (e.kind == 0x80) player->noteOff(offset, e.channel, e.a, e.b);
            else if (e.kind == 0xe0)
            {
                player->setPitchBendRange(offset, e.channel, 12);
                player->pitchWheel(offset, e.channel, static_cast<float>(e.a) / 12.0f);
            }
        }
        player->endBlock();
        for (int n = 0; n < 64; ++n)
        {
            if (!std::isfinite(left[n]) || !std::isfinite(right[n]))
                throw std::runtime_error("nonfinite audio");
            audio[static_cast<std::size_t>(2 * (frame + n))] = left[n];
            audio[static_cast<std::size_t>(2 * (frame + n) + 1)] = right[n];
        }
    }
    writeWave(directory / (score.name + ".wav"), audio);
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("give a NEW output directory");
        const std::filesystem::path directory(argv[1]);
        if (std::filesystem::exists(directory)) throw std::runtime_error("output already exists");
        std::filesystem::create_directories(directory);
        std::array<Score, 5> scores {};
        auto& strum = scores[0]; strum.name = "01-curved-strums"; strum.strings = false;
        strum.parameters.picking = acustra::PickingTechnique::Pick;
        for (int stroke = 0; stroke < 8; ++stroke)
            for (const int note : { 40, 47, 52, 56, 59, 64 })
                strum.note(0.2 + stroke * 0.72, 0.52, 1, note, stroke % 3 == 0 ? 108 : 80);
        auto& point = scores[1]; point.name = "02-contact-position-and-force";
        point.parameters.picking = acustra::PickingTechnique::Pick;
        point.parameters.pluckPosition = 0.15f;
        for (int n = 0; n < 16; ++n)
            point.note(0.2 + n * 0.34, 0.24, 3, 52, 100);
        auto& bend = scores[2]; bend.name = "03-pre-bent-string"; bend.mpe = true;
        for (int n = 0; n < 9; ++n)
        {
            const double time = 0.2 + n * 0.64;
            bend.bend(time, 2, (n % 3) * 2);
            bend.note(time, 0.45, 2, 57, 110);
        }
        auto& thumb = scores[3]; thumb.name = "04-thumb-contact";
        thumb.parameters.picking = acustra::PickingTechnique::Thumb;
        // Sample one smooth crescendo/diminuendo at each contact. These are
        // MIDI 1 velocities, not sound layers: only the final transport value
        // is rounded. Keep pitch/string fixed so their tone cannot disguise
        // the continuously changing force and contact response.
        for (int n = 0; n < 24; ++n)
        {
            const double phase = 3.14159265358979323846 * n / 23.0;
            const double gesture = std::sin(phase);
            const int velocity = static_cast<int>(std::lround(
                24.0 + 96.0 * gesture * gesture));
            thumb.note(0.2 + n * 0.24, 0.18, 2, 45, velocity);
        }
        auto& release = scores[4]; release.name = "05-picking-hand-release";
        release.parameters.picking = acustra::PickingTechnique::Pick;
        release.parameters.pluckPosition = 1.0f;
        release.parameters.releaseNoise = 0.5f;
        for (int n = 0; n < 8; ++n)
        {
            const double time = 0.2 + n * 0.72;
            release.bend(time, 1, n % 3 == 0 ? 0 : n % 3 == 1 ? 7 : -7);
            release.note(time, 0.16, 1, n % 2 ? 48 : 40, 108);
        }
        for (const auto& score : scores) render(score, directory);
        std::cout << "Five identical-score 8-second dry stereo cases at 48 kHz, native gain.\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
