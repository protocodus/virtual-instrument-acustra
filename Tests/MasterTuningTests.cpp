// Host reference tuning changes open-string tension, not finger position.
// Check the independently known string geometry and controller composition,
// then exercise the live path against uncached and reordered equivalents.
#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <memory>
#include <vector>

namespace acustra
{
// These checks hold a note to its exact reference geometry, which the
// fretting finger's own pitch offset would move (frettingPitchCents).
inline void exactIntonation(AcustraEngine& engine) noexcept
{
    auto realism = engine.performanceRealism();
    realism.frettingPitch = false;
    engine.setPerformanceRealism(realism);
}

struct AcustraEngineTestAccess
{
    static const auto& voice(const AcustraEngine& engine, int string)
    { return engine.voices_[static_cast<std::size_t>(string)]; }
    static float vibrato(const AcustraEngine& engine, int string)
    { const auto& v = voice(engine, string); return engine.vibratoSemitones(v, v.speakingLengthMetres); }
    static void forceConfiguration(AcustraEngine& engine)
    {
        for (auto& voice : engine.voices_)
        {
            voice.pitchGeometry.valid = false;
            voice.configurationKey.generation = 0;
        }
    }
    static float wheel(const AcustraEngine& engine, int channel)
    { return engine.pitchBendSemitones_[static_cast<std::size_t>(channel - 1)]; }
};
}

