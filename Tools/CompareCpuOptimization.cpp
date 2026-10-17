// Exact, untimed old/new native DSP comparison. Build through
// BuildCpuOptimizationComparison.sh so each adapter uses its own Source headers.
// The same frozen Tests/PerformanceBattery.h authors both event streams.
// Usage: AcustraCpuOptimizationComparison OUTPUT.json [--quick] [--flush-denormals]
//        [--rounding nearest|up|down|zero]
// Comparator checks: --negative-control or --negative-state-control must exit 1.
// --quick retains 8 original configurations plus the bounded realism witnesses;
// the default retains the 96 original configurations and adds rate/control edges.
// Exact executed counts and partitions are recorded. Inherited FTZ/DAZ is retained unless
// explicitly enabled; rounding defaults to nearest and is recorded in JSON.
// No clock reads or timing claims. Samples and serialized public state are
// compared directly, not merely by hash. Hashes are additional evidence only.
#include <algorithm>
#include <array>
#include <cmath>
#include <cfenv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

struct Configuration
{
    int scenario {}, rate {48000}, block {64}, technique {}, capture {}, shape {2}, wood {};
    bool gather {}, observers {true}, auxiliary {true}, sevenRealism {true};
    float room {}, age {0.15f}, touch {0.58f}, releaseNoise {0.5f};
};
struct Rendered
{
    std::vector<float> left, right, piezo;
    std::vector<std::uint64_t> states;
    std::uint32_t dropped {};
    int events {}, controls {}, checkpoints {};
    unsigned mechanismWitnesses {};
};
enum { batteryScenarioCount = 17 };
constexpr int legacyScenarioCount = 21;
enum { connectedScenario = 21, fingerPairScenario = 22, woundScenario = 23,
       heldMuteScenario = 24, roomTailScenario = 25 };
constexpr unsigned hammerWitness = 1u, pullWitness = 2u, pairWitness = 4u;
constexpr unsigned ridgeWitness = 8u, heldMuteWitness = 16u, roomWitness = 32u;
constexpr unsigned directionalWitness = 64u, conversionWitness = 128u, silenceWitness = 256u;
constexpr std::array<const char*, 26> scenarioNames {
    "single-notes", "same-sample-chords", "rolled-chords", "strums", "release-velocities",
    "sustain", "bridge-hand", "vibrato", "pitch-bend-rpn", "mpe-lower-zone", "string-per-channel",
    "gather-boundaries", "gather-switch", "notes-off-and-panic", "host-edge-cases", "controls", "construction",
    "repeated-string-tails-and-tempo", "dense-queued-strums", "coefficient-key-edges", "reprepare-observer-and-output-lifecycle",
    "connected-work-and-reattack", "finger-pair-timing", "wound-slide-reversal",
    "held-shape-owner-lifecycle", "room-tail-silence-and-restart"};
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)

#if defined(ACUSTRA_PARITY_ADAPTER)
#include "DSP/AcustraPerformer.h"
#include "PerformanceBattery.h"

namespace acustra
{
// Read-only activation witnesses, not comparisons of private padding or caches.
// Exact sound/public-state equality remains the admission gate. Observe these
// at fixed sample checkpoints so short contacts cannot hide between blocks.
struct AcustraEngineTestAccess
{
    static unsigned mechanisms(const AcustraEngine& e, int frame)
    {
        unsigned mask = e.room_.active ? roomWitness : 0u;
        if (e.room_.active && e.room_.directional) mask |= directionalWitness;
        if (e.room_.active && e.room_.rateConversion && e.room_.decimation > 1)
            mask |= conversionWitness;
        if (e.fingerPairRandomState_ != 0x9a735c21u) mask |= pairWitness;
        for (const auto& v : e.voices_)
        {
            if (v.legatoContactSamples > 0)
            {
                if (e.performanceRealism_.hammerWork && v.legatoContactAmplitude < 0.0f)
                    mask |= hammerWitness;
                if (e.performanceRealism_.pullOffPluck && v.legatoContactAmplitude > 0.0f
                    && v.legatoNormalGain == 0.5f && v.legatoParallelGain > 0.86f)
                    mask |= pullWitness;
            }
            if (v.fretMotion.ridgeForce != 0.0f) mask |= ridgeWitness;
            // The original transient contact ends before this point. Require
            // an actual surviving cohort, not merely its initial dead stroke.
            if (frame >= static_cast<int>(0.7 * e.sampleRate())
                && v.mutedStroke && v.heldShapeOwners != 0u && v.returnSamples == 1)
                mask |= heldMuteWitness;
        }
        return mask;
    }
};
}

