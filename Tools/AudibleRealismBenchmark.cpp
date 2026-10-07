// Paired, hot native-DSP callback diagnostic; timings are observations, not tests.
// Build main normally. Build this file, AcustraEngine.cpp and AcustraPerformer.cpp
// for each frozen tree with -DACUSTRA_REALISM_ADAPTER=baseline/current and
// -Dacustra=acustra_baseline/acustra_current, using that tree's Source headers.
// Link all seven objects, with matching Release flags and no LTO or fast-math.
// Usage: AcustraAudibleRealismBenchmark OUTPUT.json [pairs=256] [warmup=32]
//        [--technique finger|pick|thumb] [--velocity 1..127] [--include-44100]
//        [--held-rebend] [--controls-only | --initial-only | --transitions-only | --single-low-e-only]
// Natural-performance comparison adds --natural-performance, --capture stereo|mono|piezo,
// --touch 0..1 and --untimed-check. The latter executes/checks callbacks without
// invoking Clock::now(); use it before the coordinated quiet timing window.
// Tools/BuildAudibleRealismBenchmark.sh OLD_SOURCE CURRENT_SOURCE NEW_BUILD
// builds this harness against both frozen source trees with matching flags.
// Defaults preserve the original Pick/v108/two-rate/seven-scenario matrix.
// Controls-only selects dry 64-frame initial/return/held callbacks; initial-only
// keeps the full rate/block/model/room grid but measures just the first attack.
// Transitions-only selects dry 64-frame return-to-open/live-tuning callbacks
// and their follow-ups after 100 ms of untimed settling.
// The prepared, fixed-storage player is restored outside timing from a snapshot
// before every callback. Its complete controller, PRNG and coefficient-cache
// history is identical across observations. This warms instance memory; results
// describe hot native DSP, excluding host/JUCE and cold-cache scheduling costs.

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
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#define ACUSTRA_REALISM_JOIN_INNER(a, b) a##b
#define ACUSTRA_REALISM_JOIN(a, b) ACUSTRA_REALISM_JOIN_INNER(a, b)
#define ACUSTRA_REALISM_SYMBOL_INNER(a, b) \
    ACUSTRA_REALISM_JOIN(ACUSTRA_REALISM_JOIN(realism_, a), b)

#if defined(ACUSTRA_REALISM_ADAPTER)

#include "DSP/AcustraPerformer.h"
#define ACUSTRA_REALISM_SYMBOL(b) \
    ACUSTRA_REALISM_SYMBOL_INNER(ACUSTRA_REALISM_ADAPTER, b)

namespace acustra
{
// Diagnostic proof of the single-note maximum-velocity path, read only after
// the timed interval. No observer work enters normal DSP processing.
struct AcustraEngineTestAccess
{
    static int untilRelease(const AcustraEngine& engine) noexcept
    {
        int remaining = 100000000;
        for (const auto& voice : engine.voices_)
            if (voice.releaseJoinPending)
                remaining = std::min(remaining, static_cast<int>(voice.releaseJoinAnchorSample
                    + voice.releaseJoinWindowSamples + 1 - engine.sampleClock_));
        return remaining;
    }
    static int releasing(const AcustraEngine& engine) noexcept
    {
        int count = 0;
        for (const auto& voice : engine.voices_)
            if (voice.played && !voice.releaseJoinPending && !voice.keyDown && voice.releaseSeconds > 0.0f)
                ++count;
        return count;
    }
    static int attacks(const AcustraEngine& engine) noexcept
    {
        int count = 0;
        for (const auto& voice : engine.voices_) if (voice.attackFired) ++count;
        return count;
    }
    static std::array<double, 2> lowEReleasePoles(const AcustraEngine& engine) noexcept
    {
        return { engine.voices_[0].releaseSlipPole, engine.voices_[0].releaseReferencePole };
    }
};
}

