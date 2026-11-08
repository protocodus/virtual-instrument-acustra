// Factory output calibration through the production Performer, at 48 kHz in
// 64-frame blocks. Each normal case starts with a fresh Performer/production
// RNG state, settles for 0.5 s, strikes at 0.02 s, releases at 0.65 s and runs
// for 0.8 s. The 27 cases are six open singles, alternating-string triads
// {0,2,4}/{1,3,5}, and four strings {0,1,3,5}, each at velocities 70/90/110.
// Palm Muted Steel Pick receives CC2=70, as in its demonstration performance.
//
// Independently measure maximum 300 ms rectangular-window stereo-mean RMS,
// convert by 20log10(RMS)+12 to Reason's default VU reference, then take the
// median of the 27 cases. Main should be -9 +/-0.75 VU. The separate pickup's
// median is reported, not forced to match a microphone capture. All normal
// and stress cases must stay finite and below the final SAMPLE ceiling;
// this test does not claim a true-peak bound or impose a VU ceiling on abuse.
// The held/repeated/bent open/major chords and maximum-output corner follow
// the external 2026-11-08 output-level measurement protocol. No offline gain,
// timing-dependent gate, file I/O, or test-owned allocation occurs in audio.
#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"
#include "DSP/OutputStage.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace
{
std::atomic<long> allocationCount { 0 };
}

