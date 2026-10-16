// Fixed native performance comparison; build this unchanged against each DSP.
// Only public Performer/setup calls. Eight seconds per workload, 64-frame
// callbacks, no warmup or repeated attempts. Setup, inspection and file I/O
// are outside both timing windows. Output peaks are observations, not a
// headroom/unclipped-audio assertion.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
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
constexpr int blockFrames = 64;
constexpr int seconds = 8;
constexpr std::uint64_t billion = 1000000000ULL;
constexpr clockid_t wallClock = CLOCK_MONOTONIC;
constexpr clockid_t cpuClock = CLOCK_THREAD_CPUTIME_ID;

std::uint64_t now(clockid_t clock)
{
    timespec value {};
    if (clock_gettime(clock, &value) != 0)
        throw std::runtime_error("clock_gettime failed");
    return static_cast<std::uint64_t>(value.tv_sec) * billion
        + static_cast<std::uint64_t>(value.tv_nsec);
}

std::uint64_t resolution(clockid_t clock)
{
    timespec value {};
    if (clock_getres(clock, &value) != 0)
        throw std::runtime_error("clock_getres failed");
    return static_cast<std::uint64_t>(value.tv_sec) * billion
        + static_cast<std::uint64_t>(value.tv_nsec);
}

enum class Kind { On, Off, Control, Bend, BendRange, Connected };
struct Event
{
    int frame, channel;
    Kind kind;
    int first, second;
    float value;
};

struct Score
{
    std::string name;
    int rate;
    bool strings = false;
    acustra::PickingTechnique picking = acustra::PickingTechnique::Finger;
    std::vector<Event> events {};

    void add(double at, Kind kind, int channel, int first = 0,
             int second = 0, float value = 0)
    {
        events.push_back({ static_cast<int>(std::lround(at * rate)), channel,
                           kind, first, second, value });
    }
    void note(double at, double duration, int pitch, int velocity, int channel = 1)
    {
        add(at, Kind::On, channel, pitch, velocity);
        add(at + duration, Kind::Off, channel, pitch, 64);
    }
    void connect(double at, int source, int target, int velocity)
    {
        add(at, Kind::Control, 1, 84, source);
        // Source is metadata for an ownership assertion outside the callback.
        add(at, Kind::Connected, 1, target, velocity, static_cast<float>(source));
        add(at + 0.005, Kind::Off, 1, source, 64);
    }
    void slide(double at, double duration, int channel, float from, float to)
    {
        const int count = static_cast<int>(std::lround(duration / 0.002));
        for (int index = 0; index <= count; ++index)
        {
            const double u = static_cast<double>(index) / count;
            const float position = static_cast<float>(u * u * (3.0 - 2.0 * u));
            add(at + duration * u, Kind::Bend, channel, 0, 0,
                (from + (to - from) * position) / 12.0f);
        }
    }
};

std::vector<Score> scores()
{
    std::vector<Score> result;
    for (int rate : { 48000, 96000 })
    {
        Score strums { "six-string-picked-strums", rate };
        strums.picking = acustra::PickingTechnique::Pick;
        for (int stroke = 0; stroke < 24; ++stroke)
            for (int pitch : { 40, 47, 52, 55, 59, 64 })
                strums.note(0.1 + 0.3 * stroke, 0.24, pitch,
                            82 + 7 * (stroke % 4));
        result.push_back(std::move(strums));

        Score fingers { "finger-pairs-and-legato", rate };
        for (int pair = 0; pair < 14; ++pair)
            for (int pitch : { pair % 2 ? 60 : 59, 64 })
                fingers.note(0.1 + 0.25 * pair, 0.18, pitch, 82);
        fingers.add(3.7, Kind::Control, 1, 65, 127);
        fingers.add(3.8, Kind::On, 1, 55, 100);
        int source = 55;
        for (int gesture = 0; gesture < 22; ++gesture)
        {
            const int target = gesture % 2 ? 55 : 57;
            fingers.connect(3.95 + 0.15 * gesture, source, target,
                            72 + (gesture % 3) * 12);
            source = target;
        }
        fingers.add(7.45, Kind::Off, 1, source, 64);
        result.push_back(std::move(fingers));

        Score slides { "three-wound-string-slides", rate };
        slides.strings = true;
        for (int channel = 1; channel <= 3; ++channel)
            slides.add(0.0, Kind::BendRange, channel, 12);
        constexpr std::array<int, 3> pitches { 45, 52, 55 };
        for (int gesture = 0; gesture < 6; ++gesture)
            for (int channel = 1; channel <= 3; ++channel)
            {
                const double at = 0.1 + 1.2 * gesture;
                slides.add(at - 0.01, Kind::Bend, channel);
                slides.note(at, 1.02, pitches[static_cast<std::size_t>(channel - 1)],
                            90, channel);
                slides.slide(at + 0.12, 0.35, channel, 0.0f, 5.0f);
                slides.slide(at + 0.55, 0.40, channel, 5.0f, -3.0f);
            }
        result.push_back(std::move(slides));

        Score heldShape { "held-gapped-chord-and-inner-notes", rate };
        heldShape.picking = acustra::PickingTechnique::Pick;
        for (double at : { 0.05, 4.05 })
        {
            for (int pitch : { 40, 57, 64 })
                heldShape.note(at, pitch == 57 ? 0.75 : 3.0, pitch, 100);
            for (double repeat : { 0.80, 1.30, 1.80 })
                heldShape.note(at + repeat, 0.45, 57, 100);
        }
        result.push_back(std::move(heldShape));
    }
    for (auto& score : result)
    {
        std::stable_sort(score.events.begin(), score.events.end(),
            [](const Event& a, const Event& b) { return a.frame < b.frame; });
        if (score.events.empty() || score.events.front().frame < 0
            || score.events.back().frame >= seconds * score.rate)
            throw std::runtime_error("score outside its eight-second window");
    }
    return result;
}

