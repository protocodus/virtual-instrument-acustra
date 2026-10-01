// Renders expressive repertoire performances - one guitar or an ensemble of
// differently built guitars - through AcustraEngine, the JUCE-free signal
// path the plug-in plays. Every guitar is its own engine: its own strings,
// bridge and body, placed in the stereo field and summed. There is no
// sample playback, convolution, room, reverb or post-effect.
//
// The performance itself - which note starts when, how hard, how long, and
// how a chord is rolled - is written by Tools/PerformRepertoire.py into an
// ACUSTRA_REPERTOIRE_V1 text file, one per piece:
//
//   ACUSTRA_REPERTOIRE_V1
//   title <free text>
//   sample_rate <Hz>
//   tail <seconds rendered after the last event>
//   part <id> key=value ...     one line per guitar (see readPart)
//   # ...                       a comment (the generator writes its tempo map)
//   e <seconds> <part> on <midi> <velocity>
//   e <seconds> <part> chord <midi>:<velocity>:<delay seconds> ...
//   e <seconds> <part> off <midi> <release velocity>
//   e <seconds> <part> vibrato <0-1>
//   e <seconds> <part> tone <pluck position> <touch>
//   e <seconds> <part> hand <bridge-hand pressure 0-1>
//
// A chord is one wrist event: the fretting hand forms it as one shape
// (planChord) and each string is plucked at its own delay after the first,
// which is how a rolled chord or a thumb-before-fingers attack is played.
// Events of one part at one sample apply key-ups before plucks, so a
// repeated note is plucked again rather than doubled.
//
// The mix is normalised once, whole file, to -1 dBFS peak, then written as
// 24-bit PCM with TPDF dither (or 32-bit float with --float) in a
// WAVE_FORMAT_EXTENSIBLE file. Each guitar's own peak is checked against the
// engine's safety limiter so a render that reaches it fails instead of
// quietly changing its transients.

#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using acustra::AcustraEngine;
using acustra::EngineParameters;

constexpr int renderBlockSize = 256;
constexpr double normalisedPeak = 0.8912509381337456; // -1 dBFS
// The engine's safety limiter begins here (RenderDemos keeps under it too).
constexpr double limiterThreshold = 0.89125094;
constexpr double pi = 3.14159265358979323846;

struct PluckedString
{
    int note {};
    float velocity {};
    double delaySeconds {};
};

struct Event
{
    enum class Kind { On, Chord, Off, Vibrato, Tone, Hand };
    double seconds {};
    Kind kind {};
    int note {};
    float value {};
    float second {};
    std::vector<PluckedString> strings;
    std::size_t order {};
};

struct Part
{
    std::string id;
    EngineParameters parameters;
    double pan {};   // -1 left, 0 centre, +1 right
    double gain { 1.0 };
    // When set, the part is scaled so its loudness while it plays sits this
    // many dB from the others' (see balanceParts); gain applies on top.
    bool levelled { false };
    double levelDb {};
    std::vector<Event> events;
    std::vector<float> left;
    std::vector<float> right;
    double peak {};
    int silentNotes {};
    std::string error;
};

struct Performance
{
    std::string title;
    double sampleRate { 96000.0 };
    double tailSeconds { 6.0 };
    std::vector<Part> parts;
};

template <typename Enum>
Enum lookup(const std::map<std::string, Enum>& names, const std::string& value,
            const char* what)
{
    const auto found = names.find(value);
    if (found == names.end())
        throw std::runtime_error(std::string("unknown ") + what + ": " + value);
    return found->second;
}

float number(const std::string& text)
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value {};
    stream >> value;
    if (!stream || !std::isfinite(value))
        throw std::runtime_error("not a number: " + text);
    return static_cast<float>(value);
}

