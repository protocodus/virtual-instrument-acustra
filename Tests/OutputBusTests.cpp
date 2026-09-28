// The separate outputs (AcustraEngine::OutputBuses): the Mic pair and the
// Piezo line render alongside Main in one pass. Wanting them must not change
// Main by a bit; the Mic pair must be Main whenever Capture is on Stereo mic
// and the Piezo line Main's mono whenever Capture is on Piezo; neither may
// depend on what Capture selects; and not wanting them must cost nothing.
// The whole performance battery is played through the player to check it.
#include "DSP/AcustraPerformer.h"
#include "PerformanceBattery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
using acustra::AcustraEngine;
using acustra::CaptureType;
using acustra::Performer;
using acustra::StringMaterial;
using namespace acustra::battery;

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (! condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Buses
{
    std::vector<float> left, right, micLeft, micRight, piezo;
};

bool bitwiseEqual(const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size()
        && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// Float equality, which treats +0 and -0 as one value; reports how many
// samples differ in their bits only by the sign of a zero.
bool valueEqual(const std::vector<float>& a, const std::vector<float>& b,
                long* signedZeros = nullptr)
{
    if (a.size() != b.size())
        return false;
    long zeros = 0;
    for (std::size_t index = 0; index < a.size(); ++index)
    {
        if (a[index] != b[index])
            return false;
        if (std::signbit(a[index]) != std::signbit(b[index]))
            ++zeros;
    }
    if (signedZeros != nullptr)
        *signedZeros += zeros;
    return true;
}

// What a scenario's front-end controls do to the parameters. A forced
// capture or material ignores the battery's own switches of it.
struct Force
{
    std::optional<CaptureType> capture;
    std::optional<StringMaterial> material;
};

void applyControl(acustra::EngineParameters& parameters, bool& gather,
                  bool& panic, const Control& control, const Force& force)
{
    using Kind = Control::Kind;
    const int index = static_cast<int>(control.value);
    switch (control.kind)
    {
        case Kind::GatherChords: gather = control.value >= 0.5f; break;
        case Kind::Panic: panic = true; break;
        case Kind::StringMaterial:
            if (! force.material)
                parameters.stringMaterial = index == 0 ? StringMaterial::Nylon
                                                       : StringMaterial::Steel;
            break;
        case Kind::CaptureMode:
            if (! force.capture)
                parameters.capture = index == 0 ? CaptureType::StereoMic
                    : index == 1 ? CaptureType::MonoMic : CaptureType::Piezo;
            break;
        case Kind::Picking:
            parameters.picking = static_cast<acustra::PickingTechnique>(index);
            break;
        case Kind::Tuning: parameters.tuning = static_cast<acustra::Tuning>(index); break;
        case Kind::BodyAmount: parameters.bodyAmount = 0.01f * control.value; break;
        case Kind::Output:
            parameters.outputGain = std::pow(10.0f, 0.05f * control.value);
            break;
    }
}

// Plays a scenario through the player as the plug-in does (controls before
// the block containing them, events at their offsets), with the separate
// outputs wanted or not. The capture and material a Force names are set
// before prepare, so no crossfade separates the routes being compared.
Buses render(const Scenario& scenario, int blockSize, bool wantBuses,
             const Force& force = {}, double sampleRate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::EngineParameters parameters;
    if (force.capture)
        parameters.capture = *force.capture;
    if (force.material)
        parameters.stringMaterial = *force.material;
    performer->setParameters(parameters);
    performer->prepare(sampleRate, blockSize);
    performer->engine().setPortObserversEnabled(false);

    const int length = sampleAt(scenario.seconds, sampleRate);
    const auto padded = static_cast<std::size_t>(length + blockSize);
    Buses result;
    result.left.assign(padded, 0.0f);
    result.right.assign(padded, 0.0f);
    if (wantBuses)
    {
        // A sentinel, so a sample the engine forgets to write shows.
        result.micLeft.assign(padded, 7.0f);
        result.micRight.assign(padded, 7.0f);
        result.piezo.assign(padded, 7.0f);
    }
    bool gather = false;
    std::vector<bool> applied(scenario.controls.size(), false);
    std::vector<std::pair<int, const Event*>> block;
    for (int start = 0; start < length; start += blockSize)
    {
        bool panic = false;
        for (std::size_t index = 0; index < scenario.controls.size(); ++index)
            if (! applied[index]
                && sampleAt(scenario.controls[index].seconds, sampleRate)
                       < start + blockSize)
            {
                applyControl(parameters, gather, panic, scenario.controls[index], force);
                applied[index] = true;
            }
        block.clear();
        for (const auto& event : scenario.events)
        {
            const int at = sampleAt(event.seconds, sampleRate);
            if (at >= start && at < start + blockSize)
                block.emplace_back(at - start + event.skew, &event);
        }
        std::stable_sort(block.begin(), block.end(),
                         [] (const auto& a, const auto& b) { return a.first < b.first; });

        performer->setParameters(parameters);
        if (panic)
            performer->reset();
        performer->setGatherChords(gather);
        const auto offset = static_cast<std::size_t>(start);
        if (wantBuses)
            performer->beginBlock(result.left.data() + offset,
                                  result.right.data() + offset,
                                  { result.micLeft.data() + offset,
                                    result.micRight.data() + offset,
                                    result.piezo.data() + offset },
                                  blockSize);
        else
            performer->beginBlock(result.left.data() + offset,
                                  result.right.data() + offset, blockSize);
        for (const auto& [at, event] : block)
            performer->handleMidi(at, event->bytes.data(), event->size);
        performer->endBlock();
    }
    for (auto* channel : { &result.left, &result.right, &result.micLeft,
                           &result.micRight, &result.piezo })
        if (! channel->empty())
            channel->resize(static_cast<std::size_t>(length));
    return result;
}

bool anyAudible(const std::vector<float>& channel)
{
    return std::any_of(channel.begin(), channel.end(),
                       [] (float value) { return std::abs(value) > 1.0e-4f; });
}

bool finiteAndBounded(const std::vector<float>& channel)
{
    return std::all_of(channel.begin(), channel.end(), [] (float value)
    {
        return std::isfinite(value) && std::abs(value) <= 1.0f;
    });
}

// Wanting the separate outputs leaves Main as it was, down to the bit, over
// the whole battery (which switches capture, material, picking, gathering and
// resets mid-performance) at two block sizes; every bus sample is written.
void testWantingBusesLeavesMainUnchanged(const std::vector<Scenario>& battery)
{
    for (const int blockSize : { 64, 127 })
        for (const auto& scenario : battery)
        {
            const auto plain = render(scenario, blockSize, false);
            const auto withBuses = render(scenario, blockSize, true);
            const std::string name = std::string { scenario.name } + " at block "
                + std::to_string(blockSize);
            expect(bitwiseEqual(plain.left, withBuses.left)
                       && bitwiseEqual(plain.right, withBuses.right),
                   "wanting the separate outputs changed Main on " + name);
            for (const auto* channel : { &withBuses.micLeft, &withBuses.micRight,
                                         &withBuses.piezo })
                expect(finiteAndBounded(*channel),
                       "a separate output left a sample unwritten or unbounded on "
                           + name);
        }
}

// With Capture held on one route for the whole performance, that route's
// separate output is Main; and each separate output is the same whatever
// Capture selects, for both string materials.
void testBusesAreTheCaptureRoutes(const std::vector<Scenario>& battery)
{
    long signedZeros = 0;
    long piezoSamples = 0;
    for (const auto material : { StringMaterial::Steel, StringMaterial::Nylon })
        for (const auto& scenario : battery)
        {
            const std::string name = std::string { scenario.name }
                + (material == StringMaterial::Steel ? " (steel)" : " (nylon)");
            const auto stereo = render(scenario, 127, true,
                                       { CaptureType::StereoMic, material });
            const auto mono = render(scenario, 127, true,
                                     { CaptureType::MonoMic, material });
            const auto piezo = render(scenario, 127, true,
                                      { CaptureType::Piezo, material });

            expect(bitwiseEqual(stereo.micLeft, stereo.left)
                       && bitwiseEqual(stereo.micRight, stereo.right),
                   "the Mic pair is not Main with Capture on Stereo mic on " + name);
            expect(valueEqual(piezo.piezo, piezo.left, &signedZeros)
                       && valueEqual(piezo.piezo, piezo.right, &signedZeros),
                   "the Piezo line is not Main with Capture on Piezo on " + name);
            piezoSamples += 2 * static_cast<long>(piezo.piezo.size());
            for (const auto* other : { &mono, &piezo })
                expect(bitwiseEqual(other->micLeft, stereo.micLeft)
                           && bitwiseEqual(other->micRight, stereo.micRight)
                           && bitwiseEqual(other->piezo, stereo.piezo),
                       "a separate output depends on what Capture selects on " + name);
            if (anyAudible(stereo.left))
                expect(anyAudible(stereo.piezo) && anyAudible(stereo.micLeft),
                       "a separate output is silent while Main plays on " + name);
        }
    std::cout << "Piezo line vs Main on Piezo: equal as floats on " << piezoSamples
              << " samples, " << signedZeros << " differing only in a zero's sign\n";
}

// While Capture crossfades between routes, the separate outputs keep
// observing the unchanged instrument: a mid-note switch to Piezo and back
// leaves them bit-identical to a performance that never switched.
void testCaptureSwitchLeavesBusesAlone()
{
    Scenario scenario;
    scenario.name = "capture switch";
    scenario.seconds = 1.2;
    const auto on = [] (double t, int note)
    {
        return detail::message(t, 0x90, note, 100);
    };
    scenario.events = { on(0.0, 40), on(0.0, 47), on(0.0, 52), on(0.4, 55) };
    scenario.controls = { { 0.25, Control::Kind::CaptureMode, 2.0f },
                          { 0.6, Control::Kind::CaptureMode, 1.0f },
                          { 0.9, Control::Kind::CaptureMode, 0.0f } };
    Scenario still = scenario;
    still.controls.clear();
    const auto switched = render(scenario, 64, true);
    const auto held = render(still, 64, true);
    expect(bitwiseEqual(switched.micLeft, held.micLeft)
               && bitwiseEqual(switched.micRight, held.micRight)
               && bitwiseEqual(switched.piezo, held.piezo),
           "switching Capture changed a separate output");
    expect(! bitwiseEqual(switched.left, held.left),
           "the Capture switch did not reach Main");
}

// Each bus pointer is its own request: any subset renders what the full set
// does, and an unwanted pointer is never touched.
void testEachBusIsItsOwnRequest()
{
    constexpr int rate = 48000;
    constexpr int length = rate / 2;
    const auto run = [] (bool micLeft, bool micRight, bool piezo)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        for (const int note : { 40, 45, 50, 55, 59, 64 })
            engine->noteOn(note, 0.8f);
        Buses out;
        out.left.assign(length, 0.0f);
        out.right.assign(length, 0.0f);
        out.micLeft.assign(length, 7.0f);
        out.micRight.assign(length, 7.0f);
        out.piezo.assign(length, 7.0f);
        for (int start = 0; start < length; start += 64)
        {
            const auto at = static_cast<std::size_t>(start);
            engine->process(out.left.data() + at, out.right.data() + at,
                            { micLeft ? out.micLeft.data() + at : nullptr,
                              micRight ? out.micRight.data() + at : nullptr,
                              piezo ? out.piezo.data() + at : nullptr },
                            std::min(64, length - start));
        }
        return out;
    };
    const auto all = run(true, true, true);
    const std::vector<float> untouched(length, 7.0f);
    for (int mask = 0; mask < 8; ++mask)
    {
        const bool l = mask & 1, r = mask & 2, p = mask & 4;
        const auto some = run(l, r, p);
        expect(bitwiseEqual(some.left, all.left) && bitwiseEqual(some.right, all.right),
               "a subset of separate outputs changed Main");
        expect(bitwiseEqual(some.micLeft, l ? all.micLeft : untouched)
                   && bitwiseEqual(some.micRight, r ? all.micRight : untouched)
                   && bitwiseEqual(some.piezo, p ? all.piezo : untouched),
               "a separate output was written unwanted, or differs when wanted alone");
    }
    // The player's block forms, with a note inside the block and without.
    auto performer = std::make_unique<Performer>();
    performer->prepare(rate, 64);
    std::vector<float> left(4800), right(4800), micLeft(4800), micRight(4800),
        piezo(4800);
    performer->beginBlock(left.data(), right.data(),
                          { micLeft.data(), micRight.data(), piezo.data() }, 64);
    performer->noteOn(17, 1, 52, 100);
    performer->endBlock();
    performer->process(left.data() + 64, right.data() + 64,
                       { micLeft.data() + 64, micRight.data() + 64,
                         piezo.data() + 64 }, 4800 - 64);
    expect(anyAudible(left) && bitwiseEqual(micLeft, left)
               && bitwiseEqual(micRight, right) && anyAudible(piezo),
           "Performer::process did not render the separate outputs");
}

// Before any note, and after a reset, every output is exact silence, so a
// host may idle the instrument whichever outputs are cabled.
void testIdleOutputsAreExactSilence()
{
    auto performer = std::make_unique<Performer>();
    performer->prepare(48000.0, 64);
    std::vector<float> left(64), right(64), micLeft(64), micRight(64), piezo(64);
    const auto silent = [&]
    {
        for (const auto* channel : { &left, &right, &micLeft, &micRight, &piezo })
            if (std::any_of(channel->begin(), channel->end(),
                            [] (float value) { return value != 0.0f; }))
                return false;
        return true;
    };
    bool quiet = true;
    for (int block = 0; block < 750; ++block)
    {
        performer->process(left.data(), right.data(),
                           { micLeft.data(), micRight.data(), piezo.data() }, 64);
        quiet = quiet && silent();
    }
    expect(quiet, "an idle instrument's separate outputs are not exact silence");
    performer->beginBlock(left.data(), right.data(),
                          { micLeft.data(), micRight.data(), piezo.data() }, 64);
    performer->noteOn(0, 1, 45, 110);
    performer->endBlock();
    for (int block = 0; block < 100; ++block)
        performer->process(left.data(), right.data(),
                           { micLeft.data(), micRight.data(), piezo.data() }, 64);
    expect(! silent(), "the note before the reset did not sound");
    performer->reset();
    performer->process(left.data(), right.data(),
                       { micLeft.data(), micRight.data(), piezo.data() }, 64);
    expect(silent(), "a reset instrument's separate outputs are not exact silence");
}

// The cost, per 64-frame block of a ringing six-string chord re-plucked every
// half second: Main alone and Main with every separate output, interleaved,
// best of several passes. Printed for the record; the gate is loose so a
// busy machine cannot fail it, while a second render pass would.
void testCost()
{
    constexpr double rate = 48000.0;
    constexpr int blocks = 1500; // 2 s
    const auto pass = [&] (bool wantBuses)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        engine->setPortObserversEnabled(false);
        std::array<float, 64> left {}, right {}, micLeft {}, micRight {}, piezo {};
        const AcustraEngine::OutputBuses buses { micLeft.data(), micRight.data(),
                                                 piezo.data() };
        float sink = 0.0f;
        const auto start = std::chrono::steady_clock::now();
        for (int block = 0; block < blocks; ++block)
        {
            if (block % 375 == 0)
                for (const int note : { 40, 45, 50, 55, 59, 64 })
                    engine->noteOn(note, 0.8f);
            if (wantBuses)
                engine->process(left.data(), right.data(), buses, 64);
            else
                engine->process(left.data(), right.data(), 64);
            sink += left[63] + piezo[63];
        }
        const double micros = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - start).count() / blocks;
        return std::isfinite(sink) ? micros : 0.0;
    };
    double plain = 1.0e30, withBuses = 1.0e30;
    for (int repeat = 0; repeat < 9; ++repeat)
    {
        plain = std::min(plain, pass(false));
        withBuses = std::min(withBuses, pass(true));
    }
    std::cout << "Cost per 64-frame block, six ringing strings: Main "
              << plain << " us, Main + Mic + Piezo " << withBuses << " us ("
              << 100.0 * (withBuses / plain - 1.0) << "%)\n";
    expect(withBuses < 1.25 * plain,
           "rendering the separate outputs costs more than a quarter of Main");
}
} // namespace

int main()
{
    const auto battery = makeBattery();
    testWantingBusesLeavesMainUnchanged(battery);
    testBusesAreTheCaptureRoutes(battery);
    testCaptureSwitchLeavesBusesAlone();
    testEachBusIsItsOwnRequest();
    testIdleOutputsAreExactSilence();
    testCost();

    if (failures != 0)
    {
        std::cerr << failures << " Acustra output bus test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Acustra output bus tests passed\n";
    return EXIT_SUCCESS;
}
