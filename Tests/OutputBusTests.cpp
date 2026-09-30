// The separate output (AcustraEngine::OutputBuses): the Piezo line renders
// alongside Main in one pass. Wanting it must not change Main by a bit; it
// must be Main's mono whenever Capture is on Piezo; it may not depend on what
// Capture selects; and not wanting it must cost nothing. The whole
// performance battery is played through the player to check it. Piezo Mix
// puts that same line into Main under the microphones.
#include "DSP/AcustraPerformer.h"
#include "PerformanceBattery.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
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
using acustra::EngineParameters;
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
    std::vector<float> left, right, piezo;
};

// The only separate output is the mono Piezo line: Main plus one pointer.
static_assert(sizeof(AcustraEngine::OutputBuses) == sizeof(float*),
              "OutputBuses carries the Piezo pointer alone");

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
// capture ignores the battery's own switches of it.
struct Force
{
    std::optional<CaptureType> capture;
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
        case Kind::Shape: parameters.shape = static_cast<acustra::BodyShape>(index); break;
        case Kind::Wood:
            parameters.bodyMaterial = static_cast<acustra::BodyMaterial>(index);
            break;
        case Kind::Model:
            parameters.guitarModel = static_cast<acustra::GuitarModel>(index);
            break;
        case Kind::Width: parameters.stereoWidth = 0.01f * control.value; break;
        case Kind::Age: parameters.stringAge = 0.01f * control.value; break;
        case Kind::Pluck: parameters.pluckPosition = 0.01f * control.value; break;
        case Kind::Touch: parameters.touch = 0.01f * control.value; break;
        case Kind::PiezoMix: parameters.piezoMix = 0.01f * control.value; break;
    }
}

// Plays a scenario through the player as the plug-in does (controls before
// the block containing them, events at their offsets), with the separate
// output wanted or not. The capture a Force names is set before prepare, so no crossfade separates the routes being compared.
Buses render(const Scenario& scenario, int blockSize, bool wantBuses,
             const Force& force = {}, double sampleRate = 48000.0)
{
    auto performer = std::make_unique<Performer>();
    acustra::EngineParameters parameters;
    if (force.capture)
        parameters.capture = *force.capture;
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
                                  AcustraEngine::OutputBuses {
                                      result.piezo.data() + offset },
                                  blockSize);
        else
            performer->beginBlock(result.left.data() + offset,
                                  result.right.data() + offset, blockSize);
        for (const auto& [at, event] : block)
            performer->handleMidi(at, event->bytes.data(), event->size);
        performer->endBlock();
    }
    for (auto* channel : { &result.left, &result.right, &result.piezo })
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

// Wanting the Piezo output leaves Main as it was, down to the bit, over the
// whole battery (which switches capture, Piezo Mix, picking, gathering and
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
                   "wanting the Piezo output changed Main on " + name);
            expect(finiteAndBounded(withBuses.piezo),
                   "the Piezo output left a sample unwritten or unbounded on "
                       + name);
        }
}

// With Capture held on Piezo for the whole performance, the Piezo output is
// Main (Piezo Mix, which the battery moves, adds nothing there); and it is
// the same whatever Capture selects.
void testBusesAreTheCaptureRoutes(const std::vector<Scenario>& battery)
{
    long signedZeros = 0;
    long piezoSamples = 0;
    for (const auto& scenario : battery)
    {
        const std::string name { scenario.name };
        const auto stereo = render(scenario, 127, true, { CaptureType::StereoMic });
        const auto mono = render(scenario, 127, true, { CaptureType::MonoMic });
        const auto piezo = render(scenario, 127, true, { CaptureType::Piezo });

        expect(valueEqual(piezo.piezo, piezo.left, &signedZeros)
                   && valueEqual(piezo.piezo, piezo.right, &signedZeros),
               "the Piezo line is not Main with Capture on Piezo on " + name);
        piezoSamples += 2 * static_cast<long>(piezo.piezo.size());
        for (const auto* other : { &mono, &piezo })
            expect(bitwiseEqual(other->piezo, stereo.piezo),
                   "the Piezo output depends on what Capture selects on " + name);
        if (anyAudible(stereo.left))
            expect(anyAudible(stereo.piezo),
                   "the Piezo output is silent while Main plays on " + name);
    }
    std::cout << "Piezo line vs Main on Piezo: equal as floats on " << piezoSamples
              << " samples, " << signedZeros << " differing only in a zero's sign\n";
}