// part <id> model= shape= material= tuning= picking= capture= age= pluck=
//      touch= body= width= output= release= piezo= room= pan= gain= level=
Part readPart(std::istringstream& line)
{
    using namespace acustra;
    static const std::map<std::string, GuitarModel> models {
        { "original", GuitarModel::Original },
        { "bellido1978", GuitarModel::Bellido1978 } };
    static const std::map<std::string, BodyShape> shapes {
        { "parlor", BodyShape::Parlor }, { "auditorium", BodyShape::Auditorium },
        { "dreadnought", BodyShape::Dreadnought }, { "jumbo", BodyShape::Jumbo } };
    static const std::map<std::string, BodyMaterial> materials {
        { "spruce", BodyMaterial::Spruce }, { "mahogany", BodyMaterial::Mahogany },
        { "maple", BodyMaterial::Maple } };
    static const std::map<std::string, Tuning> tunings {
        { "standard", Tuning::Standard }, { "drop_d", Tuning::DropD },
        { "dadgad", Tuning::Dadgad }, { "open_g", Tuning::OpenG },
        { "half_step_down", Tuning::HalfStepDown } };
    static const std::map<std::string, PickingTechnique> pickings {
        { "finger", PickingTechnique::Finger }, { "pick", PickingTechnique::Pick },
        { "thumb", PickingTechnique::Thumb } };
    static const std::map<std::string, CaptureType> captures {
        { "stereo_mic", CaptureType::StereoMic }, { "mono_mic", CaptureType::MonoMic },
        { "piezo", CaptureType::Piezo } };

    Part part;
    if (!(line >> part.id))
        throw std::runtime_error("part without an id");
    auto& parameters = part.parameters;
    std::string field;
    while (line >> field)
    {
        const auto equals = field.find('=');
        if (equals == std::string::npos)
            throw std::runtime_error("part field without '=': " + field);
        const auto key = field.substr(0, equals);
        const auto value = field.substr(equals + 1);
        if (key == "model") parameters.guitarModel = lookup(models, value, "model");
        else if (key == "shape") parameters.shape = lookup(shapes, value, "shape");
        else if (key == "material") parameters.bodyMaterial = lookup(materials, value, "material");
        else if (key == "tuning") parameters.tuning = lookup(tunings, value, "tuning");
        else if (key == "picking") parameters.picking = lookup(pickings, value, "picking");
        else if (key == "capture") parameters.capture = lookup(captures, value, "capture");
        else if (key == "age") parameters.stringAge = number(value);
        else if (key == "pluck") parameters.pluckPosition = number(value);
        else if (key == "touch") parameters.touch = number(value);
        else if (key == "body") parameters.bodyAmount = number(value);
        else if (key == "width") parameters.stereoWidth = number(value);
        else if (key == "output") parameters.outputGain = number(value);
        else if (key == "release") parameters.releaseNoise = number(value);
        else if (key == "piezo") parameters.piezoMix = number(value);
        else if (key == "room") parameters.room = number(value);
        else if (key == "pan") part.pan = std::clamp(static_cast<double>(number(value)), -1.0, 1.0);
        else if (key == "gain") part.gain = number(value);
        else if (key == "level")
        {
            part.levelled = true;
            part.levelDb = number(value);
        }
        else throw std::runtime_error("unknown part field: " + key);
    }
    return part;
}

