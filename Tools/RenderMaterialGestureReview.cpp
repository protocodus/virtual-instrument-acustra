// Identical authored scores for before/after material and gesture listening.
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
    void note(double time, double length, int channel, int note, int velocity, int releaseVelocity = 64)
    {
        events.push_back({ static_cast<int>(std::lround(time * rate)), 0x90, channel, note, velocity });
        events.push_back({ static_cast<int>(std::lround((time + length) * rate)), 0x80, channel, note, releaseVelocity });
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
            else if (e.kind == 0xb0) player->controlChange(offset, e.channel, e.a, e.b);
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
        std::array<Score, 9> scores {};
        auto& unison = scores[0]; unison.name = "01-open-and-fretted-unisons";
        for (int n = 0; n < 9; ++n)
            unison.note(0.2+n*0.76, 0.47, 6-n%3, 64, 90);
        auto& steel = scores[1]; steel.name = "02-plain-steel-up-the-neck";
        steel.parameters.picking = acustra::PickingTechnique::Pick;
        constexpr int frets[] { 0, 5, 12, 19 };
        for (int n = 0; n < 8; ++n)
            steel.note(0.2+n*0.83, 0.65, n<4 ? 5 : 6,
                       (n<4 ? 59 : 64)+frets[n%4], 101);
        auto& wound = scores[2]; wound.name = "03-wound-open-and-fretted";
        constexpr int basses[] { 40, 45, 50, 55 };
        for (int n = 0; n < 8; ++n)
            wound.note(0.2+n*0.77, 0.55, n/2+1, basses[n/2]+12*(n%2), 93);
        auto& hand = scores[3]; hand.name = "04-one-hand-mixed-fret-chords";
        hand.parameters.picking = acustra::PickingTechnique::Thumb;
        constexpr int chord[] { 40, 52, 57, 55, 64, 76 };
        for (int stroke = 0; stroke < 7; ++stroke)
            for (int string = 0; string < 6; ++string)
                hand.note(0.2+stroke*0.86+string*0.013, 0.61,
                          string+1, chord[string], 86);
        auto& damping = scores[4]; damping.name = "05-fretting-and-picking-hand-stops";
        damping.parameters.picking = acustra::PickingTechnique::Pick;
        for (int n = 0; n < 12; ++n)
            damping.note(0.2+n*0.55, 0.12, 6, 64+(n%4)*5, 102);
        auto& posture = scores[5]; posture.name = "06-phrases-and-rests";
        constexpr double times[] { .2, .55, .9, 3.02, 3.37, 3.72, 5.85, 6.2, 6.55 };
        for (int n = 0; n < 9; ++n)
            posture.note(times[n], 0.21, 3, 57, 99);
        auto& brush = scores[6]; brush.name = "07-continuous-release-brush";
        brush.parameters.releaseNoise = 0.5f;
        for (int n = 0; n < 15; ++n)
        {
            const double u = std::sin(3.14159265358979323846*n/14.0);
            brush.note(0.2+n*0.46, .19, 2, 52, 104,
                       static_cast<int>(std::lround(12+110*u*u)));
        }
        auto& slide = scores[7]; slide.name = "08-slide-to-and-from-open";
        slide.parameters.picking = acustra::PickingTechnique::Pick;
        slide.parameters.releaseNoise = 0.35f;
        for (int n = 0; n < 8; ++n)
        {
            const double time = .2+n*.84;
            slide.bend(time, 2, n%2 ? -7 : 7);
            slide.note(time, .30, 2, n%2 ? 52 : 45, 104);
        }
        auto& vibrato = scores[8]; vibrato.name = "09-fretting-vibrato-and-nut";
        vibrato.events.push_back({ 0, 0xb0, 2, 1, 110 });
        for (int n = 0; n < 4; ++n)
        {
            const double time = .2+n*1.7;
            vibrato.bend(time, 2, n%2 ? -7 : 7);
            vibrato.note(time, 1.4, 2, n%2 ? 52 : 45, 94);
        }
        for (const auto& score : scores) render(score, directory);
        std::cout << "Nine identical-score 8-second dry stereo cases at 48 kHz, native gain.\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