// While Capture crossfades between routes, the Piezo output keeps observing
// the unchanged instrument: a mid-note switch to Piezo and back leaves it
// bit-identical to a performance that never switched.
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
    expect(bitwiseEqual(switched.piezo, held.piezo),
           "switching Capture changed the Piezo output");
    expect(! bitwiseEqual(switched.left, held.left),
           "the Capture switch did not reach Main");
}

// The Piezo pointer is a request: null renders Main exactly as a given
// pointer does and never writes anywhere, and a given pointer is written in
// full.
void testPiezoIsARequest()
{
    constexpr int rate = 48000;
    constexpr int length = rate / 2;
    const auto run = [] (bool piezo)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        for (const int note : { 40, 45, 50, 55, 59, 64 })
            engine->noteOn(note, 0.8f);
        Buses out;
        out.left.assign(length, 0.0f);
        out.right.assign(length, 0.0f);
        out.piezo.assign(length, 7.0f);
        for (int start = 0; start < length; start += 64)
        {
            const auto at = static_cast<std::size_t>(start);
            engine->process(out.left.data() + at, out.right.data() + at,
                            AcustraEngine::OutputBuses {
                                piezo ? out.piezo.data() + at : nullptr },
                            std::min(64, length - start));
        }
        return out;
    };
    const auto wanted = run(true);
    const auto unwanted = run(false);
    expect(bitwiseEqual(unwanted.left, wanted.left)
               && bitwiseEqual(unwanted.right, wanted.right),
           "requesting the Piezo output changed Main");
    expect(bitwiseEqual(unwanted.piezo, std::vector<float>(length, 7.0f)),
           "the Piezo output was written unwanted");
    expect(anyAudible(wanted.piezo) && finiteAndBounded(wanted.piezo),
           "the Piezo output was not written when wanted");
    // The player's block forms, with a note inside the block and without.
    auto performer = std::make_unique<Performer>();
    performer->prepare(rate, 64);
    std::vector<float> left(4800), right(4800), piezo(4800, 7.0f);
    performer->beginBlock(left.data(), right.data(),
                          AcustraEngine::OutputBuses { piezo.data() }, 64);
    performer->noteOn(17, 1, 52, 100);
    performer->endBlock();
    performer->process(left.data() + 64, right.data() + 64,
                       AcustraEngine::OutputBuses { piezo.data() + 64 }, 4800 - 64);
    expect(anyAudible(left) && anyAudible(piezo) && finiteAndBounded(piezo),
           "Performer::process did not render the Piezo output");
}

