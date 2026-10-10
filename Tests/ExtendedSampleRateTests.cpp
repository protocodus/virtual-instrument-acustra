#include "DSP/AcustraPerformer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <memory>
#include <string>

#if !ACUSTRA_EXTENDED_SAMPLE_RATES
#error This suite must link the extended-rate engine with the same storage layout.
#endif

namespace acustra
{
struct AcustraEngineTestAccess
{
    static bool activeStorageIsBounded()
    {
        using History = AcustraEngine::DelayHistory<32768>;
        auto source = std::make_unique<History>();
        auto destination = std::make_unique<History>();
        source->prepareSize(8192);
        source->data()[9000] = 17.0f;
        source->fill(0.25f);
        if (source->data()[9000] != 17.0f || (*source)[32767] != 0.25f)
            return false;
        destination->data()[9000] = 23.0f;
        *destination = *source;
        auto copied = std::make_unique<History>(*source);
        if (destination->data()[9000] != 23.0f || copied->size() != 8192
            || (*copied)[32767] != 0.25f)
            return false;
        source->prepareSize(32768);
        if (source->data()[9000] != 0.0f)
            return false;
        source->fill(1.0f);
        source->prepareSize(8192);
        source->prepareSize(32768);
        return std::all_of(source->begin(), source->end(), [](float value) { return value == 0.0f; });
    }

    static bool capacitiesMatchRate(const AcustraEngine& engine, double rate)
    {
        const std::size_t expected = rate <= 192000.0 ? 8192 : rate <= 384000.0 ? 16384 : 32768;
        if (engine.pickReleaseDisplacement_.size() != expected
            || engine.pickReleaseVelocity_.size() != expected)
            return false;
        for (const auto& loop : engine.repluckOldLoops_)
            if (loop.delay.size() != expected) return false;
        for (const auto& voice : engine.voices_)
        {
            for (const auto& loop : voice.loops)
                if (loop.delay.size() != expected) return false;
            if (voice.tailLoop.delay.size() != expected
                || voice.tailParallelLoop.delay.size() != expected) return false;
            for (const auto* travel : { &voice.contactTravel, &voice.tailContactTravel,
                    &voice.legatoContactTravel, &voice.tailLegatoContactTravel,
                    &voice.releaseNoiseTravel })
                if (travel->history.size() != expected) return false;
            for (const auto* arrivals : { &voice.repluckArrivals, &voice.tailRepluckArrivals })
                for (const auto& plane : arrivals->wave)
                    if (plane.size() != 2 * expected) return false;
        }
        return true;
    }

    static bool derivativePreservesReferenceTime(double rate)
    {
        AcustraEngine::FixedDerivative derivative;
        const float ratio = static_cast<float>(rate / 48000.0);
        const float expected = 0.125f * ratio;
        for (int n = 0; n < 96; ++n)
        {
            const float result = derivative.process(0.125f * n, ratio);
            if (n > 32 && std::abs(result - expected) > 2.0e-6f)
                return false;
        }
        // A body/configuration step must preserve the preceding slope,
        // including the >8-sample reference history at 705.6/768 kHz.
        const float stepped = derivative.processAcrossStep(0.125f * 96 + 9.0f, ratio);
        return std::abs(stepped - expected) < 2.0e-6f;
    }

    static float requestedFrequency(const AcustraEngine& engine, int note)
    {
        for (const auto& voice : engine.voices_)
            if (voice.played && voice.midiNote == note)
                return voice.pitchGeometry.value.frequency;
        return 0.0f;
    }

    static int fingerContactSamples(const AcustraEngine& engine)
    {
        for (const auto& voice : engine.voices_)
            if (voice.legatoContactSamples > 0)
                return voice.legatoContactSamples;
        return 0;
    }

    static bool lossIsPassive(double rate)
    {
        for (const float reference : { 0.01f, 0.25f, 0.75f, 0.99f })
        {
            AcustraEngine::OnePole filter;
            filter.configureRate(reference, rate);
            if (!std::isfinite(filter.ratePole) || std::abs(filter.ratePole) >= 1.0f)
                return false;
        }
        return true;
    }