namespace
{
using namespace acustra;
using namespace acustra::battery;

std::vector<Scenario> scenarios()
{
    auto result = makeBattery();
    if (result.size() != batteryScenarioCount)
        throw std::runtime_error("PerformanceBattery scenario count changed; audit the parity catalogue");
    for (std::size_t i = 0; i < result.size(); ++i)
        if (std::strcmp(result[i].name, scenarioNames[i]) != 0)
            throw std::runtime_error("PerformanceBattery scenario order/name changed");
    using detail::Builder;
    using Kind = Control::Kind;
    {
        Builder b("repeated-string-tails-and-tempo", 1.6);
        b.cc(0.0, 1, 126, 6);
        for (int i = 0; i < 28; ++i)
        {
            const double t = i * 0.041;
            b.on(t, 1, 43, 32 + (i * 17) % 96).off(t + 0.025, 1, 43, 1 + (i * 29) % 127);
            if (i % 5 == 0) b.on(t + 0.006, 6, 67, 110).off(t + 0.031, 6, 67, 127);
        }
        b.control(0.455, Kind::Tuning, 2).control(0.732, Kind::Age, 95)
            .control(0.936, Kind::Age, 0);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("dense-queued-strums", 0.7);
        constexpr std::array<int, 6> chord {40,47,52,56,59,64};
        for (int stroke = 0; stroke < 12; ++stroke)
        {
            const double t = 0.008 * stroke;
            b.chord(t, 1, chord, 127 - stroke * 3);
            b.release(0.25 + t, 1, chord);
        }
        b.cc(0.05, 1, 2, 90).bend(0.055, 1, 10000).cc(0.08, 1, 2, 0);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("coefficient-key-edges", 0.9);
        b.chord(0.0, 1, std::array<int, 6> {40,47,52,56,59,64}, 101);
        for (int i = 0; i < 96; ++i)
        {
            const double t = 0.15 + i * 0.004;
            const float age = i % 3 == 0 ? 50.0f : std::nextafter(50.0f, i % 3 == 1 ? 0.0f : 100.0f);
            b.control(t, Kind::Age, age);
            b.cc(t, 1, 2, i % 8 == 0 ? 0 : 80).bend(t, 1, 8192 + i % 3);
        }
        b.control(0.6, Kind::Shape, 3).control(0.6, Kind::Wood, 2)
            .control(0.63, Kind::Tuning, 3).control(0.661, Kind::Tuning, 0);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("reprepare-observer-and-output-lifecycle", 1.0);
        b.chord(0.0, 1, std::array<int, 6> {40,47,52,56,59,64}, 120);
        b.off(0.11, 1, 40, 127).on(0.117, 1, 40, 45).cc(0.19, 1, 64, 127)
            .off(0.23, 1, 52).cc(0.28, 1, 64, 0).on(0.41, 1, 47, 110)
            .control(0.58, Kind::Panic).on(0.60, 1, 52, 90)
            .control(0.8, Kind::CaptureMode, 2).on(0.82, 1, 55, 127);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("connected-work-and-reattack", 7.8);
        b.cc(0.0, 1, 126, 6).cc(0.0, 3, 65, 127).on(0.01, 3, 55, 100);
        // Ringing hammer, pull to open D, then a late held-source hammer.
        b.cc(0.35, 3, 84, 55).on(0.35, 3, 57, 88).off(0.36, 3, 55);
        b.cc(0.7, 3, 84, 57).on(0.7, 3, 50, 96).off(0.71, 3, 57);
        b.cc(6.0, 3, 84, 50).on(6.0, 3, 55, 104).off(6.01, 3, 50);
        // A same-note pick while the finite finger pulse is still in flight.
        b.on(6.001, 3, 55, 72).off(6.3, 3, 55).off(6.31, 3, 55);
        b.on(6.6, 3, 55, 100).cc(6.8, 3, 84, 55).on(6.8, 3, 57, 91);
        b.off(6.81, 3, 55).cc(7.0, 3, 84, 57).on(7.0, 3, 55, 83);
        b.off(7.01, 3, 57).off(7.2, 3, 55);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("finger-pair-timing", 1.8);
        b.chord(0.01, 1, std::array<int, 2>{59, 64}, 94)
            .release(0.25, 1, std::array<int, 2>{59, 64});
        b.on(0.4, 1, 59, 78).on(0.41, 1, 64, 97)
            .off(0.64, 1, 59).off(0.64, 1, 64);
        b.chord(0.8, 1, std::array<int, 2>{55, 64}, 110)
            .release(1.08, 1, std::array<int, 2>{55, 64});
        b.chord(1.2, 1, std::array<int, 3>{55, 59, 64}, 90)
            .release(1.45, 1, std::array<int, 3>{55, 59, 64});
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("wound-slide-reversal", 2.2);
        b.cc(0.0, 1, 126, 6);
        for (int channel : {1, 2, 3})
        {
            b.rpn(0.0, channel, 0, 12).on(0.01, channel, 40 + channel * 5, 96);
            for (int step = 0; step <= 120; ++step)
            {
                const double u = step / 120.0;
                const double semitones = u < 0.5 ? 8.0 * u : 8.0 - 8.0 * u;
                const int raw = 8192 + static_cast<int>(semitones * (8191.0 / 12.0));
                b.bend(0.25 + 1.2 * u, channel, raw);
            }
            b.off(1.6, channel, 40 + channel * 5);
        }
        // Conventional vibrato at fixed string length is a negative input to
        // the motion source, while the earlier SPC bends moved the fret.
        b.cc(1.75, 1, 127, 0).on(1.76, 1, 52, 90)
            .cc(1.8, 1, 1, 90).bend(1.85, 1, 10000).off(2.0, 1, 52);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("held-shape-owner-lifecycle", 3.1);
        b.chord(0.01, 1, std::array<int, 3>{40, 57, 64}, 100);
        b.off(0.4, 1, 57).on(0.65, 1, 57, 100).off(0.8, 1, 57)
            .on(0.9, 1, 40, 83).off(1.0, 1, 40);
        b.cc(1.1, 1, 64, 127).off(1.2, 1, 40).off(1.2, 1, 64)
            .cc(1.4, 1, 64, 0);
        b.chord(1.6, 1, std::array<int, 3>{40, 57, 64}, 105)
            .control(1.8, Kind::Sustain, 1)
            .release(2.0, 1, std::array<int, 3>{40, 57, 64})
            .control(2.2, Kind::Sustain, 0).control(2.4, Kind::Panic)
            .on(2.5, 1, 52, 90).off(2.7, 1, 52);
        result.push_back(std::move(b.scenario));
    }
    {
        Builder b("room-tail-silence-and-restart", 13.2);
        b.chord(0.05, 1, std::array<int, 3>{40, 57, 64}, 105)
            .release(0.3, 1, std::array<int, 3>{40, 57, 64});
        b.control(10.9, Kind::Panic).on(11.5, 1, 59, 90).off(11.75, 1, 59)
            .control(12.0, Kind::Panic).on(12.05, 1, 55, 100)
            .control(12.3, Kind::CaptureMode, 2).control(12.5, Kind::CaptureMode, 0)
            .control(12.6, Kind::Output, -12).off(12.8, 1, 55);
        result.push_back(std::move(b.scenario));
    }
    if (result.size() != scenarioNames.size())
        throw std::runtime_error("parity scenario catalogue size mismatch");
    for (std::size_t i = 0; i < result.size(); ++i)
        if (std::strcmp(result[i].name, scenarioNames[i]) != 0)
            throw std::runtime_error("parity scenario catalogue order/name mismatch");
    for (auto& scenario : result)
    {
        std::stable_sort(scenario.events.begin(), scenario.events.end(),
            [](const Event& a, const Event& b) { return a.seconds < b.seconds; });
        std::stable_sort(scenario.controls.begin(), scenario.controls.end(),
            [](const Control& a, const Control& b) { return a.seconds < b.seconds; });
    }
    return result;
}

void apply(Performer& performer, EngineParameters& p, const Control& c)
{
    using Kind = Control::Kind;
    const auto index = static_cast<int>(c.value);
    switch (c.kind)
    {
        case Kind::GatherChords: performer.setGatherChords(c.value >= 0.5f); break;
        case Kind::Sustain: performer.setSustainHold(c.value >= 0.5f); break;
        case Kind::Panic: performer.reset(); break;
        case Kind::CaptureMode: p.capture = index == 0 ? CaptureType::StereoMic : index == 1 ? CaptureType::MonoMic : CaptureType::Piezo; break;
        case Kind::Picking: p.picking = static_cast<PickingTechnique>(index); break;
        case Kind::Tuning: p.tuning = static_cast<Tuning>(index); break;
        case Kind::BodyAmount: p.bodyAmount = 0.01f * c.value; break;
        case Kind::Output: p.outputGain = std::pow(10.0f, 0.05f * c.value); break;
        case Kind::Shape: p.shape = static_cast<BodyShape>(index); break;
        case Kind::Wood: p.bodyMaterial = static_cast<BodyMaterial>(index); break;
        case Kind::Width: p.stereoWidth = 0.01f * c.value; break;
        case Kind::Age: p.stringAge = 0.01f * c.value; break;
        case Kind::Pluck: p.pluckPosition = 0.01f * c.value; break;
        case Kind::Touch: p.touch = 0.01f * c.value; break;
        case Kind::PiezoMix: p.piezoMix = 0.01f * c.value; break;
    }
    performer.setParameters(p);
}

std::uint64_t floatBits(float x)
{
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    if (!std::isfinite(x)) throw std::runtime_error("nonfinite public observer");
    return bits;
}
void observe(const Performer& performer, Rendered& output, int frame)
{
    auto& state = output.states;
    const auto integer = [&](std::int64_t x) { state.push_back(static_cast<std::uint64_t>(x)); };
    const auto scalar = [&](float x) { state.push_back(floatBits(x)); };
    const auto& engine = performer.engine();
    integer(frame); integer(performer.latencySamples()); integer(performer.isGatheringChords());
    integer(static_cast<int>(performer.strokeMode()));
    integer(performer.isKeyswitchSustainEnabled()); integer(performer.isHoldingSustain());
    integer(performer.isSustainActive());
    integer(performer.droppedEventCount()); integer(engine.getActiveVoiceCount());
    integer(engine.getSympatheticStringCount()); integer(engine.isStringPerChannelMode());
    std::uint64_t rateBits;
    const auto rate = engine.sampleRate();
    std::memcpy(&rateBits, &rate, sizeof(rateBits)); state.push_back(rateBits);
    for (const auto& s : engine.getStringActivity())
    {
        integer(s.openMidi); integer(s.midiNote); integer(s.fret); integer(s.harmonic);
        integer(s.keyDown); integer(s.played); integer(s.pedalHeld); scalar(s.level);
    }
    scalar(engine.getLastBridgeVelocity()); scalar(engine.getLastBridgeReactionForce());
    scalar(engine.getLastBridgeBodyForce());
    scalar(engine.getLastPiezoVoltage());
    scalar(engine.getLastBridgePower()); scalar(engine.getLastBridgeBodyPower());
    const auto probe = engine.getLastPiezoProbe();
    scalar(probe.openCircuit); scalar(probe.jack); scalar(probe.bufferInput); scalar(probe.gainStageDrive);
    constexpr std::array<int, 18> notes {20,28,38,40,43,45,47,50,52,55,57,59,62,64,67,71,76,84};
    for (int channel = 1; channel <= 16; ++channel)
    {
        integer(engine.sustainHolds(channel));
        for (const int note : notes) integer(engine.heldString(note, channel));
    }
    // Allocation queries are read-only; include their externally visible result.
    for (const int channel : {1, 6, 16})
        for (const int note : notes)
        {
            integer(engine.plannedString(note, channel));
            integer(engine.canSound(note, channel));
        }
    output.mechanismWitnesses |= AcustraEngineTestAccess::mechanisms(engine, frame);
    if (frame > static_cast<int>(11.0 * engine.sampleRate())
        && frame < static_cast<int>(11.5 * engine.sampleRate())
        && engine.getActiveVoiceCount() == 0
        && (AcustraEngineTestAccess::mechanisms(engine, frame) & roomWitness) == 0u
        && output.left[static_cast<std::size_t>(frame - 1)] == 0.0f
        && output.right[static_cast<std::size_t>(frame - 1)] == 0.0f
        && (output.piezo.empty() || output.piezo[static_cast<std::size_t>(frame - 1)] == 0.0f))
        output.mechanismWitnesses |= silenceWitness;
    ++output.checkpoints;
}
}

