#include "DSP/AcustraPerformer.h"
#include "DSP/AcustraPresets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
struct Event
{
    int frame, key, velocity;
    int channel { 1 };
};

using Frame = std::array<float, 3>;
using Audio = std::vector<Frame>;
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

Audio play(acustra::Performer& performer, const std::vector<Event>& events,
           int frames, int blockSize)
{
    Audio result(static_cast<std::size_t>(frames));
    std::array<float, 257> left {}, right {}, piezo {};
    std::size_t next = 0;
    for (int start = 0; start < frames; start += blockSize)
    {
        const int count = std::min(blockSize, frames - start);
        performer.beginBlock(left.data(), right.data(), { piezo.data() }, count);
        while (next < events.size() && events[next].frame < start + count)
        {
            const auto& event = events[next++];
            const int offset = event.frame - start;
            if (event.key < 0)
                performer.controlChange(offset, event.channel, 120, 0);
            else if (event.velocity == 0)
                performer.noteOff(offset, event.channel, event.key);
            else
                performer.noteOn(offset, event.channel, event.key, event.velocity);
        }
        performer.endBlock();
        for (int frame = 0; frame < count; ++frame)
            result[static_cast<std::size_t>(start + frame)] =
                { left[frame], right[frame], piezo[frame] };
    }
    return result;
}

std::unique_ptr<acustra::Performer> fresh(int rate)
{
    auto performer = std::make_unique<acustra::Performer>();
    performer->prepare(rate, 257);
    return performer;
}

double power(const Audio& audio, int channel)
{
    double sum = 0.0;
    for (const auto& frame : audio)
        sum += static_cast<double>(frame[channel]) * frame[channel];
    return sum;
}

void testSoundPaletteAndVelocity()
{
    using acustra::Performer;
    for (const int rate : { 8000, 48000, 96000 })
    {
        std::array<Audio, Performer::noiseKeys.size()> loud {};
        for (std::size_t gesture = 0; gesture < Performer::noiseKeys.size(); ++gesture)
        {
            const int key = Performer::noiseKeys[gesture];
            std::array<double, 3> previous {};
            for (int velocity : { 32, 72, 116 })
            {
                auto performer = fresh(rate);
                auto audio = play(*performer, {{ 17, key, velocity }}, rate / 3, 64);
                const int delay = performer->engine().outputLatencySamples();
                expect(std::all_of(audio.begin(), audio.begin() + 17 + delay,
                           [](const Frame& frame) { return frame == Frame {}; }),
                       "a contact spoke before the MIDI frame and output delay");
                bool bounded = true;
                for (const auto& frame : audio)
                    for (float sample : frame)
                        bounded = bounded && std::isfinite(sample) && std::abs(sample) < 1.0f;
                expect(bounded, "one-shot output exceeded headroom or became nonfinite");
                for (int channel = 0; channel < 3; ++channel)
                {
                    const double energy = power(audio, channel);
                    if (!(energy > 1.0e-8 && energy > 1.6 * previous[channel]))
                    {
                        std::cerr << rate << " Hz, MIDI " << key << " v" << velocity
                                  << ", output " << channel << ", energy " << energy
                                  << " after " << previous[channel] << '\n';
                        expect(false, "one-shot lost velocity dynamics or an output route");
                    }
                    previous[channel] = energy;
                }
                expect(performer->engine().getActiveVoiceCount() == 0,
                       "noise key took a pitched string");
                if (velocity == 116)
                    loud[gesture] = std::move(audio);
            }
        }
        for (std::size_t a = 0; a < loud.size(); ++a)
            for (std::size_t b = a + 1; b < loud.size(); ++b)
            {
                const double first = std::sqrt(power(loud[a], 0));
                const double second = std::sqrt(power(loud[b], 0));
                double difference = 0.0;
                for (std::size_t frame = 0; frame < loud[a].size(); ++frame)
                {
                    const double delta = loud[a][frame][0] / first - loud[b][frame][0] / second;
                    difference += delta * delta;
                }
                if (difference <= 0.16)
                    std::cerr << rate << " Hz gestures " << a << '/' << b
                              << ", normalized distance " << difference << '\n';
                expect(difference > 0.16, "two gesture types differ only in level");
            }
        double quietestBody = power(loud[1], 0) + power(loud[1], 1);
        for (std::size_t gesture = 2; gesture < loud.size(); ++gesture)
            quietestBody = std::min(quietestBody,
                power(loud[gesture], 0) + power(loud[gesture], 1));
        expect(power(loud[0], 0) + power(loud[0], 1) < 0.2 * quietestBody,
               "fretting-hand movement is too loud relative to body percussion");
    }
}