// Before any note, and after a reset, Main and Piezo are exact silence, so a
// host may idle the instrument whichever outputs are cabled.
// An instrument left to ring out reaches exact silence on Main and the
// Piezo bus too, not only a fresh or reset one. Its strings and bridge used
// to hold a rounding-level residue between them for good - about 1e-12 at
// the output at 96 kHz (audit F30) - which a host listening for exact
// silence never heard end.
void testRungOutInstrumentReachesExactSilence()
{
    struct Case { const char* name; acustra::GuitarModel model; double rate; };
    for (const auto& c : { Case { "the Original at 96 kHz", acustra::GuitarModel::Original, 96000.0 },
                           Case { "the Original at 44.1 kHz", acustra::GuitarModel::Original, 44100.0 },
                           Case { "the Bellido at 96 kHz", acustra::GuitarModel::Bellido1978, 96000.0 } })
    {
        EngineParameters parameters;
        parameters.guitarModel = c.model;
        auto engine = std::make_unique<AcustraEngine>();
        engine->setParameters(parameters);
        engine->prepare(c.rate, 512);
        engine->beginStrum();
        const std::array<int, 6> chord { 40, 47, 52, 56, 59, 64 };
        for (std::size_t k = 0; k < chord.size(); ++k)
            engine->noteOn(chord[k], 0.8f, 1, static_cast<int>(k) * 300, true);
        std::vector<float> left(512), right(512), piezo(512);
        const AcustraEngine::OutputBuses buses { piezo.data() };
        const int blocksPerSecond = static_cast<int>(c.rate / 512.0);
        bool silentAtEnd = true;
        for (int second = 0; second < 25; ++second)
        {
            if (second == 3)
                engine->allNotesOff();
            for (int block = 0; block < blocksPerSecond; ++block)
            {
                engine->process(left.data(), right.data(), buses, 512);
                if (second == 24)
                    for (const auto* channel : { &left, &right, &piezo })
                        silentAtEnd = silentAtEnd && std::all_of(channel->begin(),
                            channel->end(), [] (float value) { return value == 0.0f; });
            }
        }
        expect(silentAtEnd, std::string(c.name)
                   + ": a chord left to ring out did not reach exact silence in 21 s");
        // And it plays again from there as a fresh instrument does.
        engine->noteOn(45, 0.8f);
        engine->process(left.data(), right.data(), buses, 512);
        engine->process(left.data(), right.data(), buses, 512);
        expect(std::any_of(left.begin(), left.end(), [] (float value) { return value != 0.0f; }),
               std::string(c.name) + ": a note after the instrument fell silent did not sound");
    }
}

// Piezo Mix puts the Piezo line into Main under the microphones. Renders a
// quiet chord (well under the safety limiter) straight through the engine,
// with the mix set before prepare and then, from block `from`, moved to each
// of `moves` in turn every `every` blocks.
Buses renderPiezoMix(CaptureType capture, float mix, std::vector<float> moves = {},
                     int from = 0, int every = 0)
{
    constexpr int rate = 48000;
    constexpr int blockSize = 64;
    constexpr int length = rate * 3 / 2;
    EngineParameters parameters;
    parameters.capture = capture;
    parameters.piezoMix = mix;
    auto engine = std::make_unique<AcustraEngine>();
    engine->setParameters(parameters);
    engine->prepare(rate, blockSize);
    engine->beginStrum();
    const std::array<int, 6> chord { 40, 47, 52, 56, 59, 64 };
    for (std::size_t k = 0; k < chord.size(); ++k)
        engine->noteOn(chord[k], 0.3f, 1, static_cast<int>(k) * 300, true);
    Buses out;
    out.left.assign(length, 0.0f);
    out.right.assign(length, 0.0f);
    out.piezo.assign(length, 7.0f);
    for (int start = 0, block = 0; start < length; start += blockSize, ++block)
    {
        if (every > 0 && block >= from && (block - from) % every == 0
            && static_cast<std::size_t>((block - from) / every) < moves.size())
        {
            parameters.piezoMix = moves[static_cast<std::size_t>((block - from) / every)];
            engine->setParameters(parameters);
        }
        const auto at = static_cast<std::size_t>(start);
        engine->process(out.left.data() + at, out.right.data() + at,
                        AcustraEngine::OutputBuses { out.piezo.data() + at },
                        std::min(blockSize, length - start));
    }
    return out;
}

double peakOf(const std::vector<float>& channel)
{
    double peak = 0.0;
    for (const float value : channel)
        peak = std::max(peak, static_cast<double>(std::abs(value)));
    return peak;
}