namespace
{
// The player contains fixed storage plus output pointers set by beginBlock;
// the engine's bank pointer points at immutable static model data. Copying
// the prepared snapshot never aliases mutable DSP storage.
static_assert(std::is_trivially_copyable_v<acustra::Performer>);

struct CallbackState
{
    acustra::Performer prepared, player;
    acustra::EngineParameters tuningParameters;
    std::array<float, 128> left {}, right {};
    int rate {}, frames {}, scenario {}, velocity {};
    static constexpr std::array chord { 40, 47, 52, 56, 59, 64 };
    CallbackState(int sampleRate, int blockSize, int guitar, float room, int selectedScenario,
                  int technique, int selectedVelocity, int capture, float touch)
        : rate(sampleRate), frames(blockSize), scenario(selectedScenario), velocity(selectedVelocity)
    {
        acustra::EngineParameters parameters;
        parameters.capture = static_cast<acustra::CaptureType>(capture);
        parameters.picking = static_cast<acustra::PickingTechnique>(technique);
        parameters.guitarModel = static_cast<acustra::GuitarModel>(guitar);
        parameters.shape = guitar == 1 ? acustra::BodyShape::Auditorium
                                      : acustra::BodyShape::Dreadnought;
        parameters.bodyMaterial = guitar == 1 ? acustra::BodyMaterial::Mahogany
                                             : acustra::BodyMaterial::Spruce;
        parameters.room = room;
        parameters.touch = touch;
        parameters.pluckPosition = 0.28f;
        tuningParameters = parameters;
        tuningParameters.tuning = acustra::Tuning::Dadgad;
        prepared.setParameters(parameters);
        prepared.prepare(rate, frames);
        prepared.setTempoBpm(120.0);
        if (scenario >= 13)
        {
            if (scenario == 21) return;
            if (scenario == 18)
            {
                prepared.setGatherChords(true);
                prepared.beginBlock(left.data(), right.data(), frames);
                for (int stroke = 0; stroke < 4; ++stroke)
                    for (const int pitch : chord)
                        prepared.noteOn(stroke * 8, 1, pitch, velocity);
                prepared.endBlock();
                // The 24 fixed MIDI events become due at offsets 8..32 in
                // the measured callback; no setup/queueing work is timed.
                preroll(acustra::Performer::gatherWindowSamples(rate) - frames - 8);
            }
            else
            {
                const int strokes = scenario == 13 ? 8 : 1;
                for (int stroke = 0; stroke < strokes; ++stroke)
                {
                    prepared.beginBlock(left.data(), right.data(), frames);
                    playChord(prepared);
                    prepared.endBlock();
                    preroll(rate * 3 / 20 - frames);
                }
                if (scenario == 19)
                    preroll(rate * 2 - rate * 3 / 20);
                if (scenario == 14 || scenario == 15 || scenario == 17)
                {
                    prepared.beginBlock(left.data(), right.data(), 1);
                    if (scenario == 17) prepared.controlChange(0, 1, 64, 127);
                    for (const int pitch : chord)
                        prepared.noteOff(0, 1, pitch, scenario == 14 ? 16 : 120);
                    prepared.endBlock();
                    if (scenario == 17)
                        preroll(rate / 40);
                    else
                    {
                        const int remaining = acustra::AcustraEngineTestAccess::untilRelease(prepared.engine());
                        if (remaining < frames / 2 || remaining > rate / 10)
                            throw std::runtime_error("release scenario failed to reach its ordinary join window");
                        preroll(remaining - frames / 2);
                    }
                }
            }
            return;
        }
        // The normal keyboard layout uses conventional ±2-semitone bend,
        // plans the fretted chord, and alternates the player's own strums.
        if (scenario == 1 || scenario == 2 || scenario == 6 || scenario == 7)
        {
            prepared.beginBlock(left.data(), right.data(), frames);
            playChord(prepared);
            prepared.endBlock();
            int remaining = rate / 8 - frames;
            while (remaining > 0)
            {
                const int count = std::min(remaining, frames);
                prepared.process(left.data(), right.data(), count);
                remaining -= count;
            }
        }
        else if (scenario == 5)
        {
            // A public scheduled attack isolates the contact-time geometry
            // refresh while five other strings are already sounding. Queue
            // at a control boundary; the measured wheel at offset one and
            // contact four samples later both precede the next update.
            prepared.engine().setStringPerChannelMode(true);
            for (int string = 0; string < 5; ++string)
                prepared.engine().noteOn(chord[static_cast<std::size_t>(string)],
                    static_cast<float>(velocity) / 127.0f, string + 1);
            constexpr int controlAlignment = 32; // Frozen trees' control cadence.
            int remaining = rate / 8 / controlAlignment * controlAlignment;
            while (remaining > 0)
            {
                const int count = std::min(remaining, frames);
                prepared.process(left.data(), right.data(), count);
                remaining -= count;
            }
            prepared.engine().noteOn(chord[5], static_cast<float>(velocity) / 127.0f,
                                     6, 4);
        }
        else if (scenario == 8 || scenario == 9)
        {
            // Keep five other strings sounding while a fretted low E is
            // handed back. Locate its first played->idle block through the
            // public activity observer, then restore the preceding snapshot.
            prepared.engine().setStringPerChannelMode(true);
            prepared.engine().noteOn(52, static_cast<float>(velocity) / 127.0f, 1);
            for (int string = 1; string < 6; ++string)
                prepared.engine().noteOn(chord[static_cast<std::size_t>(string)],
                    static_cast<float>(velocity) / 127.0f, string + 1);
            preroll(rate / 8);
            prepared.beginBlock(left.data(), right.data(), frames);
            prepared.noteOff(0, 1, 52, 64);
            prepared.endBlock();
            auto preceding = std::make_unique<acustra::Performer>();
            bool found = false;
            for (int elapsed = 0; elapsed < rate * 2; elapsed += frames)
            {
                *preceding = prepared;
                prepared.process(left.data(), right.data(), frames);
                if (!prepared.engine().getStringActivity()[0].played)
                {
                    prepared = *preceding;
                    found = true;
                    break;
                }
            }
            if (!found)
                throw std::runtime_error("released fretted string did not return to open");
            if (scenario == 9)
            {
                prepared.process(left.data(), right.data(), frames);
                preroll(rate / 10);
            }
        }
        else if (scenario == 10 || scenario == 11)
        {
            prepared.beginBlock(left.data(), right.data(), frames);
            playChord(prepared);
            prepared.endBlock();
            preroll(rate / 2 - frames);
            if (scenario == 11)
            {
                prepared.setParameters(tuningParameters);
                preroll(rate / 10);
            }
        }
    }