namespace
{
using Engine = acustra::AcustraEngine;
using Performer = acustra::Performer;
using Access = acustra::AcustraEngineTestAccess;
constexpr std::array<int, 6> openNotes { 40, 45, 50, 55, 59, 64 };
constexpr std::array<double, 6> tensions { 110.759, 128.554, 133.002, 133.892, 103.643, 104.088 };
int failures = 0;
void expect(bool condition, const char* message)
{ if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
double noteHz(double note) { return 440.0 * std::exp2((note - 69.0) / 12.0); }

void advance(Engine& engine, int samples)
{
    std::array<float, 64> l {}, r {};
    while (samples > 0)
    {
        const int count = std::min(samples, 64);
        engine.process(l.data(), r.data(), count);
        for (int n = 0; n < count; ++n)
            expect(std::isfinite(l[n]) && std::isfinite(r[n])
                       && std::abs(l[n]) <= 1.0f && std::abs(r[n]) <= 1.0f,
                   "master-tuned render was not finite and bounded");
        samples -= count;
    }
}

void testKnownGeometry()
{
    double largestTensionError = 0.0, largestFrequencyError = 0.0;
    int cases = 0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
        for (const float cents : { -100.0f, 0.0f, 50.0f, 100.0f })
            for (const int fret : { 0, 7, 20 })
            {
                auto engine = std::make_unique<Engine>();
                acustra::exactIntonation(*engine);
                engine->setMasterTuneCents(cents);
                engine->prepare(rate, 64);
                engine->setStringPerChannelMode(true);
                const double ratio = std::exp2(cents / 1200.0);
                for (int string = 0; string < 6; ++string)
                {
                    engine->noteOn(openNotes[string] + fret, 0.6f, string + 1);
                    const auto& v = Access::voice(*engine, string);
                    const double tension = tensions[string] * ratio * ratio;
                    const double frequency = noteHz(openNotes[string] + fret) * ratio;
                    const double length = 0.648 * std::exp2(-fret / 12.0);
                    const double impedance = tensions[string] / (2.0 * 0.648 * noteHz(openNotes[string])) * ratio;
                    const double tensionError = std::abs(v.tensionNewtons / tension - 1.0);
                    const double frequencyError = std::abs(v.pitchGeometry.value.lossDesignFrequency / frequency - 1.0);
                    expect(std::abs(v.speakingLengthMetres / length - 1.0) < 2.0e-7
                               && v.speakingFret == static_cast<float>(fret),
                           "reference tuning changed a string's physical fret");
                    expect(tensionError < 5.0e-7 && std::abs(v.characteristicImpedance / impedance - 1.0) < 5.0e-7,
                           "reference tuning did not preserve gauge at its new tension/impedance");
                    expect(frequencyError < 2.0e-7, "master-tuned requested pitch missed its independent target");
                    largestTensionError = std::max(largestTensionError, tensionError);
                    largestFrequencyError = std::max(largestFrequencyError, frequencyError);
                    ++cases;
                }
            }
    std::cout << "Master tuning: " << cases << " string/fret/rate cases, tension relative error "
              << largestTensionError << ", frequency relative error " << largestFrequencyError << '\n';
}

void testLiveOpenTuningIsNotAContact()
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        auto engine = std::make_unique<Engine>();
        acustra::exactIntonation(*engine);
        engine->prepare(rate, 64);
        engine->setStringPerChannelMode(true);
        engine->noteOn(40, 0.8f, 1);
        advance(*engine, static_cast<int>(0.1 * rate));
        const auto& v = Access::voice(*engine, 0);
        const auto random = v.fretMotionNoiseState;
        const auto normal = v.loops[0].delay;
        const auto parallel = v.loops[1].delay;
        const float delay = v.loops[0].currentDelay;
        const float port = v.characteristicImpedance * v.appliedBendImpedanceScale;
        engine->setMasterTuneCents(50.0f);
        expect(v.loops[0].delay == normal && v.loops[1].delay == parallel
                   && v.loops[0].currentDelay == delay,
               "live master tuning replaced the existing vibrating string");
        expect(std::abs(v.characteristicImpedance * v.appliedBendImpedanceScale - port) < 2.0e-7f,
               "live master tuning stepped the current junction impedance");
        engine->setVibrato(1.0f);
        advance(*engine, static_cast<int>(0.75 * rate));
        expect(v.speakingLengthMetres == 0.648f && v.speakingFret == 0.0f
                   && v.fretMotionNoiseState == random && v.fretMotion.speed == 0.0f
                   && v.fretMotion.emittedForce == 0.0f && Access::vibrato(*engine, 0) == 0.0f,
               "master tuning made an open wound string scrape or receive finger vibrato");
        expect(std::abs(v.loops[0].currentDelay / v.loops[0].targetDelay - 1.0f) < 1.0e-4f
                   && std::abs(v.appliedBendImpedanceScale - v.bendImpedanceScale) < 1.0e-4f,
               "live tuning did not converge through the existing followers");
        // Its idle neighbours must also be retuned, without being fretted.
        for (int string = 1; string < 6; ++string)
        {
            const auto& idle = Access::voice(*engine, string);
            expect(!idle.played && idle.speakingFret == 0.0f
                       && std::abs(idle.pitchGeometry.value.lossDesignFrequency
                           / (noteHz(openNotes[string]) * std::exp2(0.5 / 12.0)) - 1.0) < 2.0e-7,
                   "reference tuning missed an idle sympathetic string");
        }
    }
}

void testReferenceDoesNotMoveASlidFinger()
{
    auto engine = std::make_unique<Engine>();
    acustra::exactIntonation(*engine);
    engine->prepare(48000.0, 64);
    engine->setStringPerChannelMode(true);
    engine->setPitchBend(1.37f, 1);
    engine->noteOn(43, 0.6f, 1);
    advance(*engine, 24000);
    const auto& voice = Access::voice(*engine, 0);
    const float length = voice.speakingLengthMetres, fret = voice.speakingFret;
    const auto random = voice.fretMotionNoiseState;
    for (const float cents : { 50.0f, -73.0f, 100.0f, 0.0f })
    {
        engine->setMasterTuneCents(cents);
        advance(*engine, 4000);
        expect(voice.speakingLengthMetres == length && voice.speakingFret == fret
                   && voice.fretMotionNoiseState == random,
               "master tuning moved a held slide's physical finger through rounding");
    }
}

