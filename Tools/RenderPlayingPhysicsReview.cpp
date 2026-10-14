// Identical authored scores for the playing-physics A/B review
// (Tools/BuildPlayingPhysicsReview.py). Compile this same file against each
// frozen DSP tree. Both builds can let the release fall go (variant
// "fall-off"); the candidate's build adds -DACUSTRA_REVIEW_CANDIDATE=1 so a
// variant can switch one of the 2026-10-13/14 mechanisms off, or all of them
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

acustra::PerformanceRealism realismFor(const std::string& variant)
{
    acustra::PerformanceRealism realism;
    realism.releasePitchFall = variant != "fall-off" && variant != "all-off";
#if defined(ACUSTRA_REVIEW_CANDIDATE)
    // all-off: every mechanism of this review off and the fall let go, which
    // must be the baseline's own fall-off render bit for bit. only-fall:
    // every mechanism off but the fall, now on its measured curve, against
    // the baseline's authored one.
    const bool none = variant == "all-off" || variant == "only-fall";
    realism.fingerChords = !none && variant != "no-fingerChords";
    realism.mutedStrings = !none && variant != "no-mutedStrings";
    realism.strumScatter = !none && variant != "no-strumScatter";
    realism.twoStringStrokes = !none && variant != "no-twoStringStrokes";
    realism.strokeSpanLaw = !none && variant != "no-strokeSpanLaw";
    realism.strokeEdges = !none && variant != "no-strokeEdges";
#else
    if (variant != "baseline" && variant != "fall-off")
        throw std::runtime_error("the baseline build renders baseline and fall-off only");
#endif
    return realism;
}