    void playChord(acustra::Performer& performer) const noexcept
    {
        for (const int note : chord)
            performer.noteOn(0, 1, note, velocity);
    }

    void preroll(int samples) noexcept
    {
        for (int remaining = samples; remaining > 0;)
        {
            const int count = std::min(remaining, frames);
            prepared.process(left.data(), right.data(), count);
            remaining -= count;
        }
    }

    void restore() noexcept { player = prepared; }

    void callback() noexcept
    {
        if (scenario >= 13)
        {
            player.beginBlock(left.data(), right.data(), frames);
            if (scenario == 21) player.noteOn(0, 1, 40, velocity);
            else if (scenario == 13) playChord(player);
            else if (scenario == 16)
                for (const int pitch : chord) player.noteOff(frames / 2, 1, pitch, 64);
            else if (scenario == 17) player.controlChange(frames / 2, 1, 64, 0);
            else if (scenario == 20) player.controlChange(frames / 2, 1, 2, 96);
            player.endBlock();
            return;
        }
        if (scenario == 12)
        {
            player.beginBlock(left.data(), right.data(), frames);
            player.noteOn(0, 1, 40, velocity);
            player.endBlock();
            return;
        }
        if (scenario >= 8)
        {
            if (scenario == 10)
                player.setParameters(tuningParameters);
            player.process(left.data(), right.data(), frames);
            return;
        }
        player.beginBlock(left.data(), right.data(), frames);
        if (scenario == 3 || scenario == 4 || scenario == 6 || scenario == 7)
            player.pitchWheel(0, 1, scenario == 3 ? -1.0f : 1.0f);
        else if (scenario == 5)
            player.pitchWheel(1, 6, 1.0f);
        if (scenario != 2 && scenario != 5 && scenario != 7)
            playChord(player);
        player.endBlock();
    }

