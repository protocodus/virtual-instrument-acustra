// Identical original scores for baseline/candidate listening renders.
// Compile this source against EACH revision's headers and libAcustraDSP.a.
// No external effects, gain changes or per-note normalization are applied here.
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
constexpr int sampleRate = 48000;
struct Event { int frame, channel, kind, a, b; };
struct Song
{
    std::string name;
    double seconds;
    acustra::PickingTechnique picking;
    std::vector<Event> events;
};
int frame(double seconds) { return static_cast<int>(std::lround(seconds * sampleRate)); }
void event(Song& song, double seconds, int kind, int a, int b, int channel = 1)
{
    song.events.push_back({ frame(seconds), channel, kind, a, b });
}
void note(Song& song, double start, double length, int pitch, int velocity,
          int channel = 1, int release = 64)
{
    event(song, start, 0x90, pitch, velocity, channel);
    event(song, start + length, 0x80, pitch, release, channel);
}
std::vector<Song> songs()
{
    using acustra::PickingTechnique;
    std::vector<Song> result;
    Song picking { "01-evening-fingerpicking", 17.0, PickingTechnique::Finger, {} };
    const std::array<std::array<int, 4>, 4> shapes {{
        {{45, 52, 60, 64}}, {{41, 53, 60, 65}},
        {{48, 55, 60, 64}}, {{43, 55, 59, 67}}
    }};
    for (int bar = 0; bar < 8; ++bar)
    {
        const auto& shape = shapes[static_cast<std::size_t>(bar % 4)];
        const double start = 0.2 + bar * 1.82;
        note(picking, start, 1.70, shape[0], 69 + 3 * (bar % 3), 1, 45);
        for (int beat = 0; beat < 8; ++beat)
        {
            const int voice = 1 + ((beat == 2 || beat == 3 || beat == 6) ? 2 : beat % 3);
            note(picking, start + beat * 0.22 + 0.018, 0.52,
                 shape[static_cast<std::size_t>(voice)],
                 beat % 4 == 0 ? 91 : 69, 1, 48 + beat * 4);
        }
    }
    result.push_back(picking);

    Song strumming { "02-open-road-strumming", 19.0, PickingTechnique::Pick, {} };
    const std::array<std::array<int, 6>, 4> chords {{
        {{40,47,52,56,59,64}}, {{45,52,57,61,64,69}},
        {{48,52,55,60,64,67}}, {{43,47,50,55,59,67}}
    }};
    for (int bar = 0; bar < 8; ++bar)
    {
        const auto& chord = chords[static_cast<std::size_t>(bar % 4)];
        const double start = 0.2 + bar * 2.02;
        for (int stroke = 0; stroke < 4; ++stroke)
            for (int pitch : chord)
                note(strumming, start + stroke * 0.49, 0.44, pitch,
                     stroke == 0 ? 103 : stroke == 2 ? 84 : 54, 1,
                     stroke == 3 ? 110 : 64);
    }
    // A quiet fast brush exposes pending attacks on physically skipped strings.
    for (int stroke = 0; stroke < 8; ++stroke)
        for (int pitch : chords[0])
            note(strumming, 16.5 + stroke * 0.062, 0.052, pitch, 22, 1, 96);
    event(strumming, 17.1, 0xb0, 2, 100);
    event(strumming, 17.65, 0xb0, 2, 0);
    result.push_back(strumming);

    Song melody { "03-connected-melody", 17.0, PickingTechnique::Finger, {} };
    event(melody, 0, 0xb0, 126, 0); // Explicit physical string channels.
    const std::array<int, 8> line {{64,65,67,65,64,67,69,67}};
    for (int phrase = 0; phrase < 4; ++phrase)
    {
        const double start = 0.2 + phrase * 3.62;
        note(melody, start, 3.4, phrase % 2 ? 45 : 40, 66, 1, 38);
        event(melody, start, 0xb0, 65, 0, 6);
        note(melody, start + 0.03, 0.40, line[0], 91, 6, 64);
        event(melody, start + 0.25, 0xb0, 65, 127, 6);
        for (int i = 1; i < 8; ++i)
        {
            const double at = start + 0.03 + i * 0.36;
            event(melody, at, 0xb0, 84, line[static_cast<std::size_t>(i - 1)], 6);
            note(melody, at, i == 7 ? 0.75 : 0.40,
                 line[static_cast<std::size_t>(i)], 72 + 2 * (i % 3), 6, 80);
        }
        // Same MIDI slide gesture; candidate corrects length/dispersion physics.
        for (int step = 0; step < 12; ++step)
        {
            const int bend = 8192 + step * 300;
            event(melody, start + 2.60 + step * 0.024, 0xe0,
                  bend & 127, (bend >> 7) & 127, 6);
        }
        event(melody, start + 3.20, 0xe0, 0, 64, 6);
    }
    result.push_back(melody);

    Song groove { "04-repeated-note-groove", 17.0, PickingTechnique::Thumb, {} };
    event(groove, 0, 0xb0, 126, 0);
    for (int bar = 0; bar < 8; ++bar)
    {
        const double start = 0.2 + bar * 1.80;
        note(groove, start, 1.48, bar % 2 ? 45 : 40, 78, bar % 2 ? 2 : 1, 35);
        for (int beat = 0; beat < 6; ++beat)
            note(groove, start + 0.04 + beat * 0.27, 0.25,
                 bar % 2 ? 67 : 64, beat == 0 ? 97 : 72, 6,
                 beat % 2 ? 112 : 24);
        event(groove, start + 1.05, 0xb0, 2, bar % 2 ? 84 : 0);
        event(groove, start + 1.60, 0xb0, 2, 0);
    }
    result.push_back(groove);
    Song resonance { "05-resonance-and-body-transitions", 10.0, PickingTechnique::Finger, {} };
    // F0 events below are explicit parameter gestures, identical in both revisions.
    // First let passive bass strings answer a high melody, then retune low E.
    note(resonance,0.2,2.6,67,96);
    note(resonance,0.75,2.05,64,80);
    event(resonance,1.2,0xf0,0,1); // Standard -> Drop D while the instrument rings.
    event(resonance,2.0,0xf0,0,0);
    note(resonance,3.5,4.0,64,91);
    // A UI/controller may supersede a model before any sample is rendered.
    event(resonance,4.1,0xf0,1,1); // Bellido requested.
    event(resonance,4.1,0xf0,1,0); // Original wins at that same sample.
    event(resonance,5.2,0xf0,2,0); // Parlor requested.
    event(resonance,5.2,0xf0,2,3); // Jumbo is the final choice.
    event(resonance,6.1,0xf0,2,2); // Return to Dreadnought.
    result.push_back(resonance);
    return result;
}
void writeFloatWave(const std::filesystem::path& path, const std::vector<float>& audio)
{
    std::ofstream out(path, std::ios::binary);
    const auto word = [&] (std::uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>((value >> (8 * i)) & 255u));
    };
    const auto bytes = static_cast<std::uint32_t>(audio.size() * sizeof(float));
    out.write("RIFF",4); word(36+bytes,4); out.write("WAVEfmt ",8);
    word(16,4); word(3,2); word(2,2); word(sampleRate,4);
    word(sampleRate*8,4); word(8,2); word(32,2); out.write("data",4); word(bytes,4);
    for (float value : audio)
    {
        if (!std::isfinite(value)) throw std::runtime_error("nonfinite audio");
        out.write(reinterpret_cast<const char*>(&value),sizeof(value));
    }
    if (!out) throw std::runtime_error("wave write failed");
}
void render(Song song, const std::filesystem::path& output, float room)
{
    std::stable_sort(song.events.begin(), song.events.end(),
        [] (const Event& a, const Event& b) { return a.frame < b.frame; });
    auto player = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.picking = song.picking;
    parameters.room = room;
    parameters.releaseNoise = 0.7f;
    player->setParameters(parameters);
    player->prepare(sampleRate,128);
    player->setGatherChords(false);
    const int frames = frame(song.seconds);
    std::vector<float> audio(static_cast<std::size_t>(2*frames));
    std::array<float,128> left{}, right{};
    std::size_t next = 0;
    for (int at = 0; at < frames;)
    {
        int count = std::min(128,frames-at);
        if (next < song.events.size() && song.events[next].frame > at)
            count = std::min(count,song.events[next].frame-at);
        player->beginBlock(left.data(),right.data(),count);
        while (next < song.events.size() && song.events[next].frame < at+count)
        {
            const auto& e = song.events[next++];
            if (e.kind==0xf0)
            {
                if (e.a==0) parameters.tuning=static_cast<acustra::Tuning>(e.b);
                else if (e.a==1) parameters.guitarModel=static_cast<acustra::GuitarModel>(e.b);
                else if (e.a==2) parameters.shape=static_cast<acustra::BodyShape>(e.b);
                player->setParameters(parameters);
                continue;
            }
            const unsigned char bytes[] {static_cast<unsigned char>(e.kind| (e.channel-1)),
                static_cast<unsigned char>(e.a),static_cast<unsigned char>(e.b)};
            player->handleMidi(e.frame-at,bytes,3);
        }
        player->endBlock();
        for (int i=0; i<count; ++i)
        {
            audio[static_cast<std::size_t>(2*(at+i))]=left[static_cast<std::size_t>(i)];
            audio[static_cast<std::size_t>(2*(at+i)+1)]=right[static_cast<std::size_t>(i)];
        }
        at += count;
    }
    writeFloatWave(output/(song.name+".wav"),audio);
    // Retain the exact event input to make performance matching auditable.
    std::ofstream score(output/(song.name+".events"));
    score << "# frame channel status data1 data2; sample_rate 48000\n";
    for (const auto& e:song.events)
        score << e.frame << ' ' << e.channel << ' ' << e.kind << ' ' << e.a << ' ' << e.b << '\n';
}
}
int main(int argc,char** argv)
{
    try
    {
        if (argc!=3) throw std::runtime_error("usage: renderer NEW_OUTPUT_DIRECTORY ROOM_0_OR_0.5");
        const std::filesystem::path output(argv[1]);
        const std::string roomText(argv[2]);
        if (roomText!="0" && roomText!="0.5") throw std::runtime_error("room must be 0 or 0.5");
        if (std::filesystem::exists(output)) throw std::runtime_error("output already exists");
        std::filesystem::create_directories(output);
        for (auto song:songs()) render(std::move(song),output,roomText=="0" ? 0.0f : 0.5f);
        std::cout << "Four original songs and one control study rendered at 48 kHz float stereo\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