void testInitialAndClearedSilenceAdoptsReference()
{
    for (const bool priorPerformance : { false, true })
    {
        auto immediate = std::make_unique<Engine>();
        acustra::exactIntonation(*immediate);
        immediate->prepare(48000.0, 64);
        if (priorPerformance)
        {
            immediate->noteOn(40, 0.7f);
            advance(*immediate, 2400);
            immediate->allSoundOff(1);
        }
        auto delayed = std::make_unique<Engine>(*immediate);
        immediate->setMasterTuneCents(50.0f);
        // After a previous performance, keep the same absolute re-pick
        // time in both fixtures; elapsed-time stroke variation is unrelated
        // to the ordering of a silent tuning notification.
        if (priorPerformance) advance(*immediate, 64);
        advance(*delayed, 64);
        delayed->setMasterTuneCents(50.0f);
        for (int string = 0; string < 6; ++string)
        {
            const auto& v = Access::voice(*delayed, string);
            expect(v.loops[0].currentDelay == v.loops[0].targetDelay
                       && v.appliedBendImpedanceScale == v.bendImpedanceScale,
                   "silent callback history left reference tuning in a live transition");
        }
        immediate->noteOn(40, 0.7f); delayed->noteOn(40, 0.7f);
        std::array<float, 64> a {}, ar {}, b {}, br {};
        for (int block = 0; block < 100; ++block)
        {
            immediate->process(a.data(), ar.data(), 64); delayed->process(b.data(), br.data(), 64);
            expect(a == b && ar == br,
                   "initial or cleared silence changed the first master-tuned attack");
        }
    }
}

std::vector<float> reordered(float cents, bool tuneFirst, bool reset, bool prepareAgain)
{
    auto performer = std::make_unique<Performer>();
    acustra::exactIntonation(performer->engine());
    if (tuneFirst) performer->setMasterTuneCents(cents);
    performer->prepare(48000.0, 64);
    if (!tuneFirst) performer->setMasterTuneCents(cents);
    if (reset) performer->reset();
    if (prepareAgain) performer->prepare(48000.0, 64);
    std::array<float, 64> l {}, r {};
    std::vector<float> audio;
    for (int block = 0; block < 80; ++block)
    {
        performer->beginBlock(l.data(), r.data(), 64);
        if (block == 0) performer->noteOn(0, 1, 45, 100);
        performer->endBlock();
        audio.insert(audio.end(), l.begin(), l.end());
        audio.insert(audio.end(), r.begin(), r.end());
    }
    return audio;
}

void testReferenceStateOrderAndInvalidInput()
{
    for (const float cents : { -100.0f, 0.0f, 50.0f, 100.0f })
    {
        const auto expected = reordered(cents, true, false, false);
        expect(expected == reordered(cents, false, false, false), "master tune depends on prepare/set order");
        expect(expected == reordered(cents, true, true, false), "reset lost or changed host reference tuning");
        expect(expected == reordered(cents, false, false, true), "reprepare lost or changed host reference tuning");
    }
    const auto zero = reordered(0.0f, true, false, false);
    for (const float invalid : { std::numeric_limits<float>::quiet_NaN(),
                                 std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity() })
        expect(zero == reordered(invalid, true, false, false), "nonfinite reference tuning was not zero");
    expect(reordered(100.0f, true, false, false) == reordered(1000.0f, true, false, false)
               && reordered(-100.0f, true, false, false)
                   == reordered(-std::numeric_limits<float>::max(), true, false, false),
           "finite out-of-range master tuning escaped its supported host range");
}