void testPiezoMix()
{
    // At 0, set or sanitised from below, Main is the instrument without one,
    // down to the bit.
    const auto plain = renderPiezoMix(CaptureType::StereoMic, 0.0f);
    for (const float zero : { 0.0f, -0.5f })
    {
        const auto none = renderPiezoMix(CaptureType::StereoMic, zero);
        expect(bitwiseEqual(none.left, plain.left) && bitwiseEqual(none.right, plain.right),
               "Piezo Mix at " + std::to_string(zero) + " changed Main");
    }
    {
        // The default parameters carry no mix.
        EngineParameters defaults;
        expect(defaults.piezoMix == 0.0f, "Piezo Mix does not default to 0");
    }
    expect(peakOf(plain.left) < 0.5 && peakOf(plain.right) < 0.5 && anyAudible(plain.piezo),
           "the Piezo Mix chord is not quiet enough to stay off the limiter, or silent");

    // At 1 under the stereo microphones, Main is the stereo render plus the
    // Piezo line on both sides, to float rounding; the Piezo line itself does
    // not move.
    const auto mixed = renderPiezoMix(CaptureType::StereoMic, 1.0f);
    expect(bitwiseEqual(mixed.piezo, plain.piezo), "Piezo Mix changed the Piezo output");
    double worst = 0.0, largest = 0.0;
    for (std::size_t i = 0; i < plain.left.size(); ++i)
        for (const auto& [with, without] : { std::pair { mixed.left[i], plain.left[i] },
                                              std::pair { mixed.right[i], plain.right[i] } })
        {
            const double difference = static_cast<double>(with) - without;
            const double scale = std::max({ std::abs(static_cast<double>(with)),
                                            std::abs(static_cast<double>(without)),
                                            std::abs(static_cast<double>(mixed.piezo[i])) });
            // Two float roundings (the sum and the piezo's own) at this scale.
            const double allowed = 4.0 * 5.96e-8 * scale + 1.0e-30;
            worst = std::max(worst, std::abs(difference - mixed.piezo[i]) / allowed);
            largest = std::max(largest, std::abs(difference));
        }
    std::cout << "Piezo Mix 1 under Stereo: Main minus Stereo vs the Piezo line, worst "
              << worst << " of its rounding budget, largest added " << largest << '\n';
    expect(worst <= 1.0, "Piezo Mix 1 is not the Piezo line added to the stereo microphones");
    expect(largest > 1.0e-4, "Piezo Mix 1 added nothing audible to Main");

    // With Capture on Piezo the piezo is already all of Main: any mix, set or
    // moved, leaves Main (and the line) as it was.
    const auto piezoPlain = renderPiezoMix(CaptureType::Piezo, 0.0f);
    const auto piezoMixed = renderPiezoMix(CaptureType::Piezo, 1.0f);
    const auto piezoMoved = renderPiezoMix(CaptureType::Piezo, 0.0f, { 1.0f, 0.3f, 0.0f, 0.8f },
                                           100, 150);
    for (const auto* other : { &piezoMixed, &piezoMoved })
        expect(valueEqual(other->left, piezoPlain.left) && valueEqual(other->right, piezoPlain.right)
                   && bitwiseEqual(other->piezo, piezoPlain.piezo),
               "Piezo Mix changed Main with Capture on Piezo");

    // A mix moved up and back to 0 under the microphones glides (no step
    // into Main) and then settles onto 0 exactly: Main is the plain render's
    // again, bit for bit.
    constexpr int from = 100, every = 200;
    const auto moved = renderPiezoMix(CaptureType::StereoMic, 0.0f, { 1.0f, 0.0f }, from, every);
    const auto back = static_cast<std::size_t>((from + every) * 64);
    const auto settled = back + 48000 / 2;
    expect(bitwiseEqual(std::vector<float>(moved.left.begin(), moved.left.begin() + from * 64),
                        std::vector<float>(plain.left.begin(), plain.left.begin() + from * 64)),
           "Piezo Mix changed Main before it moved");
    expect(! bitwiseEqual(moved.left, plain.left), "a moved Piezo Mix did not reach Main");
    expect(bitwiseEqual(std::vector<float>(moved.left.begin() + static_cast<long>(settled), moved.left.end()),
                        std::vector<float>(plain.left.begin() + static_cast<long>(settled), plain.left.end()))
               && bitwiseEqual(std::vector<float>(moved.right.begin() + static_cast<long>(settled), moved.right.end()),
                               std::vector<float>(plain.right.begin() + static_cast<long>(settled), plain.right.end())),
           "a Piezo Mix moved back to 0 did not settle to Main without one");
    // The first sample after the move carries only a smoothing step of it.
    const auto first = static_cast<std::size_t>(from * 64);
    expect(std::abs(moved.left[first] - plain.left[first]) < 0.1 * std::abs(mixed.piezo[first]) + 1.0e-6,
           "Piezo Mix stepped into Main instead of gliding");
}