struct Timing
{
    int frame, eventCount;
    std::uint64_t wall, cpu;
};
struct Result
{
    Score score;
    std::vector<Timing> blocks;
    std::uint64_t checksum = 14695981039346656037ULL;
    double energy = 0, peak = 0;
    std::uint64_t finiteSamples = 0;
    std::uint32_t droppedEvents = 0;
    int connectedOwnershipChecks = 0, maximumVoices = 0;
};

void hashSample(std::uint64_t& hash, float value)
{
    std::uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    for (int shift = 0; shift < 32; shift += 8)
    {
        hash ^= (bits >> shift) & 255U;
        hash *= 1099511628211ULL;
    }
}

void run(Result& result)
{
    const auto& score = result.score;
    auto performer = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.outputGain = 1.0f;
    parameters.piezoMix = 0.2f;
    parameters.room = 0.1f;
    parameters.releaseNoise = 0.1f;
    parameters.picking = score.picking;
    performer->setParameters(parameters);
    performer->setTempoBpm(120.0);
    performer->setGatherChords(false);
    performer->setSustainHold(false);
    performer->prepare(score.rate, blockFrames);
    performer->engine().setStringPerChannelMode(score.strings);
    const int totalFrames = seconds * result.score.rate;
    result.blocks.reserve(static_cast<std::size_t>(totalFrames / blockFrames));
    std::array<float, blockFrames> left {}, right {};
    std::size_t cursor = 0;
    for (int frame = 0; frame < totalFrames; frame += blockFrames)
    {
        const auto firstEvent = cursor;
        while (cursor < result.score.events.size()
               && result.score.events[cursor].frame < frame + blockFrames)
            ++cursor;
        int sourceString = -1, sourceNote = -1, targetNote = -1;
        for (auto index = firstEvent; index < cursor; ++index)
            if (result.score.events[index].kind == Kind::Connected)
            {
                sourceNote = static_cast<int>(result.score.events[index].value);
                targetNote = result.score.events[index].first;
                sourceString = performer->engine().heldString(sourceNote, 1);
                if (sourceString < 0)
                    throw std::runtime_error("connected score lost its held source");
            }

        const auto wallStart = now(wallClock);
        const auto cpuStart = now(cpuClock);
        performer->beginBlock(left.data(), right.data(), blockFrames);
        for (auto index = firstEvent; index < cursor; ++index)
        {
            const auto& event = result.score.events[index];
            const int offset = event.frame - frame;
            switch (event.kind)
            {
                case Kind::On: case Kind::Connected:
                    performer->noteOn(offset, event.channel, event.first, event.second);
                    break;
                case Kind::Off:
                    performer->noteOff(offset, event.channel, event.first, event.second);
                    break;
                case Kind::Control:
                    performer->controlChange(offset, event.channel, event.first, event.second);
                    break;
                case Kind::Bend:
                    performer->pitchWheel(offset, event.channel, event.value);
                    break;
                case Kind::BendRange:
                    performer->setPitchBendRange(offset, event.channel, event.first);
                    break;
            }
        }
        performer->endBlock();
        const auto cpuEnd = now(cpuClock);
        const auto wallEnd = now(wallClock);
        result.blocks.push_back({ frame, static_cast<int>(cursor - firstEvent),
                                   wallEnd - wallStart, cpuEnd - cpuStart });

        // This checks public ownership only; a same-string replacement pluck
        // can also satisfy it. PerformerContactTests audit the actual gesture state.
        if (sourceString >= 0)
        {
            if (performer->engine().heldString(targetNote, 1) != sourceString
                || performer->engine().heldString(sourceNote, 1) >= 0)
                throw std::runtime_error("public CC65/84 did not transfer its original string");
            ++result.connectedOwnershipChecks;
        }
        result.maximumVoices = std::max(result.maximumVoices,
                                        performer->engine().getActiveVoiceCount());
        for (int index = 0; index < blockFrames; ++index)
            for (float value : { left[static_cast<std::size_t>(index)],
                                 right[static_cast<std::size_t>(index)] })
            {
                if (!std::isfinite(value))
                    throw std::runtime_error("nonfinite benchmark audio");
                hashSample(result.checksum, value);
                ++result.finiteSamples;
                result.energy += static_cast<double>(value) * value;
                result.peak = std::max(result.peak, std::abs(static_cast<double>(value)));
            }
    }
    result.droppedEvents = performer->droppedEventCount();
    const int requiredVoices = result.score.name == "six-string-picked-strums" ? 6
        : result.score.strings ? 3 : 2;
    if (cursor != result.score.events.size() || result.droppedEvents != 0
        || !(result.energy > 1.0e-12) || result.maximumVoices < 2
        || result.maximumVoices < requiredVoices
        || (result.score.name == "finger-pairs-and-legato" && result.connectedOwnershipChecks != 22))
        throw std::runtime_error("benchmark did not exercise its intended finite active score");
}

