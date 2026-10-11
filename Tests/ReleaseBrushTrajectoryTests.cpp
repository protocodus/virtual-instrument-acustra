// A short winding brush follows one continuous slip trajectory. Observe
// the designed filter/envelope, not a velocity-layer lookup or audio label.
#include "DSP/AcustraEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace acustra
{
struct AcustraEngineTestAccess
{
    struct Brush
    {
        std::array<float, 10> coefficients {};
        int samples = 0;
        std::uint32_t state = 0;
        std::vector<float> signal;
    };

    static Brush brush(double rate, int string, PickingTechnique technique,
                       float velocity, bool render = false, float amount = 0.5f,
                       float level = 0.2f, float peak = 0.7f, int stop = -1)
    {
        auto engine = std::make_unique<AcustraEngine>();
        // Isolate the release source from unrelated string/body changes.
        // This is a sounding, unbent string with a repeatable noise history.
        engine->sampleRate_ = rate;
        engine->parameters_.picking = technique;
        engine->targetParameters_.releaseNoise = amount;
        auto& voice = engine->voices_[static_cast<std::size_t>(string)];
        voice.played = true;
        voice.midiNote = voice.openMidi + 5;
        const bool fretSide = stop < 0 ? technique == PickingTechnique::Finger : stop != 0;
        voice.fret = fretSide ? 5 : 0;
        voice.speakingLengthMetres = fretSide ? 0.485f : 0.648f;
        voice.characteristicImpedance = 0.7f;
        voice.appliedBendImpedanceScale = 1.0f;
        voice.pluckPoint = 0.22f;
        voice.level = level;
        voice.peakLevel = peak;
        voice.releaseVelocity = velocity;
        voice.releaseNoiseState = 0x13579bdu;
        engine->startReleaseNoise(voice, string, fretSide);
        Brush result { { voice.releaseNoiseBandA1, voice.releaseNoiseBandA2,
            voice.releaseNoiseBandA3, voice.releaseNoiseBandK,
            voice.releaseNoiseBrushRiseDecay, voice.releaseNoiseBrushFallDecay,
            voice.releaseNoiseBrush, voice.releaseNoiseTouch,
            voice.releaseNoiseCoefficient, voice.releaseNoiseLeak },
            voice.releaseNoiseSamples, voice.releaseNoiseState, {} };
        if (render)
        {
            result.signal.reserve(static_cast<std::size_t>(result.samples + 1));
            for (int i = 0; i <= result.samples; ++i)
                result.signal.push_back(engine->renderReleaseNoise(voice));
        }
        return result;
    }
};
}

