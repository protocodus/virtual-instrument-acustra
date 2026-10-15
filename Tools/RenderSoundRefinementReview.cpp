// Identical authored scores for source-frozen sound-refinement comparisons.
// Public Performer entry points only. Stereo IEEE float WAVs retain native
// levels, with no alignment, normalization, limiting, or post-processing.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int rate = 48000, frames = 10 * rate, blockSize = 64;
struct Event { int frame, kind, channel, note, velocity; };

template<class Settings>
void selectRefinements(Settings& settings, const std::string& mode)
{
    // The same renderer and compiler flags also build against a pre-change
    // snapshot whose settings do not yet contain these offline switches.
    if constexpr (requires(Settings s) { s.restrainedPickRoughness; s.roomBassCoherence; })
    {
        settings.restrainedPickRoughness = mode == "production" || mode == "only-pick";
        settings.roomBassCoherence = mode == "production" || mode == "only-room";
    }
    else if (mode != "production")
        throw std::runtime_error("this DSP snapshot has no sound-refinement ablation switches");
}

acustra::EngineParameters pluginDefaults()
{
    // Pin the actual new-instance PluginProcessor defaults as of 2026-10-15.
    // EngineParameters alone defaults to a dry, quieter legacy test fixture.
    acustra::EngineParameters p;
    p.shape = acustra::BodyShape::Dreadnought;
    p.bodyMaterial = acustra::BodyMaterial::Spruce;
    p.capture = acustra::CaptureType::StereoMic;
    p.tuning = acustra::Tuning::Standard;
    p.picking = acustra::PickingTechnique::Finger;
    p.stringAge = 0.15f;
    p.pluckPosition = 0.28f;
    p.touch = 0.58f;
    p.bodyAmount = 0.82f;
    p.stereoWidth = 0.62f;
    p.outputGain = 1.0f; // Plugin Output = 0 dB, not EngineParameters' 0.42.
    p.piezoMix = 0.20f;
    p.piezoPolarityInverted = false;
    p.releaseNoise = 0.10f;
    p.room = 0.50f;
    p.detune = 0.0f;
    return p;
}

struct Score
{
    std::string name, title, description;
    acustra::EngineParameters parameters = pluginDefaults();
    std::vector<Event> events;
    void note(double time, double length, int pitch, int velocity)
    {
        events.push_back({ static_cast<int>(std::lround(time * rate)), 0x90, 1, pitch, velocity });
        events.push_back({ static_cast<int>(std::lround((time + length) * rate)), 0x80, 1, pitch, 64 });
    }
    void chord(double time, double length, std::initializer_list<int> pitches, int velocity)
    {
        for (const int pitch : pitches) note(time, length, pitch, velocity);
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
        std::memcpy(&bits, &value, sizeof(bits));
        u32(bits);
    }
    if (!stream) throw std::runtime_error("cannot write WAV: " + path.string());
}

void render(Score& score, const std::filesystem::path& directory, const std::string& mode)
{
    std::stable_sort(score.events.begin(), score.events.end(), [] (const Event& a, const Event& b)
    { return a.frame < b.frame; });
    // A fresh prepare restarts the deterministic engine random streams.
    auto player = std::make_unique<acustra::Performer>();
    auto refinements = player->engine().performanceRealism();
    selectRefinements(refinements, mode);
    player->engine().setPerformanceRealism(refinements);
    player->setParameters(score.parameters);
    player->setTempoBpm(120.0);
    player->setGatherChords(false);
    player->setSustainHold(false);
    player->prepare(rate, blockSize);
    std::vector<float> audio(static_cast<std::size_t>(frames * 2));
    std::array<float, blockSize> left {}, right {};
    std::size_t next = 0;
    for (int frame = 0; frame < frames; frame += blockSize)
    {
        const int count = std::min(blockSize, frames - frame);
        player->beginBlock(left.data(), right.data(), count);
        while (next < score.events.size() && score.events[next].frame < frame + count)
        {
            const auto& e = score.events[next++];
            if (e.kind == 0x90) player->noteOn(e.frame - frame, e.channel, e.note, e.velocity);
            else player->noteOff(e.frame - frame, e.channel, e.note, e.velocity);
        }
        player->endBlock();
        for (int n = 0; n < count; ++n)
        {
            if (!std::isfinite(left[n]) || !std::isfinite(right[n]))
                throw std::runtime_error("nonfinite audio: " + score.name);
            audio[static_cast<std::size_t>(2 * (frame + n))] = left[n];
            audio[static_cast<std::size_t>(2 * (frame + n) + 1)] = right[n];
        }
    }
    if (next != score.events.size()) throw std::runtime_error("score extends past render");
    writeWave(directory / (score.name + ".wav"), audio);
}

