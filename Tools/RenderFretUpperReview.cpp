// Freeze this renderer and compile it against each source snapshot. Every
// case starts with a freshly prepared Performer (the engine's fixed seeds),
// 64-frame callbacks and sample-timed events. Native-level float stereo WAV:
// no normalization, alignment, EQ or external processing. The manifest lists
// controls/events. Upper-note windows remain held, so their late-band change
// measures decay rather than key-up damping. Fret arrivals are compared to
// Release Noise 0 on exactly the same score; plain/open/repeated/MPE cases
// distinguish a winding shift from ordinary notes and fixed-fret bends.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
constexpr int block = 64;
constexpr std::array<int, 6> openMidi { 40, 45, 50, 55, 59, 64 };
struct Event { double seconds; int kind, channel, a, b; };
struct Score
{
    std::string id, category, technique = "Finger", family;
    int rate = 48000, string = 0, fret = 0, midi = 0, velocity = 100;
    double duration = 0.72;
    float noise = 0.0f, room = 0.0f;
    bool strings = false, mpe = false;
    std::vector<Event> events;
    std::vector<double> shifts;
    void note(double time, double length, int channel, int note, int vel = 100)
    {
        events.push_back({ time, 0x90, channel, note, vel });
        events.push_back({ time + length, 0x80, channel, note, 64 });
    }
};

void writeWave(const std::filesystem::path& path, int rate,
               const std::vector<float>& audio)
{
    std::ofstream stream(path, std::ios::binary);
    const auto u16 = [&] (std::uint16_t x)
    { for (int n = 0; n < 2; ++n) stream.put(static_cast<char>(x >> (8 * n))); };
    const auto u32 = [&] (std::uint32_t x)
    { for (int n = 0; n < 4; ++n) stream.put(static_cast<char>(x >> (8 * n))); };
    const auto bytes = static_cast<std::uint32_t>(audio.size() * sizeof(float));
    stream.write("RIFF", 4); u32(36 + bytes); stream.write("WAVEfmt ", 8);
    u32(16); u16(3); u16(2); u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * 8)); u16(8); u16(32);
    stream.write("data", 4); u32(bytes);
    for (const float value : audio)
    {
        static_assert(sizeof(float) == 4);
        std::uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    if (!stream) throw std::runtime_error("cannot write WAV: " + path.string());
}

void render(Score score, const std::filesystem::path& directory,
            std::ofstream& manifest, bool first)
{
    std::stable_sort(score.events.begin(), score.events.end(),
        [] (const Event& a, const Event& b) { return a.seconds < b.seconds; });
    auto player = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = score.technique == "Pick" ? acustra::PickingTechnique::Pick
        : score.technique == "Thumb" ? acustra::PickingTechnique::Thumb
                                     : acustra::PickingTechnique::Finger;
    parameters.releaseNoise = score.noise;
    parameters.outputGain = 1.0f;
    parameters.room = score.room;
    parameters.piezoMix = 0.2f;
    player->setParameters(parameters);
    player->prepare(score.rate, block);
    player->setTempoBpm(120.0);
    player->engine().setStringPerChannelMode(score.strings);
    if (score.mpe) player->engine().setLowerZoneMemberCount(15);
    const int frames = static_cast<int>(std::lround(score.duration * score.rate));
    std::vector<float> audio(static_cast<std::size_t>(frames * 2));
    std::array<float, block> left {}, right {};
    std::size_t next = 0;
    for (int frame = 0; frame < frames; frame += block)
    {
        const int count = std::min(block, frames - frame);
        player->beginBlock(left.data(), right.data(), count);
        while (next < score.events.size()
            && std::lround(score.events[next].seconds * score.rate) < frame + count)
        {
            const auto& event = score.events[next++];
            const int offset = static_cast<int>(std::lround(event.seconds * score.rate)) - frame;
            if (event.kind == 0x90) player->noteOn(offset, event.channel, event.a, event.b);
            else if (event.kind == 0x80) player->noteOff(offset, event.channel, event.a, event.b);
            else if (event.kind == 0xe0)
            {
                player->setPitchBendRange(offset, event.channel, 12);
                player->pitchWheel(offset, event.channel,
                                   static_cast<float>(event.a) / 8192.0f);
            }
        }
        player->endBlock();
        for (int sample = 0; sample < count; ++sample)
        {
            const float l = left[static_cast<std::size_t>(sample)];
            const float r = right[static_cast<std::size_t>(sample)];
            if (!std::isfinite(l) || !std::isfinite(r))
                throw std::runtime_error("nonfinite audio: " + score.id);
            audio[static_cast<std::size_t>(2 * (frame + sample))] = l;
            audio[static_cast<std::size_t>(2 * (frame + sample) + 1)] = r;
        }
    }
    writeWave(directory / (score.id + ".wav"), score.rate, audio);
    if (!first) manifest << ",\n";
    manifest << "    {\"id\":\"" << score.id << "\",\"file\":\"" << score.id
             << ".wav\",\"category\":\"" << score.category << "\",\"family\":\""
             << score.family << "\",\"technique\":\"" << score.technique
             << "\",\"sample_rate\":" << score.rate << ",\"frames\":" << frames
             << ",\"string_number\":" << score.string << ",\"fret\":" << score.fret
             << ",\"midi\":" << score.midi << ",\"velocity\":" << score.velocity
             << ",\"release_noise\":" << score.noise << ",\"room\":" << score.room
             << ",\"piezo_mix\":0.2,\"string_per_channel\":" << (score.strings ? "true" : "false")
             << ",\"mpe\":" << (score.mpe ? "true" : "false") << ",\"shift_seconds\":[";
    for (std::size_t n = 0; n < score.shifts.size(); ++n)
    { if (n) manifest << ','; manifest << score.shifts[n]; }
    manifest << "],\"events\":[";
    for (std::size_t n = 0; n < score.events.size(); ++n)
    {
        const auto& e = score.events[n];
        if (n) manifest << ',';
        manifest << '[' << std::lround(e.seconds * score.rate) << ',' << e.kind
                 << ',' << e.channel << ',' << e.a << ',' << e.b << ']';
    }
    manifest << "]}";
}

