// Native, paired callback diagnostic. No timing threshold is a CI assertion.
// Compile this file three times: once normally for main, once per frozen DSP
// tree with -DACUSTRA_CALLBACK_ADAPTER=baseline/current and respectively
// -Dacustra=acustra_baseline/acustra_current. Compile each AcustraEngine.cpp
// with the same namespace definition and include its own Source directory.
// Link the five objects into one executable. No LTO or fast-math is required.
// Compile a current adapter supporting named guitars with
// -DACUSTRA_CALLBACK_GUITAR_MODELS. The legacy adapter keeps Original but uses
// the matching requested material/shape/wood for its timing reference.
// Usage: AcustraCallbackBenchmark OUTPUT.json [pairs=256] [warmup=16] [--guitar-models]
// Output contains every timed observation; construction/prepare/50 ms pre-roll and
// result checking are outside the interval. Each pair alternates engine order.

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#define ACUSTRA_JOIN_INNER(a, b) a##b
#define ACUSTRA_JOIN(a, b) ACUSTRA_JOIN_INNER(a, b)
#define ACUSTRA_SYMBOL_INNER(a, b) ACUSTRA_JOIN(ACUSTRA_JOIN(callback_, a), b)

#if defined(ACUSTRA_CALLBACK_ADAPTER)

#include "DSP/AcustraEngine.h"
#define ACUSTRA_SYMBOL(b) ACUSTRA_SYMBOL_INNER(ACUSTRA_CALLBACK_ADAPTER, b)

namespace
{
struct CallbackState
{
    acustra::AcustraEngine engine;
    std::array<float, 128> left {}, right {};
    int rate {}, frames {};
    static constexpr std::array chord { 40, 47, 52, 56, 59, 64 };

    CallbackState(int sampleRate, int blockSize, int material, int guitar)
        : rate(sampleRate), frames(blockSize)
    {
        acustra::EngineParameters parameters;
        parameters.stringMaterial = material == 0
            ? acustra::StringMaterial::Nylon : acustra::StringMaterial::Steel;
        parameters.capture = acustra::CaptureType::StereoMic;
        parameters.picking = acustra::PickingTechnique::Finger;
        parameters.shape = guitar == 2 ? acustra::BodyShape::Parlor
            : guitar == 1 || guitar == 3 ? acustra::BodyShape::Auditorium
            : acustra::BodyShape::Dreadnought;
        parameters.bodyMaterial = guitar == 1
            ? acustra::BodyMaterial::Cedar : acustra::BodyMaterial::Spruce;
#if defined(ACUSTRA_CALLBACK_GUITAR_MODELS)
        parameters.guitarModel = static_cast<acustra::GuitarModel>(guitar);
#endif
        parameters.touch = 0.58f;
        parameters.pluckPosition = 0.28f;
        engine.setParameters(parameters);
        engine.prepare(rate, frames);
    }

    void playChord()
    {
        for (int string = 0; string < 6; ++string)
            engine.noteOn(chord[static_cast<std::size_t>(string)], 1.0f, string + 1);
    }

    void setup(int scenario)
    {
        engine.reset();
        engine.setStringPerChannelMode(scenario != 2);
        if (scenario == 1)
        {
            playChord();
            for (int remaining = rate / 20; remaining > 0;)
            {
                const int count = std::min(remaining, frames);
                engine.process(left.data(), right.data(), count);
                remaining -= count;
            }
        }
    }

    void callback(int scenario)
    {
        if (scenario == 2)
            engine.noteOn(100, 1.0f); // high E's eighth natural harmonic
        else
            playChord();
        engine.process(left.data(), right.data(), frames);
    }

    std::uint64_t checksum() const
    {
        std::uint64_t hash = 14695981039346656037ULL;
        double energy = 0.0;
        for (int index = 0; index < frames; ++index)
            for (const float value : { left[static_cast<std::size_t>(index)],
                                       right[static_cast<std::size_t>(index)] })
            {
                if (!std::isfinite(value))
                    throw std::runtime_error("non-finite callback output");
                energy += static_cast<double>(value) * value;
                hash ^= std::bit_cast<std::uint32_t>(value);
                hash *= 1099511628211ULL;
            }
        // A measured microphone delay may legitimately exceed a short first
        // block. Active-note checks still verify that the event was accepted.
        if (!std::isfinite(energy))
            throw std::runtime_error("non-finite callback energy");
        return hash;
    }
};
}

extern "C" void* ACUSTRA_SYMBOL(_create)(int rate, int frames, int material, int guitar)
{
    return new CallbackState(rate, frames, material, guitar);
}
extern "C" void ACUSTRA_SYMBOL(_destroy)(void* state)
{
    delete static_cast<CallbackState*>(state);
}
extern "C" void ACUSTRA_SYMBOL(_setup)(void* state, int scenario)
{
    static_cast<CallbackState*>(state)->setup(scenario);
}
extern "C" void ACUSTRA_SYMBOL(_run)(void* state, int scenario)
{
    static_cast<CallbackState*>(state)->callback(scenario);
}
extern "C" std::uint64_t ACUSTRA_SYMBOL(_checksum)(void* state)
{
    return static_cast<CallbackState*>(state)->checksum();
}
extern "C" int ACUSTRA_SYMBOL(_active)(void* state)
{
    return static_cast<CallbackState*>(state)->engine.getActiveVoiceCount();
}
extern "C" bool ACUSTRA_SYMBOL(_has_guitar_models)()
{
#if defined(ACUSTRA_CALLBACK_GUITAR_MODELS)
    return true;
#else
    return false;
#endif
}

