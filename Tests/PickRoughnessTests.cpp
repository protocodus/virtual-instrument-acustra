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
                         int rate = 48000, int block = 64)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::PerformanceRealism realism;
    realism.restrainedPickRoughness = restrained;
    engine->setPerformanceRealism(realism);
    auto calibration = acustra::fittedPhysicalCalibration;
    if (silentBurst) calibration.steel.transientScale = 0.0f;
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
    std::vector<double> changes;
    for (int note : { 40, 52, 64, 76 })
        for (float velocity : { 40.0f / 127.0f, 80.0f / 127.0f, 120.0f / 127.0f })
        {
            const auto before = render(PickingTechnique::Pick, false, false, note, velocity);
            const auto after = render(PickingTechnique::Pick, true, false, note, velocity);
            const double highDb = 10.0 * std::log10(attackHighBand(after) / attackHighBand(before));
            const double levelDb = 10.0 * std::log10(energy(after) / energy(before));
            changes.push_back(highDb);
            expect(highDb < -0.1, "pick's early high-band roughness was not reduced");
            expect(std::abs(levelDb) < 0.2, "pick refinement changed overall level excessively");
        }
    // Twelve cases have no middle one: the median is the mean of the sixth
    // and seventh. changes[6] alone read the seventh, -0.54 dB at 1.9.9 on
    // note 76 at velocity 120, which the Pick's smaller glide excursion moves
    // to -0.50 while the median moves from -0.76 to -0.73.
    std::sort(changes.begin(), changes.end());
    const double median = 0.5 * (changes[changes.size() / 2 - 1] + changes[changes.size() / 2]);
    expect(median < -0.5, "median high-band change is too small to review");
    const auto a = render(PickingTechnique::Pick, true, false, 64, 0.8f, 48000, 32);
    const auto b = render(PickingTechnique::Pick, true, false, 64, 0.8f, 48000, 127);
    expect(same(a, b), "restrained pick depends on callback partition");
    if (!failures) std::cout << "Pick roughness isolation, attack and block contracts passed\n";
    return failures ? 1 : 0;
}