Performance readPerformance(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input)
        throw std::runtime_error("cannot open " + path.string());
    Performance performance;
    std::string text;
    if (!std::getline(input, text) || text.rfind("ACUSTRA_REPERTOIRE_V1", 0) != 0)
        throw std::runtime_error(path.string() + ": not an ACUSTRA_REPERTOIRE_V1 file");

    std::map<std::string, std::size_t> partIndex;
    std::size_t order = 0;
    int lineNumber = 1;
    while (std::getline(input, text))
    {
        ++lineNumber;
        if (text.empty() || text.front() == '#')
            continue;
        std::istringstream line(text);
        line.imbue(std::locale::classic());
        std::string keyword;
        line >> keyword;
        try
        {
            if (keyword == "title")
            {
                std::getline(line >> std::ws, performance.title);
            }
            else if (keyword == "sample_rate")
            {
                std::string value;
                line >> value;
                performance.sampleRate = number(value);
                if (performance.sampleRate < 8000.0 || performance.sampleRate > 384000.0)
                    throw std::runtime_error("sample rate outside 8-384 kHz");
            }
            else if (keyword == "tail")
            {
                std::string value;
                line >> value;
                performance.tailSeconds = std::clamp(
                    static_cast<double>(number(value)), 0.0, 60.0);
            }
            else if (keyword == "part")
            {
                auto part = readPart(line);
                if (partIndex.count(part.id) != 0)
                    throw std::runtime_error("duplicate part " + part.id);
                partIndex[part.id] = performance.parts.size();
                performance.parts.push_back(std::move(part));
            }
            else if (keyword == "e")
            {
                std::string seconds, id, kind;
                line >> seconds >> id >> kind;
                const auto found = partIndex.find(id);
                if (found == partIndex.end())
                    throw std::runtime_error("event for undeclared part " + id);
                Event event;
                event.seconds = number(seconds);
                if (event.seconds < 0.0)
                    throw std::runtime_error("negative event time");
                event.order = order++;
                std::string a, b;
                if (kind == "on" || kind == "off")
                {
                    line >> a >> b;
                    event.kind = kind == "on" ? Event::Kind::On : Event::Kind::Off;
                    event.note = static_cast<int>(number(a));
                    event.value = number(b);
                }
                else if (kind == "chord")
                {
                    event.kind = Event::Kind::Chord;
                    std::string member;
                    while (line >> member)
                    {
                        const auto first = member.find(':');
                        const auto last = member.rfind(':');
                        if (first == std::string::npos || first == last)
                            throw std::runtime_error("chord member is not note:velocity:delay");
                        event.strings.push_back({
                            static_cast<int>(number(member.substr(0, first))),
                            number(member.substr(first + 1, last - first - 1)),
                            number(member.substr(last + 1)) });
                    }
                    if (event.strings.empty()
                        || event.strings.size() > static_cast<std::size_t>(AcustraEngine::stringCount))
                        throw std::runtime_error("a chord holds one to six strings");
                }
                else if (kind == "vibrato" || kind == "hand")
                {
                    line >> a;
                    event.kind = kind == "vibrato" ? Event::Kind::Vibrato : Event::Kind::Hand;
                    event.value = number(a);
                }
                else if (kind == "tone")
                {
                    line >> a >> b;
                    event.kind = Event::Kind::Tone;
                    event.value = number(a);
                    event.second = number(b);
                }
                else
                {
                    throw std::runtime_error("unknown event kind " + kind);
                }
                performance.parts[found->second].events.push_back(std::move(event));
            }
            else
            {
                throw std::runtime_error("unknown keyword " + keyword);
            }
        }
        catch (const std::exception& error)
        {
            throw std::runtime_error(path.string() + ":" + std::to_string(lineNumber)
                                     + ": " + error.what());
        }
    }
    if (performance.parts.empty())
        throw std::runtime_error(path.string() + ": no parts");
    return performance;
}