bool overdue(std::uint64_t nanoseconds, int rate)
{
    // Compare exact integer ratios; the callback deadline is not rounded.
    return nanoseconds * static_cast<std::uint64_t>(rate)
        > static_cast<std::uint64_t>(blockFrames) * billion;
}

void statistics(std::ostream& out, std::vector<std::uint64_t> values, int rate)
{
    if (values.empty()) { out << "null"; return; }
    std::sort(values.begin(), values.end());
    const auto quantile = [&](double fraction)
    {
        return values[static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1];
    };
    std::uint64_t total = 0;
    std::size_t missed = 0;
    for (auto value : values) { total += value; missed += overdue(value, rate) ? 1U : 0U; }
    out << "{\"p50_us\":" << quantile(0.50) / 1000.0
        << ",\"p95_us\":" << quantile(0.95) / 1000.0
        << ",\"p99_us\":" << quantile(0.99) / 1000.0
        << ",\"max_us\":" << values.back() / 1000.0
        << ",\"total_ns\":" << total << ",\"deadline_exceedances\":" << missed << '}';
}

void writeResults(const std::filesystem::path& output, const std::vector<Result>& results,
                  const std::vector<Timing>& overhead, bool complete)
{
    std::ofstream raw(output / "blocks.csv"), summary(output / "summary.json"),
                  timer(output / "timer-overhead.csv"), events(output / "events.csv");
    if (!raw || !summary || !timer || !events) throw std::runtime_error("cannot write results");
    raw << "workload,sample_rate,block_index,start_frame,frames,event_count,wall_ns,thread_cpu_ns,wall_exceeded_deadline,thread_cpu_exceeded_deadline\n";
    timer << "observation,wall_ns,thread_cpu_ns\n";
    events << "workload,sample_rate,frame,channel,kind,first,second,value\n";
    events << std::setprecision(9);
    for (std::size_t i = 0; i < overhead.size(); ++i)
        timer << i << ',' << overhead[i].wall << ',' << overhead[i].cpu << '\n';
    summary << std::setprecision(12)
        << "{\"schema\":1,\"block_frames\":64,\"duration_seconds\":8,\"warmup_seconds\":0,"
        << "\"wall_clock\":\"CLOCK_MONOTONIC\",\"cpu_clock\":\"CLOCK_THREAD_CPUTIME_ID\","
        << "\"wall_clock_resolution_ns\":" << resolution(wallClock)
        << ",\"thread_cpu_clock_resolution_ns\":" << resolution(cpuClock) << ','
        << "\"timing_scope\":\"beginBlock + public event dispatch + endBlock; setup, validation, checksum, allocation and file output excluded\","
        << "\"timer_overhead_subtracted\":false,\"quantiles\":\"nearest rank, ceil(p*N)-1\","
        << "\"deadline_test\":\"duration_ns * rate > 64 * 1000000000\","
        << "\"controls\":{\"output\":1,\"piezo_mix\":0.2,\"room\":0.1,\"release_noise\":0.1,\"tempo\":120},"
        << "\"behavior\":\"each revision's defaults; no ablation overrides\","
        << "\"complete\":" << (complete ? "true" : "false")
        << ",\"all_score_checks_passed\":" << (complete ? "true" : "false")
        << ",\"results\":[\n";
    for (std::size_t index = 0; index < results.size(); ++index)
    {
        const auto& result = results[index];
        for (const auto& event : result.score.events)
            events << result.score.name << ',' << result.score.rate << ',' << event.frame
                << ',' << event.channel << ',' << static_cast<int>(event.kind) << ','
                << event.first << ',' << event.second << ',' << event.value << '\n';
        std::vector<std::uint64_t> wall, cpu;
        for (std::size_t block = 0; block < result.blocks.size(); ++block)
        {
            const auto& time = result.blocks[block];
            wall.push_back(time.wall); cpu.push_back(time.cpu);
            raw << result.score.name << ',' << result.score.rate << ',' << block << ','
                << time.frame << ',' << blockFrames << ',' << time.eventCount << ','
                << time.wall << ',' << time.cpu << ',' << overdue(time.wall, result.score.rate)
                << ',' << overdue(time.cpu, result.score.rate) << '\n';
        }
        if (index != 0) summary << ",\n";
        summary << "{\"workload\":\"" << result.score.name << "\",\"sample_rate\":"
            << result.score.rate << ",\"blocks\":" << result.blocks.size()
            << ",\"events\":" << result.score.events.size()
            << ",\"deadline_us\":" << 1.0e6 * blockFrames / result.score.rate
            << ",\"string_per_channel\":" << (result.score.strings ? "true" : "false")
            << ",\"connected_ownership_checks\":" << result.connectedOwnershipChecks
            << ",\"finite_audio_samples\":" << result.finiteSamples
            << ",\"dropped_events\":" << result.droppedEvents
            << ",\"maximum_active_voices\":" << result.maximumVoices
            << ",\"audio_fnv1a64\":\"" << std::hex << result.checksum << std::dec
            << "\",\"audio_rms\":" << std::sqrt(result.energy
                / std::max(1.0, 2.0 * blockFrames * result.blocks.size()))
            << ",\"audio_peak_observed\":" << result.peak << ",\"wall\":";
        statistics(summary, std::move(wall), result.score.rate);
        summary << ",\"thread_cpu\":";
        statistics(summary, std::move(cpu), result.score.rate);
        summary << '}';
    }
    summary << "\n]}\n";
    if (!raw || !summary || !timer || !events) throw std::runtime_error("incomplete result write");
}
}