#else

#define ACUSTRA_DECLARE(name) \
    extern "C" void* ACUSTRA_SYMBOL_INNER(name, _create)(int, int, int, int); \
    extern "C" void ACUSTRA_SYMBOL_INNER(name, _destroy)(void*); \
    extern "C" void ACUSTRA_SYMBOL_INNER(name, _setup)(void*, int); \
    extern "C" void ACUSTRA_SYMBOL_INNER(name, _run)(void*, int); \
    extern "C" std::uint64_t ACUSTRA_SYMBOL_INNER(name, _checksum)(void*); \
    extern "C" int ACUSTRA_SYMBOL_INNER(name, _active)(void*); \
    extern "C" bool ACUSTRA_SYMBOL_INNER(name, _has_guitar_models)();
ACUSTRA_DECLARE(baseline)
ACUSTRA_DECLARE(current)

namespace
{
using Clock = std::chrono::steady_clock;
static_assert(Clock::is_steady);

struct EngineApi
{
    void* (*create)(int, int, int, int);
    void (*destroy)(void*);
    void (*setup)(void*, int);
    void (*run)(void*, int);
    std::uint64_t (*checksum)(void*);
    int (*active)(void*);
};
#define ACUSTRA_API(name) EngineApi { callback_##name##_create, \
    callback_##name##_destroy, callback_##name##_setup, callback_##name##_run, \
    callback_##name##_checksum, callback_##name##_active }

struct EngineInstance
{
    EngineApi api;
    void* state;
    int rate {}, frames {}, material {}, guitar {};
    std::uint64_t expectedHash {};
    bool hasHash {};
    explicit EngineInstance(EngineApi selected, int rate, int frames, int material, int guitar)
        : api(selected), state(nullptr), rate(rate), frames(frames), material(material), guitar(guitar) {}
    ~EngineInstance() { api.destroy(state); }
    EngineInstance(const EngineInstance&) = delete;
    EngineInstance& operator=(const EngineInstance&) = delete;