// Piezo Mix sums the two sensors on the instrument's one time base. The
// piezo chain's output is seven samples behind its input at every rate
// (renderPiezo), so the microphones wait the same seven samples; before
// they did, the blend summed them 146 us apart at 48 kHz and 73 us apart at
// 96 kHz, a comb that moved with the rate. A strum at 48 and 96 kHz:
// the mic x piezo cross-spectra's phases, in third octaves from 300 Hz to
// 10 kHz, line up with no lag between the rates (74 us before), and the
// blend's third-octave levels over the sensors' power sum agree within
// 0.5 dB on average from 150 Hz to 12.5 kHz (0.6-0.9 dB before).
struct BlendBands
{
    std::vector<double> centre, mic, piezo, blend;
    std::vector<std::complex<double>> cross;
};

void forwardFft(std::vector<std::complex<double>>& data)
{
    const std::size_t size = data.size();
    for (std::size_t i = 1, j = 0; i < size; ++i)
    {
        std::size_t bit = size >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= size; length <<= 1)
    {
        const auto root = std::polar(1.0, -2.0 * 3.14159265358979323846 / double(length));
        for (std::size_t start = 0; start < size; start += length)
        {
            std::complex<double> twiddle { 1.0, 0.0 };
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * twiddle;
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
                twiddle *= root;
            }
        }
    }
}

BlendBands blendBands(double rate, acustra::GuitarModel model, float mix)
{
    std::vector<float> mic, piezo, blend;
    for (const bool mixed : { false, true })
    {
        auto engine = std::make_unique<AcustraEngine>();
        EngineParameters parameters;
        parameters.guitarModel = model;
        parameters.outputGain = 0.3f;
        parameters.piezoMix = mixed ? mix : 0.0f;
        engine->setParameters(parameters);
        engine->prepare(rate, 64);
        engine->beginStrum();
        const std::array<int, 6> notes { 40, 45, 50, 55, 59, 64 };
        for (std::size_t k = 0; k < notes.size(); ++k)
            engine->noteOn(notes[k], 0.7f, 1, static_cast<int>(k) * static_cast<int>(0.012 * rate), true);
        const int length = static_cast<int>(1.5 * rate);
        std::vector<float> left(static_cast<std::size_t>(length)), right(left.size()), line(left.size());
        for (int offset = 0; offset < length; offset += 64)
        {
            AcustraEngine::OutputBuses buses;
            buses.piezo = line.data() + offset;
            engine->process(left.data() + offset, right.data() + offset, buses,
                            std::min(64, length - offset));
        }
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const float main = 0.5f * (left[i] + right[i]);
            if (mixed)
                blend.push_back(main);
            else
            {
                mic.push_back(main);
                piezo.push_back(line[i]);
            }
        }
    }
    // The same 5.9 Hz resolution at both rates.
    const std::size_t size = rate > 50000.0 ? 16384 : 8192;
    BlendBands bands;
    std::vector<double> edges;
    for (double f = 150.0; f < 12500.0; f *= std::exp2(1.0 / 3.0))
        edges.push_back(f);
    const std::size_t count = edges.size() - 1;
    for (std::size_t k = 0; k < count; ++k)
        bands.centre.push_back(std::sqrt(edges[k] * edges[k + 1]));
    bands.mic.assign(count, 0.0);
    bands.piezo.assign(count, 0.0);
    bands.blend.assign(count, 0.0);
    bands.cross.assign(count, {});
    for (std::size_t start = 0; start + size <= mic.size(); start += size / 2)
    {
        std::vector<std::complex<double>> m(size), p(size), b(size);
        for (std::size_t i = 0; i < size; ++i)
        {
            const double window = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * double(i) / double(size));
            m[i] = window * mic[start + i];
            p[i] = window * piezo[start + i];
            b[i] = window * blend[start + i];
        }
        forwardFft(m);
        forwardFft(p);
        forwardFft(b);
        for (std::size_t bin = 1; bin < size / 2; ++bin)
        {
            const double f = double(bin) * rate / double(size);
            for (std::size_t k = 0; k < count; ++k)
                if (f >= edges[k] && f < edges[k + 1])
                {
                    bands.mic[k] += std::norm(m[bin]);
                    bands.piezo[k] += std::norm(p[bin]);
                    bands.blend[k] += std::norm(b[bin]);
                    bands.cross[k] += m[bin] * std::conj(p[bin]);
                }
        }
    }
    return bands;
}