int main(int argc, char** argv)
{
    std::filesystem::path output;
    bool outputCreated = false;
    std::vector<Timing> overhead;
    std::vector<Result> results;
    try
    {
        if (argc != 2) throw std::runtime_error("usage: benchmark FRESH_OUTPUT_DIRECTORY");
        output = argv[1];
        if (std::filesystem::exists(output))
            throw std::runtime_error("preserve prior evidence: output directory exists");
        std::filesystem::create_directories(output);
        outputCreated = true;
        overhead.reserve(1024);
        for (int index = 0; index < 1024; ++index)
        {
            const auto wallStart = now(wallClock), cpuStart = now(cpuClock);
            const auto cpuEnd = now(cpuClock), wallEnd = now(wallClock);
            overhead.push_back({ index, 0, wallEnd - wallStart, cpuEnd - cpuStart });
        }
        for (auto& score : scores())
        {
            results.push_back({ std::move(score), {} });
            run(results.back());
            // Preserve each completed workload even if a later process crash
            // or external timeout prevents the final summary from being made.
            writeResults(output, results, overhead, false);
        }
        writeResults(output, results, overhead, true);
        std::cout << "Recorded eight eight-second workloads, 64-frame blocks; finite audio and public gesture assertions passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        if (outputCreated)
            try { writeResults(output, results, overhead, false); }
            catch (const std::exception& saveError) { std::cerr << saveError.what() << '\n'; }
        return 1;
    }
}