    // Every string's own loss sections, designed at the host rate, are in
    // both loops and passive as the loop runs them, each as
    // 1 - (1 - z^-1)(g0 + g1 z^-1)/A(z) (StringLoop::advance) from its float
    // coefficients, the bending section's g/A(z) and the constant-loss one
    // (constantLossSection), from 10 Hz to Nyquist: open and at the 7th,
    // 12th and 19th frets. Their poles close on z = 1 at these rates, where
    // a direct form's gain at DC would rest on float rounding.
    static bool stringLossIsPassive(double rate)
    {
        auto engine = std::make_unique<AcustraEngine>();
        engine->prepare(rate, 64);
        for (int string = 0; string < AcustraEngine::stringCount; ++string)
            for (const int fret : { 0, 7, 12, 19 })
            {
                auto& voice = engine->voices_[static_cast<std::size_t>(string)];
                voice.attackPitchCents = 0.0f;
                engine->configureVoice(voice, string, voice.openMidi + fret, true);
                for (const auto& loop : voice.loops)
                {
                    if (!loop.bendingLossActive || !loop.constantLossActive)
                        return false;
                    const double g0 = static_cast<double>(1.0f - loop.constantLossGain);
                    const double g1 = static_cast<double>(loop.constantLossN2
                                                          - loop.constantLossA2);
                    for (int point = 0; point <= 2000; ++point)
                    {
                        const double frequency = 10.0 * std::pow(
                            0.5 * rate / 10.0, point / 2000.0);
                        const double omega = 6.283185307179586 * frequency / rate;
                        const std::complex<double> z1 = std::polar(1.0, -omega);
                        // The bending section in the same exact form, its
                        // numerator tap n2 zero.
                        const double bendingG0 = static_cast<double>(1.0f - loop.bendingLossGain);
                        const double bendingG1 = -static_cast<double>(loop.bendingLossA2);
                        const std::complex<double> response = (1.0 - (1.0 - z1)
                            * (g0 + g1 * z1)
                            / (1.0 + static_cast<double>(loop.constantLossA1) * z1
                               + static_cast<double>(loop.constantLossA2) * z1 * z1))
                            * (1.0 - (1.0 - z1) * (bendingG0 + bendingG1 * z1)
                                / (1.0 + static_cast<double>(loop.bendingLossA1) * z1
                                   + static_cast<double>(loop.bendingLossA2) * z1 * z1));
                        if (!(std::abs(response) <= 1.0 + 1.0e-6))
                            return false;
                    }
                }
            }
        return true;
    }
};
}