void testTimingAndOwnership()
{
    constexpr int rate = 48000;
    constexpr int key = acustra::Performer::noiseKeys[0];
    const std::vector<Event> taps {{ 15, key, 96 }, { 229, key, 0 }, { 7100, key, 96 }};
    auto a = fresh(rate);
    auto b = fresh(rate);
    expect(a->engine().suspend(), "fresh engine could not sleep");
    a->setStrokeMode(acustra::Performer::StrokeMode::Keys);
    const auto rendered = play(*a, taps, rate / 2, 31);
    expect(rendered == play(*b, {{ 15, key, 96 }, { 7100, key, 96 }}, rate / 2, 257),
           "sleep, key-up, Keys mode or block boundaries changed one-shots");
    expect(a->strokeMode() == acustra::Performer::StrokeMode::Keys
               && a->droppedEventCount() == 0,
           "one-shots changed the strum controller or dropped MIDI");
    expect(!std::equal(rendered.begin() + 15, rendered.begin() + 6015,
                       rendered.begin() + 7100),
           "repeating the same contact replayed the same attack");

    auto strings = fresh(rate);
    auto mixed = fresh(rate);
    strings->setGatherChords(true);
    mixed->setGatherChords(true);
    static_cast<void>(play(*strings, {{ 20, 52, 98 }, { 61, 57, 90 }, { 123, 64, 88 }},
                           rate / 5, 64));
    const auto mix = play(*mixed, {{ 20, 52, 98 }, { 61, 57, 90 }, { 100, key, 103 },
                                   { 123, 64, 88 }}, rate / 5, 257);
    const auto before = strings->engine().getStringActivity();
    const auto after = mixed->engine().getStringActivity();
    for (std::size_t i = 0; i < before.size(); ++i)
        expect(before[i].midiNote == after[i].midiNote
                   && before[i].keyDown == after[i].keyDown
                   && before[i].level == after[i].level,
               "one-shot changed a gathered chord or its string energy");
    expect(power(mix, 0) > 0.0, "gathered score lost its output");

    auto panic = fresh(rate);
    const auto stopped = play(*panic, {{ 0, key, 127 }, { 2400, -1, 0 }}, 7200, 64);
    expect(std::all_of(stopped.begin() + 2400, stopped.end(),
                       [](const Frame& frame) { return frame == Frame {}; }),
           "All Sound Off left a contact or resonant tail");
}

void testBoundedVariation()
{
    acustra::GuitarNoises noises;
    for (int gesture = 0; gesture < static_cast<int>(acustra::GuitarNoise::Count); ++gesture)
    {
        std::array<float, 48> fingerprint {};
        for (auto& value : fingerprint)
        {
            noises.trigger(static_cast<acustra::GuitarNoise>(gesture), 0.72f, 1, 48000.0);
            double weighted = 0.0;
            int index = 0;
            while (noises.active() && index < 24000)
            {
                const auto frame = noises.render();
                weighted += frame.force * (1 + index);
                ++index;
            }
            expect(!noises.active() && index > 0 && std::isfinite(weighted),
                   "contact excitation exceeded its finite duration");
            value = static_cast<float>(weighted);
        }
        std::sort(fingerprint.begin(), fingerprint.end());
        expect(std::adjacent_find(fingerprint.begin(), fingerprint.end()) == fingerprint.end(),
               "contact series repeated a finite set of fingerprints");
    }
}

void testOverlappingNoiseBursts()
{
    for (int rate : { 8000, 48000, 192000 })
    {
        auto performer = fresh(rate);
        auto parameters = acustra::EngineParameters {};
        parameters.capture = acustra::CaptureType::Piezo;
        parameters.outputGain = 4.0f;
        performer->setParameters(parameters);
        performer->prepare(rate, 257);
        std::vector<Event> bursts;
        for (int hit = 0; hit < 50; ++hit)
            bursts.push_back({ hit * (rate / 500), acustra::Performer::noiseKeys[
                static_cast<std::size_t>(hit) % acustra::Performer::noiseKeys.size()], 127 });
        const auto audio = play(*performer, bursts, rate / 2, 257);
        const float ceiling = rate < 44100 ? 0.630958f : 0.841398f;
        const bool bounded = std::all_of(audio.begin(), audio.end(),
            [ceiling](const Frame& frame) {
                return std::isfinite(frame[2]) && std::abs(frame[2]) <= ceiling
                    && frame[0] == frame[2] && frame[1] == frame[2];
            });
        expect(bounded && power(audio, 2) > 0.0,
               "dense contacts broke the protected Piezo and Main routing");
        expect(performer->droppedEventCount() == 0
                   && performer->engine().getActiveVoiceCount() == 0,
               "contact burst dropped MIDI or activated a string");
    }
}