Rendered JOIN(parity_, ACUSTRA_PARITY_ADAPTER)(const Configuration& c)
{
    const auto battery = scenarios();
    const auto& scenario = battery.at(static_cast<std::size_t>(c.scenario));
    auto player = std::make_unique<Performer>();
    PerformanceRealism realism;
    realism.hammerWork = realism.pullOffPluck = realism.fingerPairs = realism.woundRidges
        = realism.directionalRoom = realism.roomRateConversion = realism.heldShapeMute = c.sevenRealism;
    realism.longitudinalSqueak = false;
    player->engine().setPerformanceRealism(realism);
    EngineParameters p;
    p.shape = static_cast<BodyShape>(c.shape);
    p.bodyMaterial = static_cast<BodyMaterial>(c.wood);
    p.picking = static_cast<PickingTechnique>(c.technique);
    p.capture = c.capture == 0 ? CaptureType::StereoMic : c.capture == 1 ? CaptureType::MonoMic : CaptureType::Piezo;
    p.room = c.room; p.stringAge = c.age; p.touch = c.touch; p.releaseNoise = c.releaseNoise;
    player->setParameters(p);
    player->prepare(c.rate, 511);
    player->setGatherChords(c.gather);
    player->engine().setPortObserversEnabled(c.observers);
    const int length = sampleAt(scenario.seconds, c.rate);
    Rendered output;
    output.left.resize(static_cast<std::size_t>(length));
    output.right.resize(static_cast<std::size_t>(length));
    if (c.auxiliary) output.piezo.resize(static_cast<std::size_t>(length));
    std::size_t event = 0, control = 0, blockIndex = 0;
    std::vector<int> checkpoints;
    for (int sample = 4096; sample < length; sample += 4096) checkpoints.push_back(sample);
    checkpoints.push_back(length);
    if (c.scenario >= legacyScenarioCount)
    {
        // Observe short 2–3 ms finger contacts and queued Finger onsets before
        // they disappear. These checkpoints do not depend on callback size.
        const auto add = [&](double seconds) {
            for (const int offset : {1, std::max(1, sampleAt(0.001, c.rate))})
            {
                const int sample = sampleAt(seconds, c.rate) + offset;
                if (sample > 0 && sample < length) checkpoints.push_back(sample);
            }
        };
        for (const auto& e : scenario.events) add(e.seconds);
        for (const auto& controlEvent : scenario.controls) add(controlEvent.seconds);
        std::sort(checkpoints.begin(), checkpoints.end());
        checkpoints.erase(std::unique(checkpoints.begin(), checkpoints.end()), checkpoints.end());
    }
    std::size_t checkpoint = 0;
    constexpr std::array<int, 8> irregular {1,17,31,32,33,64,127,511};
    observe(*player, output, 0);
    // All controls occur at the authored sample, independent of partition.
    // Periodic public-state checkpoints also occur at fixed absolute samples.
    for (int start = 0; start < length;)
    {
        while (control < scenario.controls.size() && sampleAt(scenario.controls[control].seconds, c.rate) <= start)
        { apply(*player, p, scenario.controls[control++]); ++output.controls; }
        int count = std::min(c.block == 0 ? irregular[blockIndex++ % irregular.size()] : c.block, length - start);
        count = std::min(count, checkpoints[checkpoint] - start);
        if (control < scenario.controls.size()) count = std::min(count, sampleAt(scenario.controls[control].seconds, c.rate) - start);
        // Lifecycle actions are explicit absolute-sample block boundaries.
        for (const double seconds : {0.375, 0.5, 0.75})
            if (c.scenario == 20 && sampleAt(seconds, c.rate) > start)
                count = std::min(count, sampleAt(seconds, c.rate) - start);
        if (c.scenario == 19)
            for (const auto& tune : {std::pair<double,float>{0.15, -32.0f}, {0.346, 0.001f}, {0.53, 0.0f}})
            {
                const int sample = sampleAt(tune.first, c.rate);
                if (sample == start) player->setMasterTuneCents(tune.second);
                else if (sample > start) count = std::min(count, sample - start);
            }
        if (c.scenario == 20)
        {
            if (start == sampleAt(0.375, c.rate)) { player->prepare(c.rate, 511); player->setParameters(p); }
            if (start == sampleAt(0.5, c.rate)) player->engine().setPortObserversEnabled(!c.observers);
            if (start == sampleAt(0.75, c.rate)) player->engine().setPortObserversEnabled(c.observers);
        }
        auto* piezo = c.auxiliary ? output.piezo.data() + start : nullptr;
        player->beginBlock(output.left.data() + start, output.right.data() + start,
            AcustraEngine::OutputBuses {piezo}, count);
        // Exercise immediate host-tempo updates inside blocks, independent of MIDI Gather.
        struct Timed { int sample; const Event* midi; double tempo; };
        std::vector<Timed> due;
        while (event < scenario.events.size() && sampleAt(scenario.events[event].seconds, c.rate) < start + count)
        {
            const auto& e = scenario.events[event++];
            due.push_back({std::clamp(sampleAt(e.seconds, c.rate) - start + e.skew, 0, count), &e, 0.0});
            ++output.events;
        }
        if (c.scenario == 17)
            for (const auto& t : {std::pair<double,double>{0.15,67.0}, {0.403,240.0}, {0.711,121.5}, {1.12,30.0}})
            {
                const int sample = sampleAt(t.first, c.rate);
                if (sample >= start && sample < start + count) due.push_back({sample - start, nullptr, t.second});
            }
        std::stable_sort(due.begin(), due.end(), [](const Timed& a, const Timed& b) {return a.sample < b.sample;});
        for (const auto& e : due)
            if (e.midi) player->handleMidi(e.sample, e.midi->bytes.data(), e.midi->size);
            else player->setTempoBpmAt(e.sample, e.tempo);
        player->endBlock();
        start += count;
        if (start == checkpoints[checkpoint])
        {
            observe(*player, output, start);
            ++checkpoint;
        }
    }
    output.dropped = player->droppedEventCount();
    return output;
}
#else
Rendered parity_baseline(const Configuration&);
Rendered parity_current(const Configuration&);
namespace
{
std::uint64_t hash(const void* raw, std::size_t bytes)
{
    auto value = UINT64_C(14695981039346656037);
    const auto* data = static_cast<const unsigned char*>(raw);
    for (std::size_t i = 0; i < bytes; ++i) {value ^= data[i]; value *= UINT64_C(1099511628211);}
    return value;
}
template <typename T> std::uint64_t hash(const std::vector<T>& data)
{ return hash(data.data(), data.size() * sizeof(T)); }
void same(const Rendered& a, const Rendered& b, const std::string& label)
{
    for (const auto& pair : {std::pair<const std::vector<float>*,const std::vector<float>*>{&a.left,&b.left}, {&a.right,&b.right}, {&a.piezo,&b.piezo}})
    {
        if (pair.first->size() != pair.second->size()) throw std::runtime_error(label + ": frame count differs");
        for (std::size_t i = 0; i < pair.first->size(); ++i)
            if (std::memcmp(&(*pair.first)[i], &(*pair.second)[i], sizeof(float)) != 0)
                throw std::runtime_error(label + ": sample bits differ at frame " + std::to_string(i));
    }
    if (a.states != b.states) {
        const auto mismatch = std::mismatch(a.states.begin(), a.states.end(), b.states.begin(), b.states.end());
        throw std::runtime_error(label + ": public state differs at word " + std::to_string(mismatch.first - a.states.begin()));
    }
    if (a.events != b.events || a.controls != b.controls || a.dropped != b.dropped
        || a.checkpoints != b.checkpoints || a.mechanismWitnesses != b.mechanismWitnesses)
        throw std::runtime_error(label + ": accounting differs");
}
void valid(const Rendered& output, const Configuration& c)
{
    double energy = 0;
    for (const auto* bus : {&output.left, &output.right, &output.piezo})
        for (const float x : *bus) {if (!std::isfinite(x)) throw std::runtime_error("nonfinite audio"); energy += double(x) * x;}
    if (!(energy > 1.0e-15)) throw std::runtime_error("silent performance");
    if (output.dropped != 0 && c.scenario != 14) throw std::runtime_error("unexpected dropped event");
    if (output.events == 0 || output.checkpoints < 2) throw std::runtime_error("empty performance");
    unsigned required = 0u;
    if (c.sevenRealism)
    {
        if (c.scenario == connectedScenario) required |= hammerWitness | pullWitness;
        if (c.scenario == fingerPairScenario && c.technique == 0) required |= pairWitness;
        if (c.scenario == woundScenario && c.releaseNoise > 0.0f) required |= ridgeWitness;
        if (c.scenario == heldMuteScenario && c.technique == 1) required |= heldMuteWitness;
    }
    if (c.scenario == roomTailScenario)
    {
        required |= silenceWitness;
        if (c.room > 0.0f && c.capture != 2)
        {
            required |= roomWitness;
            if (c.sevenRealism) required |= directionalWitness;
            if (c.sevenRealism && c.rate > 64000) required |= conversionWitness;
        }
    }
    if ((output.mechanismWitnesses & required) != required)
        throw std::runtime_error(std::string(scenarioNames[static_cast<std::size_t>(c.scenario)])
            + ": expected mechanism did not activate (required=" + std::to_string(required)
            + ", observed=" + std::to_string(output.mechanismWitnesses) + ")");
    if (c.scenario == woundScenario && c.releaseNoise == 0.0f
        && (output.mechanismWitnesses & ridgeWitness) != 0u)
        throw std::runtime_error("ReleaseNoise=0 activated the winding source");
}
}
int main(int argc, char** argv)
{
    if (argc < 2) {std::cerr << "usage: comparison OUTPUT.json [--quick] [--flush-denormals]\n"
        "       [--rounding nearest|up|down|zero] [--negative-control | --negative-state-control]\n"; return 2;}
    bool quick = false, negative = false, negativeState = false, flushDenormals = false;
    int rounding = FE_TONEAREST;
    std::string roundingName = "nearest";
    for (int i = 2; i < argc; ++i)
        if (std::string(argv[i]) == "--quick") quick = true;
        else if (std::string(argv[i]) == "--negative-control") negative = true;
        else if (std::string(argv[i]) == "--negative-state-control") negativeState = true;
        else if (std::string(argv[i]) == "--flush-denormals") flushDenormals = true;
        else if (std::string(argv[i]) == "--rounding" && i+1 < argc)
        {
            roundingName = argv[++i];
            if (roundingName == "nearest") rounding = FE_TONEAREST;
            else if (roundingName == "up") rounding = FE_UPWARD;
            else if (roundingName == "down") rounding = FE_DOWNWARD;
            else if (roundingName == "zero") rounding = FE_TOWARDZERO;
            else {std::cerr << "unknown rounding mode\n"; return 2;}
        }
        else {std::cerr << "unknown argument\n"; return 2;}
    if (std::fesetround(rounding) != 0) {std::cerr << "cannot set rounding mode\n"; return 2;}
    unsigned mxcsr = 0;
#if defined(__SSE2__)
    if (flushDenormals) _mm_setcsr(_mm_getcsr() | 0x8040u);
    mxcsr = _mm_getcsr();
#else
    if (flushDenormals) {std::cerr << "FTZ/DAZ mode requires SSE2\n"; return 2;}
#endif
    if (std::filesystem::exists(argv[1]))
    {
        std::cerr << "Preserve existing evidence: output already exists\n";
        return 2;
    }
    std::ofstream report(argv[1]);
    if (!report) return 2;
    report << "{\n  \"protocol\": \"exact samples and serialized public state; untimed\",\n  \"rounding\":\"" << roundingName << "\",\n  \"flush_denormals_requested\":" << flushDenormals << ",\n  \"mxcsr_initial\":" << mxcsr << ",\n  \"cases\": [\n";
    std::uint64_t frames = 0, values = 0, stateWords = 0;
    int count = 0, partitions = 0;
    try
    {
        std::vector<Configuration> configurations;
        if (quick)
            for (const int s : {0, 6, 9, 16, 17, 18, 19, 20})
            {Configuration c; c.scenario=s; c.gather=s%3==0; c.block=0; c.technique=s%3; c.capture=(s+1)%3; configurations.push_back(c);}
        else
        {
            for (int s = 0; s < legacyScenarioCount; ++s)
                for (int gather = 0; gather < 2; ++gather)
                {
                    Configuration c; c.scenario=s; c.gather=gather!=0;
                    c.block=0; c.technique=(s+gather)%3; c.capture=(s+gather)%3;
                    c.room=gather ? 0.5f : 0; c.observers=s%2==0; c.auxiliary=(s+gather)%3!=0;
                    configurations.push_back(c);
                }
            for (const int rate : {8000,44100,48000,96000,192000,384000})
                for (const int s : {0,6,8,9,10,17,18,19,20})
                {
                    Configuration c; c.scenario=s; c.rate=rate;
                    c.block=rate<48000 ? 31 : rate==48000 ? 32 : rate==96000 ? 33 : 127;
                    c.gather=s%2==0; c.technique=s%3; c.capture=s%3;
                    c.age=0.0f; c.touch=(s%3)*0.5f; c.room=s%2==0 ? 1.0f : 0.0f;
                    configurations.push_back(c);
                }
        }
        // Five authored mechanisms at shipped Room/ReleaseNoise, plus a
        // matching all-seven-off set. These are targeted additions, not a
        // Cartesian product of every feature, control, capture and rate.
        const auto target = [](int scenario, bool enabled = true) {
            Configuration c;
            c.scenario = scenario; c.block = 0; c.room = 0.1f; c.releaseNoise = 0.1f;
            c.sevenRealism = enabled;
            c.technique = scenario == heldMuteScenario || scenario == roomTailScenario ? 1 : 0;
            return c;
        };
        for (const bool enabled : {true, false})
            for (int scenario = connectedScenario; scenario <= roomTailScenario; ++scenario)
                configurations.push_back(target(scenario, enabled));
        {
            auto c = target(connectedScenario); c.rate = 96000; configurations.push_back(c);
            c = target(fingerPairScenario); c.gather = true; configurations.push_back(c);
            c = target(woundScenario); c.releaseNoise = 0.0f; configurations.push_back(c);
            c = target(roomTailScenario); c.rate = 96000; c.room = 1.0f; configurations.push_back(c);
            c = target(roomTailScenario); c.room = 0.0f; configurations.push_back(c);
        }
        if (!quick)
        {
            auto c = target(woundScenario); c.releaseNoise = 0.5f; configurations.push_back(c);
            for (const int rate : {8000, 96000})
            {c = target(woundScenario); c.rate = rate; configurations.push_back(c);}
            // Room's decimation changes at 64 kHz. Include both sides and the
            // high-rate limits, with full wet gain and a long quiet interval.
            for (const int rate : {64000, 64001, 192000, 384000})
            {c = target(roomTailScenario); c.rate = rate; c.room = 1.0f; configurations.push_back(c);}
            c = target(heldMuteScenario); c.rate = 96000; configurations.push_back(c);
            for (const int technique : {1, 2})
            {c = target(fingerPairScenario); c.technique = technique; configurations.push_back(c);}
            for (const int capture : {1, 2})
            {c = target(roomTailScenario); c.rate = 96000; c.capture = capture; configurations.push_back(c);}
        }
        for (auto c : configurations)
        {
            if (c.scenario < legacyScenarioCount)
            {
                c.shape = c.scenario % 4;
                c.wood = (c.scenario + static_cast<int>(c.gather)) % 3;
            }
            const auto a = parity_baseline(c);
            auto b = parity_current(c);
            if (negative && count==0) b.left.at(b.left.size()/2) = std::nextafter(b.left.at(b.left.size()/2), INFINITY);
            if (negativeState && count==0) b.states.at(8) ^= UINT64_C(1);
            const auto label = std::string(scenarioNames[static_cast<std::size_t>(c.scenario)]) + " rate=" + std::to_string(c.rate);
            valid(a,c); valid(b,c); same(a,b,label);
            // Every base matrix's valid MIDI performance also compares fixed
            // one-sample or 511-frame partition against its irregular rendering.
            const bool partition = c.block==0 && c.scenario!=14;
            if (partition)
            {
                auto p = c; p.block = quick || c.gather || c.scenario >= legacyScenarioCount ? 511 : 1;
                const auto alternateA = parity_baseline(p), alternateB = parity_current(p);
                same(alternateA,alternateB,label+" alternate partition old/new");
                same(a,alternateA,label+" baseline partition");
                same(b,alternateB,label+" candidate partition");
                ++partitions;
                frames += alternateA.left.size(); values += alternateA.left.size()*2+alternateA.piezo.size(); stateWords += alternateA.states.size();
            }
            if (count++) report << ",\n";
            report << "    {\"scenario\":\"" << scenarioNames[static_cast<std::size_t>(c.scenario)] << "\",\"rate\":" << c.rate
                   << ",\"block\":" << c.block << ",\"technique\":" << c.technique
                   << ",\"capture\":" << c.capture << ",\"shape\":" << c.shape << ",\"wood\":" << c.wood << ",\"gather\":" << c.gather << ",\"room\":" << c.room
                   << ",\"age\":" << c.age << ",\"touch\":" << c.touch << ",\"observers\":" << c.observers
                   << ",\"release_noise\":" << c.releaseNoise << ",\"seven_realism\":" << c.sevenRealism
                   << ",\"longitudinal_squeak\":false,\"mechanism_witnesses\":" << a.mechanismWitnesses
                   << ",\"auxiliary\":" << c.auxiliary << ",\"frames\":" << a.left.size() << ",\"events\":" << a.events
                   << ",\"controls\":" << a.controls << ",\"dropped\":" << a.dropped << ",\"state_words\":" << a.states.size()
                   << ",\"partition_compared\":" << partition << ",\"fnv1a64_main_left\":\"" << std::hex << hash(a.left)
                   << "\",\"fnv1a64_main_right\":\"" << hash(a.right) << "\",\"fnv1a64_piezo\":\"" << hash(a.piezo)
                   << "\",\"fnv1a64_state\":\"" << hash(a.states) << std::dec << "\"}";
            report.flush();
            frames += a.left.size(); values += a.left.size()*2+a.piezo.size(); stateWords += a.states.size();
            std::cout << "PASS " << count << '/' << configurations.size() << ' ' << label << std::endl;
        }
        report << "\n  ],\n  \"passed\":true,\n  \"configurations\":" << count << ",\n  \"additional_partition_configurations\":" << partitions
               << ",\n  \"frames_compared\":" << frames << ",\n  \"sample_values_compared\":" << values << ",\n  \"public_state_words_compared\":" << stateWords << "\n}\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        report << "\n  ],\n  \"passed\":false,\n  \"error\":\"" << e.what() << "\"\n}\n";
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
#endif