void* operator new(std::size_t size)
{
    ++allocationCount;
    if (void* pointer = std::malloc(size == 0 ? 1 : size))
        return pointer;
    throw std::bad_alloc {};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace
{
constexpr int rate = 48000;
constexpr int blockSize = 64;
constexpr int meterFrames = 14400; // 300 ms, independent of the output detector
constexpr double targetVu = -9.0;
constexpr double calibrationToleranceVu = 0.75;
constexpr float sampleCeiling = protocodus::OutputStage<2>::kCeiling;
int failures = 0;

void expect(bool pass, const std::string& message)
{
    if (!pass)
    {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

struct Case
{
    std::string name;
    std::vector<int> notes;
    int velocity { 127 };
    bool forcePick { false };
    bool repeat { false };
    bool bend { false };
    bool maximumOutput { false };
    bool handLift { false };
    double duration { 0.8 };
};

enum class EventKind { Off, On, Bend };
struct Event
{
    int frame;
    EventKind kind;
    int note;
    int value;
};

struct Measurement
{
    double mainVu {}, louderChannelVu {}, piezoVu {};
    float mainPeak {}, piezoPeak {};
    bool finite { true };
    long audioAllocations {};
};

// Fixed-denominator, zero-prefilled moving power sum. Prefix windows cannot
// exceed the first full window because their powers are nonnegative. Since
// every render exceeds 300 ms, its maximum therefore equals the maximum
// over full windows used by the independent offline cumulative-sum analysis.
class Meter
{
public:
    Meter() : history_(meterFrames) {}

    void add(float left, float right, float piezo) noexcept
    {
        const std::array<float, 3> samples { left, right, piezo };
        for (std::size_t channel = 0; channel < samples.size(); ++channel)
        {
            const float sample = samples[channel];
            finite_ = finite_ && std::isfinite(sample);
            const double power = double(sample) * double(sample);
            sum_[channel] += power - history_[position_][channel];
            history_[position_][channel] = power;
            peak_[channel] = std::max(peak_[channel], std::abs(sample));
        }
        position_ = position_ + 1 == history_.size() ? 0 : position_ + 1;
        maximumMainPower_ = std::max(maximumMainPower_,
            0.5 * (sum_[0] + sum_[1]) / meterFrames);
        maximumChannelPower_ = std::max(maximumChannelPower_,
            std::max(sum_[0], sum_[1]) / meterFrames);
        maximumPiezoPower_ = std::max(maximumPiezoPower_, sum_[2] / meterFrames);
    }

    Measurement result() const noexcept
    {
        const auto vu = [] (double power)
        {
            return 10.0 * std::log10(std::max(power, 1.0e-60)) + 12.0;
        };
        return { vu(maximumMainPower_), vu(maximumChannelPower_), vu(maximumPiezoPower_),
                 std::max(peak_[0], peak_[1]), peak_[2], finite_, 0 };
    }

private:
    std::vector<std::array<double, 3>> history_;
    std::array<double, 3> sum_ {};
    std::array<float, 3> peak_ {};
    std::size_t position_ {};
    double maximumMainPower_ {}, maximumChannelPower_ {}, maximumPiezoPower_ {};
    bool finite_ { true };
};

Measurement renderCase(int preset, const Case& test)
{
    auto parameters = acustra::engineParameters(
        acustra::factoryPresets[static_cast<std::size_t>(preset)]);
    if (test.forcePick)
    {
        parameters.picking = acustra::PickingTechnique::Pick;
        parameters.touch = 1.0f;
        parameters.pluckPosition = 0.0f;
    }
    if (test.maximumOutput)
        parameters.outputGain = std::pow(10.0f, 6.0f / 20.0f);

    std::vector<Event> events;
    events.reserve(200);
    const auto strike = [&] (double seconds)
    {
        for (int note : test.notes)
            events.push_back({ static_cast<int>(seconds * rate + 0.5),
                               EventKind::On, note, test.velocity });
    };
    strike(0.02);
    if (test.repeat)
        for (int repeat = 1; repeat < 13; ++repeat)
        {
            const double seconds = 0.02 + 0.1 * repeat;
            for (int note : test.notes)
                events.push_back({ static_cast<int>(seconds * rate + 0.5) - 1,
                                   EventKind::Off, note, 0 });
            strike(seconds);
        }
    if (test.bend)
    {
        events.push_back({ static_cast<int>(0.35 * rate), EventKind::Bend, 0, 1 });
        events.push_back({ static_cast<int>(0.70 * rate), EventKind::Bend, 0, -1 });
        events.push_back({ static_cast<int>(1.05 * rate), EventKind::Bend, 0, 0 });
    }
    for (int note : test.notes)
        events.push_back({ static_cast<int>((test.duration - 0.15) * rate),
                           EventKind::Off, note, 0 });
    std::stable_sort(events.begin(), events.end(), [] (const Event& a, const Event& b)
        { return a.frame < b.frame; });

    // Preparation and fixture storage are deliberately outside the watched
    // audio region. Every case gets its own RNG and unshared dynamics state.
    auto player = std::make_unique<acustra::Performer>();
    player->setParameters(parameters);
    player->prepare(rate, blockSize);
    std::array<float, blockSize> left {}, right {}, piezo {};
    const acustra::AcustraEngine::OutputBuses buses { piezo.data() };
    Meter meter;
    std::size_t next = 0;
    const int frames = static_cast<int>(test.duration * rate + 0.5);
    const long allocationsBefore = allocationCount.load();
    for (int block = 0; block < (rate / 2 + blockSize - 1) / blockSize; ++block)
    {
        player->beginBlock(left.data(), right.data(), buses, blockSize);
        if (block == 0)
        {
            player->setPitchBendRange(0, 1, 12);
            if (preset == 4 && !test.handLift)
                player->controlChange(0, 1, 2, 70);
        }
        player->endBlock();
    }
    for (int first = 0; first < frames; first += blockSize)
    {
        const int count = std::min(blockSize, frames - first);
        player->beginBlock(left.data(), right.data(), buses, count);
        while (next < events.size() && events[next].frame < first + count)
        {
            const auto& event = events[next++];
            const int offset = event.frame - first;
            if (event.kind == EventKind::Bend)
                player->pitchWheel(offset, 1, static_cast<float>(event.value));
            else if (event.kind == EventKind::On)
                player->noteOn(offset, 1, event.note, event.value);
            else
                player->noteOff(offset, 1, event.note, 64);
        }
        player->endBlock();
        for (int frame = 0; frame < count; ++frame)
        {
            const auto i = static_cast<std::size_t>(frame);
            meter.add(left[i], right[i], piezo[i]);
        }
    }
    const long audioAllocations = allocationCount.load() - allocationsBefore;
    auto measurement = meter.result();
    measurement.audioAllocations = audioAllocations;
    return measurement;
}

void checkOutput(const Measurement& measurement, int preset, const Case& test)
{
    const std::string description = std::string(acustra::factoryPresets[
        static_cast<std::size_t>(preset)].name) + ": " + test.name;
    expect(measurement.finite, description + " produced nonfinite output");
    expect(measurement.mainPeak <= sampleCeiling + 1.0e-6f,
           description + " Main exceeds the sample ceiling");
    expect(measurement.piezoPeak <= sampleCeiling + 1.0e-6f,
           description + " direct Piezo exceeds the sample ceiling");
    expect(measurement.audioAllocations == 0,
           description + " allocated during audio processing");
}

void testFactoryLevels()
{
    for (int preset = 0; preset < static_cast<int>(acustra::factoryPresets.size()); ++preset)
    {
        const auto opens = acustra::AcustraEngine::openNotes(
            acustra::factoryPresets[static_cast<std::size_t>(preset)].tuning);
        std::vector<std::vector<int>> normalNotes;
        for (int note : opens)
            normalNotes.push_back({ note });
        normalNotes.push_back({ opens[0], opens[2], opens[4] });
        normalNotes.push_back({ opens[1], opens[3], opens[5] });
        normalNotes.push_back({ opens[0], opens[1], opens[3], opens[5] });
        std::array<double, 27> mainLevels {}, piezoLevels {};
        std::size_t normal = 0;
        for (int velocity : { 70, 90, 110 })
            for (std::size_t group = 0; group < normalNotes.size(); ++group)
            {
                Case test;
                test.name = "normal-v" + std::to_string(velocity) + "-n" + std::to_string(group);
                test.notes = normalNotes[group];
                test.velocity = velocity;
                const auto measurement = renderCase(preset, test);
                checkOutput(measurement, preset, test);
                mainLevels[normal] = measurement.mainVu;
                piezoLevels[normal++] = measurement.piezoVu;
            }
        expect(normal == mainLevels.size(), "normal calibration battery does not contain 27 cases");
        std::sort(mainLevels.begin(), mainLevels.end());
        std::sort(piezoLevels.begin(), piezoLevels.end());
        const double mainMedian = mainLevels[mainLevels.size() / 2];
        const double piezoMedian = piezoLevels[piezoLevels.size() / 2];
        expect(std::abs(mainMedian - targetVu) <= calibrationToleranceVu,
               std::string(acustra::factoryPresets[static_cast<std::size_t>(preset)].name)
                   + " normal Main median is " + std::to_string(mainMedian)
                   + " VU, outside -9 +/-0.75 VU");
        std::cout << acustra::factoryPresets[static_cast<std::size_t>(preset)].name
                  << " normal Main/Piezo medians: " << std::fixed << std::setprecision(3)
                  << mainMedian << " / " << piezoMedian << " VU\n";

        const std::vector<int> all(opens.begin(), opens.end());
        const std::vector<int> major { 40, 47, 52, 56, 59, 64 };
        const std::array<const char*, 4> forms { "open", "major", "repeat", "bend" };
        double hardestMain = -300.0, hardestChannel = -300.0, hardestPiezo = -300.0;
        float peakMain = 0.0f, peakPiezo = 0.0f;
        const auto stress = [&] (const Case& test)
        {
            const auto measurement = renderCase(preset, test);
            checkOutput(measurement, preset, test);
            hardestMain = std::max(hardestMain, measurement.mainVu);
            hardestChannel = std::max(hardestChannel, measurement.louderChannelVu);
            hardestPiezo = std::max(hardestPiezo, measurement.piezoVu);
            peakMain = std::max(peakMain, measurement.mainPeak);
            peakPiezo = std::max(peakPiezo, measurement.piezoPeak);
        };
        for (bool corner : { false, true })
            for (std::size_t form = 0; form < forms.size(); ++form)
            {
                Case test;
                test.name = std::string(corner ? "corner-" : "factory-") + forms[form];
                test.notes = form == 1 ? major : all;
                test.forcePick = corner;
                test.repeat = form == 2;
                test.bend = form == 3;
                test.duration = 1.6;
                stress(test);
            }
        for (bool lift : { false, true })
        {
            Case test;
            test.name = lift ? "max-output-hand-up" : "max-output";
            test.notes = all;
            test.forcePick = test.repeat = test.maximumOutput = true;
            test.handLift = lift;
            test.duration = 1.6;
            stress(test);
        }
        std::cout << "  stress Main/louder-channel/Piezo: " << hardestMain << " / "
                  << hardestChannel << " / " << hardestPiezo << " VU; sample peaks "
                  << peakMain << " / " << peakPiezo << '\n';
    }
}
}

int main()
{
    testFactoryLevels();
    if (failures == 0)
        std::cout << "All Acustra factory calibration, sample-ceiling and allocation checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
