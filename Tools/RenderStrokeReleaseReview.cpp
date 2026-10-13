// Identical authored scores for the stroke depth, alternate picking and
// release fall A/B review (Tools/BuildStrokeReleaseReview.py). Compile this
// same file against each frozen DSP tree; the candidate build adds
// -DACUSTRA_REVIEW_SWITCHES=1 so a variant can switch one mechanism off
// (PerformanceRealism). Writes stereo float WAVs at native level: no
// normalization, limiting, EQ or timing alignment.
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
constexpr int rate = 48000, frames = 8 * rate, block = 64;
struct Event { int frame, kind, note, velocity; };
struct Score
{
    std::string name;
    acustra::PickingTechnique picking = acustra::PickingTechnique::Finger;
    double bpm = 120.0;
    std::vector<Event> events;
    void note(double time, double length, int note, int velocity)
    {
        events.push_back({ static_cast<int>(std::lround(time * rate)), 0x90, note, velocity });
        events.push_back({ static_cast<int>(std::lround((time + length) * rate)), 0x80, note, 64 });
    }
    void chord(double time, double length, const std::vector<int>& notes, int velocity)
    {
        for (const int n : notes)
            note(time, length, n, velocity);
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
        auto* destination = reinterpret_cast<unsigned char*>(&bits);
        std::copy(source, source + 4, destination);
        u32(bits);
    }
    if (!stream) throw std::runtime_error("cannot write WAV");
}

// The plug-in's defaults (PluginProcessor's parameter layout): Output 0 dB,
// Room 50 %, Piezo Mix 20 %, Release Noise 10 %, stereo microphones.
acustra::EngineParameters pluginDefaults(acustra::PickingTechnique picking)
{
    acustra::EngineParameters parameters;
    parameters.picking = picking;
    parameters.outputGain = 1.0f;
    parameters.room = 0.5f;
    parameters.piezoMix = 0.2f;
    parameters.releaseNoise = 0.1f;
    return parameters;
}

void render(const Score& score, const std::string& variant,
            const std::filesystem::path& directory)
{
    auto events = score.events;
    std::stable_sort(events.begin(), events.end(), [] (const Event& a, const Event& b)
    { return a.frame < b.frame; });
    auto player = std::make_unique<acustra::Performer>();
#if defined(ACUSTRA_REVIEW_SWITCHES)
    acustra::PerformanceRealism realism;
    const bool allOff = variant == "all-off";
    realism.strokeDepth = !allOff && variant != "no-depth";
    realism.alternatePicking = !allOff && variant != "no-alternate";
    realism.releasePitchFall = !allOff && variant != "no-fall";
    player->engine().setPerformanceRealism(realism);
#else
    if (variant != "baseline")
        throw std::runtime_error("this build has no review switches");
#endif
    player->setParameters(pluginDefaults(score.picking));
    player->prepare(rate, block);
    player->setTempoBpm(score.bpm);
    std::vector<float> audio(static_cast<std::size_t>(frames * 2));
    std::array<float, block> left {}, right {};
    std::size_t next = 0;
    for (int frame = 0; frame < frames; frame += block)
    {
        player->beginBlock(left.data(), right.data(), block);
        while (next < events.size() && events[next].frame < frame + block)
        {
            const auto& e = events[next++];
            const int offset = e.frame - frame;
            if (e.kind == 0x90) player->noteOn(offset, 1, e.note, e.velocity);
            else player->noteOff(offset, 1, e.note, e.velocity);
        }
        player->endBlock();
        for (int n = 0; n < block; ++n)
        {
            if (!std::isfinite(left[static_cast<std::size_t>(n)])
                || !std::isfinite(right[static_cast<std::size_t>(n)]))
                throw std::runtime_error("nonfinite audio");
            audio[static_cast<std::size_t>(2 * (frame + n))] = left[static_cast<std::size_t>(n)];
            audio[static_cast<std::size_t>(2 * (frame + n) + 1)] = right[static_cast<std::size_t>(n)];
        }
    }
    writeWave(directory / (score.name + ".wav"), audio);
}