void testPiezoMixBlendsOnOneTimeBase()
{
    expect(AcustraEngine::outputLatencySamples() == 7,
           "the engine's output latency is not the piezo chain's seven samples");
    for (const auto model : { acustra::GuitarModel::Original, acustra::GuitarModel::Bellido1978 })
        for (const float mix : { 0.5f, 1.0f })
        {
            const auto low = blendBands(48000.0, model, mix);
            const auto high = blendBands(96000.0, model, mix);
            double deviation = 0.0;
            for (std::size_t k = 0; k < low.centre.size(); ++k)
            {
                const auto excess = [mix] (const BlendBands& bands, std::size_t band)
                {
                    return 10.0 * std::log10(bands.blend[band]
                        / (bands.mic[band] + double(mix) * mix * bands.piezo[band]));
                };
                deviation += std::abs(excess(low, k) - excess(high, k)) / double(low.centre.size());
            }
            // The lag between the rates that best lines up their cross-spectra.
            double lag = 0.0, best = -1e300;
            for (double tau = -200e-6; tau <= 200e-6; tau += 1e-6)
            {
                double score = 0.0;
                for (std::size_t k = 0; k < low.centre.size(); ++k)
                    if (low.centre[k] > 300.0 && low.centre[k] < 10000.0)
                    {
                        const auto ratio = low.cross[k] * std::conj(high.cross[k]);
                        score += std::real(ratio / std::abs(ratio)
                            * std::polar(1.0, -2.0 * 3.14159265358979323846 * low.centre[k] * tau));
                    }
                if (score > best)
                {
                    best = score;
                    lag = tau;
                }
            }
            std::cout << "Piezo Mix " << mix << ", model " << static_cast<int>(model)
                      << ": 48 vs 96 kHz blend over power sum differs " << deviation
                      << " dB on average; mic/piezo timing differs " << lag * 1e6 << " us\n";
            expect(deviation < 0.5, "Piezo Mix's blend moves with the sample rate (a comb)");
            expect(std::abs(lag) < 20e-6,
                   "the microphones and the piezo are summed on different time bases");
        }
}