    std::uint64_t checksum() const
    {
        std::uint64_t hash = 14695981039346656037ULL;
        for (int index = 0; index < frames; ++index)
            for (const float value : { left[static_cast<std::size_t>(index)],
                                       right[static_cast<std::size_t>(index)] })
            {
                if (!std::isfinite(value))
                    throw std::runtime_error("non-finite callback output");
                hash ^= std::bit_cast<std::uint32_t>(value);
                hash *= 1099511628211ULL;
            }
        const int expectedActive = scenario == 12 || scenario == 21 ? 1 : scenario == 8 || scenario == 9 ? 5 : 6;
        if (player.engine().getActiveVoiceCount() != expectedActive)
            throw std::runtime_error("unexpected active physical-string count");
        if ((scenario == 8 || scenario == 9)
            && player.engine().getStringActivity()[0].played)
            throw std::runtime_error("return-to-open callback did not hand back its released string");
        if (player.droppedEventCount() != 0)
            throw std::runtime_error("player dropped a benchmark event");
        if ((scenario == 14 || scenario == 15 || scenario == 17)
            && acustra::AcustraEngineTestAccess::releasing(player.engine()) != 6)
            throw std::runtime_error("release benchmark did not reach physical damping on all six strings");
        if (scenario == 16)
            for (const auto& activity : player.engine().getStringActivity())
                if (activity.keyDown) throw std::runtime_error("key-up benchmark retained MIDI ownership");
        if (scenario == 18 && acustra::AcustraEngineTestAccess::attacks(player.engine()) == 0)
            throw std::runtime_error("queued burst never reached a physical attack");
        return hash;
    }
};
}

extern "C" void* ACUSTRA_REALISM_SYMBOL(_create)(int rate, int frames, int guitar,
                                                  float room, int scenario,
                                                  int technique, int velocity, int capture, float touch)
{
    return new CallbackState(rate, frames, guitar, room, scenario, technique, velocity, capture, touch);
}
extern "C" void ACUSTRA_REALISM_SYMBOL(_destroy)(void* state)
{
    delete static_cast<CallbackState*>(state);
}
extern "C" void ACUSTRA_REALISM_SYMBOL(_restore)(void* state)
{
    static_cast<CallbackState*>(state)->restore();
}
extern "C" void ACUSTRA_REALISM_SYMBOL(_run)(void* state)
{
    static_cast<CallbackState*>(state)->callback();
}
extern "C" std::uint64_t ACUSTRA_REALISM_SYMBOL(_checksum)(void* state)
{
    return static_cast<CallbackState*>(state)->checksum();
}
extern "C" void ACUSTRA_REALISM_SYMBOL(_release_poles)(void* state, double* slip, double* reference)
{
    const auto poles = acustra::AcustraEngineTestAccess::lowEReleasePoles(
        static_cast<CallbackState*>(state)->player.engine());
    *slip = poles[0];
    *reference = poles[1];
}

#else