namespace
{
using Access = acustra::AcustraEngineTestAccess;
using Picking = acustra::PickingTechnique;
constexpr double pi = 3.14159265358979323846;
constexpr float nominal = 64.0f / 127.0f;
int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

double centre(const Access::Brush& brush, double rate)
{
    return std::atan(double(brush.coefficients[1]) / brush.coefficients[0])
        * rate / pi;
}

double duration(float decay, double rate)
{
    return -1.0 / (rate * std::log(double(decay)));
}

double envelopeEnergy(const Access::Brush& brush, double rate)
{
    // Independent infinite sum of (fall^n-rise^n)^2, after undoing the
    // stated band-pass noise-bandwidth normalisation. This measures the
    // force-envelope budget, not one lucky finite random realization.
    const double rise = brush.coefficients[4], fall = brush.coefficients[5];
    const double energy = 1.0 / (1.0 - fall * fall)
                       - 2.0 / (1.0 - fall * rise)
                       + 1.0 / (1.0 - rise * rise);
    const double noiseBandwidth = pi * centre(brush, rate)
        * brush.coefficients[3] / rate / 3.0;
    return double(brush.coefficients[6]) * brush.coefficients[6]
        * noiseBandwidth * energy / rate;
}

bool same(const Access::Brush& a, const Access::Brush& b)
{
    return a.coefficients == b.coefficients && a.samples == b.samples
        && a.state == b.state && a.signal == b.signal;
}

void testTrajectoryAndForceBudget()
{
    double worstConservation = 0.0, worstBudget = 0.0;
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (int string = 0; string < 4; ++string)
        {
            const auto reference = Access::brush(rate, string, Picking::Finger, nominal);
            const double cycles = centre(reference, rate)
                * duration(reference.coefficients[5], rate);
            // Arbitrary non-MIDI values explicitly exercise the continuum.
            for (float velocity : { 0.0f, 0.037f, 0.193f, 0.417f, 0.673f, 0.891f, 1.0f })
            {
                const auto brush = Access::brush(rate, string, Picking::Finger, velocity);
                const double speed = centre(brush, rate) / centre(reference, rate);
                const double fallRatio = duration(brush.coefficients[5], rate)
                    / duration(reference.coefficients[5], rate);
                const double riseRatio = duration(brush.coefficients[4], rate)
                    / duration(reference.coefficients[4], rate);
                const double error = std::abs(centre(brush, rate)
                    * duration(brush.coefficients[5], rate) / cycles - 1.0);
                worstConservation = std::max(worstConservation, error);
                expect(error < 0.001 && std::abs(speed * riseRatio - 1.0) < 0.001,
                       "brush did not traverse a fixed winding distance at its inferred speed");
                expect(std::abs(fallRatio / riseRatio - 1.0) < 0.001,
                       "slip changed the trajectory shape instead of its time scale");
                expect(speed >= 0.34999 && speed <= 1.70001,
                       "brush speed escaped its established release-control bounds");
                // Existing firmness sets RMS proportional to this speed.
                // Compare against that SAME-velocity former force budget.
                const double relativeBudget = envelopeEnergy(brush, rate)
                    / (envelopeEnergy(reference, rate) * speed * speed);
                worstBudget = std::max(worstBudget, relativeBudget);
                expect(relativeBudget <= 1.003,
                       "longer/slower brush added a new integrated force budget");
                expect(brush.state == reference.state,
                       "trajectory consumed extra independent random choices");
            }
        }
    std::cout << "Brush fixed-distance relative error: " << worstConservation
              << "; maximum prior-envelope energy ratio: " << worstBudget << '\n';
}

void testContinuityAndNominalCompatibility()
{
    for (auto technique : { Picking::Finger, Picking::Pick, Picking::Thumb })
        for (int string : { 0, 3, 4, 5 })
        {
            const auto nominalBrush = Access::brush(48000, string, technique, nominal, true);
            expect(same(nominalBrush, Access::brush(48000, string, technique, -1.0f, true)),
                   "missing release velocity differed from the nominal release");
            expect(same(nominalBrush, Access::brush(48000, string, technique, nominal, true)),
                   "release trajectory was not deterministic");
            expect(std::all_of(nominalBrush.signal.begin(), nominalBrush.signal.end(),
                               [] (float x) { return std::isfinite(x); }),
                   "nominal release source became nonfinite");
        }
    double previous = 0.0;
    for (int probe = 0; probe <= 256; ++probe)
    {
        const float velocity = 0.19f + 0.81f * static_cast<float>(probe) / 256.0f;
        const auto a = Access::brush(48000, 0, Picking::Finger, velocity);
        const auto b = Access::brush(48000, 0, Picking::Finger, velocity - 0.00001f);
        const double current = centre(a, 48000);
        expect(current > previous, "continuous release-speed sweep contained a velocity layer");
        expect(std::abs(current / centre(b, 48000) - 1.0) < 0.0001,
               "nearby release commands produced a discontinuous winding frequency");
        previous = current;
    }
    for (float velocity : { 0.0f, 0.3137f, 0.7941f, 1.0f })
    {
        const auto plain = Access::brush(48000, 5, Picking::Thumb, velocity, true);
        const auto silent = Access::brush(48000, 0, Picking::Finger, velocity,
                                         true, 0.5f, 0.0f, 0.0f);
        const auto disabled = Access::brush(48000, 0, Picking::Finger, velocity,
                                           true, 0.0f);
        expect(plain.coefficients[6] == 0.0f, "plain string acquired winding noise");
        expect(silent.samples == 0 && disabled.samples == 0,
               "silent or disabled release acquired a brush");
        const auto active = Access::brush(48000, 0, Picking::Pick, velocity, true);
        expect(std::all_of(active.signal.begin(), active.signal.end(), [] (float x)
                           { return std::isfinite(x); }) && active.signal.back() == 0.0f,
               "release-speed edge became nonfinite or failed to terminate");
    }
}

// Compile this same test against the frozen baseline and candidate and
// compare --baseline-parity output. It hashes the complete source output,
// coefficients and RNG for nominal/missing velocities and all plain-string
// velocities, without embedding platform-specific waveform goldens.
void baselineParity()
{
    std::uint64_t hash = 14695981039346656037ull;
    const auto append = [&] (const auto& value)
    {
        std::array<unsigned char, sizeof(value)> bytes {};
        std::memcpy(bytes.data(), &value, bytes.size());
        for (unsigned char byte : bytes) { hash ^= byte; hash *= 1099511628211ull; }
    };
    for (double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
        for (auto technique : { Picking::Finger, Picking::Pick, Picking::Thumb })
            for (int string = 0; string < 6; ++string)
                for (int stop : { 0, 1 })
                for (float velocity : { -1.0f, nominal, 0.13f, 0.873f })
                {
                    if (string < 4 && velocity != -1.0f && velocity != nominal) continue;
                    const auto brush = Access::brush(rate, string, technique, velocity,
                                                     true, 0.5f, 0.2f, 0.7f, stop);
                    for (float value : brush.coefficients) append(value);
                    append(brush.samples);
                    append(brush.state);
                    for (float value : brush.signal) append(value);
                }
    std::cout << std::hex << hash << '\n';
}
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string(argv[1]) == "--baseline-parity")
    { baselineParity(); return 0; }
    testTrajectoryAndForceBudget();
    testContinuityAndNominalCompatibility();
    if (failures != 0) return 1;
    std::cout << "Release brush trajectory checks passed.\n";
    return 0;
}