// A Capture or Piezo Mix glide reaches its target exactly at every rate. At
// 192 kHz a smoothing step near 1 used to fall below half an ulp before the
// glide was within its snapping distance, so it stopped short for good: Main
// under a Capture moved away and back never became the plain stereo render
// again, and a mix raised to 1 stayed a little under it.
void testGlidesSettleAtHighRates()
{
    static constexpr int rate = 192000;
    static constexpr int blockSize = 64;
    static constexpr int length = rate * 2;
    const auto run = [] (EngineParameters initial, std::vector<std::pair<int, EngineParameters>> moves)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->setParameters(initial);
        engine->prepare(rate, blockSize);
        for (const int note : { 40, 52, 59 })
            engine->noteOn(note, 0.3f);
        Buses out;
        out.left.assign(length, 0.0f);
        out.right.assign(length, 0.0f);
        std::size_t next = 0;
        for (int start = 0; start < length; start += blockSize)
        {
            if (next < moves.size() && start >= moves[next].first)
                engine->setParameters(moves[next++].second);
            const auto at = static_cast<std::size_t>(start);
            engine->process(out.left.data() + at, out.right.data() + at,
                            AcustraEngine::OutputBuses {}, std::min(blockSize, length - start));
        }
        return out;
    };
    const auto tail = [] (const std::vector<float>& channel)
    {
        return std::vector<float>(channel.begin() + length * 3 / 4, channel.end());
    };
    EngineParameters stereo;
    EngineParameters mono = stereo;
    mono.capture = CaptureType::MonoMic;
    const auto plain = run(stereo, {});
    const auto returned = run(stereo, { { rate / 8, mono }, { rate / 2, stereo } });
    expect(bitwiseEqual(tail(returned.left), tail(plain.left))
               && bitwiseEqual(tail(returned.right), tail(plain.right)),
           "a Capture moved away and back at 192 kHz did not settle onto Stereo");
    EngineParameters full = stereo;
    full.piezoMix = 1.0f;
    const auto set = run(full, {});
    const auto raised = run(stereo, { { rate / 8, full } });
    expect(bitwiseEqual(tail(raised.left), tail(set.left))
               && bitwiseEqual(tail(raised.right), tail(set.right)),
           "a Piezo Mix raised to 1 at 192 kHz did not settle onto 1");
}

void testIdleOutputsAreExactSilence()
{
    auto performer = std::make_unique<Performer>();
    performer->prepare(48000.0, 64);
    std::vector<float> left(64), right(64), piezo(64);
    const AcustraEngine::OutputBuses buses { piezo.data() };
    const auto silent = [&]
    {
        for (const auto* channel : { &left, &right, &piezo })
            if (std::any_of(channel->begin(), channel->end(),
                            [] (float value) { return value != 0.0f; }))
                return false;
        return true;
    };
    bool quiet = true;
    for (int block = 0; block < 750; ++block)
    {
        performer->process(left.data(), right.data(),
                           buses, 64);
        quiet = quiet && silent();
    }
    expect(quiet, "an idle instrument's outputs are not exact silence");
    performer->beginBlock(left.data(), right.data(),
                          buses, 64);
    performer->noteOn(0, 1, 45, 110);
    performer->endBlock();
    for (int block = 0; block < 100; ++block)
        performer->process(left.data(), right.data(),
                           buses, 64);
    expect(! silent(), "the note before the reset did not sound");
    performer->reset();
    performer->process(left.data(), right.data(),
                       buses, 64);
    expect(silent(), "a reset instrument's outputs are not exact silence");
}

// The cost, per 64-frame block of a ringing six-string chord re-plucked every
// half second: Main alone and Main with the Piezo output, interleaved,
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
        std::array<float, 64> left {}, right {}, piezo {};
        const AcustraEngine::OutputBuses buses { piezo.data() };
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
              << plain << " us, Main + Piezo " << withBuses << " us ("
              << 100.0 * (withBuses / plain - 1.0) << "%)\n";
    expect(withBuses < 1.25 * plain,
           "rendering the Piezo output costs more than a quarter of Main");
}
} // namespace

int main()
{
    const auto battery = makeBattery();
    testWantingBusesLeavesMainUnchanged(battery);
    testBusesAreTheCaptureRoutes(battery);
    testCaptureSwitchLeavesBusesAlone();
    testPiezoIsARequest();
    testIdleOutputsAreExactSilence();
    testRungOutInstrumentReachesExactSilence();
    testPiezoMix();
    testPiezoMixBlendsOnOneTimeBase();
    testGlidesSettleAtHighRates();
    testCost();

    if (failures != 0)
    {
        std::cerr << failures << " Acustra output bus test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Acustra output bus tests passed\n";
    return EXIT_SUCCESS;
}