void render(const Score& score, const std::string& variant,
            const std::filesystem::path& directory)
{
    auto events = score.events;
    std::stable_sort(events.begin(), events.end(), [] (const Event& a, const Event& b)
    { return a.frame < b.frame; });
    auto player = std::make_unique<acustra::Performer>();
    player->engine().setPerformanceRealism(realismFor(variant));
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
    // 1. Bossa-like fingerstyle comping: the thumb's bass on the beats and
    // three- and four-string chords between, which fingers pluck together.
    {
        Score s; s.name = "01-finger-comping"; s.bpm = 100.0;
        const double beat = 60.0 / s.bpm, eighth = 0.5 * beat;
        const std::vector<std::pair<std::array<int, 2>, std::vector<int>>> bars {
            { { 45, 52 }, { 57, 60, 64 } },          // A minor: A3 C4 E4
            { { 50, 45 }, { 57, 62, 65 } },          // D minor: A3 D4 F4
            { { 40, 47 }, { 52, 56, 62, 64 } },      // E7: E3 G#3 D4 E4
            { { 45, 52 }, { 55, 60, 64 } } };        // A minor 7: G3 C4 E4
        double t = 0.1;
        for (const auto& [bass, chord] : bars)
        {
            s.note(t, 0.95 * beat, bass[0], 88);
            s.chord(t + eighth, beat - 0.03, chord, 80);
            s.note(t + 2 * beat, 0.95 * beat, bass[1], 84);
            s.chord(t + 2 * beat + eighth, 0.5 * beat, chord, 78);
            s.chord(t + 3 * beat + eighth, 0.5 * beat - 0.03, chord, 74);
            t += 4 * beat;
        }
        result.push_back(s);
    }
    // 2. Open-voiced chords strummed down and up with a pick: E2-B3-E4,
    // G2-D3-B3-G4 and C3-G3-E4 leave strings out between the ones they sound.
    {
        Score s; s.name = "02-gapped-strums"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 96.0;
        const double eighth = 30.0 / s.bpm;
        const std::vector<std::vector<int>> chords {
            { 40, 59, 64 }, { 43, 50, 59, 67 }, { 48, 55, 64 }, { 40, 59, 64 } };
        for (int stroke = 0; stroke < 24; ++stroke)
        {
            const auto& chord = chords[static_cast<std::size_t>(stroke / 6 % 4)];
            const bool last = stroke == 23;
            s.chord(0.1 + stroke * eighth, last ? 1.4 : eighth - 0.02, chord,
                    stroke % 2 == 0 ? 98 : 84);
        }
        result.push_back(s);
    }
    // 3. Staccato fingerstyle, as the 1.4.0 review: fretted notes let go
    // after half their length, then chord stabs into rests. Every ending is
    // a fretted release.
    {
        Score s; s.name = "03-staccato-fingerstyle"; s.bpm = 92.0;
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
    // 4. One chord strummed over and over at one velocity, as a sequencer
    // sends it: E major, then A major, down-up eighths with a pick.
    {
        Score s; s.name = "04-repeated-strums"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 100.0;
        const double eighth = 30.0 / s.bpm;
        for (int stroke = 0; stroke < 26; ++stroke)
        {
            const bool last = stroke == 25;
            const std::vector<int> chord = stroke < 13
                ? std::vector<int> { 40, 47, 52, 56, 59, 64 }
                : std::vector<int> { 45, 52, 57, 61, 64 };
            s.chord(0.1 + stroke * eighth, last ? 1.6 : eighth - 0.02, chord, 92);
        }
        result.push_back(s);
    }
    // 5. A power-chord riff with a pick: two strings at a time in eighths.
    {
        Score s; s.name = "05-power-chords"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 120.0;
        const double eighth = 30.0 / s.bpm;
        const std::vector<std::array<int, 2>> riff {
            { 40, 47 }, { 40, 47 }, { 43, 50 }, { 43, 50 }, { 45, 52 }, { 45, 52 }, { 43, 50 }, { 40, 47 },
            { 40, 47 }, { 40, 47 }, { 48, 55 }, { 48, 55 }, { 47, 54 }, { 47, 54 }, { 45, 52 }, { 43, 50 } };
        double t = 0.1;
        for (int round = 0; round < 2; ++round)
            for (std::size_t k = 0; k < riff.size(); ++k)
            {
                s.chord(t, eighth - 0.03, { riff[k][0], riff[k][1] }, k % 2 == 0 ? 104 : 90);
                t += eighth;
            }
        s.chord(t, 1.2, { 40, 47 }, 104);
        result.push_back(s);
    }
    // 6. Small strokes with a pick: three- and four-string chords on the top
    // strings, strummed down and up.
    {
        Score s; s.name = "06-small-strokes"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 100.0;
        const double eighth = 30.0 / s.bpm;
        const std::vector<std::vector<int>> chords {
            { 55, 59, 67 }, { 57, 62, 66 }, { 55, 60, 64 }, { 50, 57, 62, 66 } };
        for (int stroke = 0; stroke < 26; ++stroke)
        {
            const auto& chord = chords[static_cast<std::size_t>(stroke / 6 % 4)];
            const bool last = stroke == 25;
            s.chord(0.1 + stroke * eighth, last ? 1.4 : eighth - 0.02, chord,
                    stroke % 2 == 0 ? 96 : 82);
        }
        result.push_back(s);
    }
    // 7. Down-up eighth-note strumming of full chords, as the 1.4.0 review:
    // G, C, D and E minor, the upstrokes a little softer.
    {
        Score s; s.name = "07-strum-eighths"; s.picking = acustra::PickingTechnique::Pick;
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
    // 8. A short picked song: bass notes, down-up strums of full and open
    // chords, a power-chord fill and a stopped ending.
    {
        Score s; s.name = "08-song-pick"; s.picking = acustra::PickingTechnique::Pick;
        s.bpm = 96.0;
        const double beat = 60.0 / s.bpm, eighth = 0.5 * beat;
        double t = 0.1;
        const std::vector<std::pair<int, std::vector<int>>> bars {
            { 43, { 47, 50, 55, 59, 67 } }, { 48, { 52, 55, 60, 64 } },
            { 40, { 59, 64 } } };
        for (const auto& [bass, chord] : bars)
        {
            s.note(t, 0.95 * beat, bass, 100);
            s.chord(t + beat, eighth - 0.02, chord, 92);
            s.chord(t + beat + eighth, eighth - 0.02, chord, 78);
            s.note(t + 2 * beat, 0.95 * beat, bass + 7, 96);
            s.chord(t + 3 * beat, eighth - 0.02, chord, 92);
            s.chord(t + 3 * beat + eighth, eighth - 0.02, chord, 78);
            t += 4 * beat;
        }
        for (const auto& pair : std::vector<std::array<int, 2>> { { 45, 52 }, { 47, 54 }, { 48, 55 }, { 50, 57 } })
        { s.chord(t, 0.9 * eighth, { pair[0], pair[1] }, 96); t += eighth; }
        s.chord(t, 0.5 * beat, { 43, 47, 50, 55, 59, 67 }, 100);
        result.push_back(s);
    }
    // 9. A short fingerstyle song: bass and plucked chords, a melody over
    // them, released fretted notes and a final chord.
    {
        Score s; s.name = "09-song-finger"; s.bpm = 84.0;
        const double beat = 60.0 / s.bpm, eighth = 0.5 * beat;
        double t = 0.1;
        const std::vector<std::pair<int, std::vector<int>>> bars {
            { 48, { 55, 60, 64 } }, { 45, { 57, 60, 64 } } };
        for (const auto& [bass, chord] : bars)
        {
            s.note(t, 1.9 * beat, bass, 90);
            s.chord(t + eighth, 0.9 * beat, chord, 78);
            s.note(t + 2 * beat, 0.45 * beat, 67, 86);
            s.note(t + 2.5 * beat, 0.45 * beat, 65, 82);
            s.chord(t + 3 * beat, 0.9 * beat, chord, 76);
            t += 4 * beat;
        }
        s.chord(t, 2.0, { 50, 57, 62, 65 }, 84);
        result.push_back(s);
    }
    // 10. Unchanged control: open strings plucked one at a time with the
    // fingers. No fret to fall from, no plectrum, no chord: exact.
    {
        Score s; s.name = "10-control-open-strings"; s.bpm = 100.0;
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
            throw std::runtime_error("usage: RenderPlayingPhysicsReview NEW_OUTPUT_DIRECTORY VARIANT");
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