// One guitar, start to finish. Runs on its own thread; touches only its part.
void renderPart(Part& part, double sampleRate, double endSeconds)
{
    try
    {
        auto& events = part.events;
        // Key-ups first at a shared sample, then everything else in file order.
        const auto sampleOf = [sampleRate] (double seconds)
        {
            return static_cast<long long>(std::llround(seconds * sampleRate));
        };
        std::stable_sort(events.begin(), events.end(),
                         [&](const Event& left, const Event& right)
                         {
                             const auto l = sampleOf(left.seconds);
                             const auto r = sampleOf(right.seconds);
                             if (l != r)
                                 return l < r;
                             const bool leftOff = left.kind == Event::Kind::Off;
                             const bool rightOff = right.kind == Event::Kind::Off;
                             if (leftOff != rightOff)
                                 return leftOff;
                             return left.order < right.order;
                         });

        AcustraEngine engine;
        engine.setParameters(part.parameters);
        engine.prepare(sampleRate, renderBlockSize);
        engine.setParameters(part.parameters);
        // Observers never reach the output; the audio is bit-identical.
        engine.setPortObserversEnabled(false);

        const auto total = static_cast<std::size_t>(sampleOf(endSeconds));
        part.left.assign(total, 0.0f);
        part.right.assign(total, 0.0f);
        std::size_t rendered = 0;
        const auto renderTo = [&](std::size_t target)
        {
            target = std::min(target, total);
            while (rendered < target)
            {
                const int count = static_cast<int>(std::min<std::size_t>(
                    renderBlockSize, target - rendered));
                engine.process(part.left.data() + rendered,
                               part.right.data() + rendered, count);
                rendered += static_cast<std::size_t>(count);
            }
        };

        auto parameters = part.parameters;
        for (const auto& event : events)
        {
            renderTo(static_cast<std::size_t>(std::max(0LL, sampleOf(event.seconds))));
            switch (event.kind)
            {
            case Event::Kind::On:
                if (!engine.canSound(event.note))
                    ++part.silentNotes;
                engine.noteOn(event.note, std::clamp(event.value, 0.0f, 1.0f));
                break;
            case Event::Kind::Chord:
            {
                std::array<int, AcustraEngine::stringCount> notes {};
                const int count = static_cast<int>(event.strings.size());
                for (int index = 0; index < count; ++index)
                    notes[static_cast<std::size_t>(index)]
                        = event.strings[static_cast<std::size_t>(index)].note;
                engine.planChord(notes.data(), count);
                for (const auto& string : event.strings)
                {
                    if (!engine.canSound(string.note))
                        ++part.silentNotes;
                    const int delay = static_cast<int>(std::llround(
                        std::max(0.0, string.delaySeconds) * sampleRate));
                    engine.noteOn(string.note, std::clamp(string.velocity, 0.0f, 1.0f),
                                  1, delay, false);
                }
                break;
            }
            case Event::Kind::Off:
                engine.noteOffWithVelocity(event.note, 1,
                                           std::clamp(event.value, 0.0f, 1.0f));
                break;
            case Event::Kind::Vibrato:
                engine.setVibrato(event.value);
                break;
            case Event::Kind::Tone:
                parameters.pluckPosition = event.value;
                parameters.touch = event.second;
                engine.setParameters(parameters);
                break;
            case Event::Kind::Hand:
                engine.setPalmMutePressure(event.value);
                break;
            }
        }
        renderTo(total);

        double peak = 0.0;
        for (std::size_t index = 0; index < total; ++index)
        {
            if (!std::isfinite(part.left[index]) || !std::isfinite(part.right[index]))
                throw std::runtime_error("non-finite output");
            peak = std::max({ peak, std::abs(static_cast<double>(part.left[index])),
                              std::abs(static_cast<double>(part.right[index])) });
        }
        part.peak = peak;
    }
    catch (const std::exception& error)
    {
        part.error = error.what();
    }
}

struct Mix
{
    std::vector<float> left;
    std::vector<float> right;
};

// A guitar's loudness while it plays: the RMS over the 50 ms windows within
// 40 dB of its loudest, so rests and the tail do not count.
double activeRms(const Part& part, double sampleRate)
{
    const auto window = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(0.05 * sampleRate)));
    std::vector<double> energies;
    for (std::size_t start = 0; start + window <= part.left.size(); start += window)
    {
        double energy = 0.0;
        for (std::size_t index = start; index < start + window; ++index)
            energy += 0.5 * (static_cast<double>(part.left[index]) * part.left[index]
                             + static_cast<double>(part.right[index]) * part.right[index]);
        energies.push_back(energy / static_cast<double>(window));
    }
    if (energies.empty())
        return 0.0;
    const double loudest = *std::max_element(energies.begin(), energies.end());
    double sum = 0.0;
    std::size_t count = 0;
    for (const double energy : energies)
        if (energy >= loudest * 1.0e-4)
        {
            sum += energy;
            ++count;
        }
    return count > 0 ? std::sqrt(sum / static_cast<double>(count)) : 0.0;
}

// Constructions differ in how loud they are, so an ensemble's balance is set
// by level rather than left to whichever body radiates most: every levelled
// part is brought to its level relative to the levelled parts' mean.
void balanceParts(std::vector<Part>& parts, double sampleRate)
{
    double logSum = 0.0;
    int count = 0;
    std::vector<double> loudness(parts.size(), 0.0);
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        if (!parts[index].levelled)
            continue;
        loudness[index] = activeRms(parts[index], sampleRate);
        if (loudness[index] > 0.0)
        {
            logSum += std::log(loudness[index]);
            ++count;
        }
    }
    if (count == 0)
        return;
    const double reference = std::exp(logSum / count);
    for (std::size_t index = 0; index < parts.size(); ++index)
        if (parts[index].levelled && loudness[index] > 0.0)
            parts[index].gain *= reference / loudness[index]
                * std::pow(10.0, parts[index].levelDb / 20.0);
}

