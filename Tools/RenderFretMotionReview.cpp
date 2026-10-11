// Identical authored scores for before/after subtle fret-motion listening.
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
struct Event { int frame, kind, channel; float a; int b; };
struct Score
{
    std::string name;
    acustra::EngineParameters parameters;
    bool strings = true, mpe = false;
    std::vector<Event> events;
    void note(double time, double length, int channel, int note, int velocity, int releaseVelocity = 64)
    {
        events.push_back({ static_cast<int>(std::lround(time * rate)), 0x90, channel, static_cast<float>(note), velocity });
        events.push_back({ static_cast<int>(std::lround((time + length) * rate)), 0x80, channel, static_cast<float>(note), releaseVelocity });
    }
    void bend(double time, int channel, float semitones)
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
            if (e.kind == 0x90) player->noteOn(offset, e.channel, static_cast<int>(e.a), e.b);
            else if (e.kind == 0x80) player->noteOff(offset, e.channel, static_cast<int>(e.a), e.b);
            else if (e.kind == 0xb0) player->controlChange(offset, e.channel, static_cast<int>(e.a), e.b);
            else if (e.kind == 0xe0)
            {
                player->setPitchBendRange(offset, e.channel, 12);
                player->pitchWheel(offset, e.channel, e.a / 12.0f);
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
        // Continuous semitone trajectories sampled every 2 ms; identical
        // host events, physical controls and random seeds in both versions.
        const auto slide = [] (Score& score, int channel, double start,
                               double duration, float from, float to)
        {
            const int count = static_cast<int>(std::lround(duration / .002));
            for (int i = 0; i <= count; ++i)
            {
                const double u = static_cast<double>(i) / count;
                const float position = static_cast<float>(u*u*(3-2*u));
                score.bend(start + duration*u, channel, from+(to-from)*position);
            }
        };
        std::array<Score, 6> scores {};
        auto& slow = scores[0]; slow.name = "01-wound-slow-slides";
        slow.parameters.releaseNoise = .7f;
        slow.note(.2, 6.4, 2, 52, 96);
        slide(slow, 2, .75, .65, 0, 4);
        slide(slow, 2, 2.2, .45, 4, 0);
        slide(slow, 2, 3.5, .7, 0, -7);
        slide(slow, 2, 5.2, .55, -7, 0);
        auto& quick = scores[1]; quick = slow;
        quick.name = "02-wound-brief-slides";
        quick.events.clear();
        quick.note(.2, 6.4, 4, 62, 96);
        slide(quick, 4, .7, .12, 0, 3);
        slide(quick, 4, 1.5, .18, 3, -2);
        slide(quick, 4, 2.4, .15, -2, 0);
        slide(quick, 4, 4.3, .22, 0, 5);
        slide(quick, 4, 5.4, .2, 5, 0);
        auto& plain = scores[2]; plain = slow;
        plain.name = "03-plain-string-control";
        for (auto& event : plain.events)
        {
            event.channel = 6;
            if (event.kind == 0x90 || event.kind == 0x80) event.a = 71;
        }
        auto& still = scores[3]; still.name = "04-ordinary-notes-control";
        still.parameters.releaseNoise = .7f;
        for (int i = 0; i < 9; ++i) still.note(.2+.72*i, .43, 2, 50+i%4, 96);
        auto& bypass = scores[4]; bypass = slow;
        bypass.name = "05-zero-noise-control";
        bypass.parameters.releaseNoise = 0;
        auto& member = scores[5]; member = slow;
        member.name = "06-member-bend-control";
        member.mpe = true;
        for (const auto& score : scores) render(score, directory);
        std::cout << "Six identical-score 8-second cases: two wound slides and four unchanged controls.\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