namespace
{
int failures = 0;
void expect(bool condition, const std::string& message)
{
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct Audio
{
    std::array<float, 256> left {}, right {}, piezo {};
};

bool renderFinite(acustra::Performer& performer, int samples, double& energy)
{
    Audio audio;
    bool finite = true;
    for (int rendered = 0; rendered < samples; rendered += 256)
    {
        const int count = std::min(256, samples - rendered);
        performer.beginBlock(audio.left.data(), audio.right.data(), { audio.piezo.data() }, count);
        performer.endBlock();
        for (int i = 0; i < count; ++i)
            for (const float value : { audio.left[static_cast<std::size_t>(i)],
                                       audio.right[static_cast<std::size_t>(i)],
                                       audio.piezo[static_cast<std::size_t>(i)] })
            {
                finite = finite && std::isfinite(value) && std::abs(value) <= 1.00001f;
                energy += static_cast<double>(value) * value;
            }
    }
    return finite;
}

void lowPitchAndStability(double rate)
{
    auto performer = std::make_unique<acustra::Performer>();
    acustra::EngineParameters parameters;
    parameters.tuning = acustra::Tuning::DropD;
    parameters.room = 0.2f;
    parameters.piezoMix = 0.25f;
    performer->setParameters(parameters);
    performer->prepare(rate, 256);
    expect(performer->engine().sampleRate() == rate, "requested high rate was clamped");
    expect(acustra::AcustraEngineTestAccess::capacitiesMatchRate(performer->engine(), rate),
           "a main/tail/contact/scratch history has the wrong active capacity");
    performer->setMasterTuneCents(-100.0f);
    // A downward octave plus global tuning needs >22000 samples at 768k.
    performer->engine().setPitchBend(-13.0f);
    performer->engine().noteOn(38, 0.75f);
    const double expected = 440.0 * std::exp2((38.0 - 13.0 - 69.0) / 12.0);
    const float actual = acustra::AcustraEngineTestAccess::requestedFrequency(performer->engine(), 38);
    expect(std::abs(1200.0 * std::log2(actual / expected)) < 2.0,
           "low bent Drop D reached a delay limit at " + std::to_string(rate));
    double energy = 0.0;
    expect(renderFinite(*performer, static_cast<int>(0.2 * rate), energy),
           "high-rate low note produced unbounded/nonfinite audio");
    expect(energy > 1.0e-5, "high-rate low note rendered silence");
    performer->engine().noteOff(38);
    expect(renderFinite(*performer, static_cast<int>(0.12 * rate), energy),
           "high-rate release or room tail became unstable");
    performer->setGatherChords(true);
    expect(performer->latencySamples() == 7 + static_cast<int>(std::lround(0.030 * rate)),
           "performer gather latency is not measured in internal samples");
}

void controllerAndReprepare(double rate)
{
    auto performer = std::make_unique<acustra::Performer>();
    performer->prepare(rate, 256);
    performer->setMasterTuneCents(-100.0f);
    Audio audio;
    performer->beginBlock(audio.left.data(), audio.right.data(), 256);
    performer->setPitchBendRange(0, 1, 12);
    performer->pitchWheel(0, 1, -1.0f);
    performer->noteOn(0, 1, 40, 96);
    performer->endBlock();
    const double expected = 440.0 * std::exp2((40.0 - 13.0 - 69.0) / 12.0);
    const float actual = acustra::AcustraEngineTestAccess::requestedFrequency(performer->engine(), 40);
    expect(std::abs(1200.0 * std::log2(actual / expected)) < 2.0,
           "master tune plus wheel failed at " + std::to_string(rate));

    // Reprepare must clear old rate-dependent history, pending notes and
    // controllers. Matching a fresh instance is stronger than finite output.
    performer->prepare(48000.0, 256);
    performer->prepare(rate, 256);
    auto fresh = std::make_unique<acustra::Performer>();
    fresh->setMasterTuneCents(-100.0f);
    fresh->setPitchBendRange(0, 1, 12);
    fresh->prepare(rate, 256);
    performer->engine().noteOn(59, 0.6f);
    fresh->engine().noteOn(59, 0.6f);
    Audio other;
    for (int block = 0; block < 144; ++block)
    {
        performer->process(audio.left.data(), audio.right.data(), 256);
        fresh->process(other.left.data(), other.right.data(), 256);
        expect(audio.left == other.left && audio.right == other.right,
               "reprepare retained high-rate history or random state");
    }
}

void fingerDuration(double rate, bool hammer)
{
    auto performer = std::make_unique<acustra::Performer>();
    performer->prepare(rate, 256);
    const int start = hammer ? 40 : 42;
    const int end = hammer ? 42 : 40;
    performer->engine().noteOn(start, 0.6f);
    double energy = 0.0;
    renderFinite(*performer, static_cast<int>(0.035 * rate), energy);
    const bool transitioned = performer->engine().transitionNote(start, end, 0.65f);
    expect(transitioned, "finger-duration fixture could not connect its note");
    if (transitioned)
        expect(acustra::AcustraEngineTestAccess::fingerContactSamples(performer->engine())
                   == static_cast<int>((hammer ? 0.002 : 0.003) * rate),
               "high-rate connected contact was truncated to the old capacity");
    expect(renderFinite(*performer, static_cast<int>(0.025 * rate), energy),
           "high-rate connected contact became unstable");
}
}

int main()
{
    using Access = acustra::AcustraEngineTestAccess;
    expect(Access::activeStorageIsBounded(), "history fill/copy touched dormant storage or resize retained it");
    expect(acustra::AcustraEngine::maximumSupportedSampleRate == 768000.0,
           "extended build does not expose its actual rate limit");
    for (const double rate : { 48000.0, 96000.0, 192000.0, 352800.0, 384000.0, 705600.0, 768000.0 })
    {
        expect(Access::derivativePreservesReferenceTime(rate), "bridge derivative lost its 48k reference time");
        expect(Access::lossIsPassive(rate), "remapped loss pole left the unit circle");
        expect(Access::stringLossIsPassive(rate),
               "a string's loss section was missing or active at " + std::to_string(rate));
        lowPitchAndStability(rate);
        controllerAndReprepare(rate);
        fingerDuration(rate, true);
        fingerDuration(rate, false);
    }
    auto bound = std::make_unique<acustra::AcustraEngine>();
    bound->prepare(1536000.0, 64);
    expect(bound->sampleRate() == 768000.0, "extended upper bound did not clamp");
    std::cout << "Extended-rate engine bytes=" << sizeof(acustra::AcustraEngine)
              << ", performer bytes=" << sizeof(acustra::Performer)
              << ", failures=" << failures << '\n';
    return failures == 0 ? 0 : 1;
}