#define ACUSTRA_REALISM_DECLARE(name) \
    extern "C" void* ACUSTRA_REALISM_SYMBOL_INNER(name, _create)(int, int, int, float, int, int, int, int, float); \
    extern "C" void ACUSTRA_REALISM_SYMBOL_INNER(name, _destroy)(void*); \
    extern "C" void ACUSTRA_REALISM_SYMBOL_INNER(name, _restore)(void*); \
    extern "C" void ACUSTRA_REALISM_SYMBOL_INNER(name, _run)(void*); \
    extern "C" std::uint64_t ACUSTRA_REALISM_SYMBOL_INNER(name, _checksum)(void*); \
    extern "C" void ACUSTRA_REALISM_SYMBOL_INNER(name, _release_poles)(void*, double*, double*);
ACUSTRA_REALISM_DECLARE(baseline)
ACUSTRA_REALISM_DECLARE(current)

namespace
{
using Clock = std::chrono::steady_clock;
static_assert(Clock::is_steady);

struct PlayerApi
{
    void* (*create)(int, int, int, float, int, int, int, int, float);
    void (*destroy)(void*);
    void (*restore)(void*);
    void (*run)(void*);
    std::uint64_t (*checksum)(void*);
    void (*releasePoles)(void*, double*, double*);
};
#define ACUSTRA_REALISM_API(name) PlayerApi { realism_##name##_create, \
    realism_##name##_destroy, realism_##name##_restore, realism_##name##_run, \
    realism_##name##_checksum, realism_##name##_release_poles }

struct PlayerInstance
{
    PlayerApi api;
    void* state;
    std::uint64_t expectedHash {};
    bool hasHash {};
    bool requireSingleSlipProof {}, baseline {};
    double slipPole {}, referencePole {};
    PlayerInstance(PlayerApi selected, int rate, int frames, int guitar, float room, int scenario,
                   int technique, int velocity, int capture, float touch)
        : api(selected), state(api.create(rate, frames, guitar, room, scenario, technique, velocity, capture, touch)),
          requireSingleSlipProof(scenario == 12), baseline(selected.create == realism_baseline_create) {}
    ~PlayerInstance() { api.destroy(state); }
    PlayerInstance(const PlayerInstance&) = delete;
    PlayerInstance& operator=(const PlayerInstance&) = delete;