std::vector<Score> scores()
{
    std::vector<Score> result;
    // 1. Down-up eighth-note strumming with a pick, G - C - D - Em, the
    // upstrokes a little softer as a player accents the beat. Every second
    // stroke is the hand's return: the stroke depth's comparison.
    {
        Score s; s.name = "1-strum-eighths"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 104.0;
        const double eighth = 30.0 / s.bpm;
        const std::vector<std::vector<int>> chords {
            { 43, 47, 50, 55, 59, 67 }, { 48, 52, 55, 60, 64 },
            { 50, 57, 62, 66 }, { 40, 47, 52, 55, 59, 64 } };
        for (int stroke = 0; stroke < 26; ++stroke)
        {
            const auto& chord = chords[static_cast<std::size_t>(stroke / 7 % 4)];
            const bool last = stroke == 25;
            s.chord(0.1 + stroke * eighth, last ? 1.6 : eighth - 0.02, chord,
                    stroke % 2 == 0 ? 100 : 84);
        }
        result.push_back(s);
    }
    // 2. A picked line: eighths and sixteenths across the strings, then a
    // sixteenth-note tremolo on one note. Single notes with a pick: the
    // alternate picking comparison.
    {
        Score s; s.name = "2-picked-line"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 112.0;
        const double eighth = 30.0 / s.bpm, sixteenth = 0.5 * eighth;
        double t = 0.1;
        for (const int n : { 55, 57, 59, 62, 64, 62, 59, 57, 55, 59, 62, 67 })
        { s.note(t, 0.9 * eighth, n, 96); t += eighth; }
        for (const int n : { 66, 67, 69, 67, 66, 64, 62, 64, 66, 62, 59, 62 })
        { s.note(t, 0.9 * sixteenth, n, 92); t += sixteenth; }
        for (int repeat = 0; repeat < 16; ++repeat)
        { s.note(t, 0.9 * sixteenth, 71, repeat % 4 == 0 ? 100 : 90); t += sixteenth; }
        s.note(t, 1.2, 67, 96);
        result.push_back(s);
    }
    // 3. Staccato fingerstyle: fretted bass and treble notes let go after
    // half their length, then fretted chord stabs released into rests. Every
    // ending is a fretted release: the release fall's comparison.
    {
        Score s; s.name = "3-staccato-fingerstyle"; s.bpm = 92.0;
        const double beat = 60.0 / s.bpm;
        double t = 0.1;
        for (const auto& pair : std::vector<std::array<int, 2>> {
                 { 43, 62 }, { 45, 64 }, { 47, 66 }, { 48, 67 }, { 50, 69 }, { 47, 67 } })
        {
            s.note(t, 0.45 * beat, pair[0], 90);
            s.note(t + 0.5 * beat, 0.4 * beat, pair[1], 88);
            t += beat;
        }
        for (const auto& chord : std::vector<std::vector<int>> {
                 { 45, 52, 57, 61, 64 }, { 47, 54, 59, 63, 66 }, { 48, 55, 60, 64, 67 } })
        {
            s.chord(t, 0.45 * beat, chord, 94);
            t += beat;
        }
        s.chord(t, 0.3 * beat, { 50, 57, 62, 66 }, 96);
        result.push_back(s);
    }
    // 4. A short song with a pick: bass notes and down-up strums, a picked
    // fill and a stopped ending. All three mechanisms at once: baseline
    // against the candidate.
    {
        Score s; s.name = "4-song"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 96.0;
        const double beat = 60.0 / s.bpm, eighth = 0.5 * beat;
        double t = 0.1;
        const std::vector<std::pair<int, std::vector<int>>> bars {
            { 43, { 47, 50, 55, 59, 67 } }, { 48, { 52, 55, 60, 64 } } };
        for (const auto& [bass, chord] : bars)
        {
            s.note(t, 0.95 * beat, bass, 100);                    // beat 1, bass
            s.chord(t + beat, eighth - 0.02, chord, 92);           // beat 2
            s.chord(t + beat + eighth, eighth - 0.02, chord, 78);  // and
            s.note(t + 2 * beat, 0.95 * beat, bass + 7, 96);       // beat 3, bass
            s.chord(t + 3 * beat, eighth - 0.02, chord, 92);       // beat 4
            s.chord(t + 3 * beat + eighth, eighth - 0.02, chord, 78);
            t += 4 * beat;
        }
        for (const int n : { 62, 64, 66, 67 })
        { s.note(t, 0.9 * eighth, n, 94); t += eighth; }
        s.chord(t, 0.5 * beat, { 43, 47, 50, 55, 59, 67 }, 100);
        result.push_back(s);
    }
    // 5. Unchanged control: open strings plucked one at a time with the
    // fingers. No fret to fall from, no plectrum, no strum: exact.
    {
        Score s; s.name = "5-control-open-strings"; s.bpm = 100.0;
        double t = 0.1;
        for (int round = 0; round < 3; ++round)
            for (const int n : { 40, 50, 55, 59, 64, 59, 55, 45 })
            { s.note(t, 0.4, n, 90); t += 0.3; }
        result.push_back(s);
    }
    return result;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3)
            throw std::runtime_error("usage: RenderStrokeReleaseReview NEW_OUTPUT_DIRECTORY VARIANT");
        const std::filesystem::path directory(argv[1]);
        const std::string variant(argv[2]);
        if (std::filesystem::exists(directory))
            throw std::runtime_error("output already exists");
        std::filesystem::create_directories(directory);
        for (const auto& score : scores())
            render(score, variant, directory);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