// Each guitar keeps its own microphone image and is set in the field by a
// constant-power balance: panned left, its right channel is attenuated and
// its left raised, so the guitar moves without folding to mono.
Mix mixParts(const std::vector<Part>& parts, std::size_t frames)
{
    Mix mix;
    mix.left.assign(frames, 0.0f);
    mix.right.assign(frames, 0.0f);
    for (const auto& part : parts)
    {
        const double angle = (part.pan + 1.0) * pi / 4.0;
        const double leftGain = part.gain * std::sqrt(2.0) * std::cos(angle);
        const double rightGain = part.gain * std::sqrt(2.0) * std::sin(angle);
        for (std::size_t index = 0; index < frames; ++index)
        {
            mix.left[index] += static_cast<float>(part.left[index] * leftGain);
            mix.right[index] += static_cast<float>(part.right[index] * rightGain);
        }
    }
    return mix;
}

void fadeTail(Mix& mix, double sampleRate, double seconds)
{
    const auto frames = mix.left.size();
    const auto length = std::min<std::size_t>(
        frames, static_cast<std::size_t>(std::llround(seconds * sampleRate)));
    for (std::size_t index = 0; index < length; ++index)
    {
        const double x = static_cast<double>(index) / static_cast<double>(length);
        const auto gain = static_cast<float>(0.5 * (1.0 + std::cos(pi * x)));
        const auto at = frames - length + index;
        mix.left[at] *= gain;
        mix.right[at] *= gain;
    }
}

double mixPeak(const Mix& mix)
{
    double peak = 0.0;
    for (std::size_t index = 0; index < mix.left.size(); ++index)
        peak = std::max({ peak, std::abs(static_cast<double>(mix.left[index])),
                          std::abs(static_cast<double>(mix.right[index])) });
    return peak;
}

void put(std::ostream& output, std::uint32_t value, int bytes)
{
    for (int byte = 0; byte < bytes; ++byte)
        output.put(static_cast<char>((value >> (8 * byte)) & 0xffu));
}

// WAVE_FORMAT_EXTENSIBLE, stereo (front left, front right), 24-bit PCM with
// TPDF dither or 32-bit IEEE float. The dither's generator is seeded, so a
// render is byte-for-byte repeatable on one build.
bool writeWav(const std::filesystem::path& path, const Mix& mix, double sampleRate,
              bool floatingPoint)
{
    const auto frames = mix.left.size();
    const std::uint32_t bytesPerSample = floatingPoint ? 4u : 3u;
    constexpr std::uint32_t channels = 2u;
    if (frames > (std::numeric_limits<std::uint32_t>::max() - 80u)
                     / (channels * bytesPerSample))
        return false;
    const auto dataBytes = static_cast<std::uint32_t>(frames) * channels * bytesPerSample;
    const auto rate = static_cast<std::uint32_t>(std::llround(sampleRate));

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write("RIFF", 4);
    put(output, 4u + 48u + 8u + dataBytes, 4);
    output.write("WAVEfmt ", 8);
    put(output, 40u, 4);
    put(output, 0xfffeu, 2); // WAVE_FORMAT_EXTENSIBLE
    put(output, channels, 2);
    put(output, rate, 4);
    put(output, rate * channels * bytesPerSample, 4);
    put(output, channels * bytesPerSample, 2);
    put(output, bytesPerSample * 8u, 2);
    put(output, 22u, 2);                    // extension size
    put(output, bytesPerSample * 8u, 2);    // valid bits
    put(output, 0x3u, 4);                   // SPEAKER_FRONT_LEFT | RIGHT
    put(output, floatingPoint ? 3u : 1u, 4); // KSDATAFORMAT_SUBTYPE_{IEEE_FLOAT,PCM}
    static constexpr unsigned char guidTail[12] {
        0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 };
    output.write(reinterpret_cast<const char*>(guidTail), sizeof guidTail);
    output.write("data", 4);
    put(output, dataBytes, 4);

    std::uint32_t state = 0x9e3779b9u;
    const auto uniform = [&state] ()
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) / 4294967296.0;
    };
    constexpr double scale = 8388607.0;
    const auto encode = [&](float sample)
    {
        if (floatingPoint)
        {
            std::uint32_t bits {};
            static_assert(sizeof bits == sizeof sample);
            std::memcpy(&bits, &sample, sizeof bits);
            put(output, bits, 4);
            return;
        }
        const double dither = uniform() - uniform(); // TPDF, +-1 LSB
        const double value = std::clamp(
            std::round(static_cast<double>(sample) * scale + dither),
            -scale - 1.0, scale);
        put(output, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)) & 0xffffffu, 3);
    };
    for (std::size_t frame = 0; frame < frames; ++frame)
    {
        encode(mix.left[frame]);
        encode(mix.right[frame]);
    }
    output.close();
    return !output.fail();
}