    double measure(int scenario)
    {
        // A fresh prepared instrument prevents coefficient-cache history from
        // one trial changing the initial state of the next. All of this setup
        // is outside the timed audio callback.
        api.destroy(state);
        state = api.create(rate, frames, material, guitar);
        api.setup(state, scenario);
        const auto start = Clock::now();
        api.run(state, scenario);
        const auto stop = Clock::now();
        const auto hash = api.checksum(state);
        if (hasHash && hash != expectedHash)
            throw std::runtime_error("reset trials did not reproduce identical output");
        expectedHash = hash;
        hasHash = true;
        if (api.active(state) != (scenario == 2 ? 1 : 6))
            throw std::runtime_error("unexpected active-string count");
        return std::chrono::duration<double, std::micro>(stop - start).count();
    }
};

double percentile(std::vector<double> values, double quantile)
{
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(quantile * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
}

void samples(std::ostream& output, const std::vector<double>& values)
{
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index)
        output << (index == 0 ? "" : ",") << values[index];
    output << ']';
}

void statistics(std::ostream& output, const std::vector<double>& values,
                double deadlineUs, std::uint64_t checksum)
{
    const double p50 = percentile(values, 0.50);
    const double p95 = percentile(values, 0.95);
    const double maximum = *std::max_element(values.begin(), values.end());
    const auto over = std::count_if(values.begin(), values.end(),
                                   [deadlineUs](double value) { return value > deadlineUs; });
    output << "{\"p50_us\":" << p50 << ",\"p95_us\":" << p95
           << ",\"max_us\":" << maximum << ",\"p50_deadline_ratio\":" << p50 / deadlineUs
           << ",\"p95_deadline_ratio\":" << p95 / deadlineUs
           << ",\"max_deadline_ratio\":" << maximum / deadlineUs
           << ",\"over_deadline_count\":" << over
           << ",\"output_fnv64\":\"" << checksum << "\",\"samples_us\":";
    samples(output, values);
    output << '}';
}

int positiveInteger(const char* text)
{
    std::size_t length {};
    const std::string value(text);
    const int result = std::stoi(value, &length);
    if (length != value.size() || result < 1 || result > 100000)
        throw std::runtime_error("iteration count must be an integer from 1 to 100000");
    return result;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2 || argc > 5)
            throw std::runtime_error("usage: AcustraCallbackBenchmark OUTPUT.json [pairs=256] [warmup=16] [--guitar-models]");
        const int repeats = argc >= 3 ? positiveInteger(argv[2]) : 256;
        const int warmups = argc >= 4 ? positiveInteger(argv[3]) : 16;
        const bool namedGuitars = argc == 5 && std::string(argv[4]) == "--guitar-models";
        if (argc == 5 && !namedGuitars)
            throw std::runtime_error("unknown final option");
        if (namedGuitars && !callback_current_has_guitar_models())
            throw std::runtime_error("current adapter was compiled without named-guitar support");
        if (std::filesystem::exists(argv[1]))
            throw std::runtime_error("output already exists");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("could not create output");
        output << std::setprecision(12)
               << "{\n\"protocol\":\"paired_native_noteOn_plus_process_v2\","
                  "\n\"scope\":\"native DSP including MIDI noteOns; excludes JUCE and host overhead\","
                  "\n\"ordering\":\"baseline first on even trials, current first on odd trials\","
                  "\n\"setup\":\"fresh engine prepared before each timed callback\","
                  "\n\"quantiles\":\"nearest-rank\",\n\"measured_pairs\":" << repeats
               << ",\n\"warmup_pairs\":" << warmups
               << ",\n\"repick_preroll_seconds\":0.05,\n\"velocity\":1.0,"
                  "\n\"touch\":0.58,\n\"pluck_position\":0.28,"
                  "\n\"capture\":\"stereo_mic\",\n\"picking\":\"finger\","
                  "\n\"chord_midi\":[40,47,52,56,59,64],\n\"harmonic_midi\":100,"
                  "\n\"named_model_comparison\":\"current named model versus baseline Original with matching material, shape and wood\","
                  "\n\"results\":[\n";
        constexpr std::array scenarios { "six_string_initial", "six_string_repick", "eighth_harmonic" };
        struct ModelCase { int material, guitar; const char* name; const char* shape; const char* wood; };
        std::vector<ModelCase> cases {
            { 0, 0, "Original", "dreadnought", "spruce" },
            { 1, 0, "Original", "dreadnought", "spruce" }
        };
        if (namedGuitars)
        {
            cases.push_back({ 0, 1, "Bellido1978", "auditorium", "cedar" });
            cases.push_back({ 1, 2, "Washburn1897", "parlor", "spruce" });
            cases.push_back({ 1, 3, "SantaCruzOM2022", "auditorium", "spruce" });
            cases.push_back({ 1, 4, "MartinD18V2007", "dreadnought", "spruce" });
        }
        bool first = true;
        for (const int rate : { 48000, 96000 })
            for (const int frames : { 32, 64, 128 })
                for (const auto& model : cases)
                    for (int scenario = 0; scenario < 3; ++scenario)
                    {
                        const int material = model.material;
                        EngineInstance baseline(ACUSTRA_API(baseline), rate, frames, material, model.guitar);
                        EngineInstance current(ACUSTRA_API(current), rate, frames, material, model.guitar);
                        std::vector<double> before, after;
                        before.reserve(static_cast<std::size_t>(repeats));
                        after.reserve(static_cast<std::size_t>(repeats));
                        for (int trial = -warmups; trial < repeats; ++trial)
                        {
                            double b {}, c {};
                            if ((trial & 1) == 0)
                            {
                                b = baseline.measure(scenario);
                                c = current.measure(scenario);
                            }
                            else
                            {
                                c = current.measure(scenario);
                                b = baseline.measure(scenario);
                            }
                            if (trial >= 0)
                            {
                                before.push_back(b);
                                after.push_back(c);
                            }
                        }
                        const double deadline = 1.0e6 * frames / rate;
                        std::vector<double> ratios;
                        ratios.reserve(before.size());
                        for (std::size_t index = 0; index < before.size(); ++index)
                            ratios.push_back(after[index] / before[index]);
                        output << (first ? "" : ",\n") << "{\"rate\":" << rate
                               << ",\"frames\":" << frames << ",\"material\":\""
                               << (material == 0 ? "nylon" : "steel")
                               << "\",\"current_guitar_model\":\"" << model.name
                               << "\",\"baseline_guitar_model\":\"Original"
                               << "\",\"body_shape\":\"" << model.shape
                               << "\",\"body_material\":\"" << model.wood
                               << "\",\"scenario\":\"" << scenarios[static_cast<std::size_t>(scenario)]
                               << "\",\"deadline_us\":" << deadline << ",\"baseline\":";
                        statistics(output, before, deadline, baseline.expectedHash);
                        output << ",\"current\":";
                        statistics(output, after, deadline, current.expectedHash);
                        output << ",\"paired_current_over_baseline_p50\":" << percentile(ratios, 0.5)
                               << ",\"paired_current_over_baseline_p95\":" << percentile(ratios, 0.95) << '}';
                        output.flush();
                        first = false;
                        std::cout << rate << " Hz / " << frames << " / "
                                  << (material == 0 ? "nylon" : "steel") << " / "
                                  << model.name << " / "
                                  << scenarios[static_cast<std::size_t>(scenario)]
                                  << ": baseline/current p95 " << percentile(before, .95)
                                  << " / " << percentile(after, .95) << " us\n" << std::flush;
                    }
        output << "\n],\n\"complete\":true\n}\n";
        output.close();
        if (!output)
            throw std::runtime_error("could not finish output");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
#endif
