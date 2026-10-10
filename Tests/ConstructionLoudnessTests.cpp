// Every construction and Picking plays at the default construction's loudness
// (Docs/decisions.md, 2026-09-29, "Every construction as loud as the
// default"; the gains are Source/DSP/ConstructionLoudnessData.h, measured by
// Tools/CalibrateConstructionLoudness.py).
//
// This plays its own phrase - not the calibration's - on every construction
// that sounds different (Shape x Wood) with every Picking, measures ITU-R BS.1770-4 integrated
// loudness, and requires each within a few LU of the default construction:
// before the gains the same phrase spread over about 15 LU. The mono
// microphone and the piezo are held to the same target on a spread of
// constructions.
#include "DSP/AcustraEngine.h"
#ifndef ACUSTRA_CONSTRUCTION_LOUDNESS_BEFORE
#include "DSP/ConstructionLoudnessData.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

constexpr double sampleRate = 48000.0;
constexpr int block = 64;

// One biquad in direct form I, double precision.
struct Biquad
{
    double b0, b1, b2, a1, a2;
    double x1 { 0.0 }, x2 { 0.0 }, y1 { 0.0 }, y2 { 0.0 };
    double process(double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// ITU-R BS.1770-4 integrated loudness of a stereo signal at 48 kHz: the
// K-weighting (the standard's 48 kHz coefficients), 400 ms blocks with 75%
// overlap, the -70 LUFS absolute and -10 LU relative gates.
double integratedLoudness(const std::vector<float>& left, const std::vector<float>& right)
{
    std::array<std::vector<double>, 2> weighted;
    const std::array<const std::vector<float>*, 2> channels { &left, &right };
    for (std::size_t channel = 0; channel < 2; ++channel)
    {
        Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285,
                       -1.69065929318241, 0.73248077421585 };
        Biquad highPass { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621 };
        weighted[channel].reserve(channels[channel]->size());
        for (const float sample : *channels[channel])
            weighted[channel].push_back(highPass.process(shelf.process(sample)));
    }
    const auto length = static_cast<std::size_t>(0.4 * sampleRate);
    const auto hop = length / 4;
    std::vector<double> powers;
    for (std::size_t start = 0; start + length <= left.size(); start += hop)
    {
        double power = 0.0;
        for (const auto& channel : weighted)
        {
            double sum = 0.0;
            for (std::size_t i = start; i < start + length; ++i)
                sum += channel[i] * channel[i];
            power += sum / static_cast<double>(length);
        }
        powers.push_back(power);
    }
    const auto loudness = [] (double power) { return -0.691 + 10.0 * std::log10(power); };
    const auto gatedMean = [&] (double threshold)
    {
        double sum = 0.0;
        int count = 0;
        for (const double power : powers)
            if (power > 0.0 && loudness(power) > threshold)
            {
                sum += power;
                ++count;
            }
        return count > 0 ? sum / count : 0.0;
    };
    const double ungated = gatedMean(-70.0);
    if (!(ungated > 0.0))
        return -200.0;
    return loudness(gatedMean(loudness(ungated) - 10.0));
}

struct Event
{
    double seconds;
    int note;
    float velocity; // 0 is a key-up
};

// A strum down, a softer strum up, single notes low and high from soft to
// hard, and a soft held chord.
std::vector<Event> phrase()
{
    std::vector<Event> events;
    const auto chord = [&] (double at, std::vector<int> notes, float velocity,
                            double spacing, double release)
    {
        for (std::size_t rank = 0; rank < notes.size(); ++rank)
        {
            events.push_back({ at + spacing * static_cast<double>(rank), notes[rank], velocity });
            events.push_back({ release, notes[rank], 0.0f });
        }
    };
    chord(0.1, { 40, 47, 52, 56, 59, 64 }, 0.75f, 0.010, 1.15);
    chord(1.2, { 64, 60, 57, 52 }, 0.5f, 0.008, 2.15);
    const std::array<std::pair<int, float>, 5> singles {
        { { 45, 0.4f }, { 52, 0.9f }, { 69, 0.6f }, { 76, 1.0f }, { 48, 0.25f } } };
    double at = 2.2;
    for (const auto& [note, velocity] : singles)
    {
        events.push_back({ at, note, velocity });
        events.push_back({ at + 0.36, note, 0.0f });
        at += 0.4;
    }
    chord(4.3, { 43, 47, 50, 55, 59, 67 }, 0.3f, 0.012, 6.2);
    std::stable_sort(events.begin(), events.end(),
                     [] (const Event& a, const Event& b) { return a.seconds < b.seconds; });
    return events;
}

double loudnessOf(acustra::EngineParameters parameters)
{
    // On the heap: the engine is larger than a worker thread's stack.
    const auto owned = std::make_unique<acustra::AcustraEngine>();
    auto& engine = *owned;
    engine.setParameters(parameters);
    engine.prepare(sampleRate, block);
    const auto events = phrase();
    const auto total = static_cast<std::size_t>(6.8 * sampleRate);
    std::vector<float> left(total), right(total);
    std::size_t next = 0;
    for (std::size_t offset = 0; offset < total; offset += block)
    {
        while (next < events.size()
               && events[next].seconds * sampleRate < static_cast<double>(offset + block))
        {
            if (events[next].velocity > 0.0f)
                engine.noteOn(events[next].note, events[next].velocity);
            else
                engine.noteOff(events[next].note);
            ++next;
        }
        const int count = static_cast<int>(std::min<std::size_t>(block, total - offset));
        engine.process(left.data() + offset, right.data() + offset, count);
    }
    return integratedLoudness(left, right);
}

std::string describe(const acustra::EngineParameters& p)
{
    static const char* shapes[] { "Parlor", "Auditorium", "Dreadnought", "Jumbo" };
    static const char* woods[] { "Spruce", "Mahogany", "Maple" };
    static const char* pickings[] { "Finger", "Pick", "Thumb" };
    std::string text = shapes[static_cast<int>(p.shape)];
    text += ' ';
    text += woods[static_cast<int>(p.bodyMaterial)];
    text += ' ';
    text += pickings[static_cast<int>(p.picking)];
    if (p.capture == acustra::CaptureType::MonoMic)
        text += ", mono mic";
    else if (p.capture == acustra::CaptureType::Piezo)
        text += ", piezo";
    return text;
}

std::vector<acustra::EngineParameters> constructions()
{
    std::vector<acustra::EngineParameters> result;
    for (int shape = 0; shape < 4; ++shape)
        for (int wood = 0; wood < 3; ++wood)
        {
            acustra::EngineParameters p;
            p.shape = static_cast<acustra::BodyShape>(shape);
            p.bodyMaterial = static_cast<acustra::BodyMaterial>(wood);
            result.push_back(p);
        }
    return result;
}

#ifndef ACUSTRA_CONSTRUCTION_LOUDNESS_BEFORE
// The table's own shape: one cell per construction and Picking, and the
// default construction's microphone cells exactly 1, so the default patch
// renders as it did.
void testTheTableLeavesTheDefault()
{
    using namespace acustra;
    const auto cell = [] (int shape, int wood, int picking)
    {
        return static_cast<std::size_t>((shape * 3 + wood) * 3 + picking);
    };
    static_assert(detail::constructionMicReference.size() == 4 * 3 * 3
                      && detail::constructionMonoTrim.size() == 4 * 3 * 3
                      && detail::constructionPiezoTrim.size() == 4 * 3 * 3,
                  "the construction tables are not one cell per Shape, Wood and Picking");
    EngineParameters defaultParameters;
    const auto defaults = cell(static_cast<int>(defaultParameters.shape),
                               static_cast<int>(defaultParameters.bodyMaterial),
                               static_cast<int>(defaultParameters.picking));
    expect(defaults == cell(2, 0, 0), "the default construction moved");
    expect(detail::constructionMicReference[defaults] == 1.0f
               && detail::constructionMonoTrim[defaults] == 1.0f,
           "the default construction's microphone levels are not exactly 1");
    bool finite = true;
    for (const auto* table : { &detail::constructionMicReference,
                               &detail::constructionMonoTrim,
                               &detail::constructionPiezoTrim })
        for (const float value : *table)
            finite = finite && std::isfinite(value) && value > 0.1f && value < 10.0f;
    expect(finite, "a construction level is out of range");
}
#endif

void testEveryConstructionAndPickingPlaysAtOneLoudness()
{
    std::vector<acustra::EngineParameters> cases { acustra::EngineParameters {} };
    int index = 0;
    for (auto p : constructions())
        for (const auto picking : { acustra::PickingTechnique::Finger,
                                    acustra::PickingTechnique::Pick,
                                    acustra::PickingTechnique::Thumb })
        {
            p.picking = picking;
            p.capture = acustra::CaptureType::StereoMic;
            cases.push_back(p);
            // The mono microphone and the piezo meet the same target: a
            // spread of constructions and Pickings, every capture in turn.
            if (index % 5 == 0)
            {
                p.capture = acustra::CaptureType::MonoMic;
                cases.push_back(p);
            }
            if (index % 5 == 2)
            {
                p.capture = acustra::CaptureType::Piezo;
                cases.push_back(p);
            }
            ++index;
        }
    std::vector<double> loudness(cases.size());
    std::atomic<std::size_t> next { 0 };
    std::vector<std::thread> workers;
    const unsigned threads = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    for (unsigned t = 0; t < threads; ++t)
        workers.emplace_back([&]
        {
            for (std::size_t i = next++; i < cases.size(); i = next++)
                loudness[i] = loudnessOf(cases[i]);
        });
    for (auto& worker : workers)
        worker.join();

    const double target = loudness[0];
    std::cout << "Acustra construction loudness: default " << target << " LUFS\n";
    expect(target > -40.0 && target < -10.0, "the default construction's loudness is implausible");
    // The calibration holds its own phrases within +-1 LU; this shorter,
    // different phrase weighs strums, single notes and chords otherwise, and
    // is held within +-3 (before the construction levels it spread over
    // about 15 LU).
    constexpr double tolerance = 3.0;
    double lowest = 0.0, highest = 0.0;
    std::string quietest, loudest;
    for (std::size_t i = 1; i < cases.size(); ++i)
    {
        const double offset = loudness[i] - target;
        if (offset < lowest)
        {
            lowest = offset;
            quietest = describe(cases[i]);
        }
        if (offset > highest)
        {
            highest = offset;
            loudest = describe(cases[i]);
        }
        expect(std::abs(offset) <= tolerance,
               describe(cases[i]) + " plays " + std::to_string(offset) + " LU from the default");
    }
    std::cout << "Acustra construction loudness spread over " << cases.size() - 1
              << " renders: " << lowest << " .. " << highest << " LU (quietest "
              << quietest << ", loudest " << loudest << ")\n";
}
} // namespace

int main()
{
#ifndef ACUSTRA_CONSTRUCTION_LOUDNESS_BEFORE
    testTheTableLeavesTheDefault();
#endif
    testEveryConstructionAndPickingPlaysAtOneLoudness();
    if (failures != 0)
    {
        std::cerr << failures << " construction loudness check(s) failed\n";
        return 1;
    }
    std::cout << "Acustra construction loudness tests passed\n";
    return 0;
}