int renderFile(const std::filesystem::path& input, const std::filesystem::path& output,
               bool floatingPoint)
{
    auto performance = readPerformance(input);
    double lastEvent = 0.0;
    for (const auto& part : performance.parts)
        for (const auto& event : part.events)
        {
            double end = event.seconds;
            for (const auto& string : event.strings)
                end = std::max(end, event.seconds + string.delaySeconds);
            lastEvent = std::max(lastEvent, end);
        }
    const double endSeconds = lastEvent + performance.tailSeconds;

    std::vector<std::thread> threads;
    const unsigned width = std::max(1u, std::thread::hardware_concurrency());
    for (std::size_t first = 0; first < performance.parts.size(); first += width)
    {
        threads.clear();
        for (std::size_t index = first;
             index < std::min(performance.parts.size(), first + width); ++index)
            threads.emplace_back(renderPart, std::ref(performance.parts[index]),
                                 performance.sampleRate, endSeconds);
        for (auto& thread : threads)
            thread.join();
    }

    bool ok = true;
    for (const auto& part : performance.parts)
    {
        if (!part.error.empty())
        {
            std::fprintf(stderr, "%s: part %s: %s\n", input.string().c_str(),
                         part.id.c_str(), part.error.c_str());
            ok = false;
            continue;
        }
        std::printf("  %-12s peak %6.1f dBFS, active RMS %6.1f dBFS%s\n", part.id.c_str(),
                    20.0 * std::log10(std::max(part.peak, 1.0e-12)),
                    20.0 * std::log10(std::max(activeRms(part, performance.sampleRate),
                                               1.0e-12)),
                    part.silentNotes > 0 ? "  (some notes cannot sound)" : "");
        if (part.silentNotes > 0)
        {
            std::fprintf(stderr, "%s: part %s has %d notes it cannot sound\n",
                         input.string().c_str(), part.id.c_str(), part.silentNotes);
            ok = false;
        }
        if (part.peak > limiterThreshold || part.peak < 1.0e-4)
        {
            std::fprintf(stderr, "%s: part %s peaks at %.6f, outside the linear range\n",
                         input.string().c_str(), part.id.c_str(), part.peak);
            ok = false;
        }
    }
    if (!ok)
        return 1;

    balanceParts(performance.parts, performance.sampleRate);
    for (const auto& part : performance.parts)
        if (part.levelled)
            std::printf("  %-12s levelled: %+.1f dB, active RMS in the mix %6.1f dBFS\n",
                        part.id.c_str(), 20.0 * std::log10(part.gain),
                        20.0 * std::log10(std::max(activeRms(part, performance.sampleRate)
                                                   * part.gain, 1.0e-12)));
    const auto frames = performance.parts.front().left.size();
    auto mix = mixParts(performance.parts, frames);
    fadeTail(mix, performance.sampleRate, std::min(1.5, performance.tailSeconds));
    const double peak = mixPeak(mix);
    if (peak <= 1.0e-9)
    {
        std::fprintf(stderr, "%s: silent mix\n", input.string().c_str());
        return 1;
    }
    const double gain = normalisedPeak / peak;
    for (std::size_t index = 0; index < frames; ++index)
    {
        mix.left[index] = static_cast<float>(mix.left[index] * gain);
        mix.right[index] = static_cast<float>(mix.right[index] * gain);
    }
    if (!writeWav(output, mix, performance.sampleRate, floatingPoint))
    {
        std::fprintf(stderr, "could not write %s\n", output.string().c_str());
        return 1;
    }
    std::printf("Rendered %s: %s, %.1f s, %zu guitar%s, mix peak %.1f dBFS, "
                "normalised %+.1f dB\n",
                output.filename().string().c_str(), performance.title.c_str(),
                static_cast<double>(frames) / performance.sampleRate,
                performance.parts.size(), performance.parts.size() == 1 ? "" : "s",
                20.0 * std::log10(peak), 20.0 * std::log10(gain));
    return 0;
}