std::vector<Score> scores()
{
    std::vector<Score> result;
    for (const int rate : { 44100, 48000, 96000 })
    {
        for (const std::string technique : { "Finger", "Pick", "Thumb" })
            for (const int string : { 5, 6 })
                for (const int fret : { 9, 12, 13, 14, 16, 19, 20 })
                    for (const int velocity : { 40, 100 })
                {
                    if (velocity == 40 && fret != 12 && fret != 16 && fret != 20)
                        continue;
                    Score score;
                    score.category = "upper"; score.family = "held-note";
                    score.rate = rate; score.technique = technique;
                    score.string = string; score.fret = fret;
                    score.velocity = velocity;
                    score.midi = openMidi[static_cast<std::size_t>(string - 1)] + fret;
                    score.strings = true;
                    score.id = "upper-" + std::to_string(rate) + "-" + technique
                        + "-s" + std::to_string(string) + "-f" + std::to_string(fret)
                        + "-v" + std::to_string(velocity);
                    score.note(0.05, 0.62, string, score.midi, velocity);
                    result.push_back(score);
                }
        for (const float noise : { 0.0f, 0.1f, 0.5f })
            for (const std::string family : { "barre-shifts", "wound-line", "plain-line",
                                              "same-fret", "open", "member-bend" })
            {
                Score score;
                score.rate = rate; score.duration = 3.4; score.category = "fret";
                score.family = family; score.room = 0.1f; score.noise = noise;
                score.id = "fret-" + std::to_string(rate) + "-" + family
                    + "-n" + std::to_string(static_cast<int>(std::lround(noise * 100)));
                if (family == "barre-shifts")
                {
                    score.technique = "Pick";
                    for (int chord = 0; chord < 5; ++chord)
                    {
                        const int shift = std::array<int, 5> { 1, 3, 5, 3, 1 }[static_cast<std::size_t>(chord)];
                        const double time = 0.12 + 0.55 * chord;
                        for (const int note : { 40, 47, 52, 56, 59, 64 })
                            score.note(time, 0.50, 1, note + shift);
                        if (chord) score.shifts.push_back(time);
                    }
                }
                else if (family == "member-bend")
                {
                    score.mpe = true;
                    score.note(0.12, 2.7, 2, 52);
                    // MPE member wheels change tension at a fixed fret:
                    // none of this score should become winding friction.
                    for (int step = 0; step <= 80; ++step)
                    {
                        const double phase = static_cast<double>(step) / 80.0;
                        score.events.push_back({ 0.6 + 1.5 * phase, 0xe0, 2,
                            static_cast<int>(std::lround(2048.0 * std::sin(phase * 6.283185307179586))), 0 });
                    }
                    score.shifts = { 0.65, 1.15, 1.65 };
                }
                else
                {
                    score.strings = true;
                    for (int index = 0; index < 6; ++index)
                    {
                        const int string = family == "plain-line" ? 5 + index / 3
                            : family == "open" ? index + 1 : 2 + index / 3;
                        const int fret = family == "open" ? 0 : family == "same-fret" ? 5
                            : std::array<int, 6> { 3, 5, 7, 9, 5, 3 }[static_cast<std::size_t>(index)];
                        const double time = 0.12 + 0.45 * index;
                        score.note(time, 0.43, string,
                            openMidi[static_cast<std::size_t>(string - 1)] + fret);
                        if (index % 3 != 0) score.shifts.push_back(time);
                    }
                }
                result.push_back(score);
            }
    }
    return result;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("usage: RenderFretUpperReview NEW_OUTPUT_DIRECTORY");
        const std::filesystem::path directory(argv[1]);
        if (std::filesystem::exists(directory)) throw std::runtime_error("output already exists");
        std::filesystem::create_directories(directory);
        std::ofstream manifest(directory / "manifest.json");
        manifest << std::setprecision(9)
            << "{\n  \"schema\":1,\n  \"block_frames\":64,\n"
               "  \"audio\":\"native-level IEEE float32 stereo\",\n"
               "  \"event_fields\":[\"sample\",\"kind\",\"channel\",\"a\",\"b\"],\n"
               "  \"upper_note_on_seconds\":0.05,\n  \"cases\":[\n";
        const auto matrix = scores();
        bool first = true;
        for (const auto& score : matrix)
        { render(score, directory, manifest, first); first = false; }
        manifest << "\n  ]\n}\n";
        if (!manifest) throw std::runtime_error("cannot write manifest");
        std::cout << matrix.size() << " cases rendered at 44.1/48/96 kHz\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