void testMpeWheelsComposeOnce()
{
    auto p = std::make_unique<Performer>();
    acustra::exactIntonation(p->engine());
    p->setMasterTuneCents(50.0f);
    p->prepare(48000.0, 64);
    std::array<float, 64> l {}, r {};
    p->beginBlock(l.data(), r.data(), 64);
    p->controlChange(0, 1, 101, 0); p->controlChange(0, 1, 100, 6); p->controlChange(0, 1, 6, 15);
    p->setPitchBendRange(0, 1, 2); p->setPitchBendRange(0, 2, 2);
    p->pitchWheel(0, 1, 0.5f); p->pitchWheel(0, 2, 0.5f);
    p->noteOn(0, 2, 45, 100);
    p->endBlock();
    int string = 0;
    while (string < 6 && !Access::voice(p->engine(), string).played) ++string;
    expect(string < 6, "MPE master-tuning fixture did not play");
    if (string == 6) return;
    const auto& v = Access::voice(p->engine(), string);
    // MIDI half-wheel rounds to its nearest 14-bit step. Read that delivered
    // interval, then use it in independent Hz and physical-length formulas.
    const float manager = Access::wheel(p->engine(), 1), member = Access::wheel(p->engine(), 2);
    expect(std::abs(manager - 1.0f) < 0.0002f && std::abs(member - 1.0f) < 0.0002f,
           "host tuning leaked into MPE wheel storage");
    const double expected = noteHz(45.0 + 0.5 + manager + member);
    expect(std::abs(v.pitchGeometry.value.lossDesignFrequency / expected - 1.0) < 3.0e-7,
           "master tuning or MPE manager/member pitch was applied more than once");
    const double length = 0.648 * std::exp2(-(45.0 - openNotes[string] + manager) / 12.0);
    expect(std::abs(v.speakingLengthMetres / length - 1.0) < 3.0e-7,
           "master tuning or member tension bend moved the MPE fretting point");
    p->beginBlock(l.data(), r.data(), 64); p->controlChange(0, 1, 121, 0); p->endBlock();
    expect(std::abs(v.pitchGeometry.value.lossDesignFrequency / noteHz(45.5) - 1.0) < 3.0e-7,
           "Reset All Controllers removed the host tuning reference");
}

void testCachedStateCopiesAndRepeats()
{
    auto cached = std::make_unique<Engine>();
    acustra::exactIntonation(*cached);
    cached->prepare(48000.0, 64);
    cached->setStringPerChannelMode(true);
    cached->noteOn(43, 0.8f, 1);
    advance(*cached, 2400);
    cached->noteOn(45, 0.7f, 1); // captures a ringing old-fret tail
    auto forced = std::make_unique<Engine>(*cached);
    expect(Access::voice(*cached, 0).tailActive, "master-tuning tail fixture had no tail");
    const auto tail = Access::voice(*cached, 0).tailLoop;
    const float tailImpedance = Access::voice(*cached, 0).tailCharacteristicImpedance;
    std::array<float, 64> a {}, ar {}, b {}, br {};
    for (int block = 0; block < 200; ++block)
    {
        if (block % 30 == 0)
        {
            const float cents = block % 60 == 0 ? 75.0f : -50.0f;
            cached->setMasterTuneCents(cents); forced->setMasterTuneCents(cents);
            if (block == 0)
            {
                const auto& v = Access::voice(*cached, 0);
                expect(v.tailActive && v.tailLoop.delay == tail.delay
                           && v.tailLoop.currentDelay == tail.currentDelay
                           && v.tailCharacteristicImpedance == tailImpedance,
                       "master tuning discarded or retuned an already captured tail");
            }
        }
        if (block == 45 || block == 100)
        {
            cached->noteOn(45, 0.7f, 1); forced->noteOn(45, 0.7f, 1);
        }
        if (block == 130)
        {
            cached->setParameters(acustra::EngineParameters {});
            forced->setParameters(acustra::EngineParameters {});
        }
        Access::forceConfiguration(*forced);
        cached->process(a.data(), ar.data(), 64); forced->process(b.data(), br.data(), 64);
        expect(a == b && ar == br, "master tuning depended on cached pitch/configuration state");
        const auto& x = Access::voice(*cached, 0);
        const auto& y = Access::voice(*forced, 0);
        expect(x.speakingLengthMetres == y.speakingLengthMetres && x.tensionNewtons == y.tensionNewtons
                   && x.pitchGeometry.value.frequency == y.pitchGeometry.value.frequency,
               "copied master-tuned geometry diverged after repeated plucks");
    }
}

