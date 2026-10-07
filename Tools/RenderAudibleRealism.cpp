// Compile this identical source against each frozen DSP tree's own headers
// and library. These matched passages preserve native relative level, with
// one common gain (unity); no listening preference is inferred by rendering.
// Usage: AcustraRenderAudibleRealism NEW_OUTPUT_DIRECTORY
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int rate = 48000;
constexpr int duration = 8 * rate;
struct Event { int frame, channel, kind, a, b; };
struct Passage { std::string name; acustra::PickingTechnique picking; std::vector<Event> events; };
int frame(double seconds) { return static_cast<int>(std::lround(seconds * rate)); }
void event(Passage& p, double seconds, int kind, int a, int b, int channel = 1)
{ p.events.push_back({ frame(seconds), channel, kind, a, b }); }
void note(Passage& p, double seconds, double length, int pitch, int velocity,
          int channel = 1, int release = 64)
{ event(p, seconds, 0x90, pitch, velocity, channel); event(p, seconds + length, 0x80, pitch, release, channel); }
void bend(Passage& p, double seconds, int value, int channel)
{ event(p, seconds, 0xe0, value & 127, (value >> 7) & 127, channel); }
std::vector<Passage> passages()
{
    using acustra::PickingTechnique;
    Passage strums { "01-pick-alternating-strums", PickingTechnique::Pick, {} };
    constexpr std::array<std::array<int, 6>, 3> shapes {{
        {{40,47,52,56,59,64}}, {{45,52,57,60,64,69}}, {{43,47,50,55,59,67}}
    }};
    for (int stroke = 0; stroke < 12; ++stroke)
        for (const int pitch : shapes[static_cast<std::size_t>((stroke / 4) % 3)])
            note(strums, 0.2 + 0.4 * stroke, 0.36, pitch,
                 stroke % 4 == 0 ? 105 : stroke % 2 == 0 ? 82 : 62);
    // Soft, fast return strokes expose continuing-string phase interaction.
    for (int stroke = 0; stroke < 12; ++stroke)
        for (const int pitch : shapes[0])
            note(strums, 5.2 + 0.075 * stroke, 0.067, pitch, stroke % 3 == 0 ? 60 : 30);
    event(strums, 6.25, 0xb0, 2, 94); event(strums, 6.7, 0xb0, 2, 0);
    std::vector<Passage> result { strums };
    for (const auto picking : { PickingTechnique::Finger, PickingTechnique::Pick, PickingTechnique::Thumb })
    {
        const std::string style = picking == PickingTechnique::Finger ? "finger"
            : picking == PickingTechnique::Pick ? "pick" : "thumb";
        Passage slides { "02-" + style + "-slid-attacks", picking, {} };
        event(slides, 0.0, 0xb0, 126, 0); // Explicit physical-string channels.
        // The conventional wheel is a longitudinal slide, with a declared
        // +/-12-semitone RPN range. All revisions receive identical MIDI.
        for (const int channel : { 1, 3, 6 })
        {
            event(slides, 0.0, 0xb0, 101, 0, channel);
            event(slides, 0.0, 0xb0, 100, 0, channel);
            event(slides, 0.0, 0xb0, 6, 12, channel);
            event(slides, 0.0, 0xb0, 38, 0, channel);
        }
        // A reference, then equal MIDI/velocity attacks already slid up/down.
        note(slides, 0.2, 0.72, 57, 88, 3);
        bend(slides, 1.05, 14336, 3); note(slides, 1.2, 0.72, 57, 88, 3);
        bend(slides, 2.05, 4096, 1); note(slides, 2.2, 0.72, 52, 88, 1);
        bend(slides, 3.05, 12288, 6); note(slides, 3.2, 0.72, 72, 88, 6);
        // Move a held wound string before repicking, then a longer glissando.
        bend(slides, 4.05, 8192, 3); note(slides, 4.2, 2.20, 55, 79, 3);
        for (int step = 1; step <= 10; ++step)
            bend(slides, 4.4 + step * 0.025, 8192 + step * 409, 3);
        note(slides, 4.95, 0.70, 55, 79, 3);
        for (int step = 0; step < 14; ++step)
            bend(slides, 5.5 + step * 0.035, 12288 - step * 512, 3);
        note(slides, 6.15, 0.85, 55, 79, 3);
        result.push_back(std::move(slides));
    }
    return result;
}
void writeWave(const std::filesystem::path& path, const std::vector<float>& samples)
{
    std::ofstream out(path, std::ios::binary);
    const auto word = [&](std::uint32_t value, int bytes)
    { for (int byte = 0; byte < bytes; ++byte) out.put(static_cast<char>((value >> (8 * byte)) & 255u)); };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    out.write("RIFF", 4); word(36 + bytes, 4); out.write("WAVEfmt ", 8);
    word(16, 4); word(3, 2); word(2, 2); word(rate, 4); word(rate * 8, 4);
    word(8, 2); word(32, 2); out.write("data", 4); word(bytes, 4);
    for (const float sample : samples)
    {
        if (!std::isfinite(sample) || std::abs(sample) > 1.0f)
            throw std::runtime_error("nonfinite or unbounded listening audio");
        out.write(reinterpret_cast<const char*>(&sample), sizeof(sample));
    }
    if (!out) throw std::runtime_error("could not write listening wave");
}
void render(Passage passage, const std::filesystem::path& output, float room,
            acustra::GuitarModel model, std::ofstream& manifest)
{
    std::stable_sort(passage.events.begin(), passage.events.end(),
        [](const Event& a, const Event& b) { return a.frame < b.frame; });
    auto player = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = passage.picking; parameters.room = room; parameters.guitarModel = model;
    if (model == acustra::GuitarModel::Bellido1978)
    { parameters.shape = acustra::BodyShape::Auditorium; parameters.bodyMaterial = acustra::BodyMaterial::Mahogany; }
    player->setParameters(parameters); player->prepare(rate, 128); player->setGatherChords(false);
    std::vector<float> audio(static_cast<std::size_t>(2 * duration));
    std::array<float, 128> left {}, right {};
    std::size_t next = 0;
    for (int at = 0; at < duration;)
    {
        const int count = std::min(128, duration - at);
        player->beginBlock(left.data(), right.data(), count);
        while (next < passage.events.size() && passage.events[next].frame < at + count)
        {
            const auto& e = passage.events[next++];
            const unsigned char bytes[] { static_cast<unsigned char>(e.kind | (e.channel - 1)),
                static_cast<unsigned char>(e.a), static_cast<unsigned char>(e.b) };
            player->handleMidi(e.frame - at, bytes, 3);
        }
        player->endBlock();
        for (int i = 0; i < count; ++i)
        { audio[static_cast<std::size_t>(2 * (at + i))] = left[static_cast<std::size_t>(i)];
          audio[static_cast<std::size_t>(2 * (at + i) + 1)] = right[static_cast<std::size_t>(i)]; }
        at += count;
    }
    const std::string modelName = model == acustra::GuitarModel::Original ? "original" : "bellido";
    const std::string name = passage.name + "-" + modelName + (room == 0.0f ? "-dry" : "-room");
    writeWave(output / (name + ".wav"), audio);
    double sum = 0.0; float peak = 0.0f;
    for (const float value : audio) { sum += static_cast<double>(value) * value; peak = std::max(peak, std::abs(value)); }
    manifest << name << ".wav\t" << peak << '\t' << std::sqrt(sum / audio.size()) << '\n';
    std::ofstream score(output / (name + ".events"));
    score << "# frame channel status data1 data2; rate 48000; seconds 8; gain 1; room " << room << '\n';
    for (const auto& e : passage.events)
        score << e.frame << ' ' << e.channel << ' ' << e.kind << ' ' << e.a << ' ' << e.b << '\n';
    if (!score) throw std::runtime_error("could not preserve listening events");
}
}
int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("usage: AcustraRenderAudibleRealism NEW_OUTPUT_DIRECTORY");
        const std::filesystem::path output(argv[1]);
        if (std::filesystem::exists(output)) throw std::runtime_error("output already exists");
        std::filesystem::create_directories(output);
        std::ofstream manifest(output / "levels.tsv");
        manifest << std::setprecision(12) << "file\tpeak\trms\n";
        for (const auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
            for (const float room : { 0.0f, 0.5f })
                for (auto passage : passages()) render(std::move(passage), output, room, model, manifest);
        if (!manifest) throw std::runtime_error("could not write level manifest");
        std::cout << "Sixteen matched eight-second float-stereo passages; native relative levels, common unity gain, dry/studio and both models\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