void writeScore(std::ostream& stream, const Score& s)
{
    const auto& p = s.parameters;
    stream << "{\"name\":\"" << s.name << "\",\"title\":\"" << s.title
           << "\",\"description\":\"" << s.description << "\",\"parameters\":{"
           << "\"shape\":" << static_cast<int>(p.shape)
           << ",\"bodyMaterial\":" << static_cast<int>(p.bodyMaterial)
           << ",\"capture\":" << static_cast<int>(p.capture)
           << ",\"tuning\":" << static_cast<int>(p.tuning)
           << ",\"picking\":" << static_cast<int>(p.picking)
           << ",\"stringAge\":" << p.stringAge << ",\"pluckPosition\":" << p.pluckPosition
           << ",\"touch\":" << p.touch << ",\"bodyAmount\":" << p.bodyAmount
           << ",\"stereoWidth\":" << p.stereoWidth << ",\"outputGain\":" << p.outputGain
           << ",\"piezoMix\":" << p.piezoMix << ",\"piezoPolarityInverted\":false"
           << ",\"releaseNoise\":" << p.releaseNoise << ",\"room\":" << p.room
           << ",\"detune\":" << p.detune << "},\"events\":[";
    bool comma = false;
    for (const auto& e : s.events)
    {
        if (comma) stream << ',';
        comma = true;
        stream << '[' << e.frame << ',' << e.kind << ',' << e.channel << ','
               << e.note << ',' << e.velocity << ']';
    }
    stream << "]}";
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2) throw std::runtime_error("give a NEW output directory, optional --legacy-refinements/--only-pick/--only-room and --study-pick/--study-room");
        std::string mode = "production", study;
        for (int n = 2; n < argc; ++n)
        {
            const std::string option(argv[n]);
            if (option == "--legacy-refinements") mode = "legacy";
            else if (option == "--only-pick") mode = "only-pick";
            else if (option == "--only-room") mode = "only-room";
            else if (option == "--study-pick") study = "pick";
            else if (option == "--study-room") study = "room";
            else throw std::runtime_error("unknown option: " + option);
        }
        acustra::PerformanceRealism settings;
        selectRefinements(settings, mode); // Reject unsupported modes before creating output.
        const std::filesystem::path directory(argv[1]);
        if (std::filesystem::exists(directory)) throw std::runtime_error("output already exists");
        std::filesystem::create_directories(directory);
        std::vector<Score> scores(8);
        const std::array<int, 6> registerNotes { 40, 47, 55, 64, 71, 76 };

        auto& soft = scores[0];
        soft.name = "01-soft-register"; soft.title = "Soft notes across the register";
        soft.description = "Finger, MIDI velocity 38; low E through E5. Actual new-instance controls: Room 50%, Piezo Mix 20%, Release Noise 10%, Output 0 dB.";
        for (int n = 0; n < 6; ++n) soft.note(0.2 + n * 1.25, 0.85, registerNotes[n], 38);

        auto& hard = scores[1];
        hard.name = "02-hard-register"; hard.title = "Firm picked notes across the register";
        hard.description = "The same pitches at MIDI velocity 116. Picking is Pick; all other controls are actual new-instance defaults. Compare attack edge and decay.";
        hard.parameters.picking = acustra::PickingTechnique::Pick;
        for (int n = 0; n < 6; ++n) hard.note(0.2 + n * 1.25, 0.85, registerNotes[n], 116);

        auto& repeat = scores[2];
        repeat.name = "03-dry-repetitions"; repeat.title = "Dry repeated contacts";
        repeat.description = "Finger repetitions on low, middle and high notes, alternating velocities 62 and 94. Room 0%, Piezo Mix 0%; other controls remain at defaults.";
        repeat.parameters.room = 0.0f; repeat.parameters.piezoMix = 0.0f;
        for (int n = 0; n < 18; ++n)
            repeat.note(0.2 + n * 0.40, 0.28, std::array<int, 3>{40, 55, 71}[n / 6], n % 2 ? 94 : 62);

        auto& arp = scores[3];
        arp.name = "04-finger-arpeggio"; arp.title = "Fingerstyle arpeggio";
        arp.description = "Overlapping alternating-bass arpeggio at actual new-instance defaults. Compare separation, bass body and smooth upper-string sustain.";
        const std::array<int, 8> arpNotes {40, 55, 59, 64, 47, 64, 59, 55};
        for (int n = 0; n < 24; ++n) arp.note(0.2 + n * 0.28, 0.72, arpNotes[n % 8], n % 4 == 0 ? 86 : 67);

        auto& chords = scores[4];
        chords.name = "05-finger-chords"; chords.title = "Finger chords and natural releases";
        chords.description = "Four-note finger chords, soft through firm, at actual new-instance defaults. Listen for blended body, string separation and release tails.";
        for (int n = 0; n < 6; ++n)
        {
            const double time = 0.2 + n * 1.12;
            if (n % 2) chords.chord(time, 0.73, {45, 55, 60, 64}, 48 + n * 12);
            else chords.chord(time, 0.73, {40, 55, 59, 64}, 48 + n * 12);
        }

        auto& strum = scores[5];
        strum.name = "06-picked-strums"; strum.title = "Picked strums at plug-in defaults";
        strum.description = "Twenty-eight eighth-note E and A strums at 120 BPM, alternating down and up with dynamic accents. Picking is Pick; Room 50%, Piezo Mix 20%, Release Noise 10%, Output 0 dB.";
        strum.parameters.picking = acustra::PickingTechnique::Pick;
        for (int n = 0; n < 28; ++n)
        {
            // 250 ms sits above the 30 ms stroke-continuation window and
            // below the 375 ms alternation window at 120 BPM. The 180 ms
            // gate plus the 62.5 ms key-up join leaves a gap before reattack.
            const double time = 0.2 + n * 0.25;
            const int velocity = n % 4 == 0 ? 111 : (n % 2 ? 72 : 90);
            if ((n / 4) % 2) strum.chord(time, 0.18, {45, 52, 57, 61, 64, 69}, velocity);
            else strum.chord(time, 0.18, {40, 47, 52, 56, 59, 64}, velocity);
        }

        auto& mono = scores[6];
        mono.name = "07-dry-mono"; mono.title = "Dry mono clarity control";
        mono.description = "Mono microphone, Room 0%, Piezo Mix 0%, finger. The same arpeggio as scene 4 isolates direct microphone capture and mono compatibility.";
        mono.parameters.capture = acustra::CaptureType::MonoMic;
        mono.parameters.room = 0.0f; mono.parameters.piezoMix = 0.0f;
        mono.events = arp.events;

        auto& piezo = scores[7];
        piezo.name = "08-piezo-control"; piezo.title = "Piezo capture control";
        piezo.description = "Piezo capture, Pick. Alternating low and high single notes followed by strums. Room and Piezo Mix are ignored by this capture, as in the plug-in.";
        piezo.parameters.capture = acustra::CaptureType::Piezo;
        piezo.parameters.picking = acustra::PickingTechnique::Pick;
        for (int n = 0; n < 8; ++n) piezo.note(0.2 + n * 0.48, 0.32, n % 2 ? 71 : 40, 46 + (n % 4) * 22);
        for (int n = 0; n < 5; ++n) piezo.chord(4.25 + n * 0.55, 0.4, {40, 47, 52, 56, 59, 64}, 70 + n * 10);

        if (!study.empty())
        {
            Score isolated;
            if (study == "pick")
            {
                isolated.name = "09-isolated-pick"; isolated.title = "Isolated refinement: pick contact";
                isolated.description = "Only the pick-contact refinement changes. Dry stereo microphones, Piezo Mix 0%, Pick; repeated notes at MIDI 80 and 116 across low, middle and high strings.";
                isolated.parameters.picking = acustra::PickingTechnique::Pick;
                isolated.parameters.room = 0.0f; isolated.parameters.piezoMix = 0.0f;
                for (int n = 0; n < 12; ++n)
                    isolated.note(0.2 + n * 0.57, 0.38, std::array<int, 3>{40, 55, 71}[n / 4], n % 2 ? 116 : 80);
            }
            else
            {
                isolated.name = "10-isolated-room"; isolated.title = "Isolated refinement: stereo room low end";
                isolated.description = "Only the room refinement changes. Finger chords at Room 50%, Stereo Width 100%, Piezo Mix 0%. Compare bass placement and short tails in stereo; the mono sum is preserved.";
                isolated.parameters.stereoWidth = 1.0f; isolated.parameters.piezoMix = 0.0f;
                for (int n = 0; n < 5; ++n)
                    isolated.chord(0.2 + n * 1.45, 0.65, {40, 47, 55, 59}, 80 + (n % 2) * 20);
            }
            scores = { isolated };
        }

        std::ofstream metadata(directory / "scores.json");
        metadata << "{\"sample_rate\":" << rate << ",\"duration_seconds\":10,\"block_size\":" << blockSize
                 << ",\"tempo_bpm\":120,\"gather_chords\":false,\"sustain\":false,\"refinements_mode\":\"" << mode << "\","
                 << "\"midi_channel_mode\":\"conventional automatic string allocation\","
                 << "\"events_format\":[\"frame\",\"status\",\"channel_1_based\",\"note\",\"velocity\"],\"scenes\":[";
        for (std::size_t n = 0; n < scores.size(); ++n)
        {
            render(scores[n], directory, mode);
            if (n) metadata << ',';
            writeScore(metadata, scores[n]);
        }
        metadata << "]}\n";
        if (!metadata) throw std::runtime_error("cannot write scores.json");
        std::cout << "Rendered " << scores.size() << " deterministic 10-second stereo float scenes at 48 kHz (" << mode << ").\n";
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