double measuredFundamental(const std::vector<float>& audio, double rate, double target)
{
    constexpr double pi = 3.1415926535897932384626433832795;
    std::vector<double> windowed(audio.size());
    double energy = 0.0;
    for (std::size_t n = 0; n < audio.size(); ++n)
    {
        windowed[n] = audio[n] * (0.5 - 0.5 * std::cos(2.0 * pi * n / (audio.size() - 1)));
        energy += windowed[n] * windowed[n];
    }
    expect(energy > 1.0e-10, "rendered master-tuning pitch witness was silent");
    const auto power = [&] (double cents)
    {
        const double frequency = target * std::exp2(cents / 1200.0);
        const auto rotation = std::polar(1.0, -2.0 * pi * frequency / rate);
        std::complex<double> phase { 1.0, 0.0 }, sum {};
        for (const double value : windowed) { sum += value * phase; phase *= rotation; }
        return std::norm(sum);
    };
    // Search +/-200 cents: a missing 100-cent tune must remain detectable,
    // rather than assuming the answer inside a tiny target-centred window.
    double peak = -200.0, maximum = -1.0;
    for (int step = 0; step <= 80; ++step)
    {
        const double cents = -200.0 + 5.0 * step;
        const double value = power(cents);
        if (value > maximum) { maximum = value; peak = cents; }
    }
    double low = peak - 5.0, high = peak + 5.0;
    for (int step = 0; step < 28; ++step)
    {
        const double a = low + (high - low) / 3.0, b = high - (high - low) / 3.0;
        if (power(a) < power(b)) low = a; else high = b;
    }
    return target * std::exp2(0.5 * (low + high) / 1200.0);
}

void testRenderedPitchTracksReference()
{
    double largestPitchError = 0.0, largestIntervalError = 0.0;
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        double baseline = 0.0;
        for (const float cents : { 0.0f, -100.0f, 100.0f })
        {
            auto engine = std::make_unique<Engine>();
            acustra::exactIntonation(*engine);
            engine->prepare(rate, 64);
            engine->setBridgeCouplingEnabled(false);
            engine->setSympatheticStringsEnabled(false);
            engine->setMasterTuneCents(cents);
            engine->setStringPerChannelMode(true);
            engine->noteOn(55, 0.6f, 4);
            advance(*engine, static_cast<int>(0.7 * rate));
            std::vector<float> l(static_cast<std::size_t>(0.5 * rate)), r(l.size());
            engine->process(l.data(), r.data(), static_cast<int>(l.size()));
            const double target = noteHz(55.0 + cents / 100.0);
            const double measured = measuredFundamental(l, rate, target);
            const double error = 1200.0 * std::log2(measured / target);
            largestPitchError = std::max(largestPitchError, std::abs(error));
            expect(std::abs(error) < 4.0, "rendered settled H1 missed its master-tuned reference");
            if (cents == 0.0f) baseline = measured;
            else
            {
                const double intervalError = 1200.0 * std::log2(measured / baseline) - cents;
                largestIntervalError = std::max(largestIntervalError, std::abs(intervalError));
                expect(std::abs(intervalError) < 2.0, "rendered H1 did not move by the host reference interval");
            }
        }
    }
    std::cout << "Rendered master tuning: worst H1 error " << largestPitchError
              << " cents, interval error " << largestIntervalError << " cents\n";
}
}

int main()
{
    testKnownGeometry();
    testLiveOpenTuningIsNotAContact();
    testReferenceDoesNotMoveASlidFinger();
    testInitialAndClearedSilenceAdoptsReference();
    testReferenceStateOrderAndInvalidInput();
    testMpeWheelsComposeOnce();
    testCachedStateCopiesAndRepeats();
    testRenderedPitchTracksReference();
    if (failures == 0) std::cout << "Master tuning physical/state contracts passed\n";
    return failures == 0 ? 0 : 1;
}