void testFretMotionAndKeyMap()
{
    using acustra::GuitarNoise;
    using acustra::Performer;
    auto keys = Performer::noiseKeys;
    std::sort(keys.begin(), keys.end());
    expect(std::adjacent_find(keys.begin(), keys.end()) == keys.end(),
           "two gestures share a MIDI key");
    for (int key : keys)
        expect(!Performer::isKeyswitch(key) && key != 28 && key < 38,
               "a noise key replaced an existing performance or playable key");

    const auto motion = [](float velocity)
    {
        acustra::GuitarNoises noises;
        noises.trigger(GuitarNoise::FretMove, velocity, 1, 48000.0);
        int samples = 0;
        double energy = 0.0;
        while (noises.active() && samples < 48000)
        {
            const auto frame = noises.render();
            energy += frame.air * frame.air;
            ++samples;
        }
        return std::pair<int, double> { samples, energy };
    };
    const auto slow = motion(32.0f / 127.0f);
    const auto fast = motion(116.0f / 127.0f);
    expect(slow.first > 2 * fast.first && slow.second < fast.second,
           "slow travel should last longer and be quieter than fast travel");

    acustra::GuitarNoises noise;
    noise.trigger(GuitarNoise::Count, 1.0f, 1, 48000.0);
    noise.trigger(GuitarNoise::FingerKnock, 1.0f, 1, 0.0);
    expect(!noise.active(), "invalid gesture or rate started a contact");
}

// Fresh-engine comparisons reuse a contact's random draw. This sequence
// also verifies that ordinary independent hits retain clear velocity steps.
void testVelocityWithSuccessiveContactVariation()
{
    constexpr int rate = 96000; // Rack's default 2x at a 48 kHz host
    auto performer = fresh(rate);
    auto parameters = acustra::engineParameters(acustra::factoryPresets[1]);
    parameters.room = 0.0f;
    performer->setParameters(parameters);
    performer->prepare(rate, 257);
    constexpr std::array<double, 6> offsets {{ 0.20, 1.12, 2.04, 3.03, 3.60, 4.17 }};
    constexpr std::array<int, 6> velocities {{ 38, 75, 116, 96, 96, 96 }};
    std::vector<Event> events;
    for (std::size_t gesture = 0; gesture < acustra::Performer::noiseKeys.size(); ++gesture)
        for (std::size_t hit = 0; hit < offsets.size(); ++hit)
        {
            const int frame = static_cast<int>((5.0 * gesture + offsets[hit]) * rate + 0.5);
            const int key = acustra::Performer::noiseKeys[gesture];
            events.push_back({ frame, key, velocities[hit] });
            events.push_back({ frame + 8640, key, 0 });
        }
    const auto audio = play(*performer, events, 30 * rate, 257);
    for (std::size_t gesture = 0; gesture < acustra::Performer::noiseKeys.size(); ++gesture)
    {
        double previous = 0.0;
        for (std::size_t hit = 0; hit < 3; ++hit)
        {
            const int start = static_cast<int>((5.0 * gesture + offsets[hit]) * rate + 0.5);
            double energy = 0.0;
            for (int frame = start; frame < start + rate / 3; ++frame)
                energy += audio[frame][0] * audio[frame][0]
                    + audio[frame][1] * audio[frame][1];
            if (!(energy > previous * 1.58))
                std::cerr << "Successive gesture " << gesture << " v" << velocities[hit]
                          << " energy " << energy << " after " << previous << '\n';
            expect(energy > previous * 1.58,
                   "independent contact variation obscured an ordinary velocity step");
            previous = energy;
        }
    }
}
}

int main()
{
    testSoundPaletteAndVelocity();
    testTimingAndOwnership();
    testBoundedVariation();
    testOverlappingNoiseBursts();
    testFretMotionAndKeyMap();
    testVelocityWithSuccessiveContactVariation();
    if (failures == 0)
        std::cout << "Six synthetic contacts pass level, variety, timing and isolation checks.\n";
    return failures == 0 ? 0 : 1;
}