int smokeTest()
{
    // Two differently built guitars, a rolled chord and a melody, rendered
    // twice: the result must be finite, audible and identical.
    const auto directory = std::filesystem::temp_directory_path();
    const auto source = directory / "acustra-repertoire-smoke.txt";
    {
        std::ofstream file(source);
        file << "ACUSTRA_REPERTOIRE_V1\n"
                "title smoke\nsample_rate 48000\ntail 0.3\n"
                "part a model=bellido1978 shape=auditorium material=mahogany pan=-0.5\n"
                "part b shape=parlor material=maple tuning=drop_d pan=0.5 level=-3\n"
                "e 0.000 a chord 45:0.5:0 52:0.45:0.02 57:0.5:0.04 64:0.6:0.06\n"
                "e 0.000 b on 38 0.6\n"
                "e 0.250 a vibrato 0.4\n"
                "e 0.300 b tone 0.4 0.3\n"
                "e 0.300 b on 69 0.7\n"
                "e 0.600 a off 64 0.5\n"
                "e 0.700 b off 69 0.5\n";
    }
    int result = 0;
    std::vector<float> first;
    for (int pass = 0; pass < 2 && result == 0; ++pass)
    {
        auto performance = readPerformance(source);
        for (auto& part : performance.parts)
            renderPart(part, performance.sampleRate, 1.0);
        for (const auto& part : performance.parts)
            if (!part.error.empty() || part.silentNotes != 0 || part.peak < 1.0e-3
                || part.peak > limiterThreshold)
                result = 1;
        if (result != 0)
            break;
        const auto mix = mixParts(performance.parts, performance.parts.front().left.size());
        if (pass == 0)
            first = mix.left;
        else if (first != mix.left)
            result = 1;
    }
    std::error_code error;
    std::filesystem::remove(source, error);
    if (result != 0)
    {
        std::fprintf(stderr, "repertoire smoke test failed\n");
        return 1;
    }
    std::printf("Acustra repertoire renderer smoke test passed.\n");
    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path directory = "Docs/audio/repertoire";
    bool floatingPoint = false;
    for (int index = 1; index < argc; ++index)
    {
        const std::string argument = argv[index];
        if (argument == "--smoke")
            return smokeTest();
        if (argument == "--float")
            floatingPoint = true;
        else if (argument == "--out" && index + 1 < argc)
            directory = argv[++index];
        else if (argument == "--help" || argument == "-h")
        {
            std::printf("usage: AcustraRenderRepertoire [--float] [--out DIR] "
                        "PERFORMANCE.txt... | --smoke\n");
            return 0;
        }
        else if (!argument.empty() && argument.front() == '-')
        {
            std::fprintf(stderr, "unknown option: %s\n", argument.c_str());
            return 2;
        }
        else
            inputs.emplace_back(argument);
    }
    if (inputs.empty())
    {
        std::fprintf(stderr, "no performance files given (see --help)\n");
        return 2;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || !std::filesystem::is_directory(directory))
    {
        std::fprintf(stderr, "not a directory: %s\n", directory.string().c_str());
        return 1;
    }
    int status = 0;
    for (const auto& input : inputs)
    {
        try
        {
            auto name = input.stem();
            name += ".wav";
            if (renderFile(input, directory / name, floatingPoint) != 0)
                status = 1;
        }
        catch (const std::exception& failure)
        {
            std::fprintf(stderr, "%s\n", failure.what());
            status = 1;
        }
    }
    return status;
}
