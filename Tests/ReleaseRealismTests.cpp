// Explicit release gestures change hand loss without refretting, re-plucking,
// or altering the nominal/missing-velocity performance.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    static float releaseSeconds(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].releaseSeconds; }
    static float appliedGain(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].loops[0].appliedReleaseGain; }
    static double lineEnergy(const AcustraEngine& e, int string)
    {
        double result = 0.0;
        for (const auto& loop : e.voices_[static_cast<std::size_t>(string)].loops)
            for (float x : loop.delay) result += static_cast<double>(x) * x;
        return result;
    }
    static int note(const AcustraEngine& e, int string)
    { return e.voices_[static_cast<std::size_t>(string)].midiNote; }
};
}

namespace
{
int failures = 0;
void expect(bool ok, const std::string& message)
{ if (!ok) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }

std::vector<float> render(double rate, int note, float velocity)
{
    auto engine = std::make_unique<acustra::AcustraEngine>();
    engine->prepare(rate, 127);
    engine->setStringPerChannelMode(true);
    engine->setSympatheticStringsEnabled(false);
    engine->noteOn(note, 0.8f, 1);
    const int before = static_cast<int>(0.35 * rate);
    const int after = static_cast<int>(0.8 * rate);
    std::vector<float> left(static_cast<std::size_t>(before + after));
    std::vector<float> right(left.size());
    engine->process(left.data(), right.data(), before);
    if (velocity == -2.0f) engine->noteOff(note);
    else engine->noteOffWithVelocity(note, 1, velocity);
    engine->process(left.data() + before, right.data() + before, after);
    left.insert(left.end(), right.begin(), right.end());
    return left;
}

void nominalAndMonotonic()
{
    using Access = acustra::AcustraEngineTestAccess;
    for (double rate : { 44100.0, 48000.0, 96000.0 })
        for (int note : { 40, 43, 55 })
        {
            const auto nominal = render(rate, note, -2.0f);
            expect(nominal == render(rate, note, -1.0f), "missing velocity altered nominal output");
            expect(nominal == render(rate, note, 64.0f / 127.0f), "MIDI 64 altered nominal output");
            expect(nominal == render(rate, note, std::numeric_limits<float>::quiet_NaN()),
                   "non-finite velocity did not use nominal output");
            float previousTime = std::numeric_limits<float>::infinity();
            double previousEnergy = std::numeric_limits<double>::infinity();
            for (float velocity : { 0.0f, 0.25f, 64.0f / 127.0f, 0.75f, 1.0f })
            {
                auto e = std::make_unique<acustra::AcustraEngine>();
                e->prepare(rate, 127);
                e->setStringPerChannelMode(true);
                e->setSympatheticStringsEnabled(false);
                e->noteOn(note, 0.8f);
                std::array<float, 127> left {}, right {};
                for (int i = 0; i < 100; ++i) e->process(left.data(), right.data(), 127);
                const double before = Access::lineEnergy(*e, 0);
                const float oldGain = Access::appliedGain(*e, 0);
                e->noteOffWithVelocity(note, 1, velocity);
                const float time = Access::releaseSeconds(*e, 0);
                expect(time < previousTime, "higher release velocity did not shorten hand T60");
                previousTime = time;
                expect(Access::lineEnergy(*e, 0) == before, "key-up wrote a new wave into the delay lines");
                expect(Access::appliedGain(*e, 0) == oldGain, "key-up stepped applied hand gain");
                expect(Access::note(*e, 0) == note, "key-up refretted the vibrating string");
                expect(e->heldString(note) < 0, "key-up retained held ownership");
                const int trips = static_cast<int>(0.025 * rate / 127.0);
                for (int i = 0; i < trips; ++i)
                {
                    e->process(left.data(), right.data(), 127);
                    for (float x : left) expect(std::isfinite(x), "release became non-finite");
                    const float gain = Access::appliedGain(*e, 0);
                    expect(gain > 0.0f && gain <= 1.0f, "hand contact became an active gain or gate");
                }
                const double after = Access::lineEnergy(*e, 0);
                expect(after < before, "released string did not lose wave energy");
                expect(after < previousEnergy, "firmer damping did not remove more wave energy");
                previousEnergy = after;
            }
        }
}

void ownershipAndPedal()
{
    using Access = acustra::AcustraEngineTestAccess;
    auto e = std::make_unique<acustra::AcustraEngine>();
    e->prepare(48000.0, 64);
    e->setStringPerChannelMode(true);
    e->noteOn(43, 0.7f); e->noteOn(43, 0.7f);
    e->noteOffWithVelocity(43, 1, 1.0f);
    expect(e->heldString(43) == 0, "first duplicate off released the second owner");
    e->noteOffWithVelocity(43, 1, 0.0f);
    expect(Access::releaseSeconds(*e, 0) > 0.16f, "final owner's gentle release was lost");
    e->reset();
    e->setSustainPedal(true);
    e->noteOn(43, 0.7f);
    e->noteOffWithVelocity(43, 1, 1.0f);
    e->setSustainPedal(false);
    expect(Access::releaseSeconds(*e, 0) == 0.16f,
           "pedal-up reused a stale fast key release instead of nominal contact");
    e->allSoundOff();
    expect(e->getActiveVoiceCount() == 0, "all sound off retained a release voice");
    e->noteOn(43, 0.7f);
    e->noteOff(43);
    expect(Access::releaseSeconds(*e, 0) == 0.16f, "panic left a stale release velocity");
}
}

int main()
{
    nominalAndMonotonic(); ownershipAndPedal();
    if (failures) return 1;
    std::cout << "Explicit release damping, nominal parity, passive gain and ownership passed\n";
    return 0;
}