    double measure()
    {
        api.restore(state);
        const auto start = Clock::now();
        api.run(state);
        const auto stop = Clock::now();
        const auto hash = api.checksum(state);
        if (requireSingleSlipProof)
        {
            api.releasePoles(state, &slipPole, &referencePole);
            if (baseline ? slipPole != 0.0 || referencePole != 0.0
                         : !(slipPole > 0.0) || referencePole != 0.0)
                throw std::runtime_error("single low-E run did not exercise legacy-cancelled/current-absolute Finger slip");
        }
        if (hasHash && hash != expectedHash)
            throw std::runtime_error("snapshot trials did not reproduce identical output");
        expectedHash = hash;
        hasHash = true;
        return std::chrono::duration<double, std::micro>(stop - start).count();
    }
};

double percentile(std::vector<double> values, double quantile)
{
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(std::ceil(quantile * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
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
           << ",\"output_fnv64\":\"" << checksum << "\",\"samples_us\":[";
    for (std::size_t index = 0; index < values.size(); ++index)
        output << (index == 0 ? "" : ",") << values[index];
    output << "]}";
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
        constexpr const char* usage = "usage: AcustraAudibleRealismBenchmark OUTPUT.json [pairs=256] [warmup=32] [--technique finger|pick|thumb] [--velocity 1..127] [--capture stereo|mono|piezo] [--touch 0..1] [--include-44100] [--held-rebend] [--untimed-check] [--controls-only | --initial-only | --transitions-only | --single-low-e-only | --natural-performance]";
        if (argc < 2)
            throw std::runtime_error(usage);
        int nextArgument = 2;
        const auto hasCount = [&]()
        {
            return nextArgument < argc && std::string(argv[nextArgument]).rfind("--", 0) != 0;
        };
        const int repeats = hasCount() ? positiveInteger(argv[nextArgument++]) : 256;
        const int warmups = hasCount() ? positiveInteger(argv[nextArgument++]) : 32;
        int technique = 1;
        int velocity = 108;
        int capture = 0;
        float touch = 0.58f;
        std::string captureName = "stereo_mic";
        bool naturalPerformance = false, untimedCheck = false;
        bool include44100 = false, heldRebend = false, controlsOnly = false, initialOnly = false,
             transitionsOnly = false, singleLowEOnly = false;
        constexpr std::array techniqueNames { "finger", "pick", "thumb" };
        for (; nextArgument < argc; ++nextArgument)
        {
            const std::string option(argv[nextArgument]);
            const auto optionValue = [&]() -> const char*
            {
                if (++nextArgument >= argc)
                    throw std::runtime_error("missing value for " + option);
                return argv[nextArgument];
            };
            if (option == "--technique")
            {
                const std::string selected(optionValue());
                const auto found = std::find(techniqueNames.begin(), techniqueNames.end(), selected);
                if (found == techniqueNames.end())
                    throw std::runtime_error("technique must be finger, pick or thumb");
                technique = static_cast<int>(found - techniqueNames.begin());
            }
            else if (option == "--velocity")
            {
                velocity = positiveInteger(optionValue());
                if (velocity > 127)
                    throw std::runtime_error("MIDI velocity must be from 1 to 127");
            }
            else if (option == "--include-44100")
                include44100 = true;
            else if (option == "--capture")
            {
                const std::string value(optionValue());
                if (value == "stereo") { capture = 0; captureName = "stereo_mic"; }
                else if (value == "mono") { capture = 7; captureName = "mono_mic"; }
                else if (value == "piezo") { capture = 6; captureName = "piezo"; }
                else throw std::runtime_error("unknown capture");
            }
            else if (option == "--touch")
            {
                const std::string value(optionValue());
                std::size_t length {};
                touch = std::stof(value, &length);
                if (length != value.size() || !(touch >= 0.0f && touch <= 1.0f))
                    throw std::runtime_error("Touch must be a finite number from zero to one");
            }
            else if (option == "--natural-performance") naturalPerformance = true;
            else if (option == "--untimed-check") untimedCheck = true;
            else if (option == "--held-rebend")
                heldRebend = true;
            else if (option == "--controls-only")
                controlsOnly = true;
            else if (option == "--initial-only")
                initialOnly = true;
            else if (option == "--transitions-only")
                transitionsOnly = true;
            else if (option == "--single-low-e-only")
                singleLowEOnly = true;
            else
                throw std::runtime_error("unknown option: " + option);
        }
        if (static_cast<int>(controlsOnly) + static_cast<int>(initialOnly)
            + static_cast<int>(transitionsOnly) + static_cast<int>(singleLowEOnly)
            + static_cast<int>(naturalPerformance) > 1)
            throw std::runtime_error("select only one scenario subset");
        if (singleLowEOnly && (technique != 0 || velocity != 127))
            throw std::runtime_error("single-low-e-only requires technique finger and velocity 127");
        if (std::filesystem::exists(argv[1]))
            throw std::runtime_error("output already exists");
        std::ofstream output(argv[1]);
        if (!output)
            throw std::runtime_error("could not create output");
        output << std::setprecision(12)
               << "{\n\"protocol\":\"" << (naturalPerformance
                   ? "paired_hot_native_performer_callback_natural_performance_v1"
                   : "paired_hot_native_performer_callback_v2") << "\","
                  "\n\"scope\":\"native Performer and DSP; excludes host/JUCE overhead\","
                  "\n\"ordering\":\"baseline first on even trials, current first on odd trials\","
                  "\n\"setup\":\"prepared fixed-storage player snapshot restored before each timed callback; warms instance memory\","
                  "\n\"timing_excludes\":[\"construction\",\"prepare\",\"preroll\",\"snapshot_restore\",\"checksum\",\"output\"],"
                  "\n\"quantiles\":\"nearest-rank\",\n\"untimed_validation_only\":" << (untimedCheck ? "true" : "false")
               << ",\n\"measured_pairs\":" << (untimedCheck ? 0 : repeats)
               << ",\n\"warmup_pairs\":" << (untimedCheck ? 0 : warmups)
               << ",\n\"return_stroke_and_held_preroll_seconds\":0.125,"
                  "\n\"velocity_midi\":" << velocity << ",\n\"touch\":" << touch << ",\n\"pluck_position\":0.28,"
                  "\n\"capture\":\"" << captureName << "\",\n\"picking\":\""
               << techniqueNames[static_cast<std::size_t>(technique)] << "\","
                  "\n\"chord_midi\":[40,47,52,56,59,64],\n\"channel\":1,"
                  "\n\"scheduled_contact_scenario\":\"five held strings; new high-E attack queued outside timing with delay 4; channel 6 wheel at timed offset 1; setup ends at 32-sample control boundary\","
                  "\n\"held_rebend_scenario\":\"wheel at offset 0 on a held chord; no new noteOn\","
                  "\n\"return_to_open_scenario\":\"five held strings plus released fretted low-E; public played-to-idle observer locates handoff outside timing; measured process contains that handoff\","
                  "\n\"live_tuning_scenario\":\"held chord after 0.5 s; measured Standard-to-Dadgad setParameters plus process\","
                  "\n\"transition_followup_settling_seconds\":0.1,"
                  "\n\"single_low_e_scenario\":\"one normal Performer MIDI40 noteOn, no strum/repeat force draw; legacy zero slip/reference and current positive slip/zero reference verified after every callback outside timing\","
                  "\n\"natural_scenarios\":\"eight prior 150 ms strums before reattack; release velocities 16/120 with existing join deadline halfway through callback; ordinary keyup halfway; pedal release halfway after held keyups; 24 Gather events due at offsets 8..32; chord held 2 s; CC2=96 halfway; one normal low-E attack\","
                  "\n\"natural_validation\":\"zero dropped MIDI; six physical strings; release scenarios prove all six are physically damping; keyup releases all ownership; queued burst proves at least one physical attack; each restored callback output hash repeats\","
                  "\n\"conventional_bend_range_semitones\":2,\n\"results\":[\n";
        constexpr std::array scenarios { "initial_downstroke", "return_upstroke",
                                         "held_chord", "bend_down_new_chord", "bend_up_new_chord",
                                         "scheduled_contact_after_bend", "bend_up_return_upstroke",
                                         "bend_up_held_chord", "return_to_open_handoff",
                                         "return_to_open_settled", "live_tuning_change",
                                         "live_tuning_settled", "single_low_e_initial",
                                         "coherent_hand_repeated_strum", "gentle_release_contact",
                                         "firm_release_contact", "ordinary_key_up", "pedal_release_contact",
                                         "gather_queued_burst_24_events", "held_body_two_seconds", "bridge_hand_contact",
                                         "single_low_e_contact" };
        constexpr std::array models { "Original", "Bellido1978" };
        const std::vector<int> rates = include44100 ? std::vector<int> { 44100, 48000, 96000 }
                                                   : std::vector<int> { 48000, 96000 };
        const std::vector<int> blockSizes = controlsOnly || transitionsOnly ? std::vector<int> { 64 }
                                                                          : std::vector<int> { 64, 128 };
        const std::vector<float> rooms = controlsOnly || transitionsOnly || naturalPerformance ? std::vector<float> { 0.0f }
                                                                        : std::vector<float> { 0.0f, 0.5f };
        std::vector<int> selectedScenarios = singleLowEOnly ? std::vector<int> { 12 }
            : transitionsOnly ? std::vector<int> { 8, 9, 10, 11 }
            : initialOnly ? std::vector<int> { 0 }
            : controlsOnly ? std::vector<int> { 0, 1, 2 }
                           : std::vector<int> { 0, 1, 2, 3, 4, 5, 6 };
        if (heldRebend && !controlsOnly && !initialOnly && !transitionsOnly && !singleLowEOnly)
            selectedScenarios.push_back(7);
        if (naturalPerformance)
            selectedScenarios = {13, 14, 15, 16, 17, 18, 19, 20, 21};
        bool first = true;
        for (const int rate : rates)
            for (const int frames : blockSizes)
                for (int guitar = 0; guitar < 2; ++guitar)
                    for (const float room : rooms)
                        for (const int scenario : selectedScenarios)
                        {
                            PlayerInstance baseline(ACUSTRA_REALISM_API(baseline), rate, frames, guitar, room, scenario, technique, velocity, capture, touch);
                            PlayerInstance current(ACUSTRA_REALISM_API(current), rate, frames, guitar, room, scenario, technique, velocity, capture, touch);
                            if (untimedCheck)
                            {
                                for (auto* instance : { &baseline, &current })
                                {
                                    instance->api.restore(instance->state);
                                    instance->api.run(instance->state);
                                    const auto firstHash = instance->api.checksum(instance->state);
                                    instance->api.restore(instance->state);
                                    instance->api.run(instance->state);
                                    if (firstHash != instance->api.checksum(instance->state))
                                        throw std::runtime_error("untimed snapshot reproducibility failure");
                                    instance->expectedHash = firstHash;
                                }
                                output << (first ? "" : ",\n") << "{\"rate\":" << rate
                                       << ",\"frames\":" << frames << ",\"guitar_model\":\""
                                       << models[static_cast<std::size_t>(guitar)] << "\",\"room\":" << room
                                       << ",\"scenario\":\"" << scenarios[static_cast<std::size_t>(scenario)]
                                       << "\",\"untimed_checks_passed\":true,\"baseline_output_fnv64\":\""
                                       << baseline.expectedHash << "\",\"current_output_fnv64\":\""
                                       << current.expectedHash << "\"}";
                                first = false;
                                std::cout << "Untimed validated: " << rate << '/' << frames << '/' << guitar << '/' << scenario << '\n';
                                continue;
                            }
                            std::vector<double> before, after;
                            before.reserve(static_cast<std::size_t>(repeats));
                            after.reserve(static_cast<std::size_t>(repeats));
                            for (int trial = -warmups; trial < repeats; ++trial)
                            {
                                double b {}, c {};
                                if ((trial & 1) == 0)
                                {
                                    b = baseline.measure();
                                    c = current.measure();
                                }
                                else
                                {
                                    c = current.measure();
                                    b = baseline.measure();
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
                                   << ",\"frames\":" << frames << ",\"guitar_model\":\""
                                   << models[static_cast<std::size_t>(guitar)] << "\",\"room\":" << room
                                   << ",\"scenario\":\"" << scenarios[static_cast<std::size_t>(scenario)]
                                   << "\",\"deadline_us\":" << deadline << ",\"baseline\":";
                            statistics(output, before, deadline, baseline.expectedHash);
                            output << ",\"current\":";
                            statistics(output, after, deadline, current.expectedHash);
                            if (singleLowEOnly)
                                output << ",\"release_path_proof\":{\"verified\":true,\"baseline_slip_pole\":"
                                       << baseline.slipPole << ",\"baseline_reference_pole\":" << baseline.referencePole
                                       << ",\"current_slip_pole\":" << current.slipPole
                                       << ",\"current_reference_pole\":" << current.referencePole << '}';
                            output << ",\"bit_identical\":"
                                   << (baseline.expectedHash == current.expectedHash ? "true" : "false")
                                   << ",\"paired_current_over_baseline_p50\":" << percentile(ratios, 0.5)
                                   << ",\"paired_current_over_baseline_p95\":" << percentile(ratios, 0.95) << '}';
                            output.flush();
                            first = false;
                            std::cout << rate << " Hz / " << frames << " / "
                                      << models[static_cast<std::size_t>(guitar)] << " / room " << room << " / "
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
