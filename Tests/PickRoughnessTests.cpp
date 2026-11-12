// The restrained pick burst preserves the pitched release, other playing
// techniques, deterministic performance draws and the existing controls.
#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
int failures = 0;
void expect(bool ok, const char* message)
{
    if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
std::vector<float> render(acustra::PickingTechnique technique, bool restrained,
                         bool silentBurst, int note, float velocity,
                         int rate = 48000, int block = 64,
                         bool releaseVelocity = true)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.restrainedPickRoughness = restrained;
    engine->setPerformanceRealism(realism);
    auto calibration = acustra::fittedPhysicalCalibration;
    if (silentBurst) calibration.steel.transientScale = 0.0f;
    if (!releaseVelocity) calibration.pickReleaseVelocityShare = 0.0f;
    engine->setPhysicalCalibration(calibration);
    acustra::EngineParameters parameters;
    parameters.picking = technique;
    parameters.outputGain = 1.0f;
    parameters.piezoMix = 0.2f;
    parameters.room = 0.5f;
    parameters.releaseNoise = 0.1f;
    engine->setParameters(parameters);
    engine->prepare(rate, block);
    const int frames = rate / 2;
    std::vector<float> audio(static_cast<std::size_t>(2 * frames));
    std::vector<float> left(static_cast<std::size_t>(block));
    std::vector<float> right(static_cast<std::size_t>(block));
    std::array<int, 2> contacts { rate / 10, rate * 3 / 10 };
    int next = 0;
    for (int frame = 0; frame < frames;)
    {
        if (next < 2 && frame == contacts[static_cast<std::size_t>(next)])
        {
            engine->noteOn(note, velocity);
            ++next;
        }
        int count = std::min(block, frames - frame);
        if (next < 2) count = std::min(count, contacts[static_cast<std::size_t>(next)] - frame);
        engine->process(left.data(), right.data(), count);
        for (int i = 0; i < count; ++i)
        {
            audio[static_cast<std::size_t>(2 * (frame + i))] = left[static_cast<std::size_t>(i)];
            audio[static_cast<std::size_t>(2 * (frame + i) + 1)] = right[static_cast<std::size_t>(i)];
        }
        frame += count;
    }
    return audio;
}
bool same(const std::vector<float>& a, const std::vector<float>& b)
{ return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0; }
double energy(const std::vector<float>& a)
{
    double result = 0.0;
    for (float x : a) result += double(x) * x;
    return result;
}
double attackHighBand(const std::vector<float>& audio)
{
    // Independent Hann-windowed DFT over 15 ms. Neither the burst filter's
    // coefficients nor its envelope are used to construct this observer.
    constexpr int count = 720, start = 4800;
    constexpr double pi = 3.14159265358979323846;
    std::array<double, count> windowed {};
    for (int i = 0; i < count; ++i)
        windowed[static_cast<std::size_t>(i)] = 0.5
            * (audio[static_cast<std::size_t>(2 * (start + i))]
               + audio[static_cast<std::size_t>(2 * (start + i) + 1)])
            * (0.5 - 0.5 * std::cos(2.0 * pi * i / (count - 1)));
    double result = 0.0;
    for (int bin = 60; bin <= 240; ++bin)
    {
        double real = 0.0, imaginary = 0.0;
        for (int i = 0; i < count; ++i)
        {
            const double phase = 2.0 * pi * bin * i / count;
            real += windowed[static_cast<std::size_t>(i)] * std::cos(phase);
            imaginary += windowed[static_cast<std::size_t>(i)] * std::sin(phase);
        }
        result += real * real + imaginary * imaginary;
    }
    return result;
}
}
int main()
{
    using acustra::PickingTechnique;
    for (int rate : { 44100, 48000, 96000 })
    {
        for (auto technique : { PickingTechnique::Finger, PickingTechnique::Thumb, PickingTechnique::Pick })
        {
            const bool silentBurst = technique == PickingTechnique::Pick;
            const auto before = render(technique, false, silentBurst, 52, 0.75f, rate);
            const auto after = render(technique, true, silentBurst, 52, 0.75f, rate);
            expect(same(before, after), "non-pick or burst-free pitched release changed");
        }
        const auto audio = render(PickingTechnique::Pick, true, false, 64, 1.0f, rate);
        expect(std::all_of(audio.begin(), audio.end(), [](float x) { return std::isfinite(x); }),
               "pick roughness produced non-finite audio");
    }
    // The restraint is read twice: in the shipping pluck, and against the
    // string with no release velocity, where the burst is all it changes.
    // Since the release velocity points the way the pick carried the string
    // (writePickRelease, 2026-11-09) it fills more of this band on a loud
    // stroke, so the restraint's share of it fell: the shipping median moved
    // from -0.68 dB (1.9.35) to -0.47, every case still below -0.1. Its bound
    // was re-derived from -0.5 to -0.4 for that reason, and the burst's own
    // change, -3.17 dB on both versions, is held to -2.5.
    // 1.9.42 moved the Pick's hand from 60 mm to 141 mm from the saddle. The
    // string itself then puts less into this band and the burst is more of
    // it, so the restraint takes more: the shipping median is -1.65 dB and
    // the burst's own -5.16, and their bounds are re-derived to -1.2 and
    // -4.0. Two things moved the other way with the same cause. On the soft
    // open E (velocity 40) the release velocity now carries the band and the
    // restraint reads -0.002 dB against it, -3.13 against the string alone,
    // so each case is held to no rise rather than to -0.1. And the burst is
    // more of a loud high note's whole energy, so the restraint takes up to
    // 0.28 dB of it (0.11 before); that bound is 0.35.
    std::vector<double> shipping, alone;
    for (int note : { 40, 52, 64, 76 })
        for (float velocity : { 40.0f / 127.0f, 80.0f / 127.0f, 120.0f / 127.0f })
        {
            const auto before = render(PickingTechnique::Pick, false, false, note, velocity);
            const auto after = render(PickingTechnique::Pick, true, false, note, velocity);
            const double highDb = 10.0 * std::log10(attackHighBand(after) / attackHighBand(before));
            const double levelDb = 10.0 * std::log10(energy(after) / energy(before));
            shipping.push_back(highDb);
            expect(highDb < 0.0, "pick's early high-band roughness was not reduced");
            expect(std::abs(levelDb) < 0.35, "pick refinement changed overall level excessively");
            const auto string = render(PickingTechnique::Pick, false, false, note, velocity,
                                       48000, 64, false);
            const auto restrainedString = render(PickingTechnique::Pick, true, false, note,
                                                 velocity, 48000, 64, false);
            alone.push_back(10.0 * std::log10(attackHighBand(restrainedString)
                                              / attackHighBand(string)));
        }
    // Twelve cases have no middle one: the median is the mean of the sixth
    // and seventh. Element [6] alone read the seventh, -0.54 dB at 1.9.9 on
    // note 76 at velocity 120, which the Pick's smaller glide excursion moves
    // to -0.50 while the median moves from -0.76 to -0.73.
    const auto medianOf = [] (std::vector<double> values)
    {
        std::sort(values.begin(), values.end());
        return 0.5 * (values[values.size() / 2 - 1] + values[values.size() / 2]);
    };
    const double median = medianOf(shipping);
    const double burstMedian = medianOf(alone);
    std::cout << "Pick roughness median early 4-16 kHz change: " << median
              << " dB in the shipping pluck, " << burstMedian
              << " dB against the string alone\n";
    expect(median < -1.2, "median high-band change is too small to review");
    expect(burstMedian < -4.0, "the burst's own high-band restraint is too small to review");
    const auto a = render(PickingTechnique::Pick, true, false, 64, 0.8f, 48000, 32);
    const auto b = render(PickingTechnique::Pick, true, false, 64, 0.8f, 48000, 127);
    expect(same(a, b), "restrained pick depends on callback partition");
    if (!failures) std::cout << "Pick roughness isolation, attack and block contracts passed\n";
    return failures ? 1 : 0;
}
