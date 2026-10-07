// A shared, deterministic Performer score for independently disabling each
// natural-performance mechanism. No note/time/level normalization occurs here.
// Compile the identical source with ACUSTRA_NATURAL_BASELINE against 9bf1cff
// to prove the new all-disabled path reproduces the preceding implementation.
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

namespace acustra
{
struct AcustraEngineTestAccess
{
    static bool explicitAttackMatches(const AcustraEngine& engine, int string, int note) noexcept
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        return voice.played && voice.keyDown && voice.attackFired && voice.pluckDelay == 0
            && voice.midiNote == note && voice.midiChannel == string + 1
            && voice.fret == note - voice.openMidi;
    }
};
}

namespace
{
struct Event { int frame, channel, status, data1, data2; };
struct Passage
{
    std::string name;
    double seconds;
    acustra::PickingTechnique picking;
    std::vector<Event> events;
};
struct Variant { const char* name; bool contact, hand, damping, body, continuity; };
constexpr std::array<Variant, 7> variants {{
    { "baseline", false, false, false, false, false },
    { "contact", true, false, false, false, false },
    { "hand", false, true, false, false, false },
    { "damping", false, false, true, false, false },
    { "body", false, false, false, true, false },
    { "continuity", false, false, false, false, true },
    { "combined", true, true, true, true, true }
}};
int frame(double seconds, int rate) { return static_cast<int>(std::lround(seconds * rate)); }
void midi(Passage& p, int rate, double seconds, int status, int a, int b, int channel = 1)
{ p.events.push_back({ frame(seconds, rate), channel, status, a, b }); }
void note(Passage& p, int rate, double at, double length, int pitch, int velocity,
          int channel = 1, int release = 64)
{
    midi(p, rate, at, 0x90, pitch, velocity, channel);
    midi(p, rate, at + length, 0x80, pitch, release, channel);
}
const char* styleName(acustra::PickingTechnique picking)
{
    return picking == acustra::PickingTechnique::Finger ? "finger"
        : picking == acustra::PickingTechnique::Thumb ? "thumb" : "pick";
}
std::vector<Passage> score(int rate)
{
    using acustra::PickingTechnique;
    std::vector<Passage> result;
    for (const auto picking : { PickingTechnique::Finger, PickingTechnique::Thumb, PickingTechnique::Pick })
    {
        const std::string style = styleName(picking);
        Passage repeats { "01-repeated-notes-" + style, 11.0, picking, {} };
        midi(repeats, rate, 0, 0xb0, 126, 6); // Six explicit string channels.
        // Constant velocities expose timbral variation rather than authored
        // MIDI dynamics; three registers include wound and plain strings.
        for (int registerIndex = 0; registerIndex < 3; ++registerIndex)
            for (int attack = 0; attack < 8; ++attack)
                note(repeats, rate, 0.2 + 3.05 * registerIndex + 0.32 * attack,
                     0.30, std::array<int, 3>{{43, 57, 67}}[registerIndex], 86,
                     std::array<int, 3>{{1, 3, 6}}[registerIndex]);
        result.push_back(std::move(repeats));

        Passage release { "04-release-control-" + style, 12.0, picking, {} };
        midi(release, rate, 0, 0xb0, 126, 6);
        // Equal input notes, short then long key holds at three explicitly
        // declared release velocities. The existing 1/32 join remains active.
        for (int group = 0; group < 3; ++group)
        {
            const int velocity = std::array<int, 3>{{16, 64, 120}}[group];
            note(release, rate, 0.2 + group * 3.45, 0.18, 55, 84, 3, velocity);
            note(release, rate, 1.30 + group * 3.45, 1.05, 55, 84, 3, velocity);
        }
        result.push_back(std::move(release));
    }
    for (const auto picking : { PickingTechnique::Finger, PickingTechnique::Thumb })
    {
        Passage melody { std::string("02-melody-accompaniment-") + styleName(picking), 14.0, picking, {} };
        midi(melody, rate, 0, 0xb0, 126, 6);
        constexpr std::array<std::array<int, 4>, 4> chords {{
            {{45, 52, 60, 64}}, {{41, 53, 60, 65}}, {{48, 55, 60, 64}}, {{43, 50, 59, 67}}
        }};
        constexpr std::array<std::array<int, 6>, 4> line {{
            {{69, 67, 64, 67, 69, 71}}, {{72, 69, 67, 65, 67, 69}},
            {{67, 64, 67, 72, 71, 67}}, {{67, 69, 71, 74, 71, 67}}
        }};
        // This written microtiming is shared by EVERY variant. No fresh MIDI
        // randomization can be mistaken for a DSP improvement.
        constexpr std::array<double, 6> offsets {{0.0, -0.006, 0.004, -0.003, 0.008, 0.002}};
        for (int bar = 0; bar < 4; ++bar)
        {
            const double start = 0.2 + bar * 3.0;
            const auto& chord = chords[bar];
            note(melody, rate, start + 0.016, 2.80, chord[0], 43, 1, 40);
            for (int beat = 0; beat < 6; ++beat)
            {
                const double at = start + beat * 0.5 + offsets[beat];
                note(melody, rate, at, 0.44, line[bar][beat], beat % 3 == 0 ? 99 : 91, 6, 64);
                note(melody, rate, at + 0.037, 0.39, chord[1 + beat % 3], 32 + beat % 3 * 3,
                     3 + beat % 3, 42);
            }
        }
        result.push_back(std::move(melody));
    }
    for (const auto picking : { PickingTechnique::Finger, PickingTechnique::Pick })
    {
        Passage strums { std::string("03-alternating-strums-") + styleName(picking), 12.0, picking, {} };
        constexpr std::array<std::array<int, 6>, 4> shapes {{
            {{40,47,52,56,59,64}}, {{45,52,57,60,64,69}},
            {{48,52,55,60,64,67}}, {{43,47,50,55,59,67}}
        }};
        for (int stroke = 0; stroke < 16; ++stroke)
            for (const int pitch : shapes[stroke / 4])
                note(strums, rate, 0.2 + stroke * 0.55, 0.50, pitch,
                     stroke % 4 == 0 ? 103 : stroke % 2 == 0 ? 81 : 62);
        midi(strums, rate, 9.35, 0xb0, 2, 82);
        midi(strums, rate, 10.0, 0xb0, 2, 0);
        result.push_back(std::move(strums));
    }
    Passage held { "05-held-chord-finger", 12.0, PickingTechnique::Finger, {} };
    for (const int pitch : {40, 47, 52, 56, 59, 64}) note(held, rate, 0.2, 8.0, pitch, 87);
    result.push_back(std::move(held));

    Passage stress { "06-timing-controller-stress-pick", 10.0, PickingTechnique::Pick, {} };
    midi(stress, rate, 0, 0xb0, 126, 6);
    for (int attack = 0; attack < 16; ++attack)
        note(stress, rate, 0.2 + 0.075 * attack, 0.067, 64, attack % 4 == 0 ? 104 : 48, 6,
             attack % 2 ? 112 : 24);
    midi(stress, rate, 1.5, 0xb0, 64, 127);
    note(stress, rate, 1.6, 0.3, 52, 79, 3);
    midi(stress, rate, 2.1, 0xb0, 2, 90);
    midi(stress, rate, 2.5, 0xb0, 2, 0);
    midi(stress, rate, 2.8, 0xb0, 64, 0);
    note(stress, rate, 3.0, 1.0, 64, 88, 6);
    midi(stress, rate, 3.25, 0xb0, 1, 42, 6);
    for (int step = 0; step < 12; ++step)
    {
        const int bend = 8192 + step * 128;
        midi(stress, rate, 3.4 + step * 0.025, 0xe0, bend & 127, bend >> 7, 6);
    }
    midi(stress, rate, 4.3, 0xe0, 0, 64, 6);
    midi(stress, rate, 4.3, 0xb0, 1, 0, 6);
    // A real rest resets strum direction; panic and controller reset must
    // permit the very next sample's playable note rather than sticking.
    midi(stress, rate, 4.5, 0xb0, 120, 0);
    midi(stress, rate, 4.5 + 1.0 / rate, 0xb0, 121, 0);
    note(stress, rate, 4.5 + 2.0 / rate, 0.8, 67, 92, 6);
    midi(stress, rate, 6.0, 0xb0, 127, 0);
    for (const int pitch : {45,52,57,60,64,69}) note(stress, rate, 6.4, 1.2, pitch, 78);
    result.push_back(std::move(stress));
    for (auto& p : result)
        std::stable_sort(p.events.begin(), p.events.end(),
            [](const Event& a, const Event& b) { return a.frame < b.frame; });
    return result;
}
void word(std::ostream& out, std::uint32_t value, int bytes)
{ for (int byte = 0; byte < bytes; ++byte) out.put(static_cast<char>((value >> (byte * 8)) & 255u)); }
void samples(std::ostream& out, const std::vector<float>& audio)
{
    static_assert(sizeof(float) == 4, "the evidence format uses IEEE float32");
    for (float sample : audio)
    {
        if (!std::isfinite(sample)) throw std::runtime_error("nonfinite rendered audio");
        std::uint32_t bits;
        std::memcpy(&bits, &sample, sizeof(bits));
        word(out, bits, 4);
    }
    if (!out) throw std::runtime_error("audio write failed");
}
void writeAudio(const std::filesystem::path& path, const std::vector<float>& audio, int rate)
{
    std::ofstream raw(path.string() + ".f32", std::ios::binary);
    samples(raw, audio);
    std::ofstream wav(path.string() + ".wav", std::ios::binary);
    const auto bytes = static_cast<std::uint32_t>(audio.size() * sizeof(float));
    wav.write("RIFF", 4); word(wav, 36 + bytes, 4); wav.write("WAVEfmt ", 8);
    word(wav, 16, 4); word(wav, 3, 2); word(wav, 2, 2); word(wav, rate, 4);
    word(wav, rate * 8, 4); word(wav, 8, 2); word(wav, 32, 2);
    wav.write("data", 4); word(wav, bytes, 4); samples(wav, audio);
}
void render(const Passage& p, const Variant& variant, acustra::GuitarModel model,
            const std::filesystem::path& output, int rate, int block, std::ofstream& manifest,
            bool& first)
{
    const std::string modelName = model == acustra::GuitarModel::Original ? "original" : "classical78";
    const std::string name = p.name + "-" + modelName;
    auto player = std::make_unique<acustra::Performer>();
#ifndef ACUSTRA_NATURAL_BASELINE
    player->engine().setPerformanceRealism({ variant.contact, variant.hand, variant.damping,
                                            variant.body, variant.continuity });
#endif
    acustra::EngineParameters parameters;
    parameters.picking = p.picking;
    parameters.guitarModel = model;
    if (model == acustra::GuitarModel::Bellido1978)
    { parameters.shape = acustra::BodyShape::Auditorium; parameters.bodyMaterial = acustra::BodyMaterial::Mahogany; }
    player->setParameters(parameters);
    player->setTempoBpm(120.0);
    player->setGatherChords(false);
    player->prepare(rate, block);
    const int count = frame(p.seconds, rate);
    std::vector<float> audio(static_cast<std::size_t>(2 * count));
    std::vector<float> left(static_cast<std::size_t>(block)), right(left.size());
    std::size_t next = 0;
    bool explicitStrings = false;
    int checkedAttacks = 0;
    std::filesystem::create_directories(output / variant.name);
    std::ofstream allocations(output / variant.name / (name + ".allocations.tsv"));
    allocations << "frame\tchannel\tmidi_note\texplicit_strings\texpected_string\tactual_string\topen_midi\tfret\n";
    for (int at = 0; at < count;)
    {
        int n = std::min(block, count - at);
        // End before the next event, then process all same-sample messages
        // together in one sample. This permits a check immediately after an
        // attack, before a later Note Off can hide a wrong physical string.
        if (next < p.events.size())
            n = p.events[next].frame == at ? 1 : std::min(n, p.events[next].frame - at);
        const auto firstEvent = next;
        player->beginBlock(left.data(), right.data(), n);
        while (next < p.events.size() && p.events[next].frame < at + n)
        {
            const auto& e = p.events[next++];
            if (e.status == 0xb0 && e.channel == 1 && e.data1 == 126)
            {
                if (e.data2 != 6)
                    throw std::runtime_error("explicit-string fixture requires CC126 value 6");
                explicitStrings = true;
            }
            else if (e.status == 0xb0 && e.channel == 1 && e.data1 == 127)
                explicitStrings = false;
            const std::uint8_t bytes[] { static_cast<std::uint8_t>(e.status | (e.channel - 1)),
                static_cast<std::uint8_t>(e.data1), static_cast<std::uint8_t>(e.data2) };
            player->handleMidi(e.frame - at, bytes, 3);
        }
        player->endBlock();
        for (auto index = firstEvent; index < next; ++index)
        {
            const auto& e = p.events[index];
            if (e.status != 0x90 || e.data2 == 0) continue;
            const int actual = player->engine().heldString(e.data1, e.channel);
            if (explicitStrings)
            {
                if (actual != e.channel - 1 || !acustra::AcustraEngineTestAccess::explicitAttackMatches(
                        player->engine(), actual, e.data1))
                    throw std::runtime_error("explicit physical-string attack mismatch: " + name
                        + " at frame " + std::to_string(e.frame));
                ++checkedAttacks;
            }
            const auto activity = player->engine().getStringActivity();
            allocations << e.frame << '\t' << e.channel << '\t' << e.data1 << '\t' << explicitStrings
                << '\t' << (explicitStrings ? e.channel - 1 : -1) << '\t' << actual << '\t'
                << (actual >= 0 ? activity[static_cast<std::size_t>(actual)].openMidi : -1) << '\t'
                << (actual >= 0 ? activity[static_cast<std::size_t>(actual)].fret : -1) << '\n';
        }
        for (int i = 0; i < n; ++i)
        { audio[static_cast<std::size_t>(2 * (at + i))] = left[static_cast<std::size_t>(i)];
          audio[static_cast<std::size_t>(2 * (at + i) + 1)] = right[static_cast<std::size_t>(i)]; }
        at += n;
    }
    if (next != p.events.size() || player->droppedEventCount() != 0)
        throw std::runtime_error("score was not consumed without drops");
    if (!allocations) throw std::runtime_error("allocation evidence write failed");
    writeAudio(output / variant.name / name, audio, rate);
    double sum = 0.0;
    float peak = 0.0f;
    for (float value : audio) { sum += static_cast<double>(value) * value; peak = std::max(peak, std::abs(value)); }
    if (!(sum > 0.0)) throw std::runtime_error("silent render");
    if (!first) manifest << ",\n";
    first = false;
    manifest << "    {\"case\":\"" << name << "\",\"variant\":\"" << variant.name
        << "\",\"wav\":\"" << variant.name << '/' << name << ".wav\",\"raw\":\""
        << variant.name << '/' << name << ".f32\",\"score\":\"scores/" << p.name
        << ".tsv\",\"frames\":" << count << ",\"peak\":" << peak
        << ",\"rms\":" << std::sqrt(sum / audio.size()) << ",\"dropped_events\":0"
        << ",\"verified_explicit_attacks\":" << checkedAttacks
        << ",\"allocations\":\"" << variant.name << '/' << name << ".allocations.tsv\""
        << ",\"switches\":{\"contactRelease\":" << variant.contact
        << ",\"coherentHand\":" << variant.hand << ",\"gestureDamping\":" << variant.damping
        << ",\"playerBodyLoading\":" << variant.body << ",\"retuneContinuity\":" << variant.continuity << "}"
        << ",\"parameters\":{\"model\":\"" << modelName << "\",\"shape\":" << static_cast<int>(parameters.shape)
        << ",\"bodyMaterial\":" << static_cast<int>(parameters.bodyMaterial)
        << ",\"capture\":" << static_cast<int>(parameters.capture)
        << ",\"tuning\":" << static_cast<int>(parameters.tuning)
        << ",\"picking\":\"" << styleName(parameters.picking)
        << "\",\"stringAge\":" << parameters.stringAge << ",\"pluckPosition\":" << parameters.pluckPosition
        << ",\"touch\":" << parameters.touch << ",\"bodyAmount\":" << parameters.bodyAmount
        << ",\"stereoWidth\":" << parameters.stereoWidth << ",\"outputGain\":" << parameters.outputGain
        << ",\"piezoMix\":" << parameters.piezoMix << ",\"releaseNoise\":" << parameters.releaseNoise
        << ",\"room\":" << parameters.room << "}}";
    std::cout << variant.name << '/' << name << " peak=" << peak << '\n';
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        if (argc < 2) throw std::runtime_error("usage: renderer NEW_OUTPUT [--variant NAME] [--case SUBSTRING] [--rate HZ] [--block FRAMES]");
        std::string selected = "all", selectedCase;
        int rate = 48000, block = 128;
        for (int i = 2; i < argc; i += 2)
        {
            if (i + 1 == argc) throw std::runtime_error("missing argument value");
            const std::string key(argv[i]);
            if (key == "--variant") selected = argv[i + 1];
            else if (key == "--case") selectedCase = argv[i + 1];
            else if (key == "--rate") rate = std::stoi(argv[i + 1]);
            else if (key == "--block") block = std::stoi(argv[i + 1]);
            else throw std::runtime_error("unknown argument " + key);
        }
        if (rate < 8000 || rate > 192000 || block < 1 || block > 8192)
            throw std::runtime_error("unsupported rate or block size");
        if (selected != "all" && std::none_of(variants.begin(), variants.end(),
            [&](const Variant& v) { return selected == v.name; })) throw std::runtime_error("unknown variant");
#ifdef ACUSTRA_NATURAL_BASELINE
        if (selected != "all" && selected != "baseline") throw std::runtime_error("old API renders baseline only");
        selected = "baseline";
#endif
        const std::filesystem::path output(argv[1]);
        if (std::filesystem::exists(output)) throw std::runtime_error("output already exists");
        std::filesystem::create_directories(output / "scores");
        std::ofstream manifest(output / "manifest.json");
        manifest << std::setprecision(12) << std::boolalpha
            << "{\n  \"schema\":2,\"sample_rate\":" << rate << ",\"channels\":2,\"block_size\":" << block
            << ",\"format\":\"little-endian IEEE float32 interleaved stereo\",\"tempo_bpm\":120"
            << ",\"gather_chords\":false,\"post_gain\":1,\"latency_samples\":"
            << acustra::AcustraEngine::outputLatencySamples()
            << ",\"explicit_string_mode_cc126_value\":6"
            << ",\"event_partition\":\"split before each event time; one-sample grouped event blocks; physical string/attack/fret checked immediately\""
            << ",\"randomness\":\"Engine reset seed; no random MIDI; identical written score across variants\""
            << ",\"renders\":[\n";
        bool first = true;
        for (const auto& p : score(rate))
        {
            if (!selectedCase.empty() && p.name.find(selectedCase) == std::string::npos) continue;
            std::ofstream events(output / "scores" / (p.name + ".tsv"));
            events << "frame\tchannel\tstatus\tdata1\tdata2\n";
            for (const auto& e : p.events)
                events << e.frame << '\t' << e.channel << '\t' << e.status << '\t' << e.data1 << '\t' << e.data2 << '\n';
            if (!events) throw std::runtime_error("score write failed");
            for (const auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
                for (const auto& v : variants)
                    if (selected == "all" || selected == v.name) render(p, v, model, output, rate, block, manifest, first);
        }
        if (first) throw std::runtime_error("case filter matched no passages");
        manifest << "\n  ]\n}\n";
        if (!manifest) throw std::runtime_error("manifest write failed");
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
