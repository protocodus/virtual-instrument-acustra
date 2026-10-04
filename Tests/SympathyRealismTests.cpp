#include "DSP/AcustraEngine.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static std::vector<float> waves(const AcustraEngine& engine, int string)
    {
        const auto& voice = engine.voices_[static_cast<std::size_t>(string)];
        std::vector<float> result;
        for (const auto& loop : voice.loops)
            result.insert(result.end(), loop.delay.begin(), loop.delay.end());
        return result;
    }
    static float delay(const AcustraEngine& engine, int string)
    {
        return engine.voices_[static_cast<std::size_t>(string)].loops[0].targetDelay;
    }
    static bool idle(const AcustraEngine& engine, int string)
    {
        return !engine.voices_[static_cast<std::size_t>(string)].played;
    }
};
}
namespace
{
int failures = 0;
void expect(bool pass, const std::string& message)
{
    if (!pass) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void process(acustra::AcustraEngine& engine, int frames)
{
    std::array<float, 127> left {}, right {};
    while (frames > 0)
    {
        const int count = std::min(frames, static_cast<int>(left.size()));
        engine.process(left.data(), right.data(), count);
        frames -= count;
    }
}
void testTuningKeepsIdleWaves(double rate, float pressure)
{
    using Access = acustra::AcustraEngineTestAccess;
    auto engine = std::make_unique<acustra::AcustraEngine>();
    acustra::EngineParameters parameters;
    engine->setParameters(parameters);
    engine->prepare(rate, 127);
    engine->setStringPerChannelMode(true);
    engine->setPalmMutePressure(pressure);
    engine->noteOn(64, 0.9f, 6); // bridge excites five unplayed strings
    process(*engine, static_cast<int>(rate * 0.2));
    std::array<std::vector<float>, 5> before;
    for (int string = 0; string < 5; ++string)
    {
        expect(Access::idle(*engine, string), "sympathy probe stole an idle string");
        before[static_cast<std::size_t>(string)] = Access::waves(*engine, string);
        double energy = 0.0;
        for (float value : before[static_cast<std::size_t>(string)])
            energy += double(value) * value;
        expect(energy > 1.0e-14, "shared bridge did not excite the idle-string probe");
    }
    const float oldDelay = Access::delay(*engine, 0);
    parameters.tuning = acustra::Tuning::DropD;
    engine->setParameters(parameters);
    for (int string = 0; string < 5; ++string)
        expect(Access::waves(*engine, string) == before[static_cast<std::size_t>(string)],
            "live Drop D erased a sympathetic wave on string " + std::to_string(string)
                + " at " + std::to_string(rate) + " Hz, pressure " + std::to_string(pressure));
    expect(Access::delay(*engine, 0) > oldDelay, "idle low E did not retune to D");
    process(*engine, static_cast<int>(rate * 0.02));
    engine->reset();
    for (int string = 0; string < 6; ++string)
    {
        const auto cleared = Access::waves(*engine, string);
        expect(std::all_of(cleared.begin(), cleared.end(), [] (float value) { return value == 0.0f; }),
               "reset stopped clearing the passive strings");
    }
}
}
int main()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (float pressure : { 0.0f, 0.7f })
            testTuningKeepsIdleWaves(rate, pressure);
    if (failures == 0) std::cout << "Sympathetic retuning continuity tests passed\n";
    return failures == 0 ? 0 : 1;
}
